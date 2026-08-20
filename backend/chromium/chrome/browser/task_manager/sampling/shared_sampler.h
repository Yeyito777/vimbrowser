// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TASK_MANAGER_SAMPLING_SHARED_SAMPLER_H_
#define CHROME_BROWSER_TASK_MANAGER_SAMPLING_SHARED_SAMPLER_H_

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/process/process_handle.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace task_manager {

struct ProcessDataSnapshot;

// Defines sampler that will calculate resources for all processes all at once,
// on the worker thread. Created by TaskManagerImpl on the UI thread, but used
// mainly on a blocking pool thread.
//
// This exists because on Windows it is much faster to collect a group of
// process metrics for all processes all at once using NtQuerySystemInformation
// than to query the same data for for each process individually and because
// some types like Idle Wakeups can only be collected this way.
class SharedSampler : public base::RefCountedThreadSafe<SharedSampler> {
 public:
  explicit SharedSampler(
      const scoped_refptr<base::SequencedTaskRunner>& blocking_pool_runner);

  SharedSampler(const SharedSampler&) = delete;
  SharedSampler& operator=(const SharedSampler&) = delete;

  struct SamplingResult {
    base::TimeDelta cpu_time;
    int64_t hard_faults_per_second;
    int idle_wakeups_per_second;
    base::Time start_time;
  };
  using OnSamplingCompleteCallback =
      base::RepeatingCallback<void(std::optional<SamplingResult>)>;

  // Returns a combination of refresh flags supported by the shared sampler.
  int64_t GetSupportedFlags() const;

  // Registers task group specific callbacks.
  void RegisterCallback(base::ProcessId process_id,
                        OnSamplingCompleteCallback on_sampling_complete);

  // Unregisters task group specific callbacks.
  void UnregisterCallback(base::ProcessId process_id);

  // Triggers a refresh of the expensive process' stats, on the worker thread.
  void Refresh(base::ProcessId process_id, int64_t refresh_flags);


 private:
  friend class base::RefCountedThreadSafe<SharedSampler>;
  ~SharedSampler();

  typedef std::map<base::ProcessId, OnSamplingCompleteCallback> CallbacksMap;

};

}  // namespace task_manager

#endif  // CHROME_BROWSER_TASK_MANAGER_SAMPLING_SHARED_SAMPLER_H_
