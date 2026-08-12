// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/post_delayed_memory_reduction_task.h"

#include "base/timer/timer.h"


namespace base {

void PostDelayedMemoryReductionTask(
    scoped_refptr<SequencedTaskRunner> task_runner,
    const Location& from_here,
    OnceClosure task,
    base::TimeDelta delay) {
  task_runner->PostDelayedTask(from_here, std::move(task), delay);
}

void PostDelayedMemoryReductionTask(
    scoped_refptr<SequencedTaskRunner> task_runner,
    const Location& from_here,
    OnceCallback<void(MemoryReductionTaskContext)> task,
    base::TimeDelta delay) {
  task_runner->PostDelayedTask(
      from_here,
      BindOnce(std::move(task), MemoryReductionTaskContext::kDelayExpired),
      delay);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *                OneShotDelayedBackgroundTimer::TimerImpl                   *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// This implementation is just a small wrapper around a |base::OneShotTimer|.
class OneShotDelayedBackgroundTimer::TimerImpl final
    : public OneShotDelayedBackgroundTimer::OneShotDelayedBackgroundTimerImpl {
 public:
  ~TimerImpl() override = default;
  void Start(const Location& from_here,
             TimeDelta delay,
             OnceCallback<void(MemoryReductionTaskContext)> task) override {
    timer_.Start(
        from_here, delay,
        BindOnce(std::move(task), MemoryReductionTaskContext::kDelayExpired));
  }
  void Stop() override { timer_.Stop(); }
  bool IsRunning() const override { return timer_.IsRunning(); }
  void SetTaskRunner(scoped_refptr<SequencedTaskRunner> task_runner) override {
    timer_.SetTaskRunner(std::move(task_runner));
  }

 private:
  OneShotTimer timer_;
};

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *                                TaskImpl                                   *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *                       OneShotDelayedBackgroundTimer                       *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

OneShotDelayedBackgroundTimer::OneShotDelayedBackgroundTimer() {
  impl_ = std::make_unique<TimerImpl>();
}

OneShotDelayedBackgroundTimer::~OneShotDelayedBackgroundTimer() {
  Stop();
}

void OneShotDelayedBackgroundTimer::Stop() {
  impl_->Stop();
}

bool OneShotDelayedBackgroundTimer::IsRunning() const {
  return impl_->IsRunning();
}

void OneShotDelayedBackgroundTimer::SetTaskRunner(
    scoped_refptr<SequencedTaskRunner> task_runner) {
  impl_->SetTaskRunner(std::move(task_runner));
}

void OneShotDelayedBackgroundTimer::Start(
    const Location& from_here,
    TimeDelta delay,
    OnceCallback<void(MemoryReductionTaskContext)> task) {
  impl_->Start(from_here, delay, std::move(task));
}

}  // namespace base
