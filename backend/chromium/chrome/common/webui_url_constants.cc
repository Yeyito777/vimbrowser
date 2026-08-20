// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/webui_url_constants.h"

#include <array>
#include <string_view>

#include "base/containers/fixed_flat_set.h"
#include "build/build_config.h"
#include "components/commerce/core/commerce_constants.h"
#include "components/history_clusters/history_clusters_internals/webui/url_constants.h"
#include "components/optimization_guide/optimization_guide_internals/webui/url_constants.h"
#include "components/password_manager/content/common/web_ui_constants.h"
#include "components/safe_browsing/core/common/web_ui_constants.h"
#include "device/vr/buildflags/buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "third_party/blink/public/common/chrome_debug_urls.h"


namespace chrome {

// Note: Add hosts to `ChromeURLHosts()` at the bottom of this file to be listed
// by chrome://chrome-urls (about:about) and the built-in AutocompleteProvider.


// Add hosts here to be included in chrome://chrome-urls (about:about).
// These hosts will also be suggested by BuiltinProvider.
base::span<const base::cstring_view> ChromeURLHosts() {
  static constexpr auto kChromeURLHosts = std::to_array<base::cstring_view>({
      kChromeUIAboutHost,
      kChromeUIAccessibilityHost,
      kChromeUIActorInternalsHost,
      kChromeUIAppServiceInternalsHost,
      kChromeUIAutofillInternalsHost,
      kChromeUIBluetoothInternalsHost,
      kChromeUIBrowsingTopicsInternalsHost,
      kChromeUIChromeFindsInternalsHost,
      kChromeUIChromeURLsHost,
      kChromeUIComponentsHost,
      commerce::kChromeUICommerceInternalsHost,
      kChromeUIConnectorsInternalsHost,
      kChromeUICrashesHost,
      kChromeUICreditsHost,
#if BUILDFLAG(IS_CHROMEOS) && !defined(OFFICIAL_BUILD)
      ash::kChromeUIDeviceEmulatorHost,
#endif
      kChromeUIDeviceLogHost,
      kChromeUIDownloadInternalsHost,
      kChromeUIFamilyLinkUserInternalsHost,
      kChromeUIFlagsHost,
      kChromeUIGCMInternalsHost,
      kChromeUIHistoryHost,
      history_clusters_internals::kChromeUIHistoryClustersInternalsHost,
      kChromeUIInterstitialHost,
#if BUILDFLAG(ENABLE_CEF)
      kChromeUILicenseHost,
#endif
      kChromeUILocalStateHost,
      kChromeUIManagementHost,
      kChromeUIMediaEngagementHost,
      kChromeUIMetricsInternalsHost,
      kChromeUINetExportHost,
      kChromeUINetInternalsHost,
      kChromeUINewTabHost,
      kChromeUIOmniboxHost,
      kChromeUIOnDeviceInternalsHost,
      optimization_guide_internals::kChromeUIOptimizationGuideInternalsHost,
      kChromeUIPasswordManagerInternalsHost,
      password_manager::kChromeUIPasswordManagerHost,
      kChromeUIPolicyHost,
      kChromeUIPredictorsHost,
      kChromeUIPrefsInternalsHost,
      kChromeUIProfileInternalsHost,
      content::kChromeUIQuotaInternalsHost,
      kChromeUIWebUIToolbarHost,
      kChromeUISignInInternalsHost,
      kChromeUISiteEngagementHost,
      kChromeUISkillsHost,
      kChromeUISuggestInternalsHost,
      kChromeUINTPTilesInternalsHost,
      safe_browsing::kChromeUISafeBrowsingHost,
      kChromeUISyncInternalsHost,
      kChromeUITabSearchHost,
      kChromeUITermsHost,
      kChromeUITranslateInternalsHost,
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
      kChromeUIUpdaterHost,
#endif
      kChromeUIUsbInternalsHost,
      kChromeUIUserActionsHost,
      kChromeUIVersionHost,
      kChromeUIWebAppInternalsHost,
      content::kChromeUIPrivateAggregationInternalsHost,
      content::kChromeUIAttributionInternalsHost,
      content::kChromeUIBlobInternalsHost,
      content::kChromeUIDinoHost,
      content::kChromeUIGpuHost,
      content::kChromeUIHistogramHost,
      content::kChromeUIIndexedDBInternalsHost,
      content::kChromeUIMediaInternalsHost,
      content::kChromeUINetworkErrorsListingHost,
      content::kChromeUIProcessInternalsHost,
      content::kChromeUIServiceWorkerInternalsHost,
      content::kChromeUITracingHost,
      content::kChromeUIUkmHost,
      content::kChromeUIWebRTCInternalsHost,
#if BUILDFLAG(ENABLE_VR)
      content::kChromeUIWebXrInternalsHost,
#endif
      kChromeUIAppLauncherPageHost,
      kChromeUIBookmarksHost,
      kChromeUIDownloadsHost,
      kChromeUIHelpHost,
      kChromeUIInspectHost,
      kChromeUINewTabPageHost,
      kChromeUINewTabPageThirdPartyHost,
      kChromeUISettingsHost,
      kChromeUISystemInfoHost,
      kChromeUIWhatsNewHost,
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
      kChromeUIDiscardsHost,
#endif
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
      kChromeUIWebAppSettingsHost,
#endif
#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_ANDROID)
      kChromeUILinuxProxyConfigHost,
#endif
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_ANDROID)
      kChromeUISandboxHost,
#endif
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
      kChromeUIExtensionsHost,
      kChromeUIExtensionsInternalsHost,
#endif
      kChromeUIWebRtcLogsHost,
      kChromeUIWebNNInternalsHost,
      kChromeUIWebuiBrowserHost,
  });

  return base::span(kChromeURLHosts);
}

base::span<const base::cstring_view> ChromeDebugURLs() {
  // TODO(crbug.com/40253037): make this list comprehensive
  static constexpr auto kChromeDebugURLs = std::to_array<base::cstring_view>(
      {blink::kChromeUIBadCastCrashURL,
       blink::kChromeUIBrowserCrashURL,
       blink::kChromeUIBrowserDcheckURL,
       blink::kChromeUICrashURL,
       blink::kChromeUICrashRustURL,
#if defined(ADDRESS_SANITIZER)
       blink::kChromeUICrashRustOverflowURL,
#endif
       blink::kChromeUIDumpURL,
       blink::kChromeUIKillURL,
       blink::kChromeUIHangURL,
       blink::kChromeUIShorthangURL,
       blink::kChromeUIGpuCleanURL,
       blink::kChromeUIGpuCrashURL,
       blink::kChromeUIGpuHangURL,
       blink::kChromeUIMemoryExhaustURL,
       blink::kChromeUIMemoryPressureCriticalURL,
       blink::kChromeUIMemoryPressureModerateURL,
       kChromeUIWebUIJsErrorURL,
       kChromeUIQuitURL,
       kChromeUIRestartURL});

  return base::span(kChromeDebugURLs);
}

}  // namespace chrome
