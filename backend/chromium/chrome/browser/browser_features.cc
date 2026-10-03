// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser_features.h"

#include "base/feature_list.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"


namespace features {


// This is used to enable an experiment for modifying confidence cutoff of
// prerender and preconnect for autocomplete action predictor.
BASE_FEATURE(kAutocompleteActionPredictorConfidenceCutoff,
             base::FEATURE_DISABLED_BY_DEFAULT);

// This is used to enable an experiment for the bookmarks tree view in the
// side panel, providing users with a hierarchical view of their bookmarks.
BASE_FEATURE(kBookmarksTreeView, base::FEATURE_DISABLED_BY_DEFAULT);

// This is used as a kill switch for Bookmark triggered prerendering. See
// crbug.com/40259793 for more details of Bookmark triggered prerendering.
BASE_FEATURE(kBookmarkTriggerForPrerender2KillSwitch,
             base::FEATURE_DISABLED_BY_DEFAULT);

// This flag is used for enabling BookmarkBar triggered preconnect.
BASE_FEATURE(kBookmarkTriggerForPreconnect, base::FEATURE_ENABLED_BY_DEFAULT);

// This flag is used for enabling BookmarkBar triggered prefetch.  See
// crbug.com/413259638 for more details of Bookmark triggered prefetching.
BASE_FEATURE(kBookmarkTriggerForPrefetch, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables Certificate Transparency on Desktop and Android Browser (CT is
// disabled in Android Webview, see aw_browser_context.cc).
// Enabling CT enforcement requires maintaining a log policy, and the ability to
// update the list of accepted logs. Embedders who are planning to enable this
// should first reach out to chrome-certificate-transparency@google.com.
// On builds where CT is enabled, this flag is also used as an emergency kill
// switch.
BASE_FEATURE(kCertificateTransparencyAskBeforeEnabling,
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
             base::FEATURE_ENABLED_BY_DEFAULT);
#else
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)

// Enables using network time for certificate verification. If enabled, network
// time will be used to verify certificate validity, however certificates that
// fail to validate with network time will fall back to the system time.
// This has no effect if the network_time::kNetworkTimeServiceQuerying flag is
// disabled, or the BrowserNetworkTimeQueriesEnabled policy is set to false.
BASE_FEATURE(kCertVerificationNetworkTime, base::FEATURE_ENABLED_BY_DEFAULT);

// Killswitch that guards clearing all user data in the ProfileImpl destructor.
BASE_FEATURE(kClearUserDataUponProfileDestruction,
             base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_LINUX)
// Enables usage of os_crypt_async::SecretPortalKeyProvider.  Once
// `kSecretPortalKeyProviderUseForEncryption` is enabled, this flag cannot be
// disabled without losing data.
BASE_FEATURE(kDbusSecretPortal, base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_LINUX)

// Destroy profiles when their last browser window is closed, instead of when
// the browser exits.
BASE_FEATURE(kDestroyProfileOnBrowserClose,
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
             base::FEATURE_ENABLED_BY_DEFAULT);
#else
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

// Enables showing the email of the flex org admin that setup CBCM in the
// management disclosures.
BASE_FEATURE(kFlexOrgManagementDisclosure,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables the Incoming Call Notifications scenario. When created by an
// installed origin, an incoming call notification should have increased
// priority, colored buttons, a ringtone, and a default "close" button.
// Otherwise, if the origin is not installed, it should behave like the default
// notifications, but with the added "Close" button. See
// https://github.com/MicrosoftEdge/MSEdgeExplainers/blob/main/Notifications/notifications_actions_customization.md
BASE_FEATURE(kIncomingCallNotifications,
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(ENABLE_EXTENSIONS)
BASE_FEATURE(kInitialExternalExtensions, base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

// Adds a "Snooze" action to mute notifications during screen sharing sessions.
BASE_FEATURE(kMuteNotificationSnoozeAction, base::FEATURE_DISABLED_BY_DEFAULT);

// This feature enables monitoring of first-party network requests in order to
// find possible violations. Example: A Chrome policy is set to disabled but the
// network request controlled by that policy is observed.
BASE_FEATURE(kNetworkAnnotationMonitoring, base::FEATURE_ENABLED_BY_DEFAULT);

// This flag is used for enabling New Tab Page triggered prerendering. See
// crbug.com/1462832 for more details of New Tab Page triggered prerendering.
BASE_FEATURE(kNewTabPageTriggerForPrerender2, base::FEATURE_ENABLED_BY_DEFAULT);

// This flag is used for enabling New Tab Page triggered prefetch. See
// crbug.com/421941586 for more details of New Tab Page triggered prefetching.
BASE_FEATURE(kNewTabPageTriggerForPrefetch, base::FEATURE_DISABLED_BY_DEFAULT);

// Adds an "Unsubscribe" action to web push notifications that allows stopping
// notifications from a given origin with a single tap (with an option to undo).
BASE_FEATURE(kNotificationOneTapUnsubscribeOnDesktop,
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)
// When this feature is enabled, Chrome will register os_update_handler with
// Omaha, to be run on OS upgrade.
BASE_FEATURE(kRegisterOsUpdateHandlerWin, base::FEATURE_ENABLED_BY_DEFAULT);
// When this feature is enabled, Chrome will install the
// platform_experience_helper.
BASE_FEATURE(kInstallPlatformExperienceHelperWin,
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)

// Enables app-menu item for reporting an unsafe site to Google.
BASE_FEATURE(kReportUnsafeSite, base::FEATURE_DISABLED_BY_DEFAULT);

// When this feature is enabled, the network service will restart unsandboxed if
// a previous attempt to launch it sandboxed failed.
BASE_FEATURE(kRestartNetworkServiceUnsandboxedForFailedLaunch,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Gates sandboxed iframe navigation toward external protocol behind any of:
// - allow-top-navigation
// - allow-top-navigation-to-custom-protocols
// - allow-top-navigation-with-user-gesture (+ user gesture)
// - allow-popups
//
// Motivation:
// Developers are surprised that a sandboxed iframe can navigate and/or
// redirect the user toward an external application.
// General iframe navigation in sandboxed iframe are not blocked normally,
// because they stay within the iframe. However they can be seen as a popup or
// a top-level navigation when it leads to opening an external application. In
// this case, it makes sense to extend the scope of sandbox flags, to block
// malvertising.
//
// Implementation bug: https://crbug.com/1253379
// I2S: https://groups.google.com/a/chromium.org/g/blink-dev/c/-t-f7I6VvOI
//
// Enabled in M103. Flag to be removed in M106
BASE_FEATURE(kSandboxExternalProtocolBlocked, base::FEATURE_ENABLED_BY_DEFAULT);
// Enabled in M100. Flag to be removed in M106
BASE_FEATURE(kSandboxExternalProtocolBlockedWarning,
             base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_LINUX)
// If true, encrypt new data with the key provided by SecretPortalKeyProvider.
// Otherwise, it will only decrypt existing data.
BASE_FEATURE(kSecretPortalKeyProviderUseForEncryption,
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_LINUX)

// Enables migration of the network context data from `unsandboxed_data_path` to
// `data_path`. See the explanation in network_context.mojom.
BASE_FEATURE(kTriggerNetworkDataMigration,
             base::FEATURE_DISABLED_BY_DEFAULT
);

// Enables runtime detection of USB devices which provide a WebUSB landing page
// descriptor.
BASE_FEATURE(kWebUsbDeviceDetection, base::FEATURE_ENABLED_BY_DEFAULT);


// This flag controls whether to perform Pak integrity check on startup to
// report statistics for on-disk corruption.
// Disabled on ChromeOS, as dm-verity enforces integrity and the check would
// be redundant.
BASE_FEATURE(kReportPakFileIntegrity,
             base::FEATURE_ENABLED_BY_DEFAULT);

// This flag enables the removal of IWAs surface captures from Chrome Tabs
// category in getDisplayMedia() API. When disabled, IWAs surface captures
// show both in Chrome Tabs and Windows.
BASE_FEATURE(kRemovalOfIWAsFromTabCapture, base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace features
