// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/pref_service_incognito_allowlist.h"

#include <vector>

#include "build/build_config.h"
#include "chrome/common/pref_names.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/reading_list/core/reading_list_pref_names.h"
#include "components/ukm/ukm_pref_names.h"

#include "chrome/browser/accessibility/animation_policy_prefs.h"


namespace {

// List of keys that can be changed in the user prefs file by the incognito
// profile.
const char* const kPersistentPrefNames[] = {
    kAnimationPolicyAllowed,
    kAnimationPolicyOnce,
    kAnimationPolicyNone,
#if BUILDFLAG(ENABLE_EXTENSIONS)
    prefs::kAnimationPolicy,
#endif

    // Bookmark preferences are common between incognito and regular mode.
    bookmarks::prefs::kBookmarkEditorExpandedNodes,
    bookmarks::prefs::kEditBookmarksEnabled,
    bookmarks::prefs::kManagedBookmarks,
    bookmarks::prefs::kManagedBookmarksFolderName,
    bookmarks::prefs::kShowAppsShortcutInBookmarkBar,
    bookmarks::prefs::kShowManagedBookmarksInBookmarkBar,
    bookmarks::prefs::kShowBookmarkBar,


    // Default browser bar's status is aggregated between regular and incognito
    // modes.
    prefs::kBrowserSuppressDefaultBrowserPrompt,
    prefs::kDefaultBrowserInfobarLastDeclined,
    prefs::kDefaultBrowserSettingEnabled,

    // Devtools preferences are stored cross profiles as they are not storing
    // user data and just keep debugging environment settings.
    prefs::kDevToolsAdbKey,
    prefs::kDevToolsAvailability,
    prefs::kDevToolsDiscoverUsbDevicesEnabled,
    prefs::kDevToolsEditedFiles,
    prefs::kDevToolsFileSystemPaths,
    prefs::kDevToolsPortForwardingEnabled,
    prefs::kDevToolsPortForwardingDefaultSet,
    prefs::kDevToolsPortForwardingConfig,
    prefs::kDevToolsPreferences,
    prefs::kDevToolsDiscoverTCPTargetsEnabled,
    prefs::kDevToolsTCPDiscoveryConfig,


    // Tab stats metrics are aggregated between regular and incognio mode.
    prefs::kTabStatsTotalTabCountMax,
    prefs::kTabStatsMaxTabsPerWindow,
    prefs::kTabStatsWindowCountMax,
    prefs::kTabStatsDailySample,

#if BUILDFLAG(IS_MAC)
    prefs::kShowFullscreenToolbar,
#endif

#if BUILDFLAG(IS_LINUX)
    // Toggleing custom frames affects all open windows in the profile, hence
    // should be written to the regular profile when changed in incognito mode.
    prefs::kUseCustomChromeFrame,
#endif

    // Although UKMs are not collected in incognito, theses preferences may be
    // changed by UMA/Sync/Unity consent, and need to be the same between
    // incognito and regular modes.
    ukm::prefs::kUkmClientId,
    ukm::prefs::kUkmUnsentLogStore,
    ukm::prefs::kUkmSessionId,

    // Cookie controls preference is, as in an initial release, surfaced only in
    // the incognito mode and therefore should be persisted between incognito
    // sessions.
    prefs::kCookieControlsMode,
};

}  // namespace

namespace prefs {

std::vector<const char*> GetIncognitoPersistentPrefsAllowlist() {
  std::vector<const char*> allowlist;
  allowlist.insert(allowlist.end(), std::begin(kPersistentPrefNames),
                   std::end(kPersistentPrefNames));
  return allowlist;
}

}  // namespace prefs
