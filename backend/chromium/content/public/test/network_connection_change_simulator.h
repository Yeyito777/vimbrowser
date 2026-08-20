// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_NETWORK_CONNECTION_CHANGE_SIMULATOR_H_
#define CONTENT_PUBLIC_TEST_NETWORK_CONNECTION_CHANGE_SIMULATOR_H_

#include "services/network/public/cpp/network_connection_tracker.h"

namespace base {
class RunLoop;
}

namespace content {

// A class to help tests set the network connection type.
class NetworkConnectionChangeSimulator
    : public network::NetworkConnectionTracker::NetworkConnectionObserver {
 public:
  NetworkConnectionChangeSimulator();

  NetworkConnectionChangeSimulator(const NetworkConnectionChangeSimulator&) =
      delete;
  NetworkConnectionChangeSimulator& operator=(
      const NetworkConnectionChangeSimulator&) = delete;

  ~NetworkConnectionChangeSimulator() override;


  // Synchronously sets the connection type.
  void SetConnectionType(
      net::NetworkChangeNotifier::ConnectionType connection_type);

 private:
  static void SimulateNetworkChange(
      net::NetworkChangeNotifier::ConnectionType type);

  // network::NetworkConnectionTracker::NetworkConnectionObserver:
  void OnConnectionChanged(
      net::NetworkChangeNotifier::ConnectionType type) override;

  std::unique_ptr<base::RunLoop> run_loop_;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_NETWORK_CONNECTION_CHANGE_SIMULATOR_H_
