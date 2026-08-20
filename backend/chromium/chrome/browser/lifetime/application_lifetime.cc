// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/lifetime/application_lifetime.h"

#include <memory>
#include <set>
#include <string>

#include "base/logging.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/ui/profiles/profile_picker.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/pref_names.h"
#include "components/language/core/browser/pref_names.h"
#include "components/language/core/common/locale_util.h"
#include "components/prefs/pref_service.h"


#include "chrome/browser/lifetime/application_lifetime_desktop.h"

namespace chrome {

namespace {

void AttemptExitInternal(bool try_to_quit_application) {
  // On Mac, the platform-specific part handles setting this.
#if !BUILDFLAG(IS_MAC)
  if (try_to_quit_application) {
    browser_shutdown::SetTryingToQuit(true);
  }
#endif  // !BUILDFLAG(IS_MAC)

  OnClosingAllBrowsers(true);

  g_browser_process->platform_part()->AttemptExit(try_to_quit_application);
}

}  // namespace

// The ChromeOS implementations are in application_lifetime_chromeos.cc

void AttemptUserExit() {
  // Reset the restart bit that might have been set in cancelled restart
  // request.
  ProfilePicker::Hide();
  PrefService* pref_service = g_browser_process->local_state();
  pref_service->SetBoolean(prefs::kRestartLastSessionOnShutdown, false);
  AttemptExitInternal(false);
}

void AttemptRelaunch() {
  AttemptRestart();
}

void AttemptExit() {
  // If we know that all browsers can be closed without blocking,
  // don't notify users of crashes beyond this point.
  // Note that MarkAsCleanShutdown() does not set UMA's exit cleanly bit
  // so crashes during shutdown are still reported in UMA.
  // Android doesn't use Browser.
  if (AreAllBrowsersCloseable()) {
    MarkAsCleanShutdown();
  }
  AttemptExitInternal(true);
}


void ExitIgnoreUnloadHandlers() {
  VLOG(1) << "ExitIgnoreUnloadHandlers";
  // We always mark exit cleanly.
  MarkAsCleanShutdown();

  // For desktop browsers, always perform a silent exit.
  browser_shutdown::OnShutdownStarting(
      browser_shutdown::ShutdownType::kSilentExit);
  AttemptExitInternal(true);
}

}  // namespace chrome
