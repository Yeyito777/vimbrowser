// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_switch.h"

#include <optional>
#include <string_view>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/platform_shared_memory_handle.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/unsafe_shared_memory_region.h"
#include "base/notimplemented.h"
#include "base/process/launch.h"
#include "base/process/process_handle.h"
#include "base/process/process_info.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/unguessable_token.h"

// On Apple platforms, the shared memory handle is shared using a Mach port
// rendezvous key.
#if BUILDFLAG(IS_APPLE)
#include "base/apple/mach_port_rendezvous.h"
#endif

// On POSIX, the shared memory handle is a file_descriptor mapped in the
// GlobalDescriptors table.
#include "base/posix/global_descriptors.h"



// This file supports passing a shared memory region between a parent process
// and child process. The information about the shared memory region is encoded
// into a command-line switch value.
//
// Format: "handle,[irp],guid-high,guid-low,size".
//
// The switch value is composed of 5 segments, separated by commas:
//
// 1. The platform-specific handle id for the shared memory as a string.
// 2. [irp] to indicate whether the handle is inherited (i, most platforms),
//    sent via rendezvous (r, MacOS), or should be queried from the parent
//    (p, Windows).
// 3. The high 64 bits of the shared memory block GUID.
// 4. The low 64 bits of the shared memory block GUID.
// 5. The size of the shared memory segment as a string.

namespace base::shared_memory {
namespace {

using base::subtle::PlatformSharedMemoryRegion;
using base::subtle::ScopedPlatformSharedMemoryHandle;

// The max shared memory size is artificially limited. This serves as a sanity
// check when serializing/deserializing the handle info. This value should be
// slightly larger than the largest shared memory size used in practice.
constexpr size_t kMaxSharedMemorySize = 8 << 20;  // 8 MiB


// Serializes the shared memory region metadata to a string that can be added
// to the command-line of a child-process.
template <typename HandleType>
std::string Serialize(HandleType shmem_handle,
                      const UnguessableToken& shmem_token,
                      size_t shmem_size,
                      [[maybe_unused]] bool is_read_only,
                      SharedMemorySwitch* shared_memory_switch,
                      [[maybe_unused]] LaunchOptions* launch_options) {
#if !BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_APPLE)
  CHECK(launch_options != nullptr);
#endif

  CHECK(shmem_token);
  CHECK_NE(shmem_size, 0u);
  CHECK_LE(shmem_size, kMaxSharedMemorySize);

  // Reserve memory for the serialized value.
  // handle,method,hi,lo,size = 4 * 64-bit number + 1 char + 4 commas + NUL
  //                          = (4 * 20-max decimal char) + 6 chars
  //                          = 86 bytes
  constexpr size_t kSerializedReservedSize = 86;

  std::string serialized;
  serialized.reserve(kSerializedReservedSize);

#if BUILDFLAG(IS_APPLE)
  auto& rendezvous_key = shared_memory_switch->rendezvous_key;
  // In the receiving child, the handle is looked up using the rendezvous key.
  launch_options->mach_ports_for_rendezvous.emplace(
      rendezvous_key, MachRendezvousPort(std::move(shmem_handle)));
  StrAppend(&serialized, {NumberToString(rendezvous_key), ",r,"});
#else
  // Serialize the key by which the child can lookup the shared memory handle.
  // Ownership of the handle is transferred, via |descriptor_to_share|, to the
  // caller, who is responsible for updating |launch_options| or the zygote
  // launch parameters, as appropriate.
  //
  // TODO(crbug.com/40109064): Create a wrapper to release and return the
  // primary descriptor for android (ScopedFD) vs non-android (ScopedFDPair).
  //
  // TODO(crbug.com/40109064): Get rid of |descriptor_to_share| and just
  // populate |launch_options|. The caller should be responsible for translating
  // between |launch_options| and zygote parameters as necessary.
  shared_memory_switch->out_descriptor_to_share = std::move(shmem_handle.fd);
  DVLOG(1) << "Sharing fd="
           << shared_memory_switch->out_descriptor_to_share.get()
           << " with child process as fd_key="
           << shared_memory_switch->descriptor_key;
  StrAppend(&serialized,
            {NumberToString(shared_memory_switch->descriptor_key), ",i,"});
#endif

  StrAppend(&serialized,
            {NumberToString(shmem_token.GetHighForSerialization()), ",",
             NumberToString(shmem_token.GetLowForSerialization()), ",",
             NumberToString(shmem_size)});

  DCHECK_LT(serialized.size(), kSerializedReservedSize);
  return serialized;
}

// Deserialize |guid| from |hi_part| and |lo_part|, returning true on success.
std::optional<UnguessableToken> DeserializeGUID(std::string_view hi_part,
                                                std::string_view lo_part) {
  uint64_t hi = 0;
  uint64_t lo = 0;
  if (!StringToUint64(hi_part, &hi) || !StringToUint64(lo_part, &lo)) {
    return std::nullopt;
  }
  return UnguessableToken::Deserialize(hi, lo);
}

// Deserializes `switch_value` into a PlatformSharedMemoryRegion.
expected<PlatformSharedMemoryRegion, SharedMemoryError> Deserialize(
    std::string_view switch_value,
    PlatformSharedMemoryRegion::Mode mode) {
  std::vector<std::string_view> tokens =
      SplitStringPiece(switch_value, ",", KEEP_WHITESPACE, SPLIT_WANT_ALL);
  if (tokens.size() != 5) {
    return unexpected(SharedMemoryError::kUnexpectedTokensCount);
  }

  // Parse the handle from tokens[0].
  uint64_t shmem_handle = 0;
  if (!StringToUint64(tokens[0], &shmem_handle)) {
    return unexpected(SharedMemoryError::kParseInt0Failed);
  }

  // token[1] has a fixed value but is ignored on all platforms except
  // Windows, where it can be 'i' or 'p' to indicate that the handle is
  // inherited or must be obtained from the parent.
#if BUILDFLAG(IS_APPLE)
  DCHECK_EQ(tokens[1], "r");
  auto* rendezvous = MachPortRendezvousClient::GetInstance();
  if (!rendezvous) {
    // Note: This matches mojo behavior in content/child/child_thread_impl.cc.
    LOG(ERROR) << "No rendezvous client, terminating process (parent died?)";
    Process::TerminateCurrentProcessImmediately(0);
  }
  apple::ScopedMachSendRight scoped_handle = rendezvous->TakeSendRight(
      static_cast<MachPortsForRendezvous::key_type>(shmem_handle));
  if (!scoped_handle.is_valid()) {
    // Note: This matches mojo behavior in content/child/child_thread_impl.cc.
    LOG(ERROR) << "Mach rendezvous failed, terminating process (parent died?)";
    base::Process::TerminateCurrentProcessImmediately(0);
  }
#else
  DCHECK_EQ(tokens[1], "i");
  const int fd = GlobalDescriptors::GetInstance()->MaybeGet(
      checked_cast<GlobalDescriptors::Key>(shmem_handle));
  if (fd == -1) {
    LOG(ERROR) << "Failed global descriptor lookup: " << shmem_handle;
    return unexpected(SharedMemoryError::kGetFDFailed);
  }
  DVLOG(1) << "Opening shared memory handle " << fd << " shared as "
           << shmem_handle;
  ScopedFD scoped_handle(fd);
#endif

  // Together, tokens[2] and tokens[3] encode the shared memory guid.
  auto guid = DeserializeGUID(tokens[2], tokens[3]);
  if (!guid.has_value()) {
    return unexpected(SharedMemoryError::kDeserializeGUIDFailed);
  }

  // The size is in tokens[4].
  uint64_t size = 0;
  if (!StringToUint64(tokens[4], &size)) {
    return unexpected(SharedMemoryError::kParseInt4Failed);
  }
  if (size == 0 || size > kMaxSharedMemorySize) {
    return unexpected(SharedMemoryError::kUnexpectedSize);
  }

  // Resolve the handle to a shared memory region.
  return PlatformSharedMemoryRegion::Take(
      std::move(scoped_handle), mode, checked_cast<size_t>(size), guid.value());
}

template <typename RegionType>
void AddToLaunchParametersImpl(const RegionType& memory_region,
                               SharedMemorySwitch* shared_memory_switch,
                               CommandLine* command_line,
                               LaunchOptions* launch_options) {
  auto region =
      RegionType::TakeHandleForSerialization(memory_region.Duplicate());
  auto token = region.GetGUID();
  auto size = region.GetSize();
  auto handle = region.PassPlatformHandle();
  constexpr bool is_read_only =
      std::is_same<RegionType, ReadOnlySharedMemoryRegion>::value;
  std::string switch_value =
      Serialize(std::move(handle), token, size, is_read_only,
                shared_memory_switch, launch_options);
  command_line->AppendSwitchASCII(shared_memory_switch->switch_name,
                                  switch_value);
}

}  // namespace

SharedMemorySwitch::SharedMemorySwitch(
    std::string_view switch_name_in,
    [[maybe_unused]] RendezvousKey rendezvous_key_in,
    [[maybe_unused]] DescriptorKey descriptor_key_in)
    : switch_name(switch_name_in) {
#if BUILDFLAG(IS_APPLE)
  rendezvous_key = rendezvous_key_in;
#endif
#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE)
  descriptor_key = descriptor_key_in;
#endif
}

SharedMemorySwitch::~SharedMemorySwitch() = default;

SharedMemorySwitch::SharedMemorySwitch(SharedMemorySwitch&&) = default;
SharedMemorySwitch& SharedMemorySwitch::operator=(SharedMemorySwitch&&) =
    default;

void SharedMemorySwitch::AddToLaunchParameters(
    const ReadOnlySharedMemoryRegion& read_only_memory_region,
    CommandLine* command_line,
    LaunchOptions* launch_options) {
  CHECK(command_line);
  AddToLaunchParametersImpl(read_only_memory_region, this, command_line,
                            launch_options);
}

void SharedMemorySwitch::AddToLaunchParameters(
    const UnsafeSharedMemoryRegion& unsafe_memory_region,
    CommandLine* command_line,
    LaunchOptions* launch_options) {
  CHECK(command_line);
  AddToLaunchParametersImpl(unsafe_memory_region, this, command_line,
                            launch_options);
}

expected<UnsafeSharedMemoryRegion, SharedMemoryError>
UnsafeSharedMemoryRegionFrom(std::string_view switch_value) {
  auto platform_handle =
      Deserialize(switch_value, PlatformSharedMemoryRegion::Mode::kUnsafe);
  if (!platform_handle.has_value()) {
    return unexpected(platform_handle.error());
  }
  auto shmem_region =
      UnsafeSharedMemoryRegion::Deserialize(std::move(platform_handle).value());
  if (!shmem_region.IsValid()) {
    LOG(ERROR) << "Failed to deserialize writable memory handle";
    return unexpected(SharedMemoryError::kDeserializeFailed);
  }
  return ok(std::move(shmem_region));
}

expected<ReadOnlySharedMemoryRegion, SharedMemoryError>
ReadOnlySharedMemoryRegionFrom(std::string_view switch_value) {
  auto platform_handle =
      Deserialize(switch_value, PlatformSharedMemoryRegion::Mode::kReadOnly);
  if (!platform_handle.has_value()) {
    return unexpected(platform_handle.error());
  }
  auto shmem_region = ReadOnlySharedMemoryRegion::Deserialize(
      std::move(platform_handle).value());
  if (!shmem_region.IsValid()) {
    LOG(ERROR) << "Failed to deserialize read-only memory handle";
    return unexpected(SharedMemoryError::kDeserializeFailed);
  }
  return ok(std::move(shmem_region));
}

}  // namespace base::shared_memory
