// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_POWER_MONITOR_POWER_MONITOR_DEVICE_SOURCE_H_
#define BASE_POWER_MONITOR_POWER_MONITOR_DEVICE_SOURCE_H_

#include <memory>
#include <vector>

#include "base/base_export.h"
#include "base/power_monitor/power_monitor_source.h"
#include "base/power_monitor/power_observer.h"
#include "build/build_config.h"


#if BUILDFLAG(IS_MAC)
#include <IOKit/IOTypes.h>

#include "base/apple/scoped_cftyperef.h"
#include "base/mac/scoped_ionotificationportref.h"
#include "base/power_monitor/battery_level_provider.h"
#include "base/power_monitor/iopm_power_source_sampling_event_source.h"
#include "base/power_monitor/thermal_state_observer_mac.h"
#endif  // BUILDFLAG(IS_MAC)


namespace base {

// A class used to monitor the power state change and notify the observers about
// the change event.
class BASE_EXPORT PowerMonitorDeviceSource : public PowerMonitorSource {
 public:
  PowerMonitorDeviceSource();

  PowerMonitorDeviceSource(const PowerMonitorDeviceSource&) = delete;
  PowerMonitorDeviceSource& operator=(const PowerMonitorDeviceSource&) = delete;

  ~PowerMonitorDeviceSource() override;


 private:
  friend class PowerMonitorDeviceSourceTest;


#if (BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_IOS_TVOS)) || BUILDFLAG(IS_WIN)
  void PlatformInit();
  void PlatformDestroy();
#endif  // BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_MAC)
  // Callback from IORegisterForSystemPower(). |refcon| is the |this| pointer.
  static void SystemPowerEventCallback(void* refcon,
                                       io_service_t service,
                                       natural_t message_type,
                                       void* message_argument);
#endif  // BUILDFLAG(IS_MAC)

  // Platform-specific method to check whether the system is currently
  // running on battery power. Returns kBatteryPower if running on battery,
  // kExternalPower if running on external power or kUnknown if the power
  // state is unknown (for example, during early process lifetime when the
  // state hasn't been obtained yet).
  PowerStateObserver::BatteryPowerStatus GetBatteryPowerStatus() const override;



#if BUILDFLAG(IS_MAC)
  // PowerMonitorSource:
  PowerThermalObserver::DeviceThermalState GetCurrentThermalState()
      const override;
  int GetInitialSpeedLimit() const override;

  // Retrieves the current battery state to update `is_on_battery_`.
  void GetBatteryState();
  void OnBatteryStateReceived(
      const std::optional<BatteryLevelProvider::BatteryState>& battery_state);

  // Reference to the system IOPMrootDomain port.
  io_connect_t power_manager_port_ = IO_OBJECT_NULL;

  // Notification port that delivers power (sleep/wake) notifications.
  mac::ScopedIONotificationPortRef notification_port_;

  // Notifier reference for the |notification_port_|.
  io_object_t notifier_ = IO_OBJECT_NULL;

  // Generates power-source-change events.
  IOPMPowerSourceSamplingEventSource power_source_event_source_;

  std::unique_ptr<BatteryLevelProvider> battery_level_provider_;

  // Observer of thermal state events: critical temperature etc.
  std::unique_ptr<ThermalStateObserverMac> thermal_state_observer_;

  PowerStateObserver::BatteryPowerStatus battery_power_status_ =
      PowerStateObserver::BatteryPowerStatus::kUnknown;
#endif



};

}  // namespace base

#endif  // BASE_POWER_MONITOR_POWER_MONITOR_DEVICE_SOURCE_H_
