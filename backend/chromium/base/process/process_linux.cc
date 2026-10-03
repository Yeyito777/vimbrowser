// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process.h"

#include <errno.h>
#include <linux/magic.h>
#include <sys/resource.h>
#include <sys/vfs.h>

#include <optional>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/notreached.h"
#include "base/posix/can_lower_nice_to.h"
#include "base/process/internal_linux.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"


namespace base {


namespace {

const int kForegroundPriority = 0;

const int kBackgroundPriority = 5;

}  // namespace

Time Process::CreationTime() const {
  int64_t start_ticks = is_current()
                            ? internal::ReadProcSelfStatsAndGetFieldAsInt64(
                                  internal::VM_STARTTIME)
                            : internal::ReadProcStatsAndGetFieldAsInt64(
                                  Pid(), internal::VM_STARTTIME);

  if (!start_ticks) {
    return Time();
  }

  TimeDelta start_offset = internal::ClockTicksToTimeDelta(start_ticks);
  Time boot_time = internal::GetBootTime();
  if (boot_time.is_null()) {
    return Time();
  }
  return Time(boot_time + start_offset);
}

// static
bool Process::CanSetPriority() {

  static const bool can_reraise_priority =
      internal::CanLowerNiceTo(kForegroundPriority);
  return can_reraise_priority;
}

Process::Priority Process::GetPriority() const {
  DCHECK(IsValid());


  return GetOSPriority() == kBackgroundPriority ? Priority::kBestEffort
                                                : Priority::kUserBlocking;
}

bool Process::SetPriority(Priority priority) {
  DCHECK(IsValid());


  if (!CanSetPriority()) {
    return false;
  }

  int priority_value = priority == Priority::kBestEffort ? kBackgroundPriority
                                                         : kForegroundPriority;
  int result =
      setpriority(PRIO_PROCESS, static_cast<id_t>(process_), priority_value);
  DPCHECK(result == 0);
  return result == 0;
}


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
bool Process::IsSeccompSandboxed() {
  uint64_t seccomp_value = 0;
  if (!internal::ReadProcStatusAndGetFieldAsUint64(process_, "Seccomp",
                                                   &seccomp_value)) {
    return false;
  }
  return seccomp_value > 0;
}
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)


}  // namespace base
