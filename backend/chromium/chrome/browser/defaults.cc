// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/defaults.h"

#include "build/build_config.h"

namespace browser_defaults {

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
const bool kBrowserAliveWithNoWindows = true;
const bool kShowExitMenuItem = false;
#else
const bool kBrowserAliveWithNoWindows = false;
const bool kShowExitMenuItem = true;
#endif

const bool kShowUpgradeMenuItem = true;
const bool kShowImportOnBookmarkBar = true;
const bool kAlwaysOpenIncognitoBrowserIfStartedWithIncognitoSwitch = false;
const bool kAlwaysCreateTabbedBrowserOnSessionRestore = true;
const bool kShowHelpMenuItemIcon = false;

#if BUILDFLAG(IS_LINUX)
const bool kScrollEventChangesTab = true;
#else
const bool kScrollEventChangesTab = false;
#endif

bool bookmarks_enabled = true;

}  // namespace browser_defaults
