// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_FEATURE_ENGAGEMENT_PUBLIC_EVENT_CONSTANTS_H_
#define COMPONENTS_FEATURE_ENGAGEMENT_PUBLIC_EVENT_CONSTANTS_H_

#include "build/build_config.h"
#include "components/feature_engagement/public/feature_constants.h"

namespace feature_engagement {

namespace events {

// Desktop
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)
// The user has explicitly opened a new tab via an entry point from inside of
// Chrome.
extern const char kNewTabOpened[];
// A new tab was opened when 5 (or more) tabs were already open.
extern const char kSixthTabOpened[];
// A tab was added to reading list.
extern const char kReadingListItemAdded[];
// Reading list was opened.
extern const char kReadingListMenuOpened[];
// Bookmark star button was clicked opening the menu.
extern const char kBookmarkStarMenuOpened[];
// Customize chrome was opened.
extern const char kCustomizeChromeOpened[];

// A tab with playing media was sent to the background.
extern const char kMediaBackgrounded[];

// The user opened the Global Media Controls dialog.
extern const char kGlobalMediaControlsOpened[];

// A split tab has been created
extern const char kSplitViewCreated[];

// A side panel has been pinned.
extern const char kSidePanelPinned[];

// The side search panel was automatically triggered.
extern const char kSideSearchAutoTriggered[];
// The side search panel was opened by the user.
extern const char kSideSearchOpened[];
// The side search page action icon label was shown.
extern const char kSideSearchPageActionLabelShown[];

// Tab Search tab strip was opened by the user.
extern const char kTabSearchOpened[];

// The PWA was installed by the user.
extern const char kDesktopPwaInstalled[];

// A module's actions were clicked on the NewTabPage.
extern const char kDesktopNTPModuleUsed[];

// The user entered the special "focus help bubble" accelerator.
extern const char kFocusHelpBubbleAcceleratorPressed[];

// The screen reader promo for the "focus help bubble" accelerator was read to
// the user.
extern const char kFocusHelpBubbleAcceleratorPromoRead[];

// Th user clicked the extensions request access button in the toolbar.
extern const char kExtensionsRequestAccessButtonClicked[];

// The user has opened the cookie controls bubble.
extern const char kCookieControlsBubbleShown[];

// The user has accepted the Glic onboarding.
extern const char kGlicOnboardingCompleted[];

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)


// Android.

extern const char kTabSearchComboButtonUsed[];

}  // namespace events

}  // namespace feature_engagement

#endif  // COMPONENTS_FEATURE_ENGAGEMENT_PUBLIC_EVENT_CONSTANTS_H_
