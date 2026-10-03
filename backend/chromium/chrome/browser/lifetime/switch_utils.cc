// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/lifetime/switch_utils.h"

#include <stddef.h>

#include "build/build_config.h"
#include "chrome/common/chrome_switches.h"


namespace switches {

namespace {

// Switches enumerated here will be removed when a background instance of
// Chrome restarts itself. If your key is designed to only be used once,
// or if it does not make sense when restarting a background instance to
// pick up an automatic update, be sure to add it to this list.
constexpr const char* kSwitchesToRemoveOnAutorestart[] = {
    switches::kApp,
    switches::kAppId,
    switches::kForceFirstRun,
    switches::kGuest,
    switches::kIncognito,
    switches::kMakeDefaultBrowser,
    switches::kNoStartupWindow,
    switches::kRestoreLastSession,
    switches::kWinJumplistAction};

}  // namespace

void RemoveSwitchesForAutostart(base::CommandLine::SwitchMap* switch_list) {
  for (const char* switch_to_remove : kSwitchesToRemoveOnAutorestart) {
    switch_list->erase(switch_to_remove);
  }

}

}  // namespace switches
