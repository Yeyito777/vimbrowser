// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_CHILD_PROCESS_TERMINATION_INFO_H_
#define CONTENT_PUBLIC_BROWSER_CHILD_PROCESS_TERMINATION_INFO_H_

#include <optional>

#include "base/process/kill.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "content/public/common/result_codes.h"



namespace content {

struct CONTENT_EXPORT ChildProcessTerminationInfo {
  ChildProcessTerminationInfo();
  ChildProcessTerminationInfo(const ChildProcessTerminationInfo& other);
  ~ChildProcessTerminationInfo();

  base::TerminationStatus status = base::TERMINATION_STATUS_NORMAL_TERMINATION;

  // If |status| is TERMINATION_STATUS_LAUNCH_FAILED then |exit_code| will
  // contain a platform specific launch failure error code. Otherwise, it will
  // contain the exit code for the process (e.g. status from waitpid if on
  // posix, from GetExitCodeProcess on Windows).
  int exit_code = RESULT_CODE_NORMAL_EXIT;



  // The cumulative CPU usage of this process, if available.
  std::optional<base::TimeDelta> cpu_usage;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_CHILD_PROCESS_TERMINATION_INFO_H_
