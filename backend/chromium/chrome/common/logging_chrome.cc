// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string_view>

#include "build/build_config.h"


#include <fstream>
#include <memory>
#include <string>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/debug/debugger.h"
#include "base/debug/dump_without_crashing.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/statistics_recorder.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/env_vars.h"
#include "chrome/common/logging_chrome.h"
#include "content/public/common/content_switches.h"



namespace logging {
namespace {

// When true, this means that error dialogs should not be shown.
bool dialogs_are_suppressed_ = false;
ScopedLogAssertHandler* assert_handler_ = nullptr;

// This should be true for exactly the period between the end of
// InitChromeLogging() and the beginning of CleanupChromeLogging().
bool chrome_logging_initialized_ = false;

// Set if we called InitChromeLogging() but failed to initialize.
bool chrome_logging_failed_ = false;

// This should be true for exactly the period between the end of
// InitChromeLogging() and the beginning of CleanupChromeLogging().
bool chrome_logging_redirected_ = false;

// The directory on which we do rotation of log files instead of switching
// with symlink. Because this directory doesn't support symlinks and the logic
// doesn't work correctly.


// Assertion handler for logging errors that occur when dialogs are
// silenced.  To record a new error, pass the log string associated
// with that error in the str parameter.
NOINLINE void SilentRuntimeAssertHandler(const char* file,
                                         int line,
                                         std::string_view message,
                                         std::string_view stack_trace) {
  base::debug::BreakDebugger();
}

// Suppresses error/assertion dialogs and enables the logging of
// those errors into silenced_errors_.
void SuppressDialogs() {
  if (dialogs_are_suppressed_)
    return;

  assert_handler_ = new ScopedLogAssertHandler(
      base::BindRepeating(SilentRuntimeAssertHandler));


  dialogs_are_suppressed_ = true;
}


// `filename_is_handle`, will be set to `true` if the log-file switch contains
// an inherited handle value rather than a filepath, and `false` otherwise.
LoggingDestination LoggingDestFromCommandLine(
    const base::CommandLine& command_line,
    bool& filename_is_handle) {
  filename_is_handle = false;
#if defined(NDEBUG)
  // In Release builds, log only to the log file.
  const LoggingDestination kDefaultLoggingMode = LOG_TO_FILE;
#else
  // In Debug builds log to all destinations, for ease of discovery.
  const LoggingDestination kDefaultLoggingMode = LOG_TO_ALL;
#endif

#if BUILDFLAG(CHROME_ENABLE_LOGGING_BY_DEFAULT)
  bool enable_logging = true;
  const char* const kInvertLoggingSwitch = switches::kDisableLogging;
#else
  bool enable_logging = false;
  const char* const kInvertLoggingSwitch = switches::kEnableLogging;
#endif

  if (command_line.HasSwitch(kInvertLoggingSwitch))
    enable_logging = !enable_logging;

  if (!enable_logging)
    return LOG_NONE;
  if (command_line.HasSwitch(switches::kEnableLogging)) {
    // Let --enable-logging=stderr force only stderr, particularly useful for
    // non-debug builds where otherwise you can't get logs to stderr at all.
    std::string logging_destination =
        command_line.GetSwitchValueASCII(switches::kEnableLogging);
    if (logging_destination == "stderr") {
      return LOG_TO_SYSTEM_DEBUG_LOG | LOG_TO_STDERR;
    }
    if (logging_destination != "") {
      // The browser process should not be called with --enable-logging=handle.
      LOG(ERROR) << "Invalid logging destination: " << logging_destination;
      return kDefaultLoggingMode;
    }
  }
  return kDefaultLoggingMode;
}

}  // anonymous namespace

LoggingDestination DetermineLoggingDestination(
    const base::CommandLine& command_line) {
  bool unused = false;
  return LoggingDestFromCommandLine(command_line, unused);
}


void InitChromeLogging(const base::CommandLine& command_line,
                       OldFileDeletionState delete_old_log_file) {
  DCHECK(!chrome_logging_initialized_)
      << "Attempted to initialize logging when it was already initialized.";
  bool filename_is_handle = false;
  LoggingDestination logging_dest =
      LoggingDestFromCommandLine(command_line, filename_is_handle);
  LogLockingState log_locking_state = LOCK_LOG_FILE;
  base::FilePath log_path;

  if (logging_dest & LOG_TO_FILE) {
    if (filename_is_handle) {
    } else {
      log_path = GetLogFileName(command_line);

    }
  } else {
    log_locking_state = DONT_LOCK_LOG_FILE;
  }

  LoggingSettings settings;
  settings.logging_dest = logging_dest;
  if (!log_path.empty()) {
    settings.log_file_path = log_path.value().c_str();
  }
  settings.lock_log = log_locking_state;
  settings.delete_old = delete_old_log_file;
  bool success = InitLogging(settings);

  if (!success) {
    DPLOG(ERROR) << "Unable to initialize logging to " << log_path.value();
    chrome_logging_failed_ = true;
    return;
  }

  // We call running in unattended mode "headless", and allow headless mode to
  // be configured either by the Environment Variable or by the Command Line
  // Switch. This is for automated test purposes.
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  const bool is_headless = env->HasVar(env_vars::kHeadless) ||
                           command_line.HasSwitch(switches::kNoErrorDialogs);

  // Show fatal log messages in a dialog in debug builds when not headless.
  if (!is_headless)
    SetShowErrorDialogs(true);

  // we want process and thread IDs because we have a lot of things running
  SetLogItems(true,    // enable_process_id
              true,    // enable_thread_id
              true,    // enable_timestamp
              false);  // enable_tickcount

  // Suppress system error dialogs when headless.
  if (is_headless)
    SuppressDialogs();

  // Use a minimum log level if the command line asks for one. Ignore this
  // switch if there's vlog level switch present too (as both of these switches
  // refer to the same underlying log level, and the vlog level switch has
  // already been processed inside InitLogging). If there is neither
  // log level nor vlog level specified, then just leave the default level
  // (INFO).
  if (command_line.HasSwitch(switches::kLoggingLevel) &&
      GetMinLogLevel() >= 0) {
    std::string log_level =
        command_line.GetSwitchValueASCII(switches::kLoggingLevel);
    int level = 0;
    if (base::StringToInt(log_level, &level) && level >= 0 &&
        level < LOGGING_NUM_SEVERITIES) {
      SetMinLogLevel(level);
    } else {
      DLOG(WARNING) << "Bad log level: " << log_level;
    }
  }


  base::StatisticsRecorder::InitLogOnShutdown();

  chrome_logging_initialized_ = true;
}

// This is a no-op, but we'll keep it around in case
// we need to do more cleanup in the future.
void CleanupChromeLogging() {
  if (chrome_logging_failed_)
    return;  // We failed to initiailize logging, no cleanup.

  // Logging was not initialized, no cleanup required. This is happening with
  // the Chrome early exit error paths (i.e Process Singleton).
  if (!chrome_logging_initialized_)
    return;

  CloseLogFile();

  chrome_logging_initialized_ = false;
  chrome_logging_redirected_ = false;
}

base::FilePath GetLogFileName(const base::CommandLine& command_line) {
  // Try the command line.
  auto filename = command_line.GetSwitchValueNative(switches::kLogFile);
  // Try the environment.
  if (filename.empty()) {
    std::optional<std::string> env_filename =
        base::Environment::Create()->GetVar(env_vars::kLogFileName);
    filename = env_filename.value_or("");
  }

  if (!filename.empty()) {
    base::FilePath candidate_path(filename);
    return candidate_path;
  }

  // If command line and environment do not provide a log file we can use,
  // fallback to the default.
  const base::FilePath log_filename(FILE_PATH_LITERAL("chrome_debug.log"));
  base::FilePath log_path;

  if (base::PathService::Get(chrome::DIR_LOGS, &log_path)) {
    log_path = log_path.Append(log_filename);
    return log_path;
  } else {
    // Error with path service, just use the default in our current directory.
    return log_filename;
  }
}

bool DialogsAreSuppressed() {
  return dialogs_are_suppressed_;
}


}  // namespace logging
