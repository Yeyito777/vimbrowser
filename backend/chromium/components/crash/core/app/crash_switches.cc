// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/crash/core/app/crash_switches.h"

#include "build/build_config.h"

namespace crash_reporter {
namespace switches {

// A process type (switches::kProcessType) that indicates chrome.exe or
// setup.exe is being launched as crashpad_handler. This is only used on
// Windows. We bundle the handler into chrome.exe on Windows because there is
// high probability of a "new" .exe being blocked or interfered with by
// application firewalls, AV software, etc. On other platforms, crashpad_handler
// is a standalone executable.
const char kCrashpadHandler[] = "crashpad-handler";

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
// The process ID of the Crashpad handler.
const char kCrashpadHandlerPid[] = "crashpad-handler-pid";
#endif


}  // namespace switches
}  // namespace crash_reporter
