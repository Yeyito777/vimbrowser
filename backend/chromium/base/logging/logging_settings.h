// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_LOGGING_LOGGING_SETTINGS_H_
#define BASE_LOGGING_LOGGING_SETTINGS_H_

#include <stdint.h>

#include "base/files/file_path.h"
#include "build/build_config.h"



namespace logging {

// A bitmask of potential logging destinations.
using LoggingDestination = uint32_t;
// Specifies where logs will be written. Multiple destinations can be specified
// with bitwise OR.
// Unless destination is LOG_NONE, all logs with severity ERROR and above will
// be written to stderr in addition to the specified destination.
// LOG_TO_FILE includes logging to externally-provided file handles.
enum : uint32_t {
  LOG_NONE = 0,
  LOG_TO_FILE = 1 << 0,
  LOG_TO_SYSTEM_DEBUG_LOG = 1 << 1,
  LOG_TO_STDERR = 1 << 2,

  LOG_TO_ALL = LOG_TO_FILE | LOG_TO_SYSTEM_DEBUG_LOG | LOG_TO_STDERR,

// On Windows, use a file next to the exe.
// On POSIX platforms, where it may not even be possible to locate the
// executable on disk, use stderr.
// On Fuchsia, use the Fuchsia logging service.
  LOG_DEFAULT = LOG_TO_SYSTEM_DEBUG_LOG | LOG_TO_STDERR,
};

// Indicates that the log file should be locked when being written to.
// Unless there is only one single-threaded process that is logging to
// the log file, the file should be locked during writes to make each
// log output atomic. Other writers will block.
//
// All processes writing to the log file must have their locking set for it to
// work properly. Defaults to LOCK_LOG_FILE.
enum LogLockingState { LOCK_LOG_FILE, DONT_LOCK_LOG_FILE };

// On startup, should we delete or append to an existing log file (if any)?
// Defaults to APPEND_TO_OLD_LOG_FILE.
enum OldFileDeletionState { DELETE_OLD_LOG_FILE, APPEND_TO_OLD_LOG_FILE };


struct BASE_EXPORT LoggingSettings {
  // Equivalent to logging destination enum, but allows for multiple
  // destinations.
  uint32_t logging_dest = LOG_DEFAULT;

  // The four settings below have an effect only when LOG_TO_FILE is
  // set in |logging_dest|.
  base::FilePath::StringType log_file_path;
  LogLockingState lock_log = LOCK_LOG_FILE;
  OldFileDeletionState delete_old = APPEND_TO_OLD_LOG_FILE;
};

}  // namespace logging

#endif  // BASE_LOGGING_LOGGING_SETTINGS_H_
