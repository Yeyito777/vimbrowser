// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/transferable_socket.h"

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
#include "base/files/scoped_file.h"
#include "mojo/public/cpp/platform/platform_handle.h"
#else
#error "unsupported platform"
#endif

#include "base/dcheck_is_on.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/process/process_handle.h"
#include "net/socket/tcp_socket.h"
#include "net/socket/udp_socket.h"

namespace network {

TransferableSocket::TransferableSocket() = default;

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
TransferableSocket::TransferableSocket(net::SocketDescriptor socket)
    : socket_(base::ScopedFD(socket)) {}
#else
#error "Unsupported Platform"
#endif  // BUILDFLAG(IS_WIN)

TransferableSocket::~TransferableSocket() = default;
TransferableSocket& TransferableSocket::operator=(TransferableSocket&& other) =
    default;
TransferableSocket::TransferableSocket(TransferableSocket&& other) = default;

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
TransferableSocket::TransferableSocket(mojo::PlatformHandle socket)
    : socket_(std::move(socket)) {}
#else
#error "Unsupported Platform"
#endif

net::SocketDescriptor TransferableSocket::TakeSocket() {
#if DCHECK_IS_ON()
  DCHECK(has_been_transferred_)
      << "Cannot take a socket before transferring across processes.";
#endif
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  return socket_.ReleaseFD();
#else
#error "Unsupported platform"
#endif  // BUILDFLAG(IS_WIN)
}

}  // namespace network
