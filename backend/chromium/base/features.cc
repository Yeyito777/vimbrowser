// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/features.h"

#include <atomic>

#include "base/debug/stack_trace.h"
#include "base/files/file_path.h"
#include "base/task/sequence_manager/sequence_manager_impl.h"
#include "base/task/thread_pool/job_task_source.h"
#include "base/threading/platform_thread.h"
#include "build/blink_buildflags.h"
#include "build/buildflag.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "base/message_loop/message_pump_epoll.h"
#endif

#if BUILDFLAG(IS_APPLE)
#include "base/files/file.h"
#include "base/message_loop/message_pump_apple.h"
#include "base/synchronization/condition_variable.h"

#if !BUILDFLAG(IS_IOS) || !BUILDFLAG(USE_BLINK)
#include "base/message_loop/message_pump_kqueue.h"
#endif

#endif



namespace base::features {

namespace {

// An atomic is used because this can be queried racily by a thread checking if
// an optimization is enabled and a thread initializing this from the
// FeatureList. All operations use std::memory_order_relaxed because there are
// no dependent memory operations.
std::atomic_bool g_is_reduce_ppms_enabled{false};

}  // namespace

// Alphabetical:

// Controls caching within BASE_FEATURE_PARAM(). This is feature-controlled
// so that ScopedFeatureList can disable it to turn off caching.
BASE_FEATURE(kFeatureParamWithCache, FEATURE_ENABLED_BY_DEFAULT);

// Whether a fast implementation of FilePath::IsParent is used. This feature
// exists to ensure that the fast implementation can be disabled quickly if
// issues are found with it.
BASE_FEATURE(kFastFilePathIsParent, FEATURE_ENABLED_BY_DEFAULT);

// Use non default low memory device threshold.
// Value should be given via |LowMemoryDeviceThresholdMB|.
// Updated Desktop default threshold to match the Android 2021 definition.
#define LOW_MEMORY_DEVICE_THRESHOLD_MB 2048
BASE_FEATURE(kLowEndMemoryExperiment, FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE_PARAM(int,
                   kLowMemoryDeviceThresholdMB,
                   &kLowEndMemoryExperiment,
                   "LowMemoryDeviceThresholdMB",
                   LOW_MEMORY_DEVICE_THRESHOLD_MB);

BASE_FEATURE(kReducePPMs, FEATURE_ENABLED_BY_DEFAULT);

// Apply base::ScopedBestEffortExecutionFence to registered task queues as well
// as the thread pool.
BASE_FEATURE(kScopedBestEffortExecutionFenceForTaskQueue,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Use simdutf for base::Base64Encode() and base::Base64EncodeAppend().
BASE_FEATURE(kSimdutfBase64Encode, base::FEATURE_DISABLED_BY_DEFAULT);

// Whether to restrict the max gap between the frame pointer and the stack end
// for stack scanning. If the gap is beyond the given gap threshold, the stack
// end is treated as unreliable. Stack scanning stops when that happens.
// This feature is only in effect when BUILDFLAG(CAN_UNWIND_WITH_FRAME_POINTERS)
// is on and `TraceStackFramePointers` would run stack scanning. Default gap
// threshold is an absurdly large 100MB.
// The feature is enabled by default on ChromeOS where crashes caused by
// unreliable stack end are found. See https://crbug.com/402542102
BASE_FEATURE(kStackScanMaxFramePointerToStackEndGap,
             FEATURE_DISABLED_BY_DEFAULT
);
BASE_FEATURE_PARAM(int,
                   kStackScanMaxFramePointerToStackEndGapThresholdMB,
                   &kStackScanMaxFramePointerToStackEndGap,
                   "StackScanMaxFramePointerToStackEndGapThresholdMB",
                   100);



// When enabled, GetTerminationStatus() returns
// TERMINATION_STATUS_EVICTED_FOR_MEMORY for processes terminated due to commit
// failures. Otherwise, it returns TERMINATION_STATUS_OOM.
BASE_FEATURE(kUseTerminationStatusMemoryExhaustion, FEATURE_ENABLED_BY_DEFAULT);


bool IsReducePPMsEnabled() {
  return g_is_reduce_ppms_enabled.load(std::memory_order_relaxed);
}

void Init() {
  g_is_reduce_ppms_enabled.store(FeatureList::IsEnabled(kReducePPMs),
                                 std::memory_order_relaxed);

  sequence_manager::internal::SequenceManagerImpl::InitializeFeatures();
  sequence_manager::internal::ThreadController::InitializeFeatures();
  base::internal::JobTaskSource::InitializeFeatures();

  debug::StackTrace::InitializeFeatures();
  FilePath::InitializeFeatures();

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  MessagePumpEpoll::InitializeFeatures();
#endif

#if BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_CHROMEOS)
  PlatformThread::InitializeFeatures();
#endif

#if BUILDFLAG(IS_APPLE)
  MessagePumpCFRunLoopBase::InitializeFeatures();

// Kqueue is not used for ios blink.
#if !BUILDFLAG(IS_IOS) || !BUILDFLAG(USE_BLINK)
  MessagePumpKqueue::InitializeFeatures();
#endif

#endif


}

}  // namespace base::features
