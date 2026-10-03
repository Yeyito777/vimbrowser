// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/power_monitor/cpu_frequency_utils.h"

#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"


namespace base {
namespace {


}  // namespace

double EstimateCpuFrequency() {
  std::optional<CpuThroughputEstimationResult> result = EstimateCpuThroughput();
  return result ? result->estimated_frequency : 0.0;
}

std::optional<CpuThroughputEstimationResult> EstimateCpuThroughput() {
#if defined(ARCH_CPU_X86_FAMILY)
  TRACE_EVENT("base.power", "EstimateCpuThroughput");


  // The heuristic to estimate CPU frequency is based on UIforETW code.
  // see: https://github.com/google/UIforETW/blob/main/UIforETW/CPUFrequency.cpp
  //      https://github.com/google/UIforETW/blob/main/UIforETW/SpinALot64.asm
  base::ElapsedTimer timer;
  base::ElapsedThreadTimer thread_timer;
  const int kAmountOfIterations = 50000;
  const int kAmountOfInstructions = 10;
  for (int i = 0; i < kAmountOfIterations; ++i) {
    __asm__ __volatile__(
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        "addl  %%eax, %%eax\n"
        :
        :
        : "eax");
  }

  const base::TimeDelta elapsed_thread_time = thread_timer.Elapsed();
  const base::TimeDelta elapsed = timer.Elapsed();
  const double estimated_frequency =
      (kAmountOfIterations * kAmountOfInstructions) / elapsed.InSecondsF();

  CpuThroughputEstimationResult result{
      .estimated_frequency = estimated_frequency,
      .migrated = false,
      .wall_time = elapsed,
      .thread_time = elapsed_thread_time,
  };


  return result;
#else
  return std::nullopt;
#endif
}

BASE_EXPORT CpuFrequencyInfo GetCpuFrequencyInfo() {
  CpuFrequencyInfo cpu_info{
      .max_mhz = 0,
      .mhz_limit = 0,
      .type = CpuFrequencyInfo::CoreType::kPerformance,
      .num_active_cpus = 0,
  };


  return cpu_info;
}


}  // namespace base
