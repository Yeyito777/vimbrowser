// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/socket_broker_client.h"

#include <utility>

#include "base/process/process_handle.h"
#include "services/network/public/cpp/socket_broker_impl.h"

// SocketBrokerClient is completely covered by socket_broker_impl_unittest.cc,
// and so doesn't have its own unit tests.

namespace network {

SocketBrokerClient::SocketBrokerClient(
    mojo::PendingRemote<mojom::SocketBroker> socket_broker)
    : socket_broker_(std::move(socket_broker))
{
}

SocketBrokerClient::~SocketBrokerClient() = default;

void SocketBrokerClient::CreateTcpSocket(
    net::AddressFamily address_family,
    mojom::SocketBroker::CreateTcpSocketCallback callback) {
  socket_broker_->CreateTcpSocket(address_family, std::move(callback));
}

void SocketBrokerClient::CreateUdpSocket(
    net::AddressFamily address_family,
    mojom::SocketBroker::CreateUdpSocketCallback callback) {
  socket_broker_->CreateUdpSocket(address_family, std::move(callback));
}

}  // namespace network
