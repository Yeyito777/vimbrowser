// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/syslog_logging.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
// <syslog.h> defines LOG_INFO, LOG_WARNING macros that could conflict with
// base::LOG_INFO, base::LOG_WARNING.
#include <syslog.h>
#undef LOG_INFO
#undef LOG_WARNING
#endif

#include <ostream>
#include <string>

namespace logging {

namespace {

// The syslog logging is on by default, but tests or fuzzers can disable it.
bool g_logging_enabled = true;

}  // namespace


EventLogMessage::EventLogMessage(const char* file,
                                 int line,
                                 LogSeverity severity)
    : log_message_(file, line, severity) {}

EventLogMessage::~EventLogMessage() {
  if (!g_logging_enabled) {
    return;
  }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  const char kEventSource[] = "chrome";
  openlog(kEventSource, LOG_NOWAIT | LOG_PID, LOG_USER);
  // We can't use the defined names for the logging severity from syslog.h
  // because they collide with the names of our own severity levels. Therefore
  // we use the actual values which of course do not match ours.
  // See sys/syslog.h for reference.
  int priority = 3;
  switch (log_message_.severity()) {
    case LOGGING_INFO:
      priority = 6;
      break;
    case LOGGING_WARNING:
      priority = 4;
      break;
    case LOGGING_ERROR:
      priority = 3;
      break;
    case LOGGING_FATAL:
      priority = 2;
      break;
  }
  syslog(priority, "%s", log_message_.str().c_str());
  closelog();
#endif  // BUILDFLAG(IS_WIN)
}

void SetSyslogLoggingForTesting(bool logging_enabled) {
  g_logging_enabled = logging_enabled;
}

}  // namespace logging
