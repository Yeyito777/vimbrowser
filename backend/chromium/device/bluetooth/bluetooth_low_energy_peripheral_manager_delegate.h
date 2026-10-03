// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_BLUETOOTH_LOW_ENERGY_PERIPHERAL_MANAGER_DELEGATE_H_
#define DEVICE_BLUETOOTH_BLUETOOTH_LOW_ENERGY_PERIPHERAL_MANAGER_DELEGATE_H_

#import <CoreBluetooth/CoreBluetooth.h>

#include "build/build_config.h"

#import <IOBluetooth/IOBluetooth.h>

namespace device {
class BluetoothLowEnergyAdvertisementManagerMac;
}  // namespace device

@interface BluetoothLowEnergyPeripheralManagerDelegate
    : NSObject<CBPeripheralManagerDelegate>

- (instancetype)initWithAdvertisementManager:
    (device::BluetoothLowEnergyAdvertisementManagerMac*)advertisementManager;

@end

#endif  // DEVICE_BLUETOOTH_BLUETOOTH_LOW_ENERGY_PERIPHERAL_MANAGER_DELEGATE_H_
