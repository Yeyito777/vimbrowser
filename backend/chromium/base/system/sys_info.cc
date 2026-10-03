// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/system/sys_info.h"

#include <algorithm>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/features.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/notreached.h"
#include "base/numerics/safe_conversions.h"
#include "base/system/sys_info_internal.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace base {
namespace {
std::optional<ByteSize> g_amount_of_physical_memory_for_testing;
}  // namespace

// static
int SysInfo::NumberOfEfficientProcessors() {
  static int number_of_efficient_processors = NumberOfEfficientProcessorsImpl();
  return number_of_efficient_processors;
}

// static
ByteSize SysInfo::AmountOfTotalPhysicalMemory() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableLowEndDeviceMode)) {
    // Keep using 512MB as the simulated RAM amount for when users or tests have
    // manually enabled low-end device mode. Note this value is different from
    // the threshold used for low end devices.
    constexpr ByteSize kSimulatedMemoryForEnableLowEndDeviceMode = MiBU(512);
    return std::min(kSimulatedMemoryForEnableLowEndDeviceMode,
                    AmountOfTotalPhysicalMemoryImpl());
  }

  if (g_amount_of_physical_memory_for_testing) {
    return *g_amount_of_physical_memory_for_testing;
  }

  return AmountOfTotalPhysicalMemoryImpl();
}

// static
ByteSize SysInfo::AmountOfAvailablePhysicalMemory() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableLowEndDeviceMode)) {
    // Estimate the available memory by subtracting our memory used estimate
    // from the fake |kLowMemoryDeviceThresholdMB| limit.
    const ByteSize memory_used =
        ByteSize::FromByteSizeDelta(AmountOfTotalPhysicalMemoryImpl() -
                                    AmountOfAvailablePhysicalMemoryImpl());
    const ByteSize memory_limit = MiBU(
        checked_cast<unsigned>(features::kLowMemoryDeviceThresholdMB.Get()));
    // |memory_used| can be > |memory_limit|.
    const ByteSizeDelta memory_available = memory_limit - memory_used;
    return memory_available.is_positive() ? memory_available.AsByteSize()
                                          : ByteSize(0);
  }

  return AmountOfAvailablePhysicalMemoryImpl();
}

bool SysInfo::IsLowEndDevice() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableLowEndDeviceMode)) {
    return true;
  }

  return IsLowEndDeviceImpl();
}


// TODO(crbug.com/40264947): This method is for chromium native code.
// We need to update the java-side code, i.e.
// base/android/java/src/org/chromium/base/SysUtils.java,
// and to make the selected components in java to see this feature.
bool SysInfo::IsLowEndDeviceOrPartialLowEndModeEnabled() {
  return base::SysInfo::IsLowEndDevice();
}

bool SysInfo::IsLowEndDeviceOrPartialLowEndModeEnabled(
    const FeatureParam<bool>& param_for_exclusion) {
  return base::SysInfo::IsLowEndDevice();
}

bool DetectLowEndDevice() {
  // Keep in sync with the Android implementation of this function.
  // LINT.IfChange
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableLowEndDeviceMode)) {
    return true;
  }
  if (command_line->HasSwitch(switches::kDisableLowEndDeviceMode)) {
    return false;
  }

  ByteSize ram_size = SysInfo::AmountOfTotalPhysicalMemory();
  return ram_size > ByteSize(0) &&
         ram_size <= MiBU(checked_cast<unsigned>(
                         features::kLowMemoryDeviceThresholdMB.Get()));
  // LINT.ThenChange(//base/android/java/src/org/chromium/base/SysUtils.java)
}

// static
bool SysInfo::IsLowEndDeviceImpl() {
  static internal::LazySysInfoValue<bool, DetectLowEndDevice> instance;
  return instance.value();
}

#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_WIN) &&  !BUILDFLAG(IS_CHROMEOS)
std::string SysInfo::HardwareModelName() {
  return std::string();
}
#endif

std::string SysInfo::SocManufacturer() {
  return std::string();
}

void SysInfo::GetHardwareInfo(base::OnceCallback<void(HardwareInfo)> callback) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)
  constexpr base::TaskTraits kTraits = {base::MayBlock()};
#else
  constexpr base::TaskTraits kTraits = {};
#endif

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kTraits, base::BindOnce(&GetHardwareInfoSync),
      std::move(callback));
}

// static
base::TimeDelta SysInfo::Uptime() {
  // This code relies on an implementation detail of TimeTicks::Now() - that
  // its return value happens to coincide with the system uptime value in
  // microseconds, on Win/Mac/iOS/Linux/ChromeOS and Android.
  int64_t uptime_in_microseconds = TimeTicks::Now().ToInternalValue();
  return base::Microseconds(uptime_in_microseconds);
}

// static
std::string SysInfo::ProcessCPUArchitecture() {
#if defined(ARCH_CPU_X86)
  return "x86";
#elif defined(ARCH_CPU_X86_64)
  return "x86_64";
#elif defined(ARCH_CPU_ARMEL)
  return "ARM";
#elif defined(ARCH_CPU_ARM64)
  return "ARM_64";
#elif defined(ARCH_CPU_RISCV64)
  return "RISCV_64";
#else
  return std::string();
#endif
}

// static
std::optional<ByteSize> SysInfo::SetAmountOfPhysicalMemoryForTesting(
    ByteSize amount_of_memory) {
  std::optional<ByteSize> current = g_amount_of_physical_memory_for_testing;
  g_amount_of_physical_memory_for_testing.emplace(amount_of_memory);
  return current;
}

// static
void SysInfo::ClearAmountOfPhysicalMemoryForTesting() {
  g_amount_of_physical_memory_for_testing.reset();
}

}  // namespace base
