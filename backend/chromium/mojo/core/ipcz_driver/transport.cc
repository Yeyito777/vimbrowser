// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/351564777): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "mojo/core/ipcz_driver/transport.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/numerics/safe_conversions.h"
#include "base/process/process.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "mojo/core/ipcz_driver/data_pipe.h"
#include "mojo/core/ipcz_driver/envelope.h"
#include "mojo/core/ipcz_driver/invitation.h"
#include "mojo/core/ipcz_driver/object.h"
#include "mojo/core/ipcz_driver/shared_buffer.h"
#include "mojo/core/ipcz_driver/transmissible_platform_handle.h"
#include "mojo/core/ipcz_driver/validate_enum.h"
#include "mojo/core/ipcz_driver/wrapped_platform_handle.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/platform/platform_channel_endpoint.h"
#include "mojo/public/cpp/platform/platform_handle.h"
#include "third_party/abseil-cpp/absl/container/inlined_vector.h"
#include "third_party/ipcz/include/ipcz/ipcz.h"


namespace mojo::core::ipcz_driver {

namespace {


// Header serialized at the beginning of all mojo-ipcz driver objects.
struct IPCZ_ALIGN(8) ObjectHeader {
  // The size of this header in bytes. Used for versioning.
  uint32_t size;

  // Identifies the type of object serialized.
  ObjectBase::Type type;

};

// Header for a serialized Transport object.
struct IPCZ_ALIGN(8) TransportHeader {
  // Indicates what type of destination the other end of this serialized
  // transport is connected to.
  Transport::EndpointType destination_type;

  // Indicates whether the remote process on the other end of this transport
  // is the same process sending this object. Encodes a `bool`.
  uint8_t is_same_remote_process;

  // See notes on equivalent fields defined on Transport. Note that serialized
  // transports endpoints with `is_peer_trusted` set to true can only be
  // accepted from transports which are themselves trusted. Encodes a `bool`.
  uint8_t is_peer_trusted;
  uint8_t is_trusted_by_peer;

  // Padding for 8-byte size alignment.
  uint8_t reserved[1];
};


scoped_refptr<base::SingleThreadTaskRunner>& GetIOTaskRunnerStorage() {
  static base::NoDestructor<scoped_refptr<base::SingleThreadTaskRunner>> runner;
  return *runner;
}

}  // namespace

// static
size_t Transport::FirstHandleOffsetForTesting() {
  return sizeof(ObjectHeader);
}

Transport::Transport(EndpointTypes endpoint_types,
                     PlatformChannelEndpoint endpoint,
                     base::Process remote_process,
                     ProcessTrust remote_process_trust)
    : endpoint_types_(endpoint_types),
      remote_process_(std::move(remote_process)),
      remote_process_trust_(remote_process_trust),
      inactive_endpoint_(std::move(endpoint)) {}

// static
scoped_refptr<Transport> Transport::Create(EndpointTypes endpoint_types,
                                           PlatformChannelEndpoint endpoint,
                                           base::Process remote_process,
                                           ProcessTrust remote_process_trust) {
  return base::MakeRefCounted<Transport>(endpoint_types, std::move(endpoint),
                                         std::move(remote_process),
                                         remote_process_trust);
}

// static
std::pair<scoped_refptr<Transport>, scoped_refptr<Transport>>
Transport::CreatePair(EndpointType first_type, EndpointType second_type) {
  PlatformChannel channel;
  auto one = Create({.source = first_type, .destination = second_type},
                    channel.TakeLocalEndpoint());
  auto two = Create({.source = second_type, .destination = first_type},
                    channel.TakeRemoteEndpoint());
  return {one, two};
}

Transport::~Transport() {
  if (error_handler_) {
    const MojoProcessErrorDetails details{
        .struct_size = sizeof(details),
        .error_message_length = 0,
        .error_message = nullptr,
        .flags = MOJO_PROCESS_ERROR_FLAG_DISCONNECTED,
    };
    error_handler_(error_handler_context_, &details);
  }
}

// static
void Transport::SetIOTaskRunner(
    scoped_refptr<base::SingleThreadTaskRunner> runner) {
  GetIOTaskRunnerStorage() = std::move(runner);
}

// static
const scoped_refptr<base::SingleThreadTaskRunner>&
Transport::GetIOTaskRunner() {
  return GetIOTaskRunnerStorage();
}

void Transport::OverrideIOTaskRunner(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  io_task_runner_ = std::move(task_runner);
}

void Transport::ReportBadActivity(const std::string& error_message) {
  if (!error_handler_) {
    Invitation::InvokeDefaultProcessErrorHandler(error_message);
    return;
  }

  const MojoProcessErrorDetails details{
      .struct_size = sizeof(details),
      .error_message_length =
          base::checked_cast<uint32_t>(error_message.size()),
      .error_message = error_message.c_str(),
      .flags = MOJO_PROCESS_ERROR_FLAG_NONE,
  };
  error_handler_(error_handler_context_, &details);
}

bool Transport::Activate(IpczHandle transport,
                         IpczTransportActivityHandler activity_handler) {
  scoped_refptr<Channel> channel;
  std::vector<PendingTransmission> pending_transmissions;
  {
    base::AutoLock lock(lock_);
    if (channel_ || !inactive_endpoint_.is_valid()) {
      return false;
    }

    ipcz_transport_ = transport;
    activity_handler_ = activity_handler;
    self_reference_for_channel_ = base::WrapRefCounted(this);
    channel_ = Channel::CreateForIpczDriver(this, std::move(inactive_endpoint_),
                                            io_task_runner_);
    if (leak_channel_on_shutdown_) {
      io_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](scoped_refptr<Channel> channel) { channel->LeakHandle(); },
              channel_));
    }

    if (!pending_transmissions_.empty()) {
      pending_transmissions_.swap(pending_transmissions);
    }

    channel = channel_;
  }

  // NOTE: Some Channel implementations could re-enter this Transport from
  // within Start(), so it's critical that we don't call it while holding our
  // lock.
  channel->Start();

  if (channel->SupportsChannelUpgrade() &&
      source_type() == EndpointType::kBroker &&
      destination_type() == EndpointType::kNonBroker) {
    base::AutoLock lock(lock_);
    channel->OfferChannelUpgrade();
  }

  for (auto& transmission : pending_transmissions) {
    channel->WriteNextIpczMessage(base::span(transmission.bytes),
                                  std::move(transmission.handles));
  }

  return true;
}

bool Transport::Deactivate() {
  scoped_refptr<Channel> channel;
  {
    base::AutoLock lock(lock_);
    if (!channel_) {
      return false;
    }

    channel = std::move(channel_);
  }

  // This will post a task to the Channel's IO thread to complete shutdown. Once
  // the last Channel reference is dropped, it will invoke OnChannelDestroyed()
  // on this Transport. The Transport is kept alive in the meantime by its own
  // retained `self_reference_for_channel_`.
  channel->ShutDown();
  return true;
}

bool Transport::Transmit(base::span<const uint8_t> data,
                         base::span<const IpczDriverHandle> handles) {

  std::vector<PlatformHandle> platform_handles;
  platform_handles.reserve(handles.size());
  for (IpczDriverHandle handle : handles) {
    auto transmissible_handle =
        TransmissiblePlatformHandle::TakeFromHandle(handle);
    DCHECK(transmissible_handle);
    platform_handles.push_back(transmissible_handle->TakeHandle());
  }

  scoped_refptr<Channel> channel;
  {
    base::AutoLock lock(lock_);
    if (inactive_endpoint_.is_valid()) {
      PendingTransmission transmission;
      transmission.bytes = std::vector<uint8_t>(data.begin(), data.end());
      transmission.handles = std::move(platform_handles);
      pending_transmissions_.push_back(std::move(transmission));
      return true;
    }

    if (!channel_) {
      return false;
    }
    channel = channel_;
  }

  channel->WriteNextIpczMessage(data, std::move(platform_handles));
  return true;
}

IpczResult Transport::SerializeObject(ObjectBase& object,
                                      void* data,
                                      size_t* num_bytes,
                                      IpczDriverHandle* handles,
                                      size_t* num_handles) {
  size_t object_num_bytes;
  size_t object_num_handles;
  if (!object.GetSerializedDimensions(*this, object_num_bytes,
                                      object_num_handles)) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }

  if (object_num_handles > 0 && !CanTransmitHandles()) {
    // Let ipcz know that it must relay this object through a broker instead of
    // transmitting it over this transport.
    return IPCZ_RESULT_PERMISSION_DENIED;
  }

  const size_t required_num_bytes = sizeof(ObjectHeader) + object_num_bytes;
  const size_t required_num_handles = object_num_handles;
  const size_t data_capacity = num_bytes ? *num_bytes : 0;
  const size_t handle_capacity = num_handles ? *num_handles : 0;
  if (num_bytes) {
    *num_bytes = required_num_bytes;
  }
  if (num_handles) {
    *num_handles = required_num_handles;
  }
  if (data_capacity < required_num_bytes ||
      handle_capacity < required_num_handles) {
    return IPCZ_RESULT_RESOURCE_EXHAUSTED;
  }

  auto& header = *static_cast<ObjectHeader*>(data);
  header.size = sizeof(header);
  header.type = object.type();
  auto object_data =
      base::span(reinterpret_cast<uint8_t*>(&header + 1), object_num_bytes);

  // A small amount of stack storage is reserved to avoid heap allocation in the
  // most common cases.
  absl::InlinedVector<PlatformHandle, 2> platform_handles;
  platform_handles.resize(object_num_handles);
  if (!object.Serialize(*this, object_data, base::span(platform_handles))) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }

  bool ok = true;
  for (size_t i = 0; i < object_num_handles; ++i) {
    handles[i] = TransmissiblePlatformHandle::ReleaseAsHandle(
        base::MakeRefCounted<TransmissiblePlatformHandle>(
            std::move(platform_handles[i])));
  }
  return ok ? IPCZ_RESULT_OK : IPCZ_RESULT_INVALID_ARGUMENT;
}

IpczResult Transport::DeserializeObject(
    base::span<const uint8_t> bytes,
    base::span<const IpczDriverHandle> handles,
    scoped_refptr<ObjectBase>& object) {
  if (bytes.size() < sizeof(ObjectHeader)) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }

  const auto& header = *reinterpret_cast<const ObjectHeader*>(bytes.data());
  // Validate header fields.
  const uint32_t header_size = header.size;
  if (header_size < sizeof(ObjectHeader) || header_size > bytes.size()) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }
  if (!ValidateEnum(header.type)) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }
  // Return early for objects that cannot be deserialized.
  if (!(header.type == ObjectBase::kTransport ||
        header.type == ObjectBase::kSharedBuffer ||
        header.type == ObjectBase::kTransmissiblePlatformHandle ||
        header.type == ObjectBase::kWrappedPlatformHandle ||
        header.type == ObjectBase::kDataPipe)) {
    return IPCZ_RESULT_UNIMPLEMENTED;
  }

  auto object_data = bytes.subspan(header_size);
  const size_t num_handles = handles.size();

  // A small amount of stack storage is reserved to avoid heap allocation in the
  // most common cases.
  absl::InlinedVector<PlatformHandle, 2> platform_handles;
  platform_handles.resize(num_handles);
  for (size_t i = 0; i < num_handles; ++i) {
    platform_handles[i] =
        TransmissiblePlatformHandle::TakeFromHandle(handles[i])->TakeHandle();
    if (!platform_handles[i].is_valid()) {
      return IPCZ_RESULT_INVALID_ARGUMENT;
    }
  }

  auto object_handles = base::span(platform_handles);
  switch (header.type) {
    case ObjectBase::kTransport:
      object = Deserialize(*this, object_data, object_handles);
      break;
    case ObjectBase::kSharedBuffer:
      object = SharedBuffer::Deserialize(object_data, object_handles);
      break;
    case ObjectBase::kTransmissiblePlatformHandle:
      object =
          TransmissiblePlatformHandle::Deserialize(object_data, object_handles);
      break;

    case ObjectBase::kWrappedPlatformHandle:
      object = WrappedPlatformHandle::Deserialize(object_data, object_handles);
      break;

    case ObjectBase::kDataPipe:
      object = DataPipe::Deserialize(object_data, object_handles);
      break;

    default:
      // Validated at head of function so this should not be reached.
      NOTREACHED();
  }

  if (!object) {
    return IPCZ_RESULT_INVALID_ARGUMENT;
  }

  return IPCZ_RESULT_OK;
}

void Transport::Close() {
  Deactivate();
}

bool Transport::IsSerializable() const {
  return true;
}

bool Transport::GetSerializedDimensions(Transport& transmitter,
                                        size_t& num_bytes,
                                        size_t& num_handles) {
  num_bytes = sizeof(TransportHeader);
  num_handles = ShouldSerializeProcessHandle(transmitter) ? 2 : 1;
  return true;
}

bool Transport::Serialize(Transport& transmitter,
                          base::span<uint8_t> data,
                          base::span<PlatformHandle> handles) {
  DCHECK_EQ(sizeof(TransportHeader), data.size());
  auto& header = *reinterpret_cast<TransportHeader*>(data.data());
  header.destination_type = destination_type();
  header.is_same_remote_process = remote_process_.is_current();
  header.is_peer_trusted = is_peer_trusted();
  header.is_trusted_by_peer = is_trusted_by_peer();

  if (ShouldSerializeProcessHandle(transmitter)) {
    DCHECK_EQ(handles.size(), 2u);
    DCHECK(remote_process_.IsValid());
    DCHECK(!remote_process_.is_current());
  } else {
    DCHECK_EQ(handles.size(), 1u);
  }

  CHECK(inactive_endpoint_.is_valid());
  handles[0] = inactive_endpoint_.TakePlatformHandle();
  return true;
}

// static
scoped_refptr<Transport> Transport::Deserialize(
    Transport& from_transport,
    base::span<const uint8_t> data,
    base::span<PlatformHandle> handles) {
  if (data.size() < sizeof(TransportHeader) || handles.size() < 1) {
    return nullptr;
  }

  base::Process process;
  const auto& header = *reinterpret_cast<const TransportHeader*>(data.data());
  // Reject transports with out of range enum value in destination_type.
  if (!ValidateEnum(header.destination_type)) {
    return nullptr;
  }

  const bool is_source_trusted = from_transport.is_peer_trusted() ||
                                 from_transport.destination_type() == kBroker;

  const bool is_new_peer_trusted = header.is_peer_trusted;
  const bool is_trusted_by_peer = header.is_trusted_by_peer;

  if (is_new_peer_trusted && !is_source_trusted) {
    // Untrusted transports cannot send us trusted transports.
    return nullptr;
  }

  if (header.destination_type == kBroker && !is_source_trusted) {
    // Do not accept broker connections from untrusted transports.
    return nullptr;
  }

  if (header.is_same_remote_process &&
      from_transport.remote_process().IsValid()) {
    process = from_transport.remote_process().Duplicate();
  }
  auto transport =
      Create({.source = from_transport.source_type(),
              .destination = header.destination_type},
             PlatformChannelEndpoint(std::move(handles[0])), std::move(process),
             from_transport.remote_process_trust());
  transport->set_is_peer_trusted(is_new_peer_trusted);
  transport->set_is_trusted_by_peer(is_trusted_by_peer);

  // Inherit the IO task used by the receiving Transport. Deserialized
  // transports are always adopted by the receiving node, and we want any given
  // node to receive all of its transports' I/O on the same thread.
  transport->OverrideIOTaskRunner(from_transport.io_task_runner_);

  return transport;
}

bool Transport::IsIpczTransport() const {
  return true;
}

void Transport::OnChannelMessage(
    const void* payload,
    size_t payload_size,
    std::vector<PlatformHandle> handles,
    scoped_refptr<ipcz_driver::Envelope> envelope) {
  std::vector<IpczDriverHandle> driver_handles(handles.size());
  for (size_t i = 0; i < handles.size(); ++i) {
    driver_handles[i] = TransmissiblePlatformHandle::ReleaseAsHandle(
        base::MakeRefCounted<TransmissiblePlatformHandle>(
            std::move(handles[i])));
  }

  IpczTransportActivityOptions options{
      .size = sizeof(IpczTransportActivityOptions),
      .envelope = ObjectBase::ReleaseAsHandle(std::move(envelope)),
  };
  const IpczResult result = activity_handler_(
      ipcz_transport_, static_cast<const uint8_t*>(payload), payload_size,
      driver_handles.data(), driver_handles.size(), IPCZ_NO_FLAGS, &options);
  if (result != IPCZ_RESULT_OK && result != IPCZ_RESULT_UNIMPLEMENTED) {
    OnChannelError(Channel::Error::kReceivedMalformedData);
  }
}

void Transport::OnChannelError(Channel::Error error) {
  activity_handler_(ipcz_transport_, nullptr, 0, nullptr, 0,
                    IPCZ_TRANSPORT_ACTIVITY_ERROR, nullptr);
}

void Transport::OnChannelDestroyed() {
  activity_handler_(ipcz_transport_, nullptr, 0, nullptr, 0,
                    IPCZ_TRANSPORT_ACTIVITY_DEACTIVATED, nullptr);

  // Drop our self-reference now that the Channel is definitely done calling us.
  // May delete `this` as the stack unwinds.
  scoped_refptr<Transport> self;
  base::AutoLock lock(lock_);
  self = std::move(self_reference_for_channel_);
}

bool Transport::CanTransmitHandles() const {
  return true;
}

bool Transport::ShouldSerializeProcessHandle(Transport& transmitter) const {
  // We have no need for the process handle on other platforms.
  return false;
}

Transport::PendingTransmission::PendingTransmission() = default;

Transport::PendingTransmission::PendingTransmission(PendingTransmission&&) =
    default;

Transport::PendingTransmission& Transport::PendingTransmission::operator=(
    PendingTransmission&&) = default;

Transport::PendingTransmission::~PendingTransmission() = default;

}  // namespace mojo::core::ipcz_driver
