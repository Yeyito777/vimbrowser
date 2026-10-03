// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/platform/platform_channel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "base/logging.h"
#include "base/numerics/clamped_math.h"
#include "build/build_config.h"
#include "mojo/buildflags.h"
#include "mojo/core/embedder/features.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/files/scoped_file.h"
#include "base/posix/global_descriptors.h"

#if BUILDFLAG(MOJO_USE_APPLE_CHANNEL)
#include <mach/port.h>

#include "base/apple/mach_logging.h"
#include "base/apple/scoped_mach_port.h"
#endif

#include <sys/socket.h>

namespace mojo {

namespace {

#if BUILDFLAG(MOJO_USE_APPLE_CHANNEL)
void CreateChannel(PlatformHandle* local_endpoint,
                   PlatformHandle* remote_endpoint) {
  // Mach messaging is simplex; and in order to enable full-duplex
  // communication, the Mojo channel implementation performs an internal
  // handshake with its peer to establish two sets of Mach receive and send
  // rights. The handshake process starts with the creation of one
  // PlatformChannel endpoint.
  base::apple::ScopedMachReceiveRight receive;
  base::apple::ScopedMachSendRight send;
  // The mpl_qlimit specified here should stay in sync with
  // NamedPlatformChannel.
  CHECK(base::apple::CreateMachPort(&receive, &send, MACH_PORT_QLIMIT_LARGE));

  // In a reverse of Mach messaging semantics, in Mojo the "local" endpoint is
  // the send right, while the "remote" end is the receive right.
  *local_endpoint = PlatformHandle(std::move(send));
  *remote_endpoint = PlatformHandle(std::move(receive));
}
#else
void CreateChannel(PlatformHandle* local_endpoint,
                   PlatformHandle* remote_endpoint) {
  int fds[2];
  PCHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

  // Set non-blocking on both ends.
  PCHECK(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
  PCHECK(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);

  *local_endpoint = PlatformHandle(base::ScopedFD(fds[0]));
  *remote_endpoint = PlatformHandle(base::ScopedFD(fds[1]));
  DCHECK(local_endpoint->is_valid());
  DCHECK(remote_endpoint->is_valid());
}
#endif

}  // namespace

PlatformChannel::PlatformChannel() {
  PlatformHandle local_handle;
  PlatformHandle remote_handle;
  CreateChannel(&local_handle, &remote_handle);
  local_endpoint_ = PlatformChannelEndpoint(std::move(local_handle));
  remote_endpoint_ = PlatformChannelEndpoint(std::move(remote_handle));
}

PlatformChannel::PlatformChannel(PlatformChannelEndpoint local,
                                 PlatformChannelEndpoint remote)
    : local_endpoint_(std::move(local)), remote_endpoint_(std::move(remote)) {}

PlatformChannel::PlatformChannel(PlatformChannel&& other) = default;

PlatformChannel& PlatformChannel::operator=(PlatformChannel&& other) = default;

PlatformChannel::~PlatformChannel() = default;

void PlatformChannel::PrepareToPassRemoteEndpoint(HandlePassingInfo* info,
                                                  std::string* value) {
  remote_endpoint_.PrepareToPass(*info, *value);
}

std::string PlatformChannel::PrepareToPassRemoteEndpoint(
    base::LaunchOptions& options) {
  return remote_endpoint_.PrepareToPass(options);
}

void PlatformChannel::PrepareToPassRemoteEndpoint(
    HandlePassingInfo* info,
    base::CommandLine* command_line) {
  remote_endpoint_.PrepareToPass(*info, *command_line);
}

void PlatformChannel::PrepareToPassRemoteEndpoint(
    base::LaunchOptions* options,
    base::CommandLine* command_line) {
  remote_endpoint_.PrepareToPass(*options, *command_line);
}

void PlatformChannel::RemoteProcessLaunchAttempted() {
  remote_endpoint_.ProcessLaunchAttempted();
}

// static
PlatformChannelEndpoint PlatformChannel::RecoverPassedEndpointFromString(
    std::string_view value) {
  return PlatformChannelEndpoint::RecoverFromString(value);
}

// static
PlatformChannelEndpoint PlatformChannel::RecoverPassedEndpointFromCommandLine(
    const base::CommandLine& command_line) {
  return RecoverPassedEndpointFromString(
      command_line.GetSwitchValueASCII(kHandleSwitch));
}

// static
bool PlatformChannel::CommandLineHasPassedEndpoint(
    const base::CommandLine& command_line) {
  return command_line.HasSwitch(kHandleSwitch);
}

}  // namespace mojo
