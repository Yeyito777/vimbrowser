// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/socket_broker_impl.h"

#include <errno.h>

#include <type_traits>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/address_family.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"
#include "net/socket/socket_descriptor.h"
#include "net/socket/tcp_socket.h"
#include "services/network/public/cpp/transferable_socket.h"

#include <netinet/in.h>
#include <sys/socket.h>

namespace network {

namespace {

// If CreateTcpSocketCallback and CreateUdpSocketCallback ever become different
// types this code will have to be modified.
using CreateSocketCallback = SocketBrokerImpl::CreateTcpSocketCallback;
static_assert(std::same_as<CreateSocketCallback,
                           SocketBrokerImpl::CreateUdpSocketCallback>);


using ScopedSocketDescriptor = base::ScopedFD;

net::Error GetSystemError() {
  return net::MapSystemError(errno);
}

// Transfers `socket` to `callback`, also passing `rv`.
void TransferSocketToCallback(CreateSocketCallback callback,
                              ScopedSocketDescriptor socket,
                              int rv) {
  std::move(callback).Run(network::TransferableSocket(socket.release()), rv);
}


enum class SocketType {
  kStream,
  kDatagram,
};

std::pair<ScopedSocketDescriptor, int> CreateSocket(
    net::AddressFamily address_family,
    SocketType type,
    SocketBrokerImpl::SocketCreationInterceptor socket_creation_interceptor) {
  if (!socket_creation_interceptor.is_null()) {
    int rv = socket_creation_interceptor.Run();
    if (rv != net::OK) {
      return {ScopedSocketDescriptor(net::kInvalidSocket), rv};
    }
  }
  const int sock_type = type == SocketType::kStream ? SOCK_STREAM : SOCK_DGRAM;
  int ip_protocol = type == SocketType::kStream ? IPPROTO_TCP : IPPROTO_UDP;
  if (address_family == AF_UNIX) {
    ip_protocol = 0;
  }
  ScopedSocketDescriptor socket(net::CreatePlatformSocket(
      net::ConvertAddressFamily(address_family), sock_type, ip_protocol));
  int rv = net::OK;
  if (!socket.is_valid() || !base::SetNonBlocking(socket.get())) {
    rv = GetSystemError();
    socket.reset();
  }
  return {std::move(socket), rv};
}

}  // namespace

SocketBrokerImpl::SocketBrokerImpl() = default;

SocketBrokerImpl::~SocketBrokerImpl() = default;


void SocketBrokerImpl::CreateTcpSocket(net::AddressFamily address_family,
                                       CreateTcpSocketCallback callback) {
  auto [socket, rv] = CreateSocket(address_family, SocketType::kStream,
                                   socket_creation_interceptor_);
  TransferSocketToCallback(std::move(callback), std::move(socket), rv);
}

void SocketBrokerImpl::CreateUdpSocket(net::AddressFamily address_family,
                                       CreateUdpSocketCallback callback) {
  auto [socket, rv] = CreateSocket(address_family, SocketType::kDatagram,
                                   socket_creation_interceptor_);
  TransferSocketToCallback(std::move(callback), std::move(socket), rv);
}


mojo::PendingRemote<mojom::SocketBroker> SocketBrokerImpl::BindNewRemote() {
  mojo::PendingRemote<mojom::SocketBroker> pending_remote;
  receivers_.Add(this, pending_remote.InitWithNewPipeAndPassReceiver());
  return pending_remote;
}

}  // namespace network
