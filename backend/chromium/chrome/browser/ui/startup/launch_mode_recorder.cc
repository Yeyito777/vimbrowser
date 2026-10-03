// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/launch_mode_recorder.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/case_conversion.h"
#include "base/metrics/histogram_functions.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "components/url_formatter/url_formatter.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#include "chrome/browser/mac/dock.h"
#include "chrome/browser/mac/install_from_dmg.h"
#endif  // BUILDFLAG(IS_MAC)


namespace {

}  // namespace

// new LaunchMode implementation below.

namespace {

void RecordLaunchMode(const base::CommandLine command_line,
                      std::optional<LaunchMode> mode) {
  if (mode.value_or(LaunchMode::kNone) == LaunchMode::kNone) {
    return;
  }
  base::UmaHistogramEnumeration("Launch.Mode2", mode.value());
}

#if BUILDFLAG(IS_MAC)
std::optional<LaunchMode> GetLaunchModeSlow(
    const base::CommandLine command_line) {
  NOTREACHED();
}

std::optional<LaunchMode> GetLaunchModeFast(
    const base::CommandLine& command_line) {
  DiskImageStatus dmg_launch_status =
      IsAppRunningFromReadOnlyDiskImage(nullptr);
  dock::ChromeInDockStatus dock_launch_status = dock::ChromeIsInTheDock();

  if (dock_launch_status == dock::ChromeInDockFailure &&
      dmg_launch_status == DiskImageStatusFailure) {
    return LaunchMode::kMacDockDMGStatusError;
  }

  if (dock_launch_status == dock::ChromeInDockFailure) {
    return LaunchMode::kMacDockStatusError;
  }

  if (dmg_launch_status == DiskImageStatusFailure) {
    return LaunchMode::kMacDMGStatusError;
  }

  bool dmg_launch = dmg_launch_status == DiskImageStatusTrue;
  bool dock_launch = dock_launch_status == dock::ChromeInDockTrue;

  if (dmg_launch && dock_launch) {
    return LaunchMode::kMacDockedDMGLaunch;
  }

  if (dmg_launch) {
    return LaunchMode::kMacUndockedDMGLaunch;
  }

  if (dock_launch) {
    return LaunchMode::kMacDockedDiskLaunch;
  }

  return LaunchMode::kMacUndockedDiskLaunch;
}
#else  //  !IS_WIN && !IS_MAC
std::optional<LaunchMode> GetLaunchModeSlow(
    const base::CommandLine command_line) {
  NOTREACHED();
}

std::optional<LaunchMode> GetLaunchModeFast(
    const base::CommandLine& command_line) {
  return LaunchMode::kOtherOS;
}

#endif  // BUILDFLAG(IS_WIN)

}  // namespace

void ComputeAndRecordLaunchMode(const base::CommandLine& command_line) {
  ComputeLaunchMode(command_line,
                    base::BindOnce(&RecordLaunchMode, command_line));
}

// Computes the launch mode based on `command_line` and process state. Runs
// `result_callback` with the result either synchronously or asynchronously on
// the caller's sequence.
void ComputeLaunchMode(
    const base::CommandLine& command_line,
    base::OnceCallback<void(std::optional<LaunchMode>)> result_callback) {
  if (auto mode = GetLaunchModeFast(command_line); mode.has_value()) {
    std::move(result_callback).Run(mode);
    return;
  }
  auto split = base::SplitOnceCallback(std::move(result_callback));
  if (!base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
          base::BindOnce(&GetLaunchModeSlow, command_line),
          std::move(split.first))) {
    std::move(split.second).Run(std::nullopt);
  }
}

base::OnceCallback<void(std::optional<LaunchMode>)>
GetRecordLaunchModeForTesting() {
  return base::BindOnce(&RecordLaunchMode,
                        base::CommandLine(base::CommandLine::NO_PROGRAM));
}
