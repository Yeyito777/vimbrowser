// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feature_engagement/public/feature_list.h"

#include <vector>

#include "build/build_config.h"
#include "components/feature_engagement/public/feature_constants.h"

namespace feature_engagement {

namespace {
// Whenever a feature is added to |kAllFeatures|, it should also be added as
// DEFINE_VARIATION_PARAM in the header, and also added to the
// |kIPHDemoModeChoiceVariations| array.
const base::Feature* const kAllFeatures[] = {
    &kIPHDummyFeature,  // Ensures non-empty array for all platforms.

#if BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)
    &kIPHBottomToolbarTipFeature,
#endif  // BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
    &kEsbDownloadRowPromoFeature,
#endif
    &kIPHBatterySaverModeFeature,
    &kIPHCompanionSidePanelFeature,
    &kIPHCompanionSidePanelRegionSearchFeature,
    &kIPHComposeMSBBSettingsFeature,
    &kIPHComposeNewBadgeFeature,
    &kIPHDesktopCustomizeChromeExperimentFeature,
    &kIPHDesktopCustomizeChromeAutoOpenFeature,
    &kIPHDesktopRealboxContextualSearchFeature,
    &kIPHDiscardRingFeature,
    &kIPHDownloadEsbPromoFeature,
    &kIPHExplicitBrowserSigninPreferenceRememberedFeature,
    &kIPHGlicPromoFeature,
    &kIPHGlicTrustFirstOnboardingShortcutSnoozePromoFeature,
    &kIPHGlicTrustFirstOnboardingShortcutToastPromoFeature,
    &kIPHGlicTryItFeature,
    &kIPHHistorySearchFeature,
#if BUILDFLAG(ENABLE_EXTENSIONS)
    &kIPHExtensionsMenuFeature,
    &kIPHExtensionsRequestAccessButtonFeature,
    &kIPHExtensionsZeroStatePromoFeature,
#endif
    &kIPHFocusHelpBubbleScreenReaderPromoFeature,
    &kIPHGMCCastStartStopFeature,
    &kIPHGMCLocalMediaCastingFeature,
    &kIPHMemorySaverModeFeature,
    &kIPHLensOverlayFeature,
    &kIPHLensOverlayTranslateButtonFeature,
    &kIPHLiveCaptionFeature,
    &kIPHMerchantTrustFeature,
    &kIPHTabAudioMutingFeature,
    &kIPHPasswordsSavePrimingPromoFeature,
    &kIPHPasswordsSaveRecoveryPromoFeature,
    &kIPHPasswordsManagementBubbleAfterSaveFeature,
    &kIPHPasswordsManagementBubbleDuringSigninFeature,
    &kIPHPasswordsWebAppProfileSwitchFeature,
    &kIPHPasswordManagerShortcutFeature,
    &kIPHPasswordSharingFeature,
    &kIPHPdfInkSignaturesFeature,
    &kIPHPdfSearchifyFeature,
    &kIPHPerformanceInterventionDialogFeature,
    &kIPHPlusAddressFirstSaveFeature,
    &kIPHPowerBookmarksSidePanelFeature,
    &kIPHPriceInsightsPageActionIconLabelFeature,
    &kIPHPriceTrackingEmailConsentFeature,
    &kIPHPriceTrackingPageActionIconLabelFeature,
    &kIPHReadingListDiscoveryFeature,
    &kIPHReadingListEntryPointFeature,
    &kIPHReadingListInSidePanelFeature,
    &kIPHReadingModeSidePanelFeature,
    &kIPHReadingModePageActionLabelFeature,
    &kIPHShoppingCollectionFeature,
    &kIPHSideBySidePinnableFeature,
    &kIPHSideBySideTabSwitchFeature,
    &kIPHSidePanelGenericPinnableFeature,
    &kIPHSidePanelLensOverlayPinnableFeature,
    &kIPHSidePanelLensOverlayPinnableFollowupFeature,
    &kIPHSideSearchAutoTriggeringFeature,
    &kIPHSideSearchPageActionLabelFeature,
    &kIPHVerticalTabstripTutorialFeature,
    &kIPHTabGroupsSaveV2IntroFeature,
    &kIPHTabGroupsSaveV2CloseGroupFeature,
    &kIPHTabGroupsSharedTabChangedFeature,
    &kIPHTabGroupsSharedTabFeedbackFeature,
    &kIPHTabOrganizationSuccessFeature,
    &kIPHTabSearchComboButtonFeature,
    &kIPHTabSearchToolbarButtonFeature,
    &kIPHDesktopPwaInstallFeature,
    &kIPHProfileSwitchFeature,
    &kIPHDesktopSharedHighlightingFeature,
    &kIPHWebUiHelpBubbleTestFeature,
    &kIPHPriceTrackingInSidePanelFeature,
    &kIPHBackNavigationMenuFeature,
    &kIPHPwaQuietNotificationFeature,
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)
    &kIPHAutofillAiOptInFeature,
    &kIPHAutofillAiValuablesFeature,
    &kIPHAutofillBnplAffirmOrZipSuggestionFeature,
    &kIPHAutofillBnplAffirmZipOrKlarnaSuggestionFeature,
    &kIPHAutofillCardInfoRetrievalSuggestionFeature,
    &kIPHAutofillCreditCardBenefitFeature,
    &kIPHAutofillDisabledVirtualCardSuggestionFeature,
    &kIPHAutofillEnableLoyaltyCardsFeature,
    &kIPHAutofillExternalAccountProfileSuggestionFeature,
    &kIPHAutofillHomeWorkProfileSuggestionFeature,
    &kIPHAutofillAccountNameEmailSuggestionFeature,
    &kIPHAutofillVirtualCardCVCSuggestionFeature,
    &kIPHAutofillVirtualCardSuggestionFeature,
    &kIPHCookieControlsFeature,
    &kIPHPlusAddressCreateSuggestionFeature,
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) ||
        // BUILDFLAG(IS_FUCHSIA)


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    &kIPHDesktopPWAsLinkCapturingLaunch,
    &kIPHDesktopPWAsLinkCapturingLaunchAppInTab,
    &kIPHSignInBenefitsFeature,
    &kIPHSupervisedUserProfileSigninFeature,
#endif  // BUILDFLAG(IS_WIN) ||  BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

    &kIPHiOSPasswordPromoDesktopFeature,
    &kIPHiOSAddressPromoDesktopFeature,
    &kIPHiOSPaymentPromoDesktopFeature,
    &kIPHiOSLensPromoDesktopFeature,
    &kIPHiOSEnhancedBrowsingDesktopFeature,
    &kIPHiOSTabGroupsDesktopFeature,
    &kIPHiOSPriceTrackingDesktopFeature,

    &kIPHResumptionRailFeature,
};
}  // namespace

std::vector<const base::Feature*> GetAllFeatures() {
  return std::vector<const base::Feature*>(std::begin(kAllFeatures),
                                           std::end(kAllFeatures));
}

}  // namespace feature_engagement
