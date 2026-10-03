// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/profiler/thread_profiler_platform_configuration.h"

#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/feature_list.h"
#include "base/functional/callback.h"
#include "base/notreached.h"
#include "base/profiler/stack_sampling_profiler.h"
#include "base/rand_util.h"
#include "build/build_config.h"
#include "chrome/common/profiler/process_type.h"
#include "components/sampling_profiler/process_type.h"

BASE_FEATURE(kSamplingProfilerOnWorkerThreads,
             base::FEATURE_DISABLED_BY_DEFAULT);

namespace {

// The default configuration to use in the absence of special circumstances on a
// specific platform.
class DefaultPlatformConfiguration
    : public ThreadProfilerPlatformConfiguration {
 public:
  explicit DefaultPlatformConfiguration(bool browser_test_mode_enabled);

  RelativePopulations GetEnableRates(
      std::optional<version_info::Channel> release_channel) const override;

  double GetChildProcessPerExecutionEnableFraction(
      sampling_profiler::ProfilerProcessType process) const override;

  std::optional<sampling_profiler::ProfilerProcessType> ChooseEnabledProcess()
      const override;

  bool IsEnabledForThread(
      sampling_profiler::ProfilerProcessType process,
      sampling_profiler::ProfilerThreadType thread,
      std::optional<version_info::Channel> release_channel) const override;

 protected:
  bool IsSupportedForChannel(
      std::optional<version_info::Channel> release_channel) const override;

  bool browser_test_mode_enabled() const { return browser_test_mode_enabled_; }

 private:
  const bool browser_test_mode_enabled_;
};

DefaultPlatformConfiguration::DefaultPlatformConfiguration(
    bool browser_test_mode_enabled)
    : browser_test_mode_enabled_(browser_test_mode_enabled) {}

ThreadProfilerPlatformConfiguration::RelativePopulations
DefaultPlatformConfiguration::GetEnableRates(
    std::optional<version_info::Channel> release_channel) const {
  CHECK(IsSupportedForChannel(release_channel));

  if (!release_channel) {
    // This is a local/CQ build.
    return RelativePopulations{0.0, 100.0, 0.0};
  }


  CHECK_NE(*release_channel, version_info::Channel::UNKNOWN);

  switch (*release_channel) {
    case version_info::Channel::BETA: {
      // TODO(crbug.com/1497983): Ramp up enable rate on Non-Android platforms.
      return RelativePopulations{90.0, 0.0, 10.0};
    }
    case version_info::Channel::STABLE: {
      static constexpr double experiment_rate = 0.006;
      return RelativePopulations{100.0 - experiment_rate, 0.0, experiment_rate};
    }
    default:
      return RelativePopulations{0.0, 80.0, 20.0};
  }
}

double DefaultPlatformConfiguration::GetChildProcessPerExecutionEnableFraction(
    sampling_profiler::ProfilerProcessType process) const {
  DCHECK_NE(sampling_profiler::ProfilerProcessType::kBrowser, process);

  // Profile all supported processes in browser test mode.
  if (browser_test_mode_enabled()) {
    return 1.0;
  }

  switch (process) {
    case sampling_profiler::ProfilerProcessType::kGpu:
    case sampling_profiler::ProfilerProcessType::kNetworkService:
      return 1.0;

    case sampling_profiler::ProfilerProcessType::kRenderer:
      // Run the profiler in 20% of the processes to collect roughly as many
      // profiles for renderer processes as browser processes.
      return 0.2;

    default:
      return 0.0;
  }
}

std::optional<sampling_profiler::ProfilerProcessType>
DefaultPlatformConfiguration::ChooseEnabledProcess() const {
  // Ignore the setting, sampling more than one process.
  return std::nullopt;
}

bool DefaultPlatformConfiguration::IsEnabledForThread(
    sampling_profiler::ProfilerProcessType process,
    sampling_profiler::ProfilerThreadType thread,
    std::optional<version_info::Channel> release_channel) const {
  if (thread == sampling_profiler::ProfilerThreadType::kThreadPoolWorker) {
    return base::FeatureList::IsEnabled(kSamplingProfilerOnWorkerThreads);
  }
  // Enable for all supported threads.
  return true;
}

bool DefaultPlatformConfiguration::IsSupportedForChannel(
    std::optional<version_info::Channel> release_channel) const {
  // The profiler is always supported for local builds and the CQ.
  if (!release_channel) {
    return true;
  }


  // All channels are supported in release builds.
  return *release_channel != version_info::Channel::UNKNOWN;
}


}  // namespace

// static
std::unique_ptr<ThreadProfilerPlatformConfiguration>
ThreadProfilerPlatformConfiguration::Create(
    bool browser_test_mode_enabled,
    base::RepeatingCallback<bool(double)> is_enabled_on_dev_callback) {
  return std::make_unique<DefaultPlatformConfiguration>(
      browser_test_mode_enabled);
}

bool ThreadProfilerPlatformConfiguration::IsSupported(
    std::optional<version_info::Channel> release_channel) const {
  return base::StackSamplingProfiler::IsSupportedForCurrentPlatform() &&
         IsSupportedForChannel(release_channel);
}

// static
bool ThreadProfilerPlatformConfiguration::IsEnabled(
    double enabled_probability) {
  DCHECK_GE(enabled_probability, 0.0);
  DCHECK_LE(enabled_probability, 1.0);
  return base::RandDouble() < enabled_probability;
}
