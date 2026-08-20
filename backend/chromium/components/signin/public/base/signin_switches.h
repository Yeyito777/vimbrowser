// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_PUBLIC_BASE_SIGNIN_SWITCHES_H_
#define COMPONENTS_SIGNIN_PUBLIC_BASE_SIGNIN_SWITCHES_H_

#include "base/component_export.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "build/build_config.h"
#include "components/signin/public/base/signin_buildflags.h"

class PrefService;

namespace switches {

// The switches should be documented alongside the definition of their values in
// the .cc file.

// Symbols must be annotated with COMPONENT_EXPORT(SIGNIN_SWITCHES) so that they
// can be exported by the signin_switches component. This prevents issues with
// component layering.

// Command line switches, sorted by name.

COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const char kClearTokenService[];

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const char kForceFreDefaultBrowserStep[];
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

// Feature declarations, sorted by the name of the BASE_DECLARE_FEATURE in each
// block. Please keep all FeatureParam declarations, enum class definitions, and
// helper function declarations for a given feature in the same
// newline-separated block as the feature declaration.
//
// clang-format off
// keep-sorted start allow_yaml_lists=yes case=no group_prefixes=["#if", "#else", "#endif", "extern const", "enum class", "};", "//", "bool", "base::", "BASE_DECLARE_FEATURE", "BASE_DECLARE_FEATURE_PARAM", "COMPONENT_EXPORT(SIGNIN_SWITCHES)"] by_regex=["BASE_DECLARE_FEATURE\\(.*\\);"] skip_lines=2
// clang-format on


COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kAvatarButtonSyncPromoForTesting);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
bool IsAvatarSyncPromoFeatureEnabled();
COMPONENT_EXPORT(SIGNIN_SWITCHES)
base::TimeDelta GetAvatarSyncPromoFeatureMinimumCookeAgeParam();

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// A HaTS survey flag for the survey to gather user feedback before any changes
// to the FRE as part of Chrome Desktop FRE Refresh project.
//
// NOTE: Only signed-in (excluding enterprise) users are eligible for this
// survey.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kBeforeFirstRunDesktopRefreshSurvey);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kBoundSessionCredentialsKillSwitch);
#endif  // BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)




#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// Enables surveys to measure the effectiveness of the identity model.
// These surveys would be displayed after interactions such as signin, profile
// switching, etc. Please keep sorted alphabetically.
// LINT.IfChange
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyAddressBubbleSignin);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(double,
                           kChromeIdentitySurveyAddressBubbleSigninProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyDiceWebSigninAccepted);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveyDiceWebSigninAcceptedProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyDiceWebSigninDeclined);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveyDiceWebSigninDeclinedProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyFirstRunSignin);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(double,
                           kChromeIdentitySurveyFirstRunSigninProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyPasswordBubbleSignin);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveyPasswordBubbleSigninProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyProfileMenuDismissed);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveyProfileMenuDismissedProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyProfileMenuSignin);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(double,
                           kChromeIdentitySurveyProfileMenuSigninProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyProfilePickerAddProfileSignin);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveyProfilePickerAddProfileSigninProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveySigninInterceptProfileSeparation);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveySigninInterceptProfileSeparationProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveySigninPromoBubbleDismissed);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveySigninPromoBubbleDismissedProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveySwitchProfileFromProfileMenu);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveySwitchProfileFromProfileMenuProbability);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveySwitchProfileFromProfilePicker);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(
    double,
    kChromeIdentitySurveySwitchProfileFromProfilePickerProbability);
// LINT.ThenChange(//chrome/browser/signin/signin_hats_util.cc)
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// Controls the duration for which the launch of an identity survey is delayed.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kChromeIdentitySurveyLaunchWithDelay);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(base::TimeDelta,
                           kChromeIdentitySurveyLaunchWithDelayDuration);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// If enabled, disables feedback for U18 users on desktop platforms.
// The iOS version is kDisableU18FeedbackIos flag.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kDisableU18FeedbackDesktop);
enum class U18FeedbackDesktopState {
  kEnabled,
  // Simulates U18 user.
  kForced,
};
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const base::FeatureParam<U18FeedbackDesktopState>
    kDisableU18FeedbackDesktopState;
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)




#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnableBoundSessionCredentials);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const base::FeatureParam<std::string>
    kEnableBoundSessionCredentialsExclusiveRegistrationPath;
COMPONENT_EXPORT(SIGNIN_SWITCHES)
bool IsBoundSessionCredentialsEnabled(const PrefService* profile_prefs);
#endif  // BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnableChromeRefreshTokenBinding);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
bool IsChromeRefreshTokenBindingEnabled(const PrefService* profile_prefs);
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

#if !defined(NDEBUG)
// A fake feature corresponding to the kFakeCapabilityForTestingName account
// capability. This is only used in unit tests (and must be left disabled to
// prevent fetching the fake capability).
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnableFakeCapabilityForTesting);
#endif

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnableOAuthMultiloginCookiesBinding);
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnableOAuthMultiloginCookiesBindingServerExperiment);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(bool, kOAuthMultiloginCookieBindingEnforced);
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnableOAuthMultiloginStandardCookiesBinding);
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(
    kEnableOAuthMultiloginStandardCookiesBindingForGlicPartition);
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

// Enables a separate account-scoped storage for preferences.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnablePreferencesAccountStorage);


#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnableSearchAIModeSigninPromo);
#endif


#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kEnforceManagementDisclaimer);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const base::FeatureParam<base::TimeDelta>
    kPolicyDisclaimerRegistrationRetryDelay;
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// This feature controls running visually refreshed first run and profile
// creation flows for users outside of the search engine choice regions. To
// enable the refresh in search engine choice screen regions,
// `kFirstRunDesktopChoiceScreenRefresh` needs to be enabled as well.
//
// Clients should never use this feature directly to determine if the
// refresh is enabled, they should use `IsFirstRunDesktopRefreshEnabled()`
// instead.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kFirstRunDesktopRefresh);
// This feature controls running visually refreshed first run and profile
// creation flows, including the choice screen, for users in search engine
// choice screen regions. This feature is no-op if `kFirstRunDesktopRefresh` is
// disabled.
//
// Clients should never use this feature directly to determine if the
// refresh is enabled, they should use `IsFirstRunDesktopRefreshEnabled()`
// instead.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kFirstRunDesktopChoiceScreenRefresh);
// A helper function to determine if the first run desktop refresh is enabled
// (see `kFirstRunDesktopRefresh` and `kFirstRunDesktopChoiceScreenRefresh`
// flags).
COMPONENT_EXPORT(SIGNIN_SWITCHES)
bool IsFirstRunDesktopRefreshEnabled(bool is_in_search_engine_choice_region);
enum class FirstRunDesktopSignInPromoVariation {
  // Default sign-in promo containing both sign-in and don't sign-in buttons
  // next to each other on the promo page.
  kDefault = 0,
  // Sign-in promo containing both sign-in and don't sign-in buttons but the
  // don't sign in button is moved to the top corner of the promo page and the
  // page informs the user they can create an account in the next step(s).
  kDontSignInInTheTopCorner = 1,
  // Sign-in promo containing only the sign-in button on the promo page. The
  // don't sign in button is moved to the Gaia page.
  kDontSignInOnGaiaPage = 2,
};
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const base::FeatureParam<FirstRunDesktopSignInPromoVariation>
    kFirstRunDesktopSignInPromoVariation;
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// It enables the first run revamp (introduce new UIs and additional effects).
// This feature is no-op if `kFirstRunDesktopRefresh` is disabled.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kFirstRunDesktopRevamp);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)


#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kForceStartupSigninPromo);
#endif


#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
// TODO(crbug.com/408962000): This feature is going to be used after clients
// have the required information in local storage.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kFullscreenSignInPromoUseDate);
#endif

// When enabled, GLIC will check a new CanUseGeminiInChrome account capability
// to determine profile eligibility, instead of CanUseModelExecutionFeatures.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kGlicEligibilitySeparateAccountCapability);

// Feature to handle mdm errors on Enterprise and EDU accounts
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kHandleMdmErrorsForDasherAccounts);



COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kNonDefaultGaiaOriginCheck);

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// Experimenting with a button to all profiles from the profile picker.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kOpenAllProfilesFromProfilePickerExperiment);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const base::FeatureParam<int>
    kMaxProfilesCountToShowOpenAllButtonInProfilePicker;
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// Add new entry points for uploading passwords to account storage and update
// existing ones.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kPasswordUploadUiUpdate);

// Experimenting with changing the secondary CTA for FRE and new profile
// creation.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kProfileCreationDeclineSigninCTAExperiment);

// Experimenting with prefill name requirement for the profile creation flow.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(
    kProfileCreationFrictionReductionExperimentPrefillNameRequirement);

// Experimenting with removing signin in the profile creation flow.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(
    kProfileCreationFrictionReductionExperimentRemoveSigninStep);

// Experimenting with removing the profile customization bubble in the profile
// creation flow.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(
    kProfileCreationFrictionReductionExperimentSkipCustomizeProfile);

// Enables variations of the profile picker text.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kProfilePickerTextVariations);
enum class ProfilePickerVariation {
  kKeepWorkAndLifeSeparate = 0,
  kGotAnotherGoogleAccount = 1,
  kKeepTasksSeparate = 2,
  kSharingAComputer = 3,
  kKeepEverythingInChrome = 4,
};
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const base::FeatureParam<ProfilePickerVariation>
    kProfilePickerTextVariation;
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kProfilesReordering);


// Kill switch for Device Management Service OAuth scope.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kRestrictDeviceManagementServiceOAuthScope);

// Experimenting with showing the profile picker to all users (not only the
// users with multiple profiles).
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kShowProfilePickerToAllUsersExperiment);


// Feature to control the experiment for max count of showing contextual sign-in
// promos and UNO bubble reprompt.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kSigninPromoLimitsExperiment);
// Param that controls the threshold of the contextual sign in promo shown
// limit for the experiment.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const base::FeatureParam<int> kContextualSigninPromoShownThreshold;
// Param that controls the threshold of the contextual sign in promos dismissed
// limit for the experiment.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
extern const base::FeatureParam<int> kContextualSigninPromoDismissedThreshold;

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// Uses the Material Next theme for the signin promo.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kSignInPromoMaterialNextUI);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
// Feature to show a promo on the avatar pill on profile startup.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kSigninPromoOnAvatarPill);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(base::TimeDelta,
                           kSigninPromoOnAvatarPillStartupDelayForPromoShow);
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE_PARAM(base::TimeDelta,
                           kSigninPromoOnAvatarPillDelayForNextPromoAllowed);
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

// Feature flag used for testing purposes only:
//
// Set this flag to force the flow on any platform.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kSigninWindows10DepreciationStateForTesting);
// Set this flag to force the flow off on Windows 10 (a lot of bots run on
// Windows 10) - to avoid having generic tests having a per platform
// expectations.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kSigninWindows10DepreciationStateBypassForTesting);
COMPONENT_EXPORT(SIGNIN_SWITCHES) bool IsSigninWindows10DepreciationState();


COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kSkipRefreshTokenCheckInIdentityManager);




#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// Kill switch for displaying sign-in errors in the profile picker.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kSupportErrorsInProfilePicker);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)


// This gates the new single-model approach where account bookmarks are stored
// in separate permanent folders in BookmarkModel. The flag controls whether
// BOOKMARKS datatype is enabled in the transport mode.
// TODO(crbug.com/40943550): Remove this.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kSyncEnableBookmarksInTransportMode);
// This feature flag is used as a subset of the original code that was behind
// `kSyncEnableBookmarksInTransportMode` that introduced changes that are not
// directly related to Transport Mode. Mostly the changes are Ui-visible and
// will be migrated to be using this flag instead. This will allow to run
// a Finch study on Cros and launch independently of TransportMode on Cros. The
// flag is enabled by default on Windows/Mac/Linux.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kBookmarksMigrateUiChanges);

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kUseIssueTokenToFetchAccessTokens);
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

// If enabled, buttons for sign-in promos / intercepts will use consistent
// primary - tonal button class pattern.
COMPONENT_EXPORT(SIGNIN_SWITCHES)
BASE_DECLARE_FEATURE(kUsePrimaryAndTonalButtonsForPromos);


// keep-sorted end

// Helper functions that are no longer attached to any features.

// Returns if the current browser supports an explicit sign in (signs the user
// into transport mode, as defined above) for extension access points (e.g. the
// `ExtensionPostInstallDialogDelegate`).
COMPONENT_EXPORT(SIGNIN_SWITCHES)
bool IsExtensionsExplicitBrowserSigninEnabled();

}  // namespace switches

#endif  // COMPONENTS_SIGNIN_PUBLIC_BASE_SIGNIN_SWITCHES_H_
