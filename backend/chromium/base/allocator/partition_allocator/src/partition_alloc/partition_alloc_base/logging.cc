// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition_alloc/partition_alloc_base/logging.h"

#include "partition_alloc/partition_alloc_base/compiler_specific.h"

// TODO(crbug.com/40158212): After finishing copying //base files to PA library,
// remove defined(BASE_CHECK_H_) from here.
#if defined(                                                                                 \
    BASE_ALLOCATOR_PARTITION_ALLOCATOR_SRC_PARTITION_ALLOC_PARTITION_ALLOC_BASE_CHECK_H_) || \
    defined(BASE_CHECK_H_) ||                                                                \
    defined(                                                                                 \
        BASE_ALLOCATOR_PARTITION_ALLOCATOR_SRC_PARTITION_ALLOC_PARTITION_ALLOC_CHECK_H_)
#error "logging.h should not include check.h"
#endif

#include "partition_alloc/build_config.h"
#include "partition_alloc/partition_alloc_base/component_export.h"
#include "partition_alloc/partition_alloc_base/cxx_wrapper/algorithm.h"
#include "partition_alloc/partition_alloc_base/debug/alias.h"
#include "partition_alloc/partition_alloc_base/immediate_crash.h"


#if PA_BUILDFLAG(IS_POSIX) || PA_BUILDFLAG(IS_FUCHSIA)
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

#include "partition_alloc/partition_alloc_base/posix/eintr_wrapper.h"

namespace partition_alloc::internal::logging {

namespace {

int g_min_log_level = 0;

void WriteToStderr(const char* data, size_t length) {
  size_t bytes_written = 0;
  int rv;
  while (bytes_written < length) {
    rv = WrapEINTR(write)(STDERR_FILENO, PA_UNSAFE_TODO(data + bytes_written),
                          length - bytes_written);
    if (rv < 0) {
      // Give up, nothing we can do now.
      break;
    }
    bytes_written += rv;
  }
}

}  // namespace

void SetMinLogLevel(int level) {
  g_min_log_level = std::min(LOGGING_FATAL, level);
}

int GetMinLogLevel() {
  return g_min_log_level;
}

bool ShouldCreateLogMessage(int severity) {
  if (severity < g_min_log_level) {
    return false;
  }

  // Return true here unless we know ~LogMessage won't do anything.
  return true;
}

int GetVlogVerbosity() {
  return std::max(-1, LOGGING_INFO - GetMinLogLevel());
}

void RawLog(int level, const char* message) {
  if (level >= g_min_log_level && message) {
    const size_t message_len = strlen(message);
    WriteToStderr(message, message_len);

    if (message_len > 0 && PA_UNSAFE_TODO(message[message_len - 1]) != '\n') {
      WriteToStderr("\n", 1);
    }
  }
}

}  // namespace partition_alloc::internal::logging
