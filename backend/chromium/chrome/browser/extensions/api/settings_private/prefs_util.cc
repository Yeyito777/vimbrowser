// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/settings_private/prefs_util.h"

#include <memory>

#include "base/feature_list.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/accessibility/tree_fixing/pref_names.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/content_settings/generated_cookie_prefs.h"
#include "chrome/browser/content_settings/generated_javascript_optimizer_pref.h"
#include "chrome/browser/content_settings/generated_permission_prompting_behavior_pref.h"
#include "chrome/browser/extensions/api/settings_private/generated_prefs.h"
#include "chrome/browser/extensions/api/settings_private/generated_prefs_factory.h"
#include "chrome/browser/extensions/settings_api_helpers.h"
#include "chrome/browser/glic/glic_pref_names.h"
#include "chrome/browser/glic/public/glic_enabling.h"
#include "chrome/browser/metrics/profile_pref_names.h"
#include "chrome/browser/nearby_sharing/common/nearby_share_prefs.h"
#include "chrome/browser/password_manager/generated_password_leak_detection_pref.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/generated_safe_browsing_pref.h"
#include "chrome/browser/safe_browsing/generated_security_settings_bundle_pref.h"
#include "chrome/browser/ssl/generated_https_first_mode_pref.h"
#include "chrome/browser/ui/safety_hub/safety_hub_prefs.h"
#include "chrome/browser/ui/tabs/tab_strip_prefs.h"
#include "chrome/browser/ui/toolbar/toolbar_pref_names.h"
#include "chrome/common/extensions/api/settings_private.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/browsing_data/core/pref_names.h"
#include "components/commerce/core/pref_names.h"
#include "components/component_updater/pref_names.h"
#include "components/compose/buildflags.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/dom_distiller/core/pref_names.h"
#include "components/drive/drive_pref_names.h"
#include "components/embedder_support/pref_names.h"
#include "components/language/core/browser/pref_names.h"
#include "components/live_caption/pref_names.h"
#include "components/media_router/common/pref_names.h"
#include "components/omnibox/browser/omnibox_prefs.h"
#include "components/optimization_guide/core/feature_registry/feature_registration.h"
#include "components/optimization_guide/core/optimization_guide_prefs.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/payments/core/payment_prefs.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/permissions/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/privacy_sandbox/privacy_sandbox_prefs.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/saved_tab_groups/public/pref_names.h"
#include "components/search_engines/default_search_manager.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/supervised_user/core/common/pref_names.h"
#include "components/themes/pref_names.h"
#include "components/unified_consent/pref_names.h"
#include "components/url_formatter/url_fixer.h"
#include "extensions/browser/extension_pref_value_map.h"
#include "extensions/browser/extension_pref_value_map_factory.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/common/extension.h"


#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
#include "chrome/browser/ui/toasts/toast_features.h"  // nogncheck
#endif

namespace {


bool IsSettingReadOnly(const std::string& pref_name) {
  // download.default_directory is used to display the directory location and
  // for policy indicators, but should not be changed directly.
  if (pref_name == prefs::kDownloadDefaultDirectory) {
    return true;
  }

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_ANDROID) || \
    BUILDFLAG(IS_CHROMEOS)
  // Changing this pref value is protected by reauthentication.
  if (pref_name ==
      autofill::prefs::kAutofillAiReauthBeforeViewingSensitiveData) {
    return true;
  }
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_ANDROID) ||
        // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  // Can be changed only from C++ after successful re-auth.
  if (pref_name ==
      password_manager::prefs::kBiometricAuthenticationBeforeFilling) {
    return true;
  }
#endif
  return false;
}

}  // namespace

namespace extensions {

namespace settings_api = api::settings_private;

PrefsUtil::PrefsUtil(Profile* profile) : profile_(profile) {}

PrefsUtil::~PrefsUtil() = default;

const PrefsUtil::TypedPrefMap& PrefsUtil::GetAllowlistedKeys() {
  static PrefsUtil::TypedPrefMap* s_allowlist = nullptr;
  if (s_allowlist) {
    return *s_allowlist;
  }
  s_allowlist = new PrefsUtil::TypedPrefMap();

  // Miscellaneous
  (*s_allowlist)[::embedder_support::kAlternateErrorPagesEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[autofill::prefs::kAutofillProfileEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[autofill::prefs::kAutofillCreditCardEnabled] =
      settings_api::PrefType::kBoolean;
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  (*s_allowlist)[autofill::prefs::kAutofillPaymentMethodsMandatoryReauth] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[autofill::prefs::kAutofillAiReauthBeforeViewingSensitiveData] =
      settings_api::PrefType::kBoolean;
#endif
  (*s_allowlist)[autofill::prefs::kAutofillPaymentCvcStorage] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[autofill::prefs::kAutofillPaymentCardBenefits] =
      settings_api::PrefType::kBoolean;
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  (*s_allowlist)[autofill::prefs::kAutofillBnplEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[autofill::prefs::kAutofillAiIdentityEntitiesEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[autofill::prefs::kAutofillAiOptInStatus] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[autofill::prefs::kAutofillAiTravelEntitiesEnabled] =
      settings_api::PrefType::kBoolean;
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)
  (*s_allowlist)[payments::kCanMakePaymentEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[bookmarks::prefs::kShowBookmarkBar] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[bookmarks::prefs::kShowTabGroupsInBookmarkBar] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kSidePanelHorizontalAlignment] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kVerticalTabsEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kTabSearchRightAligned] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kTabSearchPinnedToTabstrip] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kProjectsPanelPinnedToTabstrip] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kEverythingMenuPinnedToTabstrip] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[tab_groups::prefs::kAutoPinNewTabGroups] =
      settings_api::PrefType::kBoolean;

#if BUILDFLAG(IS_LINUX)
  (*s_allowlist)[::prefs::kUseCustomChromeFrame] =
      settings_api::PrefType::kBoolean;
#endif
  (*s_allowlist)[::prefs::kShowHomeButton] = settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kShowForwardButton] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kPinContextualTaskButton] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kPinSplitTabButton] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kSplitViewDragAndDropEnabled] =
      settings_api::PrefType::kBoolean;

  // Appearance settings.
  (*s_allowlist)[::prefs::kCurrentThemeID] = settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kPinnedActions] = settings_api::PrefType::kList;
  (*s_allowlist)[themes::prefs::kPolicyThemeColor] =
      settings_api::PrefType::kNumber;
#if BUILDFLAG(IS_LINUX)
  (*s_allowlist)[::prefs::kSystemTheme] = settings_api::PrefType::kNumber;
#endif
  (*s_allowlist)[::prefs::kHomePage] = settings_api::PrefType::kUrl;
  (*s_allowlist)[::prefs::kHomePageIsNewTabPage] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kWebKitDefaultFixedFontSize] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kWebKitDefaultFontSize] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kWebKitMinimumFontSize] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kWebKitFixedFontFamily] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kWebKitSansSerifFontFamily] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kWebKitMathFontFamily] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kWebKitSerifFontFamily] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kWebKitStandardFontFamily] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kDefaultCharset] = settings_api::PrefType::kString;
#if BUILDFLAG(IS_MAC)
  (*s_allowlist)[::prefs::kWebkitTabsToLinks] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kConfirmToQuitEnabled] =
      settings_api::PrefType::kBoolean;
#endif
  (*s_allowlist)[prefs::kHoverCardImagesEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[prefs::kHoverCardMemoryUsageEnabled] =
      settings_api::PrefType::kBoolean;

  // On startup.
  (*s_allowlist)[::prefs::kRestoreOnStartup] = settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kURLsToRestoreOnStartup] =
      settings_api::PrefType::kList;

  // Downloads settings.
  (*s_allowlist)[::prefs::kDownloadDefaultDirectory] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kPromptForDownload] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[drive::prefs::kDisableDrive] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kDownloadBubblePartialViewEnabled] =
      settings_api::PrefType::kBoolean;

  // Password Manager settings.
  (*s_allowlist)[password_manager::prefs::kCredentialsEnableService] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[password_manager::prefs::kCredentialsEnableAutosignin] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[password_manager::prefs::kAutomaticPasskeyUpgrades] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[password_manager::prefs::kPasswordSharingEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[password_manager::prefs::kPasswordLeakDetectionEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)
      [password_manager::prefs::kPasswordDismissCompromisedAlertEnabled] =
          settings_api::PrefType::kBoolean;
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  (*s_allowlist)
      [password_manager::prefs::kBiometricAuthenticationBeforeFilling] =
          settings_api::PrefType::kBoolean;
#endif


#if BUILDFLAG(IS_MAC)
  (*s_allowlist)[::prefs::kCreatePasskeysInICloudKeychain] =
      settings_api::PrefType::kBoolean;
#endif

  // Miscellaneous. TODO(stevenjb): categorize.
  (*s_allowlist)[::prefs::kEnableEncryptedMedia] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::language::prefs::kApplicationLocale] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kNetworkPredictionOptions] =
      settings_api::PrefType::kNumber;

  // Privacy page
  (*s_allowlist)[::prefs::kSigninAllowedOnNextStartup] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kDnsOverHttpsMode] = settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kDnsOverHttpsTemplates] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kDnsOverHttpsAutomaticModeFallbackToDoh] =
      settings_api::PrefType::kBoolean;

  // Privacy Guide
  (*s_allowlist)[::prefs::kPrivacyGuideViewed] =
      settings_api::PrefType::kBoolean;

  // Privacy Sandbox page
  (*s_allowlist)[::prefs::kPrivacySandboxM1TopicsEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kPrivacySandboxM1FledgeEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kPrivacySandboxM1AdMeasurementEnabled] =
      settings_api::PrefType::kBoolean;

  // Security page
  (*s_allowlist)[::kGeneratedPasswordLeakDetectionPref] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kSafeBrowsingEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kSafeBrowsingEnhanced] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kSafeBrowsingScoutReportingEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::safe_browsing::kGeneratedSafeBrowsingPref] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kHttpsOnlyModeEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::kGeneratedHttpsFirstModePref] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kSecuritySettingsBundle] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::safe_browsing::kGeneratedSecuritySettingsBundlePref] =
      settings_api::PrefType::kNumber;

  // Third-party cookie settings page
  (*s_allowlist)[::prefs::kCookieControlsMode] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::content_settings::kCookieDefaultContentSetting] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::content_settings::kThirdPartyCookieBlockingSetting] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kPrivacySandboxRelatedWebsiteSetsEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kBlockAll3pcToggleEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kEnableDoNotTrack] = settings_api::PrefType::kBoolean;

  // Sync and personalization page.
  (*s_allowlist)[::prefs::kSearchSuggestEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)
      [::unified_consent::prefs::kUrlKeyedAnonymizedDataCollectionEnabled] =
          settings_api::PrefType::kBoolean;
  (*s_allowlist)[::commerce::kPriceEmailNotificationsEnabled] =
      settings_api::PrefType::kBoolean;

  // Languages page
  (*s_allowlist)[language::prefs::kSelectedLanguages] =
      settings_api::PrefType::kString;
  (*s_allowlist)[language::prefs::kForcedLanguages] =
      settings_api::PrefType::kList;
  (*s_allowlist)[::language::prefs::kAcceptLanguages] =
      settings_api::PrefType::kString;

  // Search page.
  (*s_allowlist)[DefaultSearchManager::kDefaultSearchProviderDataPrefName] =
      settings_api::PrefType::kDictionary;
  (*s_allowlist)[::omnibox::kKeywordSpaceTriggeringEnabled] =
      settings_api::PrefType::kBoolean;

  // Site Settings prefs.
  (*s_allowlist)[::content_settings::kGeneratedNotificationPref] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::content_settings::kGeneratedGeolocationPref] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::content_settings::kGeneratedJavascriptOptimizerPref] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kPluginsAlwaysOpenPdfExternally] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kProtectedContentDefault] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kEnableQuietNotificationPermissionUi] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[safety_hub_prefs::kUnusedSitePermissionsRevocationEnabled] =
      settings_api::PrefType::kBoolean;

#if BUILDFLAG(ENABLE_COMPOSE)
  (*s_allowlist)[prefs::kEnableProactiveNudge] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[prefs::kProactiveNudgeDisabledSitesWithTime] =
      settings_api::PrefType::kDictionary;
#endif  // BUILDFLAG(ENABLE_COMPOSE)

  // Clear browsing data settings.
  (*s_allowlist)[browsing_data::prefs::kDeleteBrowsingHistory] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteBrowsingHistoryBasic] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteDownloadHistory] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteCache] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteCacheBasic] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteCookies] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteCookiesBasic] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeletePasswords] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteFormData] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteSiteSettings] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteHostedAppsData] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[browsing_data::prefs::kDeleteTimePeriod] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[browsing_data::prefs::kDeleteTimePeriodBasic] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[browsing_data::prefs::kLastClearBrowsingDataTab] =
      settings_api::PrefType::kNumber;

  // Accessibility.
  (*s_allowlist)[::prefs::kAccessibilityImageLabelsEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kAccessibilityCaptionsTextSize] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kAccessibilityCaptionsTextFont] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kAccessibilityCaptionsTextColor] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kAccessibilityCaptionsTextOpacity] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kAccessibilityCaptionsBackgroundColor] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kAccessibilityCaptionsTextShadow] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kAccessibilityCaptionsBackgroundOpacity] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[::prefs::kLiveCaptionEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kLiveCaptionLanguageCode] =
      settings_api::PrefType::kString;
  (*s_allowlist)[::prefs::kLiveCaptionMaskOffensiveWords] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kAccessibilityAXTreeFixingEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kAccessibilityMainNodeAnnotationsEnabled] =
      settings_api::PrefType::kBoolean;
#if defined(USE_AURA)
  (*s_allowlist)[::prefs::kOverscrollHistoryNavigationEnabled] =
      settings_api::PrefType::kBoolean;
#endif
#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
  (*s_allowlist)[::prefs::kToastAlertLevel] = settings_api::PrefType::kNumber;
#endif

  (*s_allowlist)[::prefs::kCaretBrowsingEnabled] =
      settings_api::PrefType::kBoolean;

  // System settings.
  (*s_allowlist)[::prefs::kBackgroundModeEnabled] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kHardwareAccelerationModeEnabled] =
      settings_api::PrefType::kBoolean;

  // Import data
  (*s_allowlist)[::prefs::kImportDialogAutofillFormData] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kImportDialogBookmarks] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kImportDialogHistory] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kImportDialogSavedPasswords] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[::prefs::kImportDialogSearchEngine] =
      settings_api::PrefType::kBoolean;

  // Supervised Users.  This setting is queried in our Tast tests (b/241943380).
  (*s_allowlist)[::prefs::kSupervisedUserExtensionsMayRequestPermissions] =
      settings_api::PrefType::kBoolean;


  // This feature exists in all platforms but is enabled in ash above.
  (*s_allowlist)[prefs::kAccessibilityFocusHighlightEnabled] =
      settings_api::PrefType::kBoolean;

  // Proxy settings.
  (*s_allowlist)[proxy_config::prefs::kProxy] =
      settings_api::PrefType::kDictionary;
  // Proxy override rules.
  (*s_allowlist)[proxy_config::prefs::kProxyOverrideRules] =
      settings_api::PrefType::kList;

#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  (*s_allowlist)[::prefs::kUserFeedbackAllowed] =
      settings_api::PrefType::kBoolean;
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)

  // Media Remoting settings.
  (*s_allowlist)[media_router::prefs::kMediaRouterMediaRemotingEnabled] =
      settings_api::PrefType::kBoolean;

  // Performance settings.
  (*s_allowlist)
      [performance_manager::user_tuning::prefs::kMemorySaverModeState] =
          settings_api::PrefType::kNumber;
  (*s_allowlist)[performance_manager::user_tuning::prefs::
                     kMemorySaverModeAggressiveness] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[performance_manager::user_tuning::prefs::
                     kMemorySaverModeTimeBeforeDiscardInMinutes] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)
      [performance_manager::user_tuning::prefs::kBatterySaverModeState] =
          settings_api::PrefType::kNumber;
  (*s_allowlist)[performance_manager::user_tuning::prefs::
                     kTabDiscardingExceptionsWithTime] =
      settings_api::PrefType::kDictionary;
  (*s_allowlist)[performance_manager::user_tuning::prefs::
                     kManagedTabDiscardingExceptions] =
      settings_api::PrefType::kList;
  (*s_allowlist)
      [performance_manager::user_tuning::prefs::kDiscardRingTreatmentEnabled] =
          settings_api::PrefType::kBoolean;
  (*s_allowlist)[performance_manager::user_tuning::prefs::
                     kPerformanceInterventionNotificationEnabled] =
      settings_api::PrefType::kBoolean;

  // AI settings.
  (*s_allowlist)[optimization_guide::prefs::GetSettingEnabledPrefName(
      optimization_guide::UserVisibleFeatureKey::kCompose)] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[optimization_guide::prefs::GetSettingEnabledPrefName(
      optimization_guide::UserVisibleFeatureKey::kTabOrganization)] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[optimization_guide::prefs::GetSettingEnabledPrefName(
      optimization_guide::UserVisibleFeatureKey::kWallpaperSearch)] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[optimization_guide::prefs::GetSettingEnabledPrefName(
      optimization_guide::UserVisibleFeatureKey::kHistorySearch)] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[optimization_guide::prefs::GetSettingEnabledPrefName(
      optimization_guide::UserVisibleFeatureKey::kPasswordChangeSubmission)] =
      settings_api::PrefType::kNumber;

  // AI enterprise prefs
  (*s_allowlist)
      [optimization_guide::prefs::kTabOrganizationEnterprisePolicyAllowed] =
          settings_api::PrefType::kNumber;
  (*s_allowlist)[optimization_guide::prefs::kComposeEnterprisePolicyAllowed] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)
      [optimization_guide::prefs::kWallpaperSearchEnterprisePolicyAllowed] =
          settings_api::PrefType::kNumber;
  (*s_allowlist)
      [optimization_guide::prefs::kHistorySearchEnterprisePolicyAllowed] =
          settings_api::PrefType::kNumber;
  (*s_allowlist)[optimization_guide::prefs::
                     kProductSpecificationsEnterprisePolicyAllowed] =
      settings_api::PrefType::kNumber;
  (*s_allowlist)[optimization_guide::prefs::
                     kAutofillPredictionImprovementsEnterprisePolicyAllowed] =
      settings_api::PrefType::kNumber;

  // Glic prefs
  if (glic::GlicEnabling::IsEnabledByFlags()) {
    (*s_allowlist)[glic::prefs::kGlicPinnedToTabstrip] =
        settings_api::PrefType::kBoolean;
    (*s_allowlist)[glic::prefs::kGlicLauncherEnabled] =
        settings_api::PrefType::kBoolean;
    (*s_allowlist)[glic::prefs::kGlicClosedCaptioningEnabled] =
        settings_api::PrefType::kBoolean;
    (*s_allowlist)[glic::prefs::kGlicGeolocationEnabled] =
        settings_api::PrefType::kBoolean;
    (*s_allowlist)[glic::prefs::kGlicMicrophoneEnabled] =
        settings_api::PrefType::kBoolean;
    (*s_allowlist)[glic::prefs::kGlicTabContextEnabled] =
        settings_api::PrefType::kBoolean;
    (*s_allowlist)[glic::prefs::kGlicDefaultTabContextEnabled] =
        settings_api::PrefType::kBoolean;
    (*s_allowlist)[glic::prefs::kGlicUserStatus] =
        settings_api::PrefType::kDictionary;
    (*s_allowlist)[prefs::kGeminiSettings] = settings_api::PrefType::kNumber;
    (*s_allowlist)[glic::prefs::kGlicUserEnabledActuationOnWeb] =
        settings_api::PrefType::kBoolean;
    (*s_allowlist)[glic::prefs::kGlicKeepSidepanelOpenOnNewTabsEnabled] =
        settings_api::PrefType::kBoolean;
  }

  return *s_allowlist;
}

settings_api::PrefType PrefsUtil::GetAllowlistedPrefType(
    const std::string& pref_name) {
  const TypedPrefMap& keys = GetAllowlistedKeys();
  const auto& iter = keys.find(pref_name);
  return iter != keys.end() ? iter->second : settings_api::PrefType::kNone;
}

settings_api::PrefType PrefsUtil::GetType(const std::string& name,
                                          base::Value::Type type) {
  switch (type) {
    case base::Value::Type::BOOLEAN:
      return settings_api::PrefType::kBoolean;
    case base::Value::Type::INTEGER:
    case base::Value::Type::DOUBLE:
      return settings_api::PrefType::kNumber;
    case base::Value::Type::STRING:
      return IsPrefTypeURL(name) ? settings_api::PrefType::kUrl
                                 : settings_api::PrefType::kString;
    case base::Value::Type::LIST:
      return settings_api::PrefType::kList;
    case base::Value::Type::DICT:
      return settings_api::PrefType::kDictionary;
    default:
      return settings_api::PrefType::kNone;
  }
}

std::optional<settings_api::PrefObject> PrefsUtil::GetCrosSettingsPref(
    const std::string& name) {
  std::optional<settings_api::PrefObject> pref_object(std::in_place);


  return pref_object;
}

std::optional<settings_api::PrefObject> PrefsUtil::GetPref(
    const std::string& name) {
  if (GetAllowlistedPrefType(name) == settings_api::PrefType::kNone) {
    return std::nullopt;
  }

  settings_private::GeneratedPrefs* generated_prefs =
      settings_private::GeneratedPrefsFactory::GetForBrowserContext(profile_);

  const PrefService::Preference* pref = nullptr;
  std::optional<settings_api::PrefObject> pref_object;
  if (IsCrosSetting(name)) {
    pref_object = GetCrosSettingsPref(name);
    if (!pref_object) {
      return std::nullopt;
    }
  } else if (generated_prefs && generated_prefs->HasPref(name)) {
    return generated_prefs->GetPref(name);
  } else {
    PrefService* pref_service = FindServiceForPref(name);
    pref = pref_service->FindPreference(name);
    if (!pref) {
      return std::nullopt;
    }
    pref_object.emplace();
    pref_object->key = pref->name();
    pref_object->type = GetType(name, pref->GetType());
    pref_object->value = pref->GetValue()->Clone();
  }

  if (pref && pref->IsManaged()) {
    pref_object->controlled_by = settings_api::ControlledBy::kUserPolicy;
  }

  if (pref && pref->IsManagedByCustodian()) {
    pref_object->controlled_by = settings_api::ControlledBy::kChildRestriction;
  }

  if (pref_object->controlled_by != settings_api::ControlledBy::kNone) {
    pref_object->enforcement = settings_api::Enforcement::kEnforced;
    return pref_object;
  }

  // A pref is recommended if it has a recommended value, regardless of whether
  // the current value is set by policy. The UI will test to see whether the
  // current value matches the recommended value and inform the user.
  const base::Value* recommended = pref ? pref->GetRecommendedValue() : nullptr;
  if (recommended) {
    pref_object->controlled_by = settings_api::ControlledBy::kUserPolicy;
    pref_object->enforcement = settings_api::Enforcement::kRecommended;
    pref_object->recommended_value = recommended->Clone();
    return pref_object;
  }


  const Extension* extension = GetExtensionControllingPref(*pref_object);

  if (extension) {
    pref_object->controlled_by = settings_api::ControlledBy::kExtension;
    pref_object->enforcement = settings_api::Enforcement::kEnforced;
    pref_object->extension_id = extension->id();
    pref_object->controlled_by_name = extension->name();
    bool can_be_disabled =
        !ExtensionSystem::Get(profile_)->management_policy()->MustRemainEnabled(
            extension, nullptr);
    pref_object->extension_can_be_disabled = can_be_disabled;
    return pref_object;
  }

  // TODO(dbeam): surface !IsUserModifiable or IsPrefSupervisorControlled?

  return pref_object;
}

settings_private::SetPrefResult PrefsUtil::SetPref(const std::string& pref_name,
                                                   const base::Value* value) {
  if (GetAllowlistedPrefType(pref_name) == settings_api::PrefType::kNone) {
    return settings_private::SetPrefResult::PREF_NOT_FOUND;
  }

  if (IsCrosSetting(pref_name)) {
    return SetCrosSettingsPref(pref_name, value);
  }

  settings_private::GeneratedPrefs* generated_prefs =
      settings_private::GeneratedPrefsFactory::GetForBrowserContext(profile_);
  if (generated_prefs && generated_prefs->HasPref(pref_name)) {
    return generated_prefs->SetPref(pref_name, value);
  }

  PrefService* pref_service = FindServiceForPref(pref_name);

  if (!IsPrefUserModifiable(pref_name)) {
    return settings_private::SetPrefResult::PREF_NOT_MODIFIABLE;
  }

  const PrefService::Preference* pref = pref_service->FindPreference(pref_name);
  if (!pref) {
    return settings_private::SetPrefResult::PREF_NOT_FOUND;
  }

  switch (pref->GetType()) {
    case base::Value::Type::BOOLEAN:
    case base::Value::Type::LIST:
    case base::Value::Type::DICT:
      pref_service->Set(pref_name, *value);
      break;
    case base::Value::Type::DOUBLE:
    case base::Value::Type::INTEGER:
      // Explicitly set the double value or the integer value.
      // Otherwise if the number is a whole number like 2.0, it will
      // automatically be of type INTEGER causing type mismatches in
      // PrefService::SetUserPrefValue for doubles, and vice versa.
      if (!value->is_double() && !value->is_int()) {
        return settings_private::SetPrefResult::PREF_TYPE_MISMATCH;
      }
      double double_value;
      double_value = value->GetDouble();

      if (pref->GetType() == base::Value::Type::DOUBLE) {
        pref_service->SetDouble(pref_name, double_value);
      } else {
        pref_service->SetInteger(pref_name, static_cast<int>(double_value));
      }
      break;
    case base::Value::Type::STRING: {
      if (!value->is_string()) {
        return settings_private::SetPrefResult::PREF_TYPE_MISMATCH;
      }

      std::string string_value = value->GetString();
      if (IsPrefTypeURL(pref_name)) {
        GURL fixed = url_formatter::FixupURL(string_value);
        if (fixed.is_valid()) {
          string_value = fixed.spec();
        } else {
          string_value = std::string();
        }
      }

      pref_service->SetString(pref_name, string_value);
      break;
    }
    default:
      return settings_private::SetPrefResult::PREF_TYPE_UNSUPPORTED;
  }

  // TODO(orenb): Process setting metrics here and in the CrOS setting method
  // too (like "ProcessUserMetric" in CoreOptionsHandler).
  return settings_private::SetPrefResult::SUCCESS;
}

settings_private::SetPrefResult PrefsUtil::SetCrosSettingsPref(
    const std::string& pref_name,
    const base::Value* value) {
  return settings_private::SetPrefResult::PREF_NOT_FOUND;
}

bool PrefsUtil::AppendToListCrosSetting(const std::string& pref_name,
                                        const base::Value& value) {
  return false;
}

bool PrefsUtil::RemoveFromListCrosSetting(const std::string& pref_name,
                                          const base::Value& value) {
  return false;
}

bool PrefsUtil::IsPrefTypeURL(const std::string& pref_name) {
  return GetAllowlistedPrefType(pref_name) == settings_api::PrefType::kUrl;
}


bool PrefsUtil::IsPrefSupervisorControlled(const std::string& pref_name) {
  if (pref_name != prefs::kBrowserGuestModeEnabled &&
      pref_name != prefs::kBrowserAddPersonEnabled) {
    return false;
  }
  return profile_->IsChild();
}

bool PrefsUtil::IsPrefUserModifiable(const std::string& pref_name) {
  if (IsSettingReadOnly(pref_name)) {
    return false;
  }

  const PrefService::Preference* profile_pref =
      profile_->GetPrefs()->FindPreference(pref_name);
  if (profile_pref) {
    return profile_pref->IsUserModifiable();
  }

  const PrefService::Preference* local_state_pref =
      g_browser_process->local_state()->FindPreference(pref_name);
  if (local_state_pref) {
    return local_state_pref->IsUserModifiable();
  }

  return false;
}

PrefService* PrefsUtil::FindServiceForPref(const std::string& pref_name) {
  PrefService* user_prefs = profile_->GetPrefs();


  // Find which PrefService contains the given pref. Pref names should not
  // be duplicated across services, however if they are, prefer the user's
  // prefs.
  if (user_prefs->FindPreference(pref_name)) {
    return user_prefs;
  }

  if (g_browser_process->local_state()->FindPreference(pref_name)) {
    return g_browser_process->local_state();
  }

  return user_prefs;
}

bool PrefsUtil::IsCrosSetting(const std::string& pref_name) {
  return false;
}

const Extension* PrefsUtil::GetExtensionControllingPref(
    const settings_api::PrefObject& pref_object) {
  // Look for specific prefs that might be extension controlled. This generally
  // corresponds with some indiciator that should be shown in the settings UI.
  if (pref_object.key == ::prefs::kHomePage) {
    return GetExtensionOverridingHomepage(profile_);
  }

  if (pref_object.key == ::prefs::kRestoreOnStartup) {
    if (pref_object.value->GetInt() == SessionStartupPref::kPrefValueURLs) {
      return GetExtensionOverridingStartupPages(profile_);
    }
  }

  if (pref_object.key == ::prefs::kURLsToRestoreOnStartup) {
    return GetExtensionOverridingStartupPages(profile_);
  }

  if (pref_object.key ==
      DefaultSearchManager::kDefaultSearchProviderDataPrefName) {
    return GetExtensionOverridingSearchEngine(profile_);
  }

  if (pref_object.key == proxy_config::prefs::kProxy) {
    return GetExtensionOverridingProxy(profile_);
  }

  // If it's none of the above, attempt a more general strategy.
  std::string extension_id =
      ExtensionPrefValueMapFactory::GetForBrowserContext(profile_)
          ->GetExtensionControllingPref(pref_object.key);
  if (extension_id.empty()) {
    return nullptr;
  }

  return ExtensionRegistry::Get(profile_)->enabled_extensions().GetByID(
      extension_id);
}

}  // namespace extensions
