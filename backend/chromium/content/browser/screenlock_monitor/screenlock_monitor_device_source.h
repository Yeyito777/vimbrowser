// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SCREENLOCK_MONITOR_SCREENLOCK_MONITOR_DEVICE_SOURCE_H_
#define CONTENT_BROWSER_SCREENLOCK_MONITOR_SCREENLOCK_MONITOR_DEVICE_SOURCE_H_

#include <memory>

#include "build/build_config.h"
#include "content/browser/screenlock_monitor/screenlock_monitor_source.h"
#include "content/common/content_export.h"




namespace content {

// A class used to monitor the screenlock state change on each supported
// platform and notify the change event to monitor.
class CONTENT_EXPORT ScreenlockMonitorDeviceSource
    : public ScreenlockMonitorSource {
 public:
  ScreenlockMonitorDeviceSource();

  ScreenlockMonitorDeviceSource(const ScreenlockMonitorDeviceSource&) = delete;
  ScreenlockMonitorDeviceSource& operator=(
      const ScreenlockMonitorDeviceSource&) = delete;

  ~ScreenlockMonitorDeviceSource() override;


 private:

#if BUILDFLAG(IS_MAC)
  void StartListeningForScreenlock();
  void StopListeningForScreenlock();
#endif  // BUILDFLAG(IS_MAC)

};

}  // namespace content

#endif  // CONTENT_BROWSER_SCREENLOCK_MONITOR_SCREENLOCK_MONITOR_DEVICE_SOURCE_H_
