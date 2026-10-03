// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Instructions for adding new entries to this file:
// https://chromium.googlesource.com/chromium/src/+/main/docs/how_to_add_your_feature_flag.md#step-2_adding-the-feature-flag-to-the-chrome_flags-ui

#include "chrome/browser/about_flags.h"

#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <utility>

#include "base/allocator/partition_alloc_features.h"
#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/features.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/base_i18n_switches.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram_functions.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/task_features.h"
#include "base/values.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "cc/base/features.h"
#include "cc/base/switches.h"
#include "chrome/browser/actor/actor_switches.h"
#include "chrome/browser/apps/app_discovery_service/app_discovery_service.h"
#include "chrome/browser/browser_features.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/default_browser/default_browser_features.h"
#include "chrome/browser/devtools/features.h"
#include "chrome/browser/flag_descriptions.h"
#include "chrome/browser/glic/public/features.h"
#include "chrome/browser/login_detection/login_detection_util.h"
#include "chrome/browser/media/router/discovery/access_code/access_code_cast_constants.h"
#include "chrome/browser/media/router/discovery/access_code/access_code_cast_feature.h"
#include "chrome/browser/media/webrtc/desktop_media_picker.h"
#include "chrome/browser/navigation_predictor/navigation_predictor_features.h"
#include "chrome/browser/navigation_predictor/search_engine_preconnector.h"
#include "chrome/browser/net/stub_resolver_config_reader.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/page_info/page_info_features.h"
#include "chrome/browser/permissions/notifications_permission_revocation_config.h"
#include "chrome/browser/permissions/quiet_notification_permission_ui_config.h"
#include "chrome/browser/predictors/loading_predictor_config.h"
#include "chrome/browser/preloading/prefetch/search_prefetch/field_trial_settings.h"
#include "chrome/browser/preloading/preloading_features.h"
#include "chrome/browser/preloading/search_preload/search_preload_features.h"
#include "chrome/browser/sharing_hub/sharing_hub_features.h"
#include "chrome/browser/site_isolation/about_flags.h"
#include "chrome/browser/task_manager/common/task_manager_features.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/omnibox/omnibox_next_features.h"
#include "chrome/browser/ui/tabs/tab_group_home/constants.h"
#include "chrome/browser/ui/toasts/toast_features.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/webui/new_tab_page/composebox/variations/composebox_fieldtrial.h"
#include "chrome/browser/unexpire_flags.h"
#include "chrome/browser/unexpire_flags_gen.h"
#include "chrome/browser/web_applications/isolated_web_apps/key_distribution/features.h"
#include "chrome/browser/web_applications/link_capturing_features.h"
#include "chrome/browser/webauthn/webauthn_switches.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "components/assist_ranker/predictor_config_definitions.h"
#include "components/autofill/core/browser/manual_testing_import.h"
#include "components/autofill/core/browser/studies/autofill_experiments.h"
#include "components/autofill/core/common/autofill_debug_features.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/browser_sync/browser_sync_switches.h"
#include "components/browsing_data/core/features.h"
#include "components/collaboration/public/features.h"
#include "components/commerce/core/commerce_feature_list.h"
#include "components/commerce/core/flag_descriptions.h"
#include "components/component_updater/component_updater_command_line_config_policy.h"
#include "components/component_updater/component_updater_switches.h"
#include "components/compose/buildflags.h"
#include "components/compose/core/browser/compose_features.h"
#include "components/content_settings/core/common/features.h"
#include "components/contextual_tasks/public/features.h"
#include "components/data_sharing/public/features.h"
#include "components/data_sharing/public/switches.h"
#include "components/device_signals/core/common/signals_features.h"
#include "components/dom_distiller/core/dom_distiller_features.h"
#include "components/dom_distiller/core/dom_distiller_switches.h"
#include "components/download/public/common/download_features.h"
#include "components/enterprise/client_certificates/core/features.h"
#include "components/enterprise/data_controls/core/browser/features.h"
#include "components/error_page/common/error_page_switches.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/feature_engagement/public/feature_list.h"
#include "components/feed/feed_feature_list.h"
#include "components/heavy_ad_intervention/heavy_ad_features.h"
#include "components/history/core/browser/features.h"
#include "components/history_clusters/core/config.h"
#include "components/history_clusters/core/features.h"
#include "components/history_clusters/core/on_device_clustering_features.h"
#include "components/history_embeddings/core/history_embeddings_features.h"
#include "components/input/features.h"
#include "components/language/core/common/language_experiments.h"
#include "components/lens/buildflags.h"
#include "components/lens/lens_features.h"
#include "components/manta/features.h"
#include "components/metrics/private_metrics/private_metrics_features.h"
#include "components/mirroring/service/mirroring_features.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/ntp_tiles/features.h"
#include "components/offline_pages/core/offline_page_feature.h"
#include "components/omnibox/browser/aim_eligibility_service_features.h"
#include "components/omnibox/browser/omnibox_field_trial.h"
#include "components/omnibox/common/omnibox_feature_configs.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/open_from_clipboard/clipboard_recent_content_features.h"
#include "components/optimization_guide/core/model_execution/model_execution_features.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/optimization_guide/core/optimization_guide_switches.h"
#include "components/page_content_annotations/core/page_content_annotations_features.h"
#include "components/page_content_annotations/core/page_content_annotations_switches.h"
#include "components/page_info/core/features.h"
#include "components/paint_preview/buildflags/buildflags.h"
#include "components/paint_preview/features/features.h"
#include "components/password_manager/core/browser/features/password_features.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/payments/core/features.h"
#include "components/performance_manager/public/features.h"
#include "components/permissions/features.h"
#include "components/plus_addresses/core/common/features.h"
#include "components/policy/core/common/features.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/regional_capabilities/regional_capabilities_switches.h"
#include "components/remote_cocoa/app_shim/features.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/safe_browsing/core/common/safebrowsing_switches.h"
#include "components/safety_check/features.h"
#include "components/saved_tab_groups/public/features.h"
#include "components/search/ntp_features.h"
#include "components/search_engines/search_engines_switches.h"
#include "components/security_interstitials/content/stateful_ssl_host_state_delegate.h"
#include "components/security_interstitials/core/features.h"
#include "components/security_state/core/security_state.h"
#include "components/segmentation_platform/embedder/home_modules/constants.h"
#include "components/segmentation_platform/public/features.h"
#include "components/send_tab_to_self/features.h"
#include "components/sensitive_content/features.h"
#include "components/services/heap_profiling/public/cpp/switches.h"
#include "components/services/storage/dom_storage/features.h"
#include "components/shared_highlighting/core/common/shared_highlighting_features.h"
#include "components/sharing_message/features.h"
#include "components/signin/core/browser/dice_account_reconcilor_delegate.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/site_isolation/features.h"
#include "components/skills/features.h"
#include "components/soda/soda_features.h"
#include "components/spellcheck/spellcheck_buildflags.h"
#include "components/strike_database/strike_database_features.h"
#include "components/supervised_user/core/common/features.h"
#include "components/supervised_user/core/common/supervised_user_constants.h"
#include "components/sync/base/command_line_switches.h"
#include "components/sync/base/features.h"
#include "components/sync_preferences/features.h"
#include "components/thin_webview/features.h"
#include "components/touch_to_search/core/browser/contextual_search_field_trial.h"
#include "components/touch_to_search/core/browser/public.h"
#include "components/tracing/common/tracing_switches.h"
#include "components/trusted_vault/features.h"
#include "components/ui_devtools/switches.h"
#include "components/variations/variations_switches.h"
#include "components/version_info/channel.h"
#include "components/version_info/version_info.h"
#include "components/visited_url_ranking/public/features.h"
#include "components/viz/common/features.h"
#include "components/viz/common/switches.h"
#include "components/webapps/browser/features.h"
#include "components/webui/flags/feature_entry.h"
#include "components/webui/flags/feature_entry_macros.h"
#include "components/webui/flags/flags_state.h"
#include "components/webui/flags/flags_storage.h"
#include "components/webui/flags/flags_ui_metrics.h"
#include "components/webui/flags/flags_ui_switches.h"
#include "components/webui/flags/pref_service_flags_storage.h"
#include "content/common/features.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "device/base/features.h"
#include "device/bluetooth/bluez/bluez_features.h"
#include "device/bluetooth/chromeos_platform_features.h"
#include "device/bluetooth/floss/floss_features.h"
#include "device/fido/public/features.h"
#include "device/gamepad/public/cpp/gamepad_features.h"
#include "device/vr/buildflags/buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "gpu/config/gpu_finch_features.h"
#include "gpu/config/gpu_switches.h"
#include "media/audio/audio_features.h"
#include "media/base/media_switches.h"
#include "media/capture/capture_switches.h"
#include "media/media_buildflags.h"
#include "media/midi/midi_switches.h"
#include "media/webrtc/webrtc_features.h"
#include "mojo/core/embedder/features.h"
#include "net/base/features.h"
#include "net/base/switches.h"
#include "net/net_buildflags.h"
#include "net/nqe/effective_connection_type.h"
#include "net/nqe/network_quality_estimator_params.h"
#include "net/websockets/websocket_basic_handshake_stream.h"
#include "partition_alloc/buildflags.h"
#include "pdf/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "sandbox/policy/features.h"
#include "sandbox/policy/switches.h"
#include "services/device/public/cpp/device_features.h"
#include "services/media_session/public/cpp/features.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/network_switches.h"
#include "services/on_device_model/public/cpp/features.h"
#include "services/tracing/public/cpp/tracing_features.h"
#include "services/webnn/public/mojom/features.mojom-features.h"
#include "storage/browser/blob/features.h"
#include "storage/browser/quota/quota_features.h"
#include "third_party/blink/public/common/buildflags.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/features_generated.h"
#include "third_party/blink/public/common/switches.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/accessibility_switches.h"
#include "ui/base/ozone_buildflags.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/ui_base_switches.h"
#include "ui/compositor/compositor_switches.h"
#include "ui/display/display_features.h"
#include "ui/display/display_switches.h"
#include "ui/events/blink/blink_features.h"
#include "ui/events/event_switches.h"
#include "ui/gfx/switches.h"
#include "ui/gl/buildflags.h"
#include "ui/gl/gl_features.h"
#include "ui/gl/gl_switches.h"
#include "ui/native_theme/features/native_theme_features.h"
#include "ui/ui_features.h"
#include "url/url_features.h"

#include "chrome/browser/component_updater/iwa_key_distribution_component_installer.h"
#include "chrome/browser/media/router/discovery/access_code/access_code_cast_sink_service.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/web_applications/preinstalled_app_install_features.h"
#include "components/user_education/common/user_education_features.h"  // nogncheck

#include "components/variations/net/variations_command_line.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#endif  // BUILDFLAG(IS_MAC)


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "base/allocator/buildflags.h"
#include "ui/ozone/public/ozone_switches.h"
#endif

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
#include "chrome/browser/enterprise/data_protection/data_protection_features.h"
#include "chrome/browser/enterprise/profile_management/profile_management_features.h"
#include "chrome/browser/enterprise/webstore/features.h"
#include "components/infobars/core/features.h"
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "chrome/browser/contextual_cueing/contextual_cueing_features.h"  // nogncheck
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_ANDROID)


#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "components/enterprise/platform_auth/platform_auth_features.h"
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "components/enterprise/browser/reporting/reporting_features.h"
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
#include "components/unexportable_keys/features.h"  // nogncheck
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
#include "chrome/browser/extensions/cws_info_service.h"
#include "extensions/common/extension_features.h"
#include "extensions/common/switches.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS_CORE)

#if BUILDFLAG(ENABLE_PDF)
#include "pdf/pdf_features.h"
#endif

#if BUILDFLAG(ENABLE_PRINTING)
#include "printing/printing_features.h"
#endif

#if BUILDFLAG(ENABLE_VR)
#include "device/vr/public/cpp/features.h"
#include "device/vr/public/cpp/switches.h"
#endif

#if defined(TOOLKIT_VIEWS)
#include "ui/views/views_features.h"
#include "ui/views/views_switches.h"
#endif  // defined(TOOLKIT_VIEWS)

using flags_ui::FeatureEntry;
using flags_ui::kDeprecated;
using flags_ui::kOsAndroid;
using flags_ui::kOsCrOS;
using flags_ui::kOsCrOSOwnerOnly;
using flags_ui::kOsLinux;
using flags_ui::kOsMac;
using flags_ui::kOsWin;

namespace about_flags {

namespace {

const unsigned kOsAll = kOsMac | kOsWin | kOsLinux | kOsCrOS | kOsAndroid;
const unsigned kOsDesktop = kOsMac | kOsWin | kOsLinux | kOsCrOS;

#if defined(USE_AURA)
const unsigned kOsAura = kOsWin | kOsLinux | kOsCrOS;
#endif  // USE_AURA

#if defined(USE_AURA)
const FeatureEntry::Choice kPullToRefreshChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled, switches::kPullToRefresh, "0"},
    {flags_ui::kGenericExperimentChoiceEnabled, switches::kPullToRefresh, "1"},
    {flag_descriptions::kPullToRefreshEnabledTouchscreen,
     switches::kPullToRefresh, "2"}};
#endif  // USE_AURA

const FeatureEntry::FeatureParam kDefaultBrowserPromptSurfaces_Infobar[] = {
    {"prompt_surface", "infobar"}};
const FeatureEntry::FeatureParam kDefaultBrowserPromptSurfaces_BubbleDialog[] =
    {{"prompt_surface", "bubble_dialog"}};
const FeatureEntry::FeatureParam
    kDefaultBrowserPromptSurfaces_ModalDialogWithSettingsIllustration[] = {
        {"prompt_surface", "modal_dialog_with_settings_illustration"}};
const FeatureEntry::FeatureParam
    kDefaultBrowserPromptSurfaces_ModalDialogWithoutSettingsIllustration[] = {
        {"prompt_surface", "modal_dialog_without_settings_illustration"}};

const FeatureEntry::FeatureVariation kDefaultBrowserPromptSurfacesVariations[] =
    {{"with Infobar", kDefaultBrowserPromptSurfaces_Infobar, nullptr},
     {"with Bubble Dialog", kDefaultBrowserPromptSurfaces_BubbleDialog,
      nullptr},
     {"with Modal Dialog with Settings Illustration",
      kDefaultBrowserPromptSurfaces_ModalDialogWithSettingsIllustration,
      nullptr},
     {"with Modal Dialog without Settings Illustration",
      kDefaultBrowserPromptSurfaces_ModalDialogWithoutSettingsIllustration,
      nullptr}};

const FeatureEntry::FeatureParam kLocalNetworkAccessChecksBlock[] = {
    {"LocalNetworkAccessChecksWarn", "false"}};
const FeatureEntry::FeatureVariation kLocalNetworkAccessChecksVariations[] = {
    {"(Blocking)", kLocalNetworkAccessChecksBlock, nullptr}};

const FeatureEntry::FeatureParam
    kUnthrottleAsyncTouchMoves_UnthrottledWhenGsuUnconsumed[] = {
        {"policy", "unthrottled_when_gsu_unconsumed"}};
const FeatureEntry::FeatureParam
    kUnthrottleAsyncTouchMoves_UnthrottledAlways[] = {
        {"policy", "unthrottled_always"}};

const FeatureEntry::FeatureVariation kUnthrottleAsyncTouchMovesVariations[] = {
    {"unthrottled when GSU unconsumed",
     kUnthrottleAsyncTouchMoves_UnthrottledWhenGsuUnconsumed, nullptr},
    {"unthrottled always", kUnthrottleAsyncTouchMoves_UnthrottledAlways,
     nullptr}};

const FeatureEntry::Choice kEnableBenchmarkingChoices[] = {
    {flag_descriptions::kEnableBenchmarkingChoiceDisabled, "", ""},
    {flag_descriptions::kEnableBenchmarkingChoiceDefaultFeatureStates,
     switches::kEnableBenchmarking, ""},
    {flag_descriptions::kEnableBenchmarkingChoiceMatchFieldTrialTestingConfig,
     switches::kEnableBenchmarking,
     variations::switches::kEnableFieldTrialTestingConfig},
};

const FeatureEntry::Choice kOverlayStrategiesChoices[] = {
    {flag_descriptions::kOverlayStrategiesDefault, "", ""},
    {flag_descriptions::kOverlayStrategiesNone,
     switches::kEnableHardwareOverlays, ""},
    {flag_descriptions::kOverlayStrategiesUnoccludedFullscreen,
     switches::kEnableHardwareOverlays, "single-fullscreen"},
    {flag_descriptions::kOverlayStrategiesUnoccluded,
     switches::kEnableHardwareOverlays, "single-fullscreen,single-on-top"},
    {flag_descriptions::kOverlayStrategiesOccludedAndUnoccluded,
     switches::kEnableHardwareOverlays,
     "single-fullscreen,single-on-top,underlay"},
};

const FeatureEntry::Choice kTouchTextSelectionStrategyChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kTouchSelectionStrategyCharacter,
     blink::switches::kTouchTextSelectionStrategy,
     blink::switches::kTouchTextSelectionStrategy_Character},
    {flag_descriptions::kTouchSelectionStrategyDirection,
     blink::switches::kTouchTextSelectionStrategy,
     blink::switches::kTouchTextSelectionStrategy_Direction}};


#if BUILDFLAG(ENABLE_EXTENSIONS)
const FeatureEntry::Choice kExtensionsToolbarZeroStateChoices[] = {
    {flag_descriptions::kExtensionsToolbarZeroStateChoicesDisabled, "", ""},
    {flag_descriptions::kExtensionsToolbarZeroStateVistWebStore,
     switches::kExtensionsToolbarZeroStateVariation,
     switches::kExtensionsToolbarZeroStateSingleWebStoreLink},
    {flag_descriptions::kExtensionsToolbarZeroStateExploreExtensionsByCategory,
     switches::kExtensionsToolbarZeroStateVariation,
     switches::kExtensionsToolbarZeroStateExploreExtensionsByCategory},
};
#endif  // ENABLE_EXTENSIONS


#if BUILDFLAG(ENABLE_VR)
const FeatureEntry::Choice kWebXrForceRuntimeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kWebXrRuntimeChoiceNone, switches::kWebXrForceRuntime,
     switches::kWebXrRuntimeNone},
#if BUILDFLAG(ENABLE_ARCORE)
    {flag_descriptions::kWebXrRuntimeChoiceArCore, switches::kWebXrForceRuntime,
     switches::kWebXrRuntimeArCore},
#endif
#if BUILDFLAG(ENABLE_CARDBOARD)
    {flag_descriptions::kWebXrRuntimeChoiceCardboard,
     switches::kWebXrForceRuntime, switches::kWebXrRuntimeCardboard},
#endif
#if BUILDFLAG(ENABLE_OPENXR)
    {flag_descriptions::kWebXrRuntimeChoiceOpenXR, switches::kWebXrForceRuntime,
     switches::kWebXrRuntimeOpenXr},
#endif  // ENABLE_OPENXR
    {flag_descriptions::kWebXrRuntimeChoiceOrientationSensors,
     switches::kWebXrForceRuntime, switches::kWebXrRuntimeOrientationSensors},
};

const FeatureEntry::Choice KWebXrHandAnonymizationChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kWebXrHandAnonymizationChoiceRuntime,
     device::switches::kWebXrHandAnonymizationStrategy,
     device::switches::kWebXrHandAnonymizationStrategyRuntime},
    {flag_descriptions::kWebXrHandAnonymizationChoiceFallback,
     device::switches::kWebXrHandAnonymizationStrategy,
     device::switches::kWebXrHandAnonymizationStrategyFallback},
    {flag_descriptions::kWebXrHandAnonymizationChoiceNone,
     device::switches::kWebXrHandAnonymizationStrategy,
     device::switches::kWebXrHandAnonymizationStrategyNone},
};
#endif  // ENABLE_VR


const FeatureEntry::FeatureParam
    kWebIdentityDigitalIdentityCredentialNoDialogParam[] = {
        {"dialog", "no_dialog"}};
const FeatureEntry::FeatureParam
    kWebIdentityDigitalIdentityCredentialDefaultParam[] = {
        {"dialog", "default"}};
const FeatureEntry::FeatureParam
    kWebIdentityDigitalIdentityCredentialLowRiskDialogParam[] = {
        {"dialog", "low_risk"}};
const FeatureEntry::FeatureParam
    kWebIdentityDigitalIdentityCredentialHighRiskDialogParam[] = {
        {"dialog", "high_risk"}};
const FeatureEntry::FeatureVariation
    kWebIdentityDigitalIdentityCredentialVariations[] = {
        {"with dialog depending on what credentials are requested",
         kWebIdentityDigitalIdentityCredentialDefaultParam, nullptr},
        {"without dialog", kWebIdentityDigitalIdentityCredentialNoDialogParam,
         nullptr},
        {"with confirmation dialog with mild warning before sending identity "
         "request to Android OS",
         kWebIdentityDigitalIdentityCredentialLowRiskDialogParam, nullptr},
        {"with confirmation dialog with severe warning before sending "
         "identity request to Android OS",
         kWebIdentityDigitalIdentityCredentialHighRiskDialogParam, nullptr}};

const FeatureEntry::FeatureParam kMBIModeLegacy[] = {{"mode", "legacy"}};
const FeatureEntry::FeatureParam kMBIModeEnabledPerRenderProcessHost[] = {
    {"mode", "per_render_process_host"}};
const FeatureEntry::FeatureParam kMBIModeEnabledPerSiteInstance[] = {
    {"mode", "per_site_instance"}};

const FeatureEntry::FeatureVariation kMBIModeVariations[] = {
    {"legacy mode", kMBIModeLegacy, nullptr},
    {"per render process host", kMBIModeEnabledPerRenderProcessHost, nullptr},
    {"per site instance", kMBIModeEnabledPerSiteInstance, nullptr}};

const FeatureEntry::FeatureParam kSearchPrefetchWithoutHoldback[] = {
    {"prefetch_holdback", "false"}};
const FeatureEntry::FeatureParam kSearchPrefetchWithHoldback[] = {
    {"prefetch_holdback", "true"}};

const FeatureEntry::FeatureVariation
    kSearchPrefetchServicePrefetchingVariations[] = {
        {"without holdback", kSearchPrefetchWithoutHoldback, nullptr},
        {"with holdback", kSearchPrefetchWithHoldback, nullptr}};

const FeatureEntry::FeatureParam
    kWebUIOmniboxAimPopupAddContextNoTextNoChips[] = {
        {"Omnibox_AddContextButtonVariant", "below_results"},
        {"Omnibox_ShowContextMenuDescription", "false"},
        {"Omnibox_ShowRecentTabChip", "false"},
        {"Omnibox_ShowLensSearchChip", "false"},
};
const FeatureEntry::FeatureParam
    kWebUIOmniboxAimPopupHideClassicContextButton[] = {
        {"Omnibox_AddContextButtonVariant", "below_results"},
        {"Omnibox_ShowContextMenuDescription", "false"},
        {"Omnibox_HideClassicContextButton", "true"},
        {"Omnibox_ShowRecentTabChip", "false"},
        {"Omnibox_ShowLensSearchChip", "false"},
};
const FeatureEntry::FeatureParam
    kWebUIOmniboxAimPopupAddContextShowTextNoChips[] = {
        {"Omnibox_AddContextButtonVariant", "below_results"},
        {"Omnibox_ShowContextMenuDescription", "true"},
        {"Omnibox_ShowRecentTabChip", "false"},
        {"Omnibox_ShowLensSearchChip", "false"},
};
const FeatureEntry::FeatureParam
    kWebUIOmniboxAimPopupAddContextShowTextShowChips[] = {
        {"Omnibox_AddContextButtonVariant", "below_results"},
        {"Omnibox_ShowContextMenuDescription", "true"},
        {"Omnibox_ShowRecentTabChip", "true"},
        {"Omnibox_ShowLensSearchChip", "true"},
};

const FeatureEntry::FeatureVariation kWebUIOmniboxAimPopupVariations[] = {
    {"- \"Add Context\" button without text, no contextual chips",
     kWebUIOmniboxAimPopupAddContextNoTextNoChips, nullptr},
    {"- No Classic \"Add Context\" button, no contextual chips",
     kWebUIOmniboxAimPopupHideClassicContextButton, nullptr},
    {"- \"Add Context\" button with text, no contextual chips",
     kWebUIOmniboxAimPopupAddContextShowTextNoChips, nullptr},
    {"- \"Add Context\" button with text, show contextual chips",
     kWebUIOmniboxAimPopupAddContextShowTextShowChips, nullptr}};

const FeatureEntry::FeatureParam kWebUIOmniboxPopupDebugSxS[] = {
    {"SxS", "true"}};
const FeatureEntry::FeatureVariation kWebUIOmniboxPopupDebugVariations[] = {
    {"Side by Side", kWebUIOmniboxPopupDebugSxS, nullptr}};


const FeatureEntry::Choice kEnableGpuRasterizationChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled,
     switches::kEnableGpuRasterization, ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kDisableGpuRasterization, ""},
};

const FeatureEntry::Choice kTopChromeTouchUiChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceAutomatic, switches::kTopChromeTouchUi,
     switches::kTopChromeTouchUiAuto},
    {flags_ui::kGenericExperimentChoiceDisabled, switches::kTopChromeTouchUi,
     switches::kTopChromeTouchUiDisabled},
    {flags_ui::kGenericExperimentChoiceEnabled, switches::kTopChromeTouchUi,
     switches::kTopChromeTouchUiEnabled}};


const FeatureEntry::Choice kForceUIDirectionChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceDirectionLtr, switches::kForceUIDirection,
     switches::kForceDirectionLTR},
    {flag_descriptions::kForceDirectionRtl, switches::kForceUIDirection,
     switches::kForceDirectionRTL},
};

const FeatureEntry::Choice kForceTextDirectionChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceDirectionLtr, switches::kForceTextDirection,
     switches::kForceDirectionLTR},
    {flag_descriptions::kForceDirectionRtl, switches::kForceTextDirection,
     switches::kForceDirectionRTL},
};


const FeatureEntry::Choice kSiteIsolationOptOutChoices[] = {
    {flag_descriptions::kSiteIsolationOptOutChoiceDefault, "", ""},
    {flag_descriptions::kSiteIsolationOptOutChoiceOptOut,
     switches::kDisableSiteIsolation, ""},
};

const FeatureEntry::Choice kForceColorProfileChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceColorProfileSRGB,
     switches::kForceDisplayColorProfile, "srgb"},
    {flag_descriptions::kForceColorProfileP3,
     switches::kForceDisplayColorProfile, "display-p3-d65"},
    {flag_descriptions::kForceColorProfileRec2020,
     switches::kForceDisplayColorProfile, "rec2020"},
    {flag_descriptions::kForceColorProfileColorSpin,
     switches::kForceDisplayColorProfile, "color-spin-gamma24"},
    {flag_descriptions::kForceColorProfileSCRGBLinear,
     switches::kForceDisplayColorProfile, "scrgb-linear"},
    {flag_descriptions::kForceColorProfileHDR10,
     switches::kForceDisplayColorProfile, "hdr10"},
};

const FeatureEntry::Choice kMemlogModeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDisabled, "", ""},
    {flag_descriptions::kMemlogModeMinimal, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeMinimal},
    {flag_descriptions::kMemlogModeAll, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeAll},
    {flag_descriptions::kMemlogModeBrowser, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeBrowser},
    {flag_descriptions::kMemlogModeGpu, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeGpu},
    {flag_descriptions::kMemlogModeAllRenderers, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeAllRenderers},
    {flag_descriptions::kMemlogModeRendererSampling,
     heap_profiling::kMemlogMode, heap_profiling::kMemlogModeRendererSampling},
    {flag_descriptions::kMemlogModeUtilitySampling, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeUtilitySampling},
    {flag_descriptions::kMemlogModeAllUtilities, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeAllUtilities},
};

const FeatureEntry::Choice kMemlogStackModeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kMemlogStackModeNative,
     heap_profiling::kMemlogStackMode, heap_profiling::kMemlogStackModeNative},
    {flag_descriptions::kMemlogStackModeNativeWithThreadNames,
     heap_profiling::kMemlogStackMode,
     heap_profiling::kMemlogStackModeNativeWithThreadNames},
};

const FeatureEntry::Choice kMemlogSamplingRateChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kMemlogSamplingRate10KB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate10KB},
    {flag_descriptions::kMemlogSamplingRate50KB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate50KB},
    {flag_descriptions::kMemlogSamplingRate100KB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate100KB},
    {flag_descriptions::kMemlogSamplingRate500KB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate500KB},
    {flag_descriptions::kMemlogSamplingRate1MB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate1MB},
    {flag_descriptions::kMemlogSamplingRate5MB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate5MB},
};

const FeatureEntry::FeatureParam
    kOptimizationGuideOnDeviceModelBypassPerfParams[] = {
        {"compatible_on_device_performance_classes", "*"},
};
const FeatureEntry::FeatureParam
    kOptimizationGuideOnDeviceModelBypassPerfSmallModelParams[] = {
        {"compatible_on_device_performance_classes", "*"},
        {"compatible_low_tier_on_device_performance_classes", "*"},
};
const FeatureEntry::FeatureVariation
    kOptimizationGuideOnDeviceModelVariations[] = {
        {"BypassPerfRequirement",
         kOptimizationGuideOnDeviceModelBypassPerfParams, nullptr},
        {"Force Small Model",
         kOptimizationGuideOnDeviceModelBypassPerfSmallModelParams, nullptr},
};

const FeatureEntry::FeatureParam kTextSafetyClassifierNoRetractParams[] = {
    {"on_device_retract_unsafe_content", "false"},
};
const FeatureEntry::FeatureVariation kTextSafetyClassifierVariations[] = {
    {"Executes safety classifier but no retraction of output",
     kTextSafetyClassifierNoRetractParams, nullptr},
};


const FeatureEntry::FeatureParam kPageActionsMigrationParams[] = {
    {"ai_mode", "true"},
    {"autofill_address", "true"},
    {"bookmark_star", "true"},
    {"cookie_controls", "true"},
    {"click_to_call", "true"},
    {"collaboration_messaging", "true"},
    {"file_system_access", "true"},
    {"filled_card_information", "true"},
    {"find", "true"},
    {"intent_picker", "true"},
    {"lens_overlay_homework", "true"},
    {"manage_passwords", "true"},
    {"mandatory_reauth", "true"},
    {"reading_mode", "true"},
    {"save_payments", "true"},
    {"sharing_hub", "true"},
    {"virtual_card", "true"},
    {"zoom", "true"},
};
const FeatureEntry::FeatureVariation kPageActionsMigrationVariations[] = {
    {"with all migrated page actions enabled", kPageActionsMigrationParams,
     nullptr},
};

const FeatureEntry::FeatureParam kPageContentAnnotationsContentParams[] = {
    {"annotate_title_instead_of_page_content", "false"},
    {"extract_related_searches", "true"},
    {"max_size_for_text_dump_in_bytes", "5120"},
    {"write_to_history_service", "true"},
};
const FeatureEntry::FeatureParam kPageContentAnnotationsTitleParams[] = {
    {"annotate_title_instead_of_page_content", "true"},
    {"extract_related_searches", "true"},
    {"write_to_history_service", "true"},
};
const FeatureEntry::FeatureParam
    kPageContentAnnotationsTimeoutDurationParams[] = {
        {"PageContentAnnotationBatchSizeTimeoutDuration", "0"},
};
const FeatureEntry::FeatureVariation kPageContentAnnotationsVariations[] = {
    {"All Annotations and Persistence on Content",
     kPageContentAnnotationsContentParams, nullptr},
    {"All Annotations and Persistence on Title",
     kPageContentAnnotationsTitleParams, nullptr},
    {"Annotation timeout duration 0 seconds",
     kPageContentAnnotationsTimeoutDurationParams, nullptr}};

constexpr FeatureEntry::FeatureParam
    kHappinessTrackingSurveysForDesktopDemoWithoutAutoPrompt[] = {
        {"auto_prompt", "false"}};
constexpr FeatureEntry::FeatureVariation
    kHappinessTrackingSurveysForDesktopDemoVariations[] = {
        {"without Auto Prompt",
         kHappinessTrackingSurveysForDesktopDemoWithoutAutoPrompt, nullptr}};

const FeatureEntry::FeatureParam kJourneysShowAllVisitsParams[] = {
    {"JourneysLocaleOrLanguageAllowlist", "*"},
    // To show all visits, set the number of visits above the fold to a very
    // high number.
    {"JourneysNumVisitsToAlwaysShowAboveTheFold", "200"},
};
const FeatureEntry::FeatureParam kJourneysAllLocalesParams[] = {
    {"JourneysLocaleOrLanguageAllowlist", "*"},
};
const FeatureEntry::FeatureVariation kJourneysVariations[] = {
    {"No 'Show More' - Show all visits", kJourneysShowAllVisitsParams, nullptr},
    {"All Supported Locales", kJourneysAllLocalesParams, nullptr},
};

const FeatureEntry::FeatureParam
    kLensAimSuggestionsTypeContextualWith3Suggestions[] = {
        {"lens-aim-suggestions-type", "Contextual"},
        {"number-of-aim-suggestions", "3"}};

const FeatureEntry::FeatureParam
    kLensAimSuggestionsTypeContextualWith5Suggestions[] = {
        {"lens-aim-suggestions-type", "Contextual"},
        {"number-of-aim-suggestions", "5"}};

const FeatureEntry::FeatureParam
    kLensAimSuggestionsTypeContextualWith8Suggestions[] = {
        {"lens-aim-suggestions-type", "Contextual"},
        {"number-of-aim-suggestions", "8"}};

const FeatureEntry::FeatureParam
    kLensAimSuggestionsTypeMultimodalWith3Suggestions[] = {
        {"lens-aim-suggestions-type", "Multimodal"},
        {"number-of-aim-suggestions", "3"}};

const FeatureEntry::FeatureParam
    kLensAimSuggestionsTypeMultimodalWith5Suggestions[] = {
        {"lens-aim-suggestions-type", "Multimodal"},
        {"number-of-aim-suggestions", "5"}};

const FeatureEntry::FeatureParam
    kLensAimSuggestionsTypeMultimodalWith8Suggestions[] = {
        {"lens-aim-suggestions-type", "Multimodal"},
        {"number-of-aim-suggestions", "8"}};

const FeatureEntry::FeatureVariation kLensAimSuggestionsVariations[] = {
    {"with Contextual - 3 suggestions",
     kLensAimSuggestionsTypeContextualWith3Suggestions, nullptr},
    {"with Contextual - 5 suggestions",
     kLensAimSuggestionsTypeContextualWith5Suggestions, nullptr},
    {"with Contextual - 8 suggestions",
     kLensAimSuggestionsTypeContextualWith8Suggestions, nullptr},
    {"with Multimodal - 3 suggestions",
     kLensAimSuggestionsTypeMultimodalWith3Suggestions, nullptr},
    {"with Multimodal - 5 suggestions",
     kLensAimSuggestionsTypeMultimodalWith5Suggestions, nullptr},
    {"with Multimodal - 8 suggestions",
     kLensAimSuggestionsTypeMultimodalWith8Suggestions, nullptr},
};

const FeatureEntry::FeatureVariation kRemotePageMetadataVariations[] = {
    {"High Performance Canonicalization", {}, "3362133"},
};

const FeatureEntry::FeatureParam
    kAimServerEligibilityRequestModePostWithProto[] = {
        {"mode", "post_with_proto"}};
const FeatureEntry::FeatureParam
    kAimServerEligibilityRequestModeGetWithLocale[] = {
        {"mode", "get_with_locale"}};

const FeatureEntry::FeatureVariation
    kAimServerEligibilityIncludeClientLocaleVariations[] = {
        {"GET with Locale", kAimServerEligibilityRequestModeGetWithLocale,
         nullptr},
        {"POST with Proto", kAimServerEligibilityRequestModePostWithProto,
         nullptr},
};

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)

// A limited number of combinations of the rich autocompletion params.
const FeatureEntry::FeatureParam kOmniboxRichAutocompletionAggressive1[] = {
    {"RichAutocompletionAutocompleteTitlesMinChar", "1"},
    {"RichAutocompletionAutocompleteShortcutTextMinChar", "1"}};
const FeatureEntry::FeatureParam kOmniboxRichAutocompletionAggressive2[] = {
    {"RichAutocompletionAutocompleteTitlesMinChar", "2"},
    {"RichAutocompletionAutocompleteShortcutTextMinChar", "2"}};
const FeatureEntry::FeatureParam kOmniboxRichAutocompletionAggressive3[] = {
    {"RichAutocompletionAutocompleteTitlesMinChar", "3"},
    {"RichAutocompletionAutocompleteShortcutTextMinChar", "3"}};
const FeatureEntry::FeatureParam kOmniboxRichAutocompletionAggressive4[] = {
    {"RichAutocompletionAutocompleteTitlesMinChar", "4"},
    {"RichAutocompletionAutocompleteShortcutTextMinChar", "4"}};

const FeatureEntry::FeatureVariation
    kOmniboxRichAutocompletionPromisingVariations[] = {
        {"Min input length 1 characters", kOmniboxRichAutocompletionAggressive1,
         nullptr},
        {"Min input length 2 characters", kOmniboxRichAutocompletionAggressive2,
         nullptr},
        {"Min input length 2 characters", kOmniboxRichAutocompletionAggressive2,
         nullptr},
        {"Min input length 3 characters", kOmniboxRichAutocompletionAggressive3,
         nullptr},
        {"Min input length 4 characters", kOmniboxRichAutocompletionAggressive4,
         nullptr},
};

const FeatureEntry::FeatureParam kOmniboxStarterPackExpansionPreProdUrl[] = {
    {"StarterPackGeminiUrlOverride", "https://gemini.google.com/corp/prompt"}};
const FeatureEntry::FeatureParam kOmniboxStarterPackExpansionStagingUrl[] = {
    {"StarterPackGeminiUrlOverride",
     "https://gemini.google.com/staging/prompt"}};
const FeatureEntry::FeatureVariation kOmniboxStarterPackExpansionVariations[] =
    {{"pre-prod url", kOmniboxStarterPackExpansionPreProdUrl, nullptr},
     {"staging url", kOmniboxStarterPackExpansionStagingUrl, nullptr}};

const FeatureEntry::FeatureParam kOmniboxUrlSuggestionsOnFocusTwoDayWindow[] = {
    {"OnFocusMostVisitedRecencyWindow", "1"},
};
const FeatureEntry::FeatureParam kOmniboxUrlSuggestionsOnFocusThreeDayWindow[] =
    {
        {"OnFocusMostVisitedRecencyWindow", "2"},
};
const FeatureEntry::FeatureParam kOmniboxUrlSuggestionsOnFocusOneWeekWindow[] =
    {
        {"OnFocusMostVisitedRecencyWindow", "6"},
};
const FeatureEntry::FeatureParam kOmniboxUrlSuggestionsOnFocusTwoWeekWindow[] =
    {
        {"OnFocusMostVisitedRecencyWindow", "13"},
};
const FeatureEntry::FeatureVariation kOmniboxUrlSuggestionsOnFocusVariations[] =
    {
        {"- Two day window", kOmniboxUrlSuggestionsOnFocusTwoDayWindow,
         nullptr},
        {"- Three day window", kOmniboxUrlSuggestionsOnFocusThreeDayWindow,
         nullptr},
        {"- One week window", kOmniboxUrlSuggestionsOnFocusOneWeekWindow,
         nullptr},
        {"- Two week window", kOmniboxUrlSuggestionsOnFocusTwoWeekWindow,
         nullptr},
};

const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax8[] = {
    {"OmniboxZpsMaxSuggestions", "8"},
    {"OmniboxZpsMaxSearchSuggestions", "4"},
    {"OmniboxZpsMaxUrlSuggestions", "4"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax4[] = {
    {"OmniboxZpsMaxSuggestions", "4"},
    {"OmniboxZpsMaxSearchSuggestions", "2"},
    {"OmniboxZpsMaxUrlSuggestions", "2"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax2TwoZero[] = {
    {"OmniboxZpsMaxSuggestions", "2"},
    {"OmniboxZpsMaxSearchSuggestions", "2"},
    {"OmniboxZpsMaxUrlSuggestions", "0"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax3ThreeZero[] = {
    {"OmniboxZpsMaxSuggestions", "3"},
    {"OmniboxZpsMaxSearchSuggestions", "3"},
    {"OmniboxZpsMaxUrlSuggestions", "0"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax4FourZero[] = {
    {"OmniboxZpsMaxSuggestions", "4"},
    {"OmniboxZpsMaxSearchSuggestions", "4"},
    {"OmniboxZpsMaxUrlSuggestions", "0"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax5FourOne[] = {
    {"OmniboxZpsMaxSuggestions", "5"},
    {"OmniboxZpsMaxSearchSuggestions", "4"},
    {"OmniboxZpsMaxUrlSuggestions", "1"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax5ThreeTwo[] = {
    {"OmniboxZpsMaxSuggestions", "5"},
    {"OmniboxZpsMaxSearchSuggestions", "3"},
    {"OmniboxZpsMaxUrlSuggestions", "2"},
};
const FeatureEntry::FeatureVariation kOmniboxZpsSuggestionLimitVariations[] = {
    {"- Max 8 Suggestions (4 search, 4 url)", kOmniboxZpsSuggestionLimitMax8,
     nullptr},
    {"- Max 4 Suggestions (2 search, 2 url)", kOmniboxZpsSuggestionLimitMax4,
     nullptr},
    {"- Max 2 Suggestions (2 search, 0 url)",
     kOmniboxZpsSuggestionLimitMax2TwoZero, nullptr},
    {"- Max 3 Suggestions (3 search, 0 url)",
     kOmniboxZpsSuggestionLimitMax3ThreeZero, nullptr},
    {"- Max 4 Suggestions (4 search, 0 url)",
     kOmniboxZpsSuggestionLimitMax4FourZero, nullptr},
    {"- Max 5 Suggestions (4 search, 1 url)",
     kOmniboxZpsSuggestionLimitMax5FourOne, nullptr},
    {"- Max 5 Suggestions (3 search, 2 url)",
     kOmniboxZpsSuggestionLimitMax5ThreeTwo, nullptr},
};

const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit0[] = {
        {"Limit", "0"},
};
const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit1[] = {
        {"Limit", "1"},
};
const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit2[] = {
        {"Limit", "2"},
};
const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit3[] = {
        {"Limit", "3"},
};
const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit4[] = {
        {"Limit", "4"},
};
const FeatureEntry::FeatureVariation
    kOmniboxContextualSearchOnFocusSuggestionsVariations[] = {
        {"- Limit 0", kOmniboxContextualSearchOnFocusSuggestionsLimit0,
         nullptr},
        {"- Limit 1", kOmniboxContextualSearchOnFocusSuggestionsLimit1,
         nullptr},
        {"- Limit 2", kOmniboxContextualSearchOnFocusSuggestionsLimit2,
         nullptr},
        {"- Limit 3", kOmniboxContextualSearchOnFocusSuggestionsLimit3,
         nullptr},
        {"- Limit 4", kOmniboxContextualSearchOnFocusSuggestionsLimit4,
         nullptr},
};

const FeatureEntry::FeatureParam kOmniboxAimEntryPointHintLimitsDaily1[] = {
    {"HideAimHintText", "false"},
    {"HideAimHintTextOnNtpOpen", "false"},
    {"AimHintImpressionLimitDaily", "1"},
    {"AimHintImpressionLimitTotal", "5"},
    {"EnableHintImpressionLimits", "true"}};
const FeatureEntry::FeatureParam kOmniboxAimEntryPointHintLimitsDaily3[] = {
    {"HideAimHintText", "false"},
    {"HideAimHintTextOnNtpOpen", "false"},
    {"AimHintImpressionLimitDaily", "3"},
    {"AimHintImpressionLimitTotal", "10"},
    {"EnableHintImpressionLimits", "true"}};
const FeatureEntry::FeatureParam kOmniboxAimEntryPointHintLimitsUnlimited[] = {
    {"HideAimHintText", "false"},
    {"HideAimHintTextOnNtpOpen", "false"},
    {"EnableHintImpressionLimits", "false"}};

const FeatureEntry::FeatureVariation kOmniboxAiModeEntryPointVariations[] = {
    {"Hint Limits Daily 1 Total 5", kOmniboxAimEntryPointHintLimitsDaily1,
     nullptr},
    {"Hint Limits Daily 3 Total 10", kOmniboxAimEntryPointHintLimitsDaily3,
     nullptr},
    {"Hint Limits Unlimited", kOmniboxAimEntryPointHintLimitsUnlimited,
     nullptr},
};

const FeatureEntry::FeatureParam
    kContextualSuggestionsAblateOthersWhenPresentAblateAll[] = {
        {"AblateSearchOnly", "false"},
};

const FeatureEntry::FeatureParam
    kContextualSuggestionsAblateOthersWhenPresentAblateSearchOnly[] = {
        {"AblateSearchOnly", "true"},
};

const FeatureEntry::FeatureParam
    kContextualSuggestionsAblateOthersWhenPresentAblateUrlOnly[] = {
        {"AblateUrlOnly", "true"},
};

const FeatureEntry::FeatureVariation
    kContextualSuggestionsAblateOthersWhenPresentVariations[] = {
        {"- Ablate all", kContextualSuggestionsAblateOthersWhenPresentAblateAll,
         nullptr},
        {"- Ablate search only",
         kContextualSuggestionsAblateOthersWhenPresentAblateSearchOnly,
         nullptr},
        {"- Ablate URL only",
         kContextualSuggestionsAblateOthersWhenPresentAblateUrlOnly, nullptr},
};

const FeatureEntry::Choice kContextualSuggestionsUiImprovementsChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled, switches::kEnableFeatures,
     "LoadingSuggestionsAnimation,SuggestionsFulfilledByLensSupported,"
     "OpenLensActionUITweaks"},
};

const FeatureEntry::FeatureParam kOmniboxToolbeltLensActionsZeroInputs[] = {
    {"KeepToolbeltAfterInput", "false"},
    {"ShowLensActionOnNonNtp", "true"},
    {"ShowLensActionOnNtp", "false"},
    {"ShowAiModeActionOnNonNtp", "false"},
    {"ShowAiModeActionOnNtp", "false"},
    {"ShowHistoryActionOnNonNtp", "true"},
    {"ShowHistoryActionOnNtp", "true"},
    {"ShowBookmarksActionOnNonNtp", "true"},
    {"ShowBookmarksActionOnNtp", "true"},
    {"ShowTabsActionOnNonNtp", "true"},
    {"ShowTabsActionOnNtp", "true"},
};
const FeatureEntry::FeatureParam kOmniboxToolbeltLensActionsZeroTypedInputs[] =
    {
        {"KeepToolbeltAfterInput", "true"},
        {"ShowLensActionOnNonNtp", "true"},
        {"ShowLensActionOnNtp", "false"},
        {"ShowAiModeActionOnNonNtp", "false"},
        {"ShowAiModeActionOnNtp", "false"},
        {"ShowHistoryActionOnNonNtp", "true"},
        {"ShowHistoryActionOnNtp", "true"},
        {"ShowBookmarksActionOnNonNtp", "true"},
        {"ShowBookmarksActionOnNtp", "true"},
        {"ShowTabsActionOnNonNtp", "true"},
        {"ShowTabsActionOnNtp", "true"},
};
const FeatureEntry::FeatureParam kOmniboxToolbeltAiActionsZeroInputs[] = {
    {"KeepToolbeltAfterInput", "false"},
    {"ShowLensActionOnNonNtp", "false"},
    {"ShowLensActionOnNtp", "false"},
    {"ShowAiModeActionOnNonNtp", "true"},
    {"ShowAiModeActionOnNtp", "true"},
    {"ShowHistoryActionOnNonNtp", "true"},
    {"ShowHistoryActionOnNtp", "true"},
    {"ShowBookmarksActionOnNonNtp", "true"},
    {"ShowBookmarksActionOnNtp", "true"},
    {"ShowTabsActionOnNonNtp", "true"},
    {"ShowTabsActionOnNtp", "true"},
};
const FeatureEntry::FeatureParam kOmniboxToolbeltAiActionsZeroTypedInputs[] = {
    {"KeepToolbeltAfterInput", "true"},
    {"ShowLensActionOnNonNtp", "false"},
    {"ShowLensActionOnNtp", "false"},
    {"ShowAiModeActionOnNonNtp", "true"},
    {"ShowAiModeActionOnNtp", "true"},
    {"ShowHistoryActionOnNonNtp", "true"},
    {"ShowHistoryActionOnNtp", "true"},
    {"ShowBookmarksActionOnNonNtp", "true"},
    {"ShowBookmarksActionOnNtp", "true"},
    {"ShowTabsActionOnNonNtp", "true"},
    {"ShowTabsActionOnNtp", "true"},
};
const FeatureEntry::FeatureParam kOmniboxToolbeltLensAiActionsZeroInputs[] = {
    {"KeepToolbeltAfterInput", "false"},
    {"ShowLensActionOnNonNtp", "true"},
    {"ShowLensActionOnNtp", "false"},
    {"ShowAiModeActionOnNonNtp", "true"},
    {"ShowAiModeActionOnNtp", "true"},
    {"ShowHistoryActionOnNonNtp", "true"},
    {"ShowHistoryActionOnNtp", "true"},
    {"ShowBookmarksActionOnNonNtp", "true"},
    {"ShowBookmarksActionOnNtp", "true"},
    {"ShowTabsActionOnNonNtp", "false"},
    {"ShowTabsActionOnNtp", "false"},
};
const FeatureEntry::FeatureParam
    kOmniboxToolbeltLensAiActionsZeroTypedInputs[] = {
        {"KeepToolbeltAfterInput", "true"},
        {"ShowLensActionOnNonNtp", "true"},
        {"ShowLensActionOnNtp", "false"},
        {"ShowAiModeActionOnNonNtp", "true"},
        {"ShowAiModeActionOnNtp", "true"},
        {"ShowHistoryActionOnNonNtp", "true"},
        {"ShowHistoryActionOnNtp", "true"},
        {"ShowBookmarksActionOnNonNtp", "true"},
        {"ShowBookmarksActionOnNtp", "true"},
        {"ShowTabsActionOnNonNtp", "false"},
        {"ShowTabsActionOnNtp", "false"},
};
const FeatureEntry::FeatureParam kOmniboxToolbeltAllActionsZeroInputs[] = {
    {"KeepToolbeltAfterInput", "false"},
    {"ShowLensActionOnNonNtp", "true"},
    {"ShowLensActionOnNtp", "true"},
    {"ShowAiModeActionOnNonNtp", "true"},
    {"ShowAiModeActionOnNtp", "true"},
    {"ShowHistoryActionOnNonNtp", "true"},
    {"ShowHistoryActionOnNtp", "true"},
    {"ShowBookmarksActionOnNonNtp", "true"},
    {"ShowBookmarksActionOnNtp", "true"},
    {"ShowTabsActionOnNonNtp", "true"},
    {"ShowTabsActionOnNtp", "true"},
};
const FeatureEntry::FeatureParam kOmniboxToolbeltAllActionsZeroTypedInputs[] = {
    {"KeepToolbeltAfterInput", "true"},
    {"ShowLensActionOnNonNtp", "true"},
    {"ShowLensActionOnNtp", "true"},
    {"ShowAiModeActionOnNonNtp", "true"},
    {"ShowAiModeActionOnNtp", "true"},
    {"ShowHistoryActionOnNonNtp", "true"},
    {"ShowHistoryActionOnNtp", "true"},
    {"ShowBookmarksActionOnNonNtp", "true"},
    {"ShowBookmarksActionOnNtp", "true"},
    {"ShowTabsActionOnNonNtp", "true"},
    {"ShowTabsActionOnNtp", "true"},
};
const FeatureEntry::FeatureVariation kOmniboxToolbeltVariations[] = {
    {"1 - Lens Action - Zero Inputs (Default)",
     kOmniboxToolbeltLensActionsZeroInputs, nullptr},
    {"2 - Lens Action - Zero + Typed Inputs",
     kOmniboxToolbeltLensActionsZeroTypedInputs, nullptr},
    {"3 - AI Action - Zero Inputs", kOmniboxToolbeltAiActionsZeroInputs,
     nullptr},
    {"4 - AI Action - Zero + Typed Inputs",
     kOmniboxToolbeltAiActionsZeroTypedInputs, nullptr},
    {"5 - Lens + AI Actions - Zero Inputs",
     kOmniboxToolbeltLensAiActionsZeroInputs, nullptr},
    {"6 - Lens + AI Actions - Zero + Typed Inputs",
     kOmniboxToolbeltLensAiActionsZeroTypedInputs, nullptr},
    {"7 - All Actions - Zero Inputs", kOmniboxToolbeltAllActionsZeroInputs,
     nullptr},
    {"8 - All Actions - Zero + Typed Inputs",
     kOmniboxToolbeltAllActionsZeroTypedInputs, nullptr},
};

const FeatureEntry::FeatureParam kComposeboxNextSingleContext[] = {
    {"MaxNumFiles", "1"},
};
const FeatureEntry::FeatureParam kComposeboxNextSingleContextForRealboxNext[] =
    {
        {"NtpComposeboxMaxNumFiles", "1"},
};
const FeatureEntry::FeatureParam kComposeboxNextForRealboxNext[] = {
    {"NtpComposeboxContextMenuEnableMultiTabSelection", "true"},
};

const FeatureEntry::FeatureVariation kNtpComposeboxVariations[] = {
    {"- Next Experience Single Context", kComposeboxNextSingleContext, nullptr},
    {"- Next Experience for Realbox Next", kComposeboxNextForRealboxNext,
     nullptr},
    {"- Next Experience Single Context for Realbox Next",
     kComposeboxNextSingleContextForRealboxNext, nullptr},
};

const FeatureEntry::FeatureParam kShowNextRealboxCompact[] = {
    {"RealboxLayoutMode", ntp_realbox::kRealboxLayoutModeCompact},
};
const FeatureEntry::FeatureParam kShowNextRealboxCompactCyclingPlaceholders[] =
    {
        {"RealboxLayoutMode", ntp_realbox::kRealboxLayoutModeCompact},
        {"CyclingPlaceholders", "true"},
};

const FeatureEntry::FeatureVariation kNtpRealboxNextVariations[] = {
    {"- Show Next Realbox (Compact)", kShowNextRealboxCompact, nullptr},
    {"- Show Next Realbox: Compact, Cycling placeholders",
     kShowNextRealboxCompactCyclingPlaceholders, nullptr},
};

const FeatureEntry::FeatureParam kNtpNextShowDeepDiveSuggestions[] = {
    {"NtpNextShowDeepDiveSuggestionsParam", "true"},
    {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "false"},
};
const FeatureEntry::FeatureParam kNtpNextShowSimplificationUIWithDeepDive[] = {
    {"NtpNextShowSimplificationUIParam", "true"},
    {"NtpNextShowDeepDiveSuggestionsParam", "true"},
    {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "false"},
};

const FeatureEntry::FeatureParam
    kNtpNextShowChipsUIWithChromeNtpActionClient[] = {
        {"NtpNextShowDeepDiveSuggestionsParam", "true"},
        {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
};

const FeatureEntry::FeatureParam
    kNtpNextShowChipsUIWithChromeNtpActionClientAndCanvas[] = {
        {"NtpNextShowDeepDiveSuggestionsParam", "true"},
        {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
        {"NtpNextEnableCanvasChipParam", "true"},
};

const FeatureEntry::FeatureParam
    kNtpNextShowSimplificationUIWithChromeNtpActionClient[] = {
        {"NtpNextShowSimplificationUIParam", "true"},
        {"NtpNextShowDeepDiveSuggestionsParam", "true"},
        {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
};

const FeatureEntry::FeatureParam
    kNtpNextShowSimplificationUIWithChromeNtpActionClientAndCanvas[] = {
        {"NtpNextShowSimplificationUIParam", "true"},
        {"NtpNextShowDeepDiveSuggestionsParam", "true"},
        {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
        {"NtpNextEnableCanvasChipParam", "true"},
};

const FeatureEntry::FeatureParam
    kNtpNextShowChipsUIWithNtpActionClientWithNoRecentTabInSteadyState[] = {
        {"NtpNextShowDeepDiveSuggestionsParam", "true"},
        {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
        {"kNtpNextShowStaticRecentTabChipParam", "false"},
};

const FeatureEntry::FeatureParam
    kNtpNextShowChipsUIWithNtpActionClientWithCanvasAndNoRecentTabInSteadyState
        [] = {
            {"NtpNextShowDeepDiveSuggestionsParam", "true"},
            {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
            {"kNtpNextShowStaticRecentTabChipParam", "false"},
            {"NtpNextEnableCanvasChipParam", "true"},
};

const FeatureEntry::FeatureParam
    kNtpNextShowSimplificationUIWithNtpActionClientWithNoRecentTabInSteadyState
        [] = {
            {"NtpNextShowSimplificationUIParam", "true"},
            {"NtpNextShowDeepDiveSuggestionsParam", "true"},
            {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
            {"kNtpNextShowStaticRecentTabChipParam", "false"},
};

const FeatureEntry::FeatureParam
    kNtpNextShowSimplificationUIWithNtpActionClientWithCanvasAndNoRecentTabInSteadyState
        [] = {
            {"NtpNextShowSimplificationUIParam", "true"},
            {"NtpNextShowDeepDiveSuggestionsParam", "true"},
            {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
            {"kNtpNextShowStaticRecentTabChipParam", "false"},
            {"NtpNextEnableCanvasChipParam", "true"},
};

const FeatureEntry::FeatureParam kNtpNextShowSimplificationUIWithDismissal[] = {
    {"NtpNextShowSimplificationUIParam", "true"},
    {"NtpNextShowDeepDiveSuggestionsParam", "true"},
    {"NtpNextSuggestionsFromNewSearchSuggestionsEndpointParam", "true"},
    {"NtpNextShowDismissalUIParam", "true"},
};

const FeatureEntry::FeatureParam kNtpNextAllowDisablement[] = {
    {"NtpNextDisablementContextMenuParam", "true"},
};

const FeatureEntry::FeatureVariation kNtpNextVariations[] = {
    {"- Show Deep Dive Suggestions", kNtpNextShowDeepDiveSuggestions, nullptr},
    {"- Show Row UI With Deep Dive", kNtpNextShowSimplificationUIWithDeepDive,
     nullptr},
    {"- Show Chips UI with a New Suggestions Client",
     kNtpNextShowChipsUIWithChromeNtpActionClient, nullptr},
    {"- Show Row UI with a New Suggestions Client",
     kNtpNextShowSimplificationUIWithChromeNtpActionClient, nullptr},
    {"- Show Chips UI with a New Client and No Recent Tab Chip in the Steady "
     "State",
     kNtpNextShowChipsUIWithNtpActionClientWithNoRecentTabInSteadyState,
     nullptr},
    {"- Show Row UI with a New Client and No Recent Tab Chip in the Steady "
     "State",
     kNtpNextShowSimplificationUIWithNtpActionClientWithNoRecentTabInSteadyState,
     nullptr},
    {"- Show Chips UI with a New Suggestions Client and Canvas Chip",
     kNtpNextShowChipsUIWithChromeNtpActionClientAndCanvas, nullptr},
    {"- Show Row UI with a New Suggestions Client and Canvas Chip",
     kNtpNextShowSimplificationUIWithChromeNtpActionClientAndCanvas, nullptr},
    {"- Show Chips UI with a New Client, Canvas Chip, and No Recent Tab Chip "
     "in the Steady State",
     kNtpNextShowChipsUIWithNtpActionClientWithCanvasAndNoRecentTabInSteadyState,
     nullptr},
    {"- Show Row UI with a New Client, Canvas Chip, and No Recent Tab Chip in "
     "the Steady State",
     kNtpNextShowSimplificationUIWithNtpActionClientWithCanvasAndNoRecentTabInSteadyState,
     nullptr},
    {"- Show Dismissal UI", kNtpNextShowSimplificationUIWithDismissal, nullptr},
    {"- Allow Disable", kNtpNextAllowDisablement, nullptr},
};

const FeatureEntry::FeatureParam kNtpFeatureOptimizationModuleRemovalDefault[] =
    {
        {"ModuleMinStalenessUpdateTimeInterval", "24h"},
        {"StaleModulesCountThreshold", "14"},
};

const FeatureEntry::FeatureParam kNtpFeatureOptimizationModuleRemovalTesting[] =
    {
        {"ModuleMinStalenessUpdateTimeInterval", "1s"},
        {"StaleModulesCountThreshold", "2"},
};

const FeatureEntry::FeatureVariation
    kNtpFeatureOptimizationModuleRemovalVariations[] = {
        {"- Default Auto-Removal Timing",
         kNtpFeatureOptimizationModuleRemovalDefault, nullptr},
        {"- Auto-Removal Timing for Testing",
         kNtpFeatureOptimizationModuleRemovalTesting, nullptr},
};

const FeatureEntry::FeatureParam
    kNtpFeatureOptimizationShortcutsRemovalDefault[] = {
        {"ShortcutsMinStalenessUpdateTimeInterval", "24h"},
        {"StaleShortcutsCountThreshold", "60"},
};

const FeatureEntry::FeatureParam
    kNtpFeatureOptimizationShortcutsRemovalTesting[] = {
        {"ShortcutsMinStalenessUpdateTimeInterval", "1s"},
        {"StaleShortcutsCountThreshold", "5"},
};

const FeatureEntry::FeatureVariation
    kNtpFeatureOptimizationShortcutsRemovalVariations[] = {
        {"- Default Auto-Removal Timing",
         kNtpFeatureOptimizationShortcutsRemovalDefault, nullptr},
        {"- Auto-Removal Timing for Testing",
         kNtpFeatureOptimizationShortcutsRemovalTesting, nullptr},
};

#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

const FeatureEntry::FeatureParam kOmniboxMlUrlScoringEnabledWithFixes[] = {
    {"enable_scoring_signals_annotators_for_ml_scoring", "true"},
    {"MlUrlScoringShortcutDocumentSignals", "true"},
};
const FeatureEntry::FeatureParam kOmniboxMlUrlScoringUnlimitedNumCandidates[] =
    {
        {"MlUrlScoringUnlimitedNumCandidates", "true"},
        {"enable_scoring_signals_annotators_for_ml_scoring", "true"},
        {"MlUrlScoringShortcutDocumentSignals", "true"},
};
// Sets Bookmark(1), History Quick(4), History URL(8), Shortcuts(64),
// Document(512), and History Fuzzy(65536) providers max matches to 10.
const FeatureEntry::FeatureParam kOmniboxMlUrlScoringMaxMatchesByProvider10[] =
    {
        {"MlUrlScoringMaxMatchesByProvider",
         "1:10,4:10,8:10,64:10,512:10,65536:10"},
        {"enable_scoring_signals_annotators_for_ml_scoring", "true"},
        {"MlUrlScoringShortcutDocumentSignals", "true"},
};
// Enables ML scoring for Search suggestions.
const FeatureEntry::FeatureParam kOmniboxMlUrlScoringWithSearches[] = {
    {"MlUrlScoring_EnableMlScoringForSearches", "true"},
};
// Enables ML scoring for verbatim URL suggestions.
const FeatureEntry::FeatureParam kOmniboxMlUrlScoringWithVerbatimURLs[] = {
    {"MlUrlScoring_EnableMlScoringForVerbatimUrls", "true"},
};
// Enables ML scoring for both Search and verbatim URL suggestions.
const FeatureEntry::FeatureParam
    kOmniboxMlUrlScoringWithSearchesAndVerbatimURLs[] = {
        {"MlUrlScoring_EnableMlScoringForSearches", "true"},
        {"MlUrlScoring_EnableMlScoringForVerbatimUrls", "true"},
};

const FeatureEntry::FeatureVariation kOmniboxMlUrlScoringVariations[] = {
    {"Enabled with fixes", kOmniboxMlUrlScoringEnabledWithFixes, nullptr},
    {"unlimited suggestion candidates",
     kOmniboxMlUrlScoringUnlimitedNumCandidates, nullptr},
    {"Increase provider max limit to 10",
     kOmniboxMlUrlScoringMaxMatchesByProvider10, nullptr},
    {"with scoring of Search suggestions", kOmniboxMlUrlScoringWithSearches,
     nullptr},
    {"with scoring of verbatim URL suggestions",
     kOmniboxMlUrlScoringWithVerbatimURLs, nullptr},
    {"with scoring of Search & verbatim URL suggestions",
     kOmniboxMlUrlScoringWithSearchesAndVerbatimURLs, nullptr},
};

const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingAdjustedBy0[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,550;0.018,1300;0.14,1398;1,1422"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1400"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};
const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingDemotedBy50[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,550;0.018,1250;0.14,1348;1,1422"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1350"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};
const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingPromotedBy50[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,550;0.018,1350;0.14,1448;1,1472"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1450"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};
const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingPromotedBy100[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,550;0.018,1400;0.14,1498;1,1522"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1500"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};
const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingMobileMapping[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,590;0.006,790;0.082,1290;0.443,1360;0.464,1400;0.987,1425;1,1530"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1340"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};

const FeatureEntry::FeatureVariation
    kMlUrlPiecewiseMappedSearchBlendingVariations[] = {
        {"adjusted by 0", kMlUrlPiecewiseMappedSearchBlendingAdjustedBy0,
         nullptr},
        {"demoted by 50", kMlUrlPiecewiseMappedSearchBlendingDemotedBy50,
         nullptr},
        {"promoted by 50", kMlUrlPiecewiseMappedSearchBlendingPromotedBy50,
         nullptr},
        {"promoted by 100", kMlUrlPiecewiseMappedSearchBlendingPromotedBy100,
         nullptr},
        {"mobile mapping", kMlUrlPiecewiseMappedSearchBlendingMobileMapping,
         nullptr},
};

const FeatureEntry::FeatureParam kMlUrlSearchBlendingStable[] = {
    {"MlUrlSearchBlending_StableSearchBlending", "true"},
    {"MlUrlSearchBlending_MappedSearchBlending", "false"},
};
const FeatureEntry::FeatureParam kMlUrlSearchBlendingMappedConservativeUrls[] =
    {
        {"MlUrlSearchBlending_StableSearchBlending", "false"},
        {"MlUrlSearchBlending_MappedSearchBlending", "true"},
        {"MlUrlSearchBlending_MappedSearchBlendingMin", "0"},
        {"MlUrlSearchBlending_MappedSearchBlendingMax", "2000"},
        {"MlUrlSearchBlending_MappedSearchBlendingGroupingThreshold", "1000"},
};
const FeatureEntry::FeatureParam kMlUrlSearchBlendingMappedModerateUrls[] = {
    {"MlUrlSearchBlending_StableSearchBlending", "false"},
    {"MlUrlSearchBlending_MappedSearchBlending", "true"},
};
const FeatureEntry::FeatureParam kMlUrlSearchBlendingMappedAggressiveUrls[] = {
    {"MlUrlSearchBlending_StableSearchBlending", "false"},
    {"MlUrlSearchBlending_MappedSearchBlending", "true"},
    {"MlUrlSearchBlending_MappedSearchBlendingMin", "1000"},
    {"MlUrlSearchBlending_MappedSearchBlendingMax", "4000"},
    {"MlUrlSearchBlending_MappedSearchBlendingGroupingThreshold", "1500"},
};

const FeatureEntry::FeatureVariation kMlUrlSearchBlendingVariations[] = {
    {"Stable", kMlUrlSearchBlendingStable, nullptr},
    {"Mapped conservative urls", kMlUrlSearchBlendingMappedConservativeUrls,
     nullptr},
    {"Mapped moderate urls", kMlUrlSearchBlendingMappedModerateUrls, nullptr},
    {"Mapped aggressive urls", kMlUrlSearchBlendingMappedAggressiveUrls,
     nullptr},
};

const FeatureEntry::FeatureParam kMostVitedTilesNewScoring_DecayStaircaseCap10[] = {
    {
        "recency_factor",  // history::kMvtScoringParamRecencyFactor.name
        "decay_staircase"  // history::kMvtScoringParamRecencyFactor_DecayStaircase
    },
    {"daily_visit_count_cap",  // history::kMvtScoringParamDailyVisitCountCap.name
     "10"},
};
const FeatureEntry::FeatureParam kMostVitedTilesNewScoring_DecayCap1[] = {
    {
        "recency_factor",  // history::kMvtScoringParamRecencyFactor.name
        "decay"            // history::kMvtScoringParamRecencyFactor_Decay
    },
    {
        "decay_per_day",      // history::kMvtScoringParamDecayPerDay.name
        "0.9131007162822623"  // exp(-1.0 / 11).
    },
    {"daily_visit_count_cap",  // history::kMvtScoringParamDailyVisitCountCap.name
     "1"},
};
constexpr FeatureEntry::FeatureVariation
    kMostVisitedTilesNewScoringVariations[] = {
        {"Decay Staircase, Cap 10",
         kMostVitedTilesNewScoring_DecayStaircaseCap10, nullptr},
        {"Decay, Cap 1", kMostVitedTilesNewScoring_DecayCap1, nullptr},
};

const FeatureEntry::FeatureVariation kUrlScoringModelVariations[] = {
    {"Small model (desktop)", {}, nullptr},
    {"Full model (desktop)", {}, "3380045"},
    {"Small model (ios)", {}, "3379590"},
    {"Full model (ios)", {}, "3380197"},
    {"Small model (android)", {}, "3381543"},
    {"Full model (android)", {}, "3381544"},
};

const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRun[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "300"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "true"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRequest[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "300"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "false"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRun[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "600"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "true"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRequest[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "600"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "false"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRun[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "900"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "true"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRequest[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "900"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "false"},
};

const FeatureEntry::FeatureVariation
    kOmniboxZeroSuggestPrefetchDebouncingVariations[] = {
        {"Minimal debouncing relative to last run",
         kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRun, nullptr},
        {"Minimal debouncing relative to last request",
         kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRequest, nullptr},
        {"Moderate debouncing relative to last run",
         kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRun, nullptr},
        {"Moderate debouncing relative to last request",
         kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRequest, nullptr},
        {"Aggressive debouncing relative to last run",
         kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRun, nullptr},
        {"Aggressive debouncing relative to last request",
         kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRequest,
         nullptr},
};


const FeatureEntry::FeatureParam kMaxZeroSuggestMatches5[] = {
    {"MaxZeroSuggestMatches", "5"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches6[] = {
    {"MaxZeroSuggestMatches", "6"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches7[] = {
    {"MaxZeroSuggestMatches", "7"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches8[] = {
    {"MaxZeroSuggestMatches", "8"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches9[] = {
    {"MaxZeroSuggestMatches", "9"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches10[] = {
    {"MaxZeroSuggestMatches", "10"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches11[] = {
    {"MaxZeroSuggestMatches", "11"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches12[] = {
    {"MaxZeroSuggestMatches", "12"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches13[] = {
    {"MaxZeroSuggestMatches", "13"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches14[] = {
    {"MaxZeroSuggestMatches", "14"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches15[] = {
    {"MaxZeroSuggestMatches", "15"}};

const FeatureEntry::FeatureVariation kMaxZeroSuggestMatchesVariations[] = {
    {"5", kMaxZeroSuggestMatches5, nullptr},
    {"6", kMaxZeroSuggestMatches6, nullptr},
    {"7", kMaxZeroSuggestMatches7, nullptr},
    {"8", kMaxZeroSuggestMatches8, nullptr},
    {"9", kMaxZeroSuggestMatches9, nullptr},
    {"10", kMaxZeroSuggestMatches10, nullptr},
    {"11", kMaxZeroSuggestMatches11, nullptr},
    {"12", kMaxZeroSuggestMatches12, nullptr},
    {"13", kMaxZeroSuggestMatches13, nullptr},
    {"14", kMaxZeroSuggestMatches14, nullptr},
    {"15", kMaxZeroSuggestMatches15, nullptr}};

const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches3[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "3"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches4[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "4"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches5[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "5"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches6[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "6"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches7[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "7"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches8[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "8"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches9[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "9"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches10[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "10"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches12[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "12"}};

const FeatureEntry::FeatureVariation
    kOmniboxUIMaxAutocompleteMatchesVariations[] = {
        {"3 matches", kOmniboxUIMaxAutocompleteMatches3, nullptr},
        {"4 matches", kOmniboxUIMaxAutocompleteMatches4, nullptr},
        {"5 matches", kOmniboxUIMaxAutocompleteMatches5, nullptr},
        {"6 matches", kOmniboxUIMaxAutocompleteMatches6, nullptr},
        {"7 matches", kOmniboxUIMaxAutocompleteMatches7, nullptr},
        {"8 matches", kOmniboxUIMaxAutocompleteMatches8, nullptr},
        {"9 matches", kOmniboxUIMaxAutocompleteMatches9, nullptr},
        {"10 matches", kOmniboxUIMaxAutocompleteMatches10, nullptr},
        {"12 matches", kOmniboxUIMaxAutocompleteMatches12, nullptr}};


const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete90[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "0"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "9"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete91[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "1"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "9"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete92[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "2"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "9"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete100[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "0"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "10"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete101[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "1"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "10"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete102[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "2"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "10"}};

const FeatureEntry::FeatureVariation
    kOmniboxDynamicMaxAutocompleteVariations[] = {
        {"9 suggestions if 0 or fewer URLs", kOmniboxDynamicMaxAutocomplete90,
         nullptr},
        {"9 suggestions if 1 or fewer URLs", kOmniboxDynamicMaxAutocomplete91,
         nullptr},
        {"9 suggestions if 2 or fewer URLs", kOmniboxDynamicMaxAutocomplete92,
         nullptr},
        {"10 suggestions if 0 or fewer URLs", kOmniboxDynamicMaxAutocomplete100,
         nullptr},
        {"10 suggestions if 1 or fewer URLs", kOmniboxDynamicMaxAutocomplete101,
         nullptr},
        {"10 suggestions if 2 or fewer URLs", kOmniboxDynamicMaxAutocomplete102,
         nullptr}};

const FeatureEntry::FeatureParam kRepeatableQueries_6Searches_90Days[] = {
    {"RepeatableQueriesIgnoreDuplicateVisits", "true"},
    {"RepeatableQueriesMinVisitCount", "6"},
};
const FeatureEntry::FeatureParam kRepeatableQueries_12Searches_90Days[] = {
    {"RepeatableQueriesIgnoreDuplicateVisits", "true"},
    {"RepeatableQueriesMinVisitCount", "12"},
};
const FeatureEntry::FeatureParam kRepeatableQueries_6Searches_7Days[] = {
    {"RepeatableQueriesIgnoreDuplicateVisits", "true"},
    {"RepeatableQueriesMinVisitCount", "6"},
    {"RepeatableQueriesMaxAgeDays", "7"},
};
const FeatureEntry::FeatureParam kRepeatableQueries_12Searches_7Days[] = {
    {"RepeatableQueriesIgnoreDuplicateVisits", "true"},
    {"RepeatableQueriesMinVisitCount", "12"},
    {"RepeatableQueriesMaxAgeDays", "7"},
};

const FeatureEntry::FeatureVariation kOrganicRepeatableQueriesVariations[] = {
    {"6+ uses, once in last 90d", kRepeatableQueries_6Searches_90Days, nullptr},
    {"12+ uses, once in last 90d", kRepeatableQueries_12Searches_90Days,
     nullptr},
    {"6+ uses, once in last 7d", kRepeatableQueries_6Searches_7Days, nullptr},
    {"12+ uses, once in last 7d", kRepeatableQueries_12Searches_7Days, nullptr},
};

const FeatureEntry::FeatureParam kNtpZps0RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsRecentSearches.name, "0"}};
const FeatureEntry::FeatureParam kNtpZps5RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsRecentSearches.name, "5"}};
const FeatureEntry::FeatureParam kNtpZps10RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsRecentSearches.name, "10"}};
const FeatureEntry::FeatureParam kNtpZps15RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsRecentSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumNtpZpsRecentSearches[] = {
    {"No recents", kNtpZps0RecentSearches},
    {"5 recents", kNtpZps5RecentSearches},
    {"10 recents", kNtpZps10RecentSearches},
    {"15 recents", kNtpZps15RecentSearches},
};
const FeatureEntry::FeatureParam kNtpZps0TrendingSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsTrendingSearches.name, "0"}};
const FeatureEntry::FeatureParam kNtpZps5TrendingSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsTrendingSearches.name, "5"}};
const FeatureEntry::FeatureVariation kNumNtpZpsTrendingSearches[] = {
    {"No trends", kNtpZps0TrendingSearches},
    {"5 trends", kNtpZps5TrendingSearches},
};
const FeatureEntry::FeatureParam kWebZps0RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRecentSearches.name, "0"}};
const FeatureEntry::FeatureParam kWebZps5RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRecentSearches.name, "5"}};
const FeatureEntry::FeatureParam kWebZps10RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRecentSearches.name, "10"}};
const FeatureEntry::FeatureParam kWebZps15RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRecentSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumWebZpsRecentSearches[] = {
    {"No recents", kWebZps0RecentSearches},
    {"5 recents", kWebZps5RecentSearches},
    {"10 recents", kWebZps10RecentSearches},
    {"15 recents", kWebZps15RecentSearches},
};
const FeatureEntry::FeatureParam kWebZps0RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRelatedSearches.name, "0"}};
const FeatureEntry::FeatureParam kWebZps5RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRelatedSearches.name, "5"}};
const FeatureEntry::FeatureParam kWebZps10RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRelatedSearches.name, "10"}};
const FeatureEntry::FeatureParam kWebZps15RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRelatedSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumWebZpsRelatedSearches[] = {
    {"No related", kWebZps0RelatedSearches},
    {"5 related", kWebZps5RelatedSearches},
    {"10 related", kWebZps10RelatedSearches},
    {"15 related", kWebZps15RelatedSearches},
};
const FeatureEntry::FeatureParam kWebZps0MostVisitedUrls[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsMostVisitedUrls.name, "0"}};
const FeatureEntry::FeatureParam kWebZps5MostVisitedUrls[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsMostVisitedUrls.name, "5"}};
const FeatureEntry::FeatureParam kWebZps10MostVisitedUrls[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsMostVisitedUrls.name, "10"}};
const FeatureEntry::FeatureParam kWebZps15MostVisitedUrls[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsMostVisitedUrls.name, "15"}};
const FeatureEntry::FeatureVariation kNumWebZpsMostVisitedUrls[] = {
    {"No related", kWebZps0MostVisitedUrls},
    {"5 related", kWebZps5MostVisitedUrls},
    {"10 related", kWebZps10MostVisitedUrls},
    {"15 related", kWebZps15MostVisitedUrls},
};
const FeatureEntry::FeatureParam kSrpZps0RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRecentSearches.name, "0"}};
const FeatureEntry::FeatureParam kSrpZps5RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRecentSearches.name, "5"}};
const FeatureEntry::FeatureParam kSrpZps10RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRecentSearches.name, "10"}};
const FeatureEntry::FeatureParam kSrpZps15RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRecentSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumSrpZpsRecentSearches[] = {
    {"No recents", kSrpZps0RecentSearches},
    {"5 recents", kSrpZps5RecentSearches},
    {"10 recents", kSrpZps10RecentSearches},
    {"15 recents", kSrpZps15RecentSearches},
};
const FeatureEntry::FeatureParam kSrpZps0RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRelatedSearches.name, "0"}};
const FeatureEntry::FeatureParam kSrpZps5RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRelatedSearches.name, "5"}};
const FeatureEntry::FeatureParam kSrpZps10RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRelatedSearches.name, "10"}};
const FeatureEntry::FeatureParam kSrpZps15RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRelatedSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumSrpZpsRelatedSearches[] = {
    {"No related", kSrpZps0RelatedSearches},
    {"5 related", kSrpZps5RelatedSearches},
    {"10 related", kSrpZps10RelatedSearches},
    {"15 related", kSrpZps15RelatedSearches},
};

const FeatureEntry::FeatureParam kTabGroupsFocusingPinnedTabs[] = {
    {"tab_groups_focusing_pinned_tabs", "true"}};
const FeatureEntry::FeatureParam kTabGroupsFocusingDefaultToFocusedOnly[] = {
    {"tab_groups_focusing_default_to_focused", "true"}};
const FeatureEntry::FeatureParam kTabGroupsFocusingAll[] = {
    {"tab_groups_focusing_pinned_tabs", "true"},
    {"tab_groups_focusing_default_to_focused", "true"}};

const FeatureEntry::FeatureVariation kTabGroupsFocusingVariations[] = {
    {" - show pinned tabs", kTabGroupsFocusingPinnedTabs},
    {" - autofocus opened groups only", kTabGroupsFocusingDefaultToFocusedOnly},
    {" - autofocus and show pinned tabs", kTabGroupsFocusingAll},
};

const FeatureEntry::FeatureParam kNtpCalendarModuleFakeData[] = {
    {ntp_features::kNtpCalendarModuleDataParam, "fake"}};
const FeatureEntry::FeatureVariation kNtpCalendarModuleVariations[] = {
    {"- Fake Data", kNtpCalendarModuleFakeData, nullptr},
};

const FeatureEntry::FeatureParam kNtpDriveModuleFakeData[] = {
    {ntp_features::kNtpDriveModuleDataParam, "fake"}};
const FeatureEntry::FeatureParam kNtpDriveModuleManagedUsersOnly[] = {
    {ntp_features::kNtpDriveModuleManagedUsersOnlyParam, "true"}};
const FeatureEntry::FeatureVariation kNtpDriveModuleVariations[] = {
    {"- Fake Data", kNtpDriveModuleFakeData, nullptr},
    {"- Managed Users Only", kNtpDriveModuleManagedUsersOnly, nullptr},
};

const FeatureEntry::FeatureParam kNtpOutlookCalendarModuleFakeData[] = {
    {ntp_features::kNtpOutlookCalendarModuleDataParam, "fake"}};
const FeatureEntry::FeatureParam
    kNtpOutlookCalendarModuleFakeAttachmentsData[] = {
        {ntp_features::kNtpOutlookCalendarModuleDataParam, "fake-attachments"}};
const FeatureEntry::FeatureVariation kNtpOutlookCalendarModuleVariations[] = {
    {"- Fake Data", kNtpOutlookCalendarModuleFakeData, nullptr},
    {"- Fake Attachments Data", kNtpOutlookCalendarModuleFakeAttachmentsData,
     nullptr},
};

const FeatureEntry::FeatureParam kNtpMiddleSlotPromoDismissalFakeData[] = {
    {ntp_features::kNtpMiddleSlotPromoDismissalParam, "fake"}};
const FeatureEntry::FeatureVariation kNtpMiddleSlotPromoDismissalVariations[] =
    {
        {"- Fake Data", kNtpMiddleSlotPromoDismissalFakeData, nullptr},
};

const FeatureEntry::FeatureParam
    kNtpRealboxCr23NoShadowExpandedStateBgMatchesSteadyState[]{
        {"kNtpRealboxCr23ExpandedStateBgMatchesOmnibox", "false"},
        {"kNtpRealboxCr23SteadyStateShadow", "false"}};
const FeatureEntry::FeatureParam
    kNtpRealboxCr23ShadowExpandedStateBgMatchesOmnibox[]{
        {"kNtpRealboxCr23ExpandedStateBgMatchesOmnibox", "true"},
        {"kNtpRealboxCr23SteadyStateShadow", "true"}};
const FeatureEntry::FeatureParam
    kNtpRealboxCr23ShadowExpandedStateBgMatchesSteadyState[]{
        {"kNtpRealboxCr23ExpandedStateBgMatchesOmnibox", "false"},
        {"kNtpRealboxCr23SteadyStateShadow", "true"}};

const FeatureEntry::FeatureVariation kNtpRealboxCr23ThemingVariations[] = {
    {" - Steady state shadow",
     kNtpRealboxCr23ShadowExpandedStateBgMatchesOmnibox, nullptr},
    {" - No steady state shadow + Dark mode background color matches steady"
     "state",
     kNtpRealboxCr23NoShadowExpandedStateBgMatchesSteadyState, nullptr},
    {" -  Steady state shadow + Dark mode background color matches steady "
     "state",
     kNtpRealboxCr23ShadowExpandedStateBgMatchesSteadyState, nullptr},
};

const FeatureEntry::FeatureParam kNtpSafeBrowsingModuleFastCooldown[] = {
    {ntp_features::kNtpSafeBrowsingModuleCooldownPeriodDaysParam, "0.001"},
    {ntp_features::kNtpSafeBrowsingModuleCountMaxParam, "1"}};
const FeatureEntry::FeatureVariation kNtpSafeBrowsingModuleVariations[] = {
    {"(Fast Cooldown)", kNtpSafeBrowsingModuleFastCooldown, nullptr},
};

const FeatureEntry::FeatureParam kNtpSharepointModuleTrendingInsights[] = {
    {"NtpSharepointModuleDataParam", "trending-insights"}};
const FeatureEntry::FeatureParam kNtpSharepointModuleNonInsights[] = {
    {"NtpSharepointModuleDataParam", "non-insights"}};
const FeatureEntry::FeatureParam kNtpSharepointModuleTrendingFakeData[] = {
    {"NtpSharepointModuleDataParam", "fake-trending"}};
const FeatureEntry::FeatureParam kNtpSharepointModuleNonInsightsFakeData[] = {
    {"NtpSharepointModuleDataParam", "fake-non-insights"}};
const FeatureEntry::FeatureParam kNtpSharepointModuleCombinedSuggestions[] = {
    {"NtpSharepointModuleDataParam", "combined"}};

const FeatureEntry::FeatureVariation kNtpSharepointModuleVariations[] = {
    {"- Trending", kNtpSharepointModuleTrendingInsights, nullptr},
    {"- Recently Used and Shared", kNtpSharepointModuleNonInsights, nullptr},
    {"- Fake Trending Data", kNtpSharepointModuleTrendingFakeData, nullptr},
    {"- Fake Recently Used and Shared", kNtpSharepointModuleNonInsightsFakeData,
     nullptr},
    {"- Combined Suggestions", kNtpSharepointModuleCombinedSuggestions,
     nullptr}};

const FeatureEntry::FeatureParam kNtpTabGroupsModuleFakeData[] = {
    {ntp_features::kNtpTabGroupsModuleDataParam, "Fake Data"}};
const FeatureEntry::FeatureParam kNtpTabGroupsModuleFakeZeroState[] = {
    {ntp_features::kNtpTabGroupsModuleDataParam, "Fake Zero State"}};

const FeatureEntry::FeatureVariation kNtpTabGroupsModuleVariations[] = {
    {"- Fake Data", kNtpTabGroupsModuleFakeData, nullptr},
    {"- Fake Zero State", kNtpTabGroupsModuleFakeZeroState, nullptr},
};

const FeatureEntry::FeatureParam kDataSharingShowSendFeedbackDisabled[] = {
    {"show_send_feedback", "false"}};
const FeatureEntry::FeatureParam kDataSharingShowSendFeedbackEnabled[] = {
    {"show_send_feedback", "true"}};
const FeatureEntry::FeatureVariation kDatasharingVariations[] = {
    {"with feedback", kDataSharingShowSendFeedbackEnabled},
    {"without feedback", kDataSharingShowSendFeedbackDisabled}};


const FeatureEntry::FeatureParam
    kReportNotificationContentDetectionDataRate100[] = {
        {"ReportNotificationContentDetectionDataRate", "100"}};
const FeatureEntry::FeatureVariation
    kReportNotificationContentDetectionDataVariations[] = {
        {"with reporting rate 100",
         kReportNotificationContentDetectionDataRate100, nullptr},
};

const FeatureEntry::FeatureParam
    kResamplingScrollEventsPredictionFramesBasedEnabledV1[] = {
        {"mode", features::kPredictionTypeFramesBased},
        {"latency", features::kPredictionTypeDefaultFramesVariation1}};
const FeatureEntry::FeatureParam
    kResamplingScrollEventsPredictionFramesBasedEnabledV2[] = {
        {"mode", features::kPredictionTypeFramesBased},
        {"latency", features::kPredictionTypeDefaultFramesVariation2}};
const FeatureEntry::FeatureParam
    kResamplingScrollEventsPredictionFramesBasedEnabledV3[] = {
        {"mode", features::kPredictionTypeFramesBased},
        {"latency", features::kPredictionTypeDefaultFramesVariation3}};
const FeatureEntry::FeatureVariation
    kResamplingScrollEventsExperimentalPredictionVariations[] = {
        {"frames 0.25", kResamplingScrollEventsPredictionFramesBasedEnabledV1,
         nullptr},
        {"frames 0.375", kResamplingScrollEventsPredictionFramesBasedEnabledV2,
         nullptr},
        {"frames 0.5", kResamplingScrollEventsPredictionFramesBasedEnabledV3,
         nullptr},
};

const FeatureEntry::FeatureParam
    kShowWarningsForSuspiciousNotificationsScoreThreshold70[] = {
        {"ShowWarningsForSuspiciousNotificationsScoreThreshold", "70"},
        {"ShowWarningsForSuspiciousNotificationsShouldSwapButtons", "false"}};
const FeatureEntry::FeatureParam
    kShowWarningsForSuspiciousNotificationsScoreThreshold70SwapButtons[] = {
        {"ShowWarningsForSuspiciousNotificationsScoreThreshold", "70"},
        {"ShowWarningsForSuspiciousNotificationsShouldSwapButtons", "true"}};
const FeatureEntry::FeatureVariation
    kShowWarningsForSuspiciousNotificationsVariations[] = {
        {"with suspicious score 70",
         kShowWarningsForSuspiciousNotificationsScoreThreshold70, nullptr},
        {"with suspicious score 70 and swapped buttons",
         kShowWarningsForSuspiciousNotificationsScoreThreshold70SwapButtons,
         nullptr},
};




const FeatureEntry::FeatureParam kRenderDocument_Subframe[] = {
    {"level", "subframe"}};
const FeatureEntry::FeatureParam kRenderDocument_AllFrames[] = {
    {"level", "all-frames"}};

const FeatureEntry::FeatureVariation kRenderDocumentVariations[] = {
    {"Swap RenderFrameHosts on same-site navigations from subframes and "
     "crashed frames (experimental)",
     kRenderDocument_Subframe, nullptr},
    {"Swap RenderFrameHosts on same-site navigations from any frame "
     "(experimental)",
     kRenderDocument_AllFrames, nullptr},
};


// The choices for the Send Tab To Self enhanced handoff experiment.
const FeatureEntry::Choice kSendTabToSelfEnhancedHandoffChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled, switches::kEnableFeatures,
     "SendTabToSelfPropagateFormFields,"
     "SendTabToSelfPropagateScrollPosition"},
    {flags_ui::kGenericExperimentChoiceDisabled, switches::kDisableFeatures,
     "SendTabToSelfPropagateFormFields,"
     "SendTabToSelfPropagateScrollPosition"},
};

// The choices for --enable-experimental-cookie-features. This really should
// just be a SINGLE_VALUE_TYPE, but it is misleading to have the choices be
// labeled "Disabled"/"Enabled". So instead this is made to be a
// MULTI_VALUE_TYPE with choices "Default"/"Enabled".
const FeatureEntry::Choice kEnableExperimentalCookieFeaturesChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled,
     switches::kEnableExperimentalCookieFeatures, ""},
};


// SCT Auditing feature variations.
const FeatureEntry::FeatureParam kSCTAuditingSamplingRateNone[] = {
    {"sampling_rate", "0.0"}};
const FeatureEntry::FeatureParam kSCTAuditingSamplingRateAlternativeOne[] = {
    {"sampling_rate", "0.0001"}};
const FeatureEntry::FeatureParam kSCTAuditingSamplingRateAlternativeTwo[] = {
    {"sampling_rate", "0.001"}};

const FeatureEntry::FeatureVariation kSCTAuditingVariations[] = {
    {"Sampling rate 0%", kSCTAuditingSamplingRateNone, nullptr},
    {"Sampling rate 0.01%", kSCTAuditingSamplingRateAlternativeOne, nullptr},
    {"Sampling rate 0.1%", kSCTAuditingSamplingRateAlternativeTwo, nullptr},
};



const FeatureEntry::FeatureParam kLensOverlayNoOmniboxEntryPoint[] = {
    {"omnibox-entry-point", "false"},
};
const FeatureEntry::FeatureParam kLensOverlayResponsiveOmniboxEntryPoint[] = {
    {"omnibox-entry-point", "true"},
    {"omnibox-entry-point-always-visible", "false"},
};
const FeatureEntry::FeatureParam kLensOverlayPersistentOmniboxEntryPoint[] = {
    {"omnibox-entry-point", "true"},
    {"omnibox-entry-point-always-visible", "true"},
};

const FeatureEntry::FeatureVariation kLensOverlayVariations[] = {
    {"with no omnibox entry point", kLensOverlayNoOmniboxEntryPoint, nullptr},
    {"with responsive chip omnibox entry point",
     kLensOverlayResponsiveOmniboxEntryPoint, nullptr},
    {"with persistent icon omnibox entry point",
     kLensOverlayPersistentOmniboxEntryPoint, nullptr},
};

const FeatureEntry::FeatureParam kLensOverlayImageContextMenuActionsCopy[] = {
    {"enable-copy-as-image", "true"},
    {"enable-save-as-image", "false"},
};

const FeatureEntry::FeatureParam kLensOverlayImageContextMenuActionsSave[] = {
    {"enable-copy-as-image", "false"},
    {"enable-save-as-image", "true"},
};

const FeatureEntry::FeatureParam
    kLensOverlayImageContextMenuActionsCopyAndSave[] = {
        {"enable-copy-as-image", "true"},
        {"enable-save-as-image", "true"},
};

const FeatureEntry::FeatureVariation
    kLensOverlayImageContextMenuActionsVariations[] = {
        {"copy as image", kLensOverlayImageContextMenuActionsCopy, nullptr},
        {"save as image", kLensOverlayImageContextMenuActionsSave, nullptr},
        {"copy and save as image",
         kLensOverlayImageContextMenuActionsCopyAndSave, nullptr},
};

const FeatureEntry::FeatureParam
    kLensOverlayTextSelectionContextMenuEntrypointContextualized[] = {
        {"contextualize", "true"}};
const FeatureEntry::FeatureParam
    kLensOverlayTextSelectionContextMenuEntrypointNonContextualized[] = {
        {"contextualize", "false"}};
const FeatureEntry::FeatureVariation
    kLensOverlayTextSelectionContextMenuEntrypointVariations[] = {
        {"contextualized",
         kLensOverlayTextSelectionContextMenuEntrypointContextualized, nullptr},
        {"non-contextualized",
         kLensOverlayTextSelectionContextMenuEntrypointNonContextualized,
         nullptr},
};


// Feature variations for kIsolateSandboxedIframes.
const FeatureEntry::FeatureParam kIsolateSandboxedIframesGroupingPerSite[] = {
    {"grouping", "per-site"}};
const FeatureEntry::FeatureParam kIsolateSandboxedIframesGroupingPerOrigin[] = {
    {"grouping", "per-origin"}};
const FeatureEntry::FeatureParam kIsolateSandboxedIframesGroupingPerDocument[] =
    {{"grouping", "per-document"}};
const FeatureEntry::FeatureVariation
    kIsolateSandboxedIframesGroupingVariations[] = {
        {"with grouping by URL's site", kIsolateSandboxedIframesGroupingPerSite,
         nullptr},
        {"with grouping by URL's origin",
         kIsolateSandboxedIframesGroupingPerOrigin, nullptr},
        {"with each sandboxed frame document in its own process",
         kIsolateSandboxedIframesGroupingPerDocument, nullptr},
};

// Feature variation for kPdfInk2.
#if BUILDFLAG(ENABLE_PDF_INK2)
const FeatureEntry::FeatureParam kPdfInk2TextHighlighting[] = {
    {"text-annotations", "false"},
    {"text-highlighting", "true"},
};
const FeatureEntry::FeatureParam kPdfInk2TextAnnotations[] = {
    {"text-annotations", "true"},
    {"text-highlighting", "false"},
};
const FeatureEntry::FeatureParam kPdfInk2TextHighlightingAndAnnotations[] = {
    {"text-annotations", "true"},
    {"text-highlighting", "true"},
};

const FeatureEntry::FeatureVariation kPdfInk2Variations[] = {
    {"with text highlighting", kPdfInk2TextHighlighting, nullptr},
    {"with text annotations", kPdfInk2TextAnnotations, nullptr},
    {"with text highlighting and annotations",
     kPdfInk2TextHighlightingAndAnnotations, nullptr},
};
#endif  // BUILDFLAG(ENABLE_PDF_INK2)

const FeatureEntry::FeatureParam kWebRtcApmDownmixMethodAverage[] = {
    {"method", "average"}};
const FeatureEntry::FeatureParam kWebRtcApmDownmixMethodFirstChannel[] = {
    {"method", "first"}};
const FeatureEntry::FeatureVariation kWebRtcApmDownmixMethodVariations[] = {
    {"- Average all the input channels", kWebRtcApmDownmixMethodAverage,
     nullptr},
    {"- Use first channel", kWebRtcApmDownmixMethodFirstChannel, nullptr}};

const FeatureEntry::FeatureParam
    kSafetyCheckUnusedSitePermissionsNoDelayParam[] = {
        {"unused-site-permissions-no-delay-for-testing", "true"}};

const FeatureEntry::FeatureParam
    kSafetyCheckUnusedSitePermissionsWithDelayParam[] = {
        {"unused-site-permissions-with-delay-for-testing", "true"}};

const FeatureEntry::FeatureVariation
    kSafetyCheckUnusedSitePermissionsVariations[] = {
        {"for testing no delay", kSafetyCheckUnusedSitePermissionsNoDelayParam,
         nullptr},
        {"for testing with delay",
         kSafetyCheckUnusedSitePermissionsWithDelayParam, nullptr},
};

const FeatureEntry::FeatureParam
    kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_MainFrameInitiator
        [] = {
            {content_settings::features::kTpcdReadHeuristicsGrantsName, "true"},
            {content_settings::features::
                 kTpcdWritePopupCurrentInteractionHeuristicsGrantsName,
             "30d"},
            {content_settings::features::
                 kTpcdPopupHeuristicEnableForIframeInitiatorName,
             "none"},
            {content_settings::features::kTpcdWriteRedirectHeuristicGrantsName,
             "15m"},
            {content_settings::features::
                 kTpcdRedirectHeuristicRequireABAFlowName,
             "true"},
            {content_settings::features::
                 kTpcdRedirectHeuristicRequireCurrentInteractionName,
             "true"}};
const FeatureEntry::FeatureParam
    kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_MainFrameInitiator[] =
        {{content_settings::features::kTpcdReadHeuristicsGrantsName, "true"},
         {content_settings::features::
              kTpcdWritePopupCurrentInteractionHeuristicsGrantsName,
          "30d"},
         {content_settings::features::
              kTpcdPopupHeuristicEnableForIframeInitiatorName,
          "none"},
         {content_settings::features::kTpcdWriteRedirectHeuristicGrantsName,
          "30d"},
         {content_settings::features::kTpcdRedirectHeuristicRequireABAFlowName,
          "true"},
         {content_settings::features::
              kTpcdRedirectHeuristicRequireCurrentInteractionName,
          "true"}};
const FeatureEntry::FeatureParam
    kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_AllFrameInitiator[] =
        {{content_settings::features::kTpcdReadHeuristicsGrantsName, "true"},
         {content_settings::features::
              kTpcdWritePopupCurrentInteractionHeuristicsGrantsName,
          "30d"},
         {content_settings::features::
              kTpcdPopupHeuristicEnableForIframeInitiatorName,
          "all"},
         {content_settings::features::kTpcdWriteRedirectHeuristicGrantsName,
          "15m"},
         {content_settings::features::kTpcdRedirectHeuristicRequireABAFlowName,
          "true"},
         {content_settings::features::
              kTpcdRedirectHeuristicRequireCurrentInteractionName,
          "true"}};
const FeatureEntry::FeatureParam
    kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_AllFrameInitiator[] =
        {{content_settings::features::kTpcdReadHeuristicsGrantsName, "true"},
         {content_settings::features::
              kTpcdWritePopupCurrentInteractionHeuristicsGrantsName,
          "30d"},
         {content_settings::features::
              kTpcdPopupHeuristicEnableForIframeInitiatorName,
          "all"},
         {content_settings::features::kTpcdWriteRedirectHeuristicGrantsName,
          "30d"},
         {content_settings::features::kTpcdRedirectHeuristicRequireABAFlowName,
          "true"},
         {content_settings::features::
              kTpcdRedirectHeuristicRequireCurrentInteractionName,
          "true"}};

const FeatureEntry::FeatureVariation kTpcdHeuristicsGrantsVariations[] = {
    {"CurrentInteraction_ShortRedirect_MainFrameInitiator",
     kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_MainFrameInitiator,
     nullptr},
    {"CurrentInteraction_LongRedirect_MainFrameInitiator",
     kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_MainFrameInitiator,
     nullptr},
    {"CurrentInteraction_ShortRedirect_AllFrameInitiator",
     kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_AllFrameInitiator,
     nullptr},
    {"CurrentInteraction_LongRedirect_AllFrameInitiator",
     kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_AllFrameInitiator,
     nullptr}};


const FeatureEntry::Choice kCastMirroringTargetPlayoutDelayChoices[] = {
    {flag_descriptions::kCastMirroringTargetPlayoutDelayDefault, "", ""},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay100ms,
     switches::kCastMirroringTargetPlayoutDelay, "100"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay150ms,
     switches::kCastMirroringTargetPlayoutDelay, "150"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay250ms,
     switches::kCastMirroringTargetPlayoutDelay, "250"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay300ms,
     switches::kCastMirroringTargetPlayoutDelay, "300"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay350ms,
     switches::kCastMirroringTargetPlayoutDelay, "350"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay400ms,
     switches::kCastMirroringTargetPlayoutDelay, "400"}};



#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
const flags_ui::FeatureEntry::FeatureParam kPwaNavigationCapturingDefaultOn[] =
    {{"link_capturing_state", "on_by_default"}};
const flags_ui::FeatureEntry::FeatureParam kPwaNavigationCapturingDefaultOff[] =
    {{"link_capturing_state", "off_by_default"}};
const flags_ui::FeatureEntry::FeatureParam
    kPwaNavigationCapturingReimplDefaultOn[] = {
        {"link_capturing_state", "reimpl_default_on"}};
const flags_ui::FeatureEntry::FeatureParam
    kPwaNavigationCapturingReimplDefaultOff[] = {
        {"link_capturing_state", "reimpl_default_off"}};
const flags_ui::FeatureEntry::FeatureParam
    kPwaNavigationCapturingReimplOnViaClientMode[] = {
        {"link_capturing_state", "reimpl_on_via_client_mode"}};
const flags_ui::FeatureEntry::FeatureVariation
    kPwaNavigationCapturingVariations[] = {
        {"V1, On by default", kPwaNavigationCapturingDefaultOn, nullptr},
        {"V1, Off by default", kPwaNavigationCapturingDefaultOff, nullptr},
        {"V2, On by default", kPwaNavigationCapturingReimplDefaultOn, nullptr},
        {"V2, Off by default", kPwaNavigationCapturingReimplDefaultOff,
         nullptr},
        {"V2, On by app client_mode",
         kPwaNavigationCapturingReimplOnViaClientMode, nullptr}};
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
const FeatureEntry::Choice kReplaceSyncPromosWithSignInPromosChoices[] = {
    {"Default", "", ""},
    {"Disabled", switches::kDisableFeatures,
     "ReplaceSyncPromosWithSignInPromos"},
    {"Enabled", switches::kEnableFeatures,
     "ReplaceSyncPromosWithSignInPromos:explicit_signin_for_extensions/"
     "false/explicit_signin_for_bookmarks/false"},
    {"Enabled with follow-ups", switches::kEnableFeatures,
     "ReplaceSyncPromosWithSignInPromos:explicit_signin_for_extensions/"
     "false/explicit_signin_for_bookmarks/false,UnoPhase2FollowUp"},
    {"Enabled with explicit signin for extensions and bookmarks",
     switches::kEnableFeatures,
     "ReplaceSyncPromosWithSignInPromos:explicit_signin_for_extensions/"
     "true/explicit_signin_for_bookmarks/true"},
    {"Enabled with explicit signin for extensions and bookmarks and follow-ups",
     switches::kEnableFeatures,
     "ReplaceSyncPromosWithSignInPromos:explicit_signin_for_extensions/"
     "true/explicit_signin_for_bookmarks/true,UnoPhase2FollowUp"},
};
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

const FeatureEntry::FeatureParam kLinkPreviewTriggerTypeAltClick[] = {
    {"trigger_type", "alt_click"}};
const FeatureEntry::FeatureParam kLinkPreviewTriggerTypeAltHover[] = {
    {"trigger_type", "alt_hover"}};
const FeatureEntry::FeatureParam kLinkPreviewTriggerTypeLongPress[] = {
    {"trigger_type", "long_press"}};

const FeatureEntry::FeatureVariation kLinkPreviewTriggerTypeVariations[] = {
    {"Alt + Click", kLinkPreviewTriggerTypeAltClick, nullptr},
    {"Alt + Hover", kLinkPreviewTriggerTypeAltHover, nullptr},
    {"Long Press", kLinkPreviewTriggerTypeLongPress, nullptr}};

const FeatureEntry::FeatureParam kGroupSuggestionEnableRecentlyOpenedOnly[] = {
    {"group_suggestion_enable_recently_opened", "true"},
    {"group_suggestion_enable_switch_between", "false"},
    {"group_suggestion_enable_similar_source", "false"},
    {"group_suggestion_enable_same_origin", "false"},
};
const FeatureEntry::FeatureParam kGroupSuggestionEnableSwitchBetweenOnly[] = {
    {"group_suggestion_enable_recently_opened", "false"},
    {"group_suggestion_enable_switch_between", "true"},
    {"group_suggestion_enable_similar_source", "false"},
    {"group_suggestion_enable_same_origin", "false"},
    {"group_suggestion_trigger_calculation_on_page_load", "false"},
};
const FeatureEntry::FeatureParam kGroupSuggestionEnableSimilarSourceOnly[] = {
    {"group_suggestion_enable_recently_opened", "false"},
    {"group_suggestion_enable_switch_between", "false"},
    {"group_suggestion_enable_similar_source", "true"},
    {"group_suggestion_enable_same_origin", "false"},
    {"group_suggestion_trigger_calculation_on_page_load", "false"},
};
const FeatureEntry::FeatureParam kGroupSuggestionEnableSameOriginOnly[] = {
    {"group_suggestion_enable_recently_opened", "false"},
    {"group_suggestion_enable_switch_between", "false"},
    {"group_suggestion_enable_similar_source", "false"},
    {"group_suggestion_enable_same_origin", "true"},
};
const FeatureEntry::FeatureParam kGroupSuggestionEnableTabSwitcherOnly[] = {
    {"group_suggestion_enable_tab_switcher_only", "true"},
};
const FeatureEntry::FeatureVariation kGroupSuggestionVariations[] = {
    {"Recently Opened Only", kGroupSuggestionEnableRecentlyOpenedOnly, nullptr},
    {"Switch Between Only", kGroupSuggestionEnableSwitchBetweenOnly, nullptr},
    {"Similar Source Only", kGroupSuggestionEnableSimilarSourceOnly, nullptr},
    {"Same Origin Only", kGroupSuggestionEnableSameOriginOnly, nullptr},
    {"Tab Switcher Only", kGroupSuggestionEnableTabSwitcherOnly, nullptr},
};

#if BUILDFLAG(ENABLE_COMPOSE)
// Variations of the Compose selection nudge.
const FeatureEntry::FeatureParam kComposeSelectionNudge_1[] = {
    {"selection_nudge_length", "1"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_15[] = {
    {"selection_nudge_length", "15"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_30[] = {
    {"selection_nudge_length", "30"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_30_1s[] = {
    {"selection_nudge_length", "30"},
    {"selection_nudge_delay_milliseconds", "1000"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_30_2s[] = {
    {"selection_nudge_length", "30"},
    {"selection_nudge_delay_milliseconds", "2000"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_50[] = {
    {"selection_nudge_length", "50"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_100[] = {
    {"selection_nudge_length", "100"}};

const FeatureEntry::FeatureVariation kComposeSelectionNudgeVariations[] = {
    {"1 Char", kComposeSelectionNudge_1, nullptr},
    {"15 Char", kComposeSelectionNudge_15, nullptr},
    {"30 Char", kComposeSelectionNudge_30, nullptr},
    {"50 Char", kComposeSelectionNudge_50, nullptr},
    {"100 Char", kComposeSelectionNudge_100, nullptr},
    {"30 Char - 1sec", kComposeSelectionNudge_30_1s, nullptr},
    {"30 char - 2sec", kComposeSelectionNudge_30_2s, nullptr}};
#endif  // ENABLE_COMPOSE

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
const FeatureEntry::FeatureParam kLocationProviderManagerModeNetworkOnly[] = {
    {"LocationProviderManagerMode", "NetworkOnly"}};
const FeatureEntry::FeatureParam kLocationProviderManagerModePlatformOnly[] = {
    {"LocationProviderManagerMode", "PlatformOnly"}};
const FeatureEntry::FeatureParam kLocationProviderManagerModeHybridPlatform[] =
    {{"LocationProviderManagerMode", "HybridPlatform"}};
const FeatureEntry::FeatureParam kLocationProviderManagerModeHybridPlatform2[] =
    {{"LocationProviderManagerMode", "HybridPlatform2"}};

const FeatureEntry::FeatureVariation kLocationProviderManagerVariations[] = {
    {"Network only", kLocationProviderManagerModeNetworkOnly, nullptr},
    {"Platform only", kLocationProviderManagerModePlatformOnly, nullptr},
    {"Wi-Fi fallback", kLocationProviderManagerModeHybridPlatform, nullptr},
    {"Fallback on error", kLocationProviderManagerModeHybridPlatform2,
     nullptr}};
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

const FeatureEntry::FeatureParam kVisitedURLRankingDomainDeduplicationParam[] =
    {{"url_deduplication_include_title", "false"}};

const FeatureEntry::FeatureParam
    kVisitedURLRankingDomainDeduplicationIncludeQueryParam[] = {
        {"url_deduplication_include_title", "false"},
        {"url_deduplication_fallback", "false"}};

const FeatureEntry::FeatureParam
    kVisitedURLRankingDomainDeduplicationIncludePathQueryParam[] = {
        {"url_deduplication_include_title", "false"},
        {"url_deduplication_clear_path", "false"},
        {"url_deduplication_fallback", "false"}};

const FeatureEntry::FeatureVariation
    kVisitedURLRankingDomainDeduplicationVariations[] = {
        {"- Domain Deduplication", kVisitedURLRankingDomainDeduplicationParam,
         nullptr},
        {"- Domain Deduplication - Include Query",
         kVisitedURLRankingDomainDeduplicationIncludeQueryParam, nullptr},
        {"- Domain Deduplication - Include Path and Query",
         kVisitedURLRankingDomainDeduplicationIncludePathQueryParam, nullptr}};

#if BUILDFLAG(ENABLE_EXTENSIONS)
constexpr char kExtensionAiDataInternalName[] =
    "enable-extension-ai-data-collection";
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

const FeatureEntry::FeatureParam kDiscountOnShoppyPage[] = {
    {commerce::kDiscountOnShoppyPageParam, "true"}};

const FeatureEntry::FeatureVariation kDiscountsVariations[] = {
    {"Discount on Shoppy page", kDiscountOnShoppyPage, nullptr}};


const FeatureEntry::FeatureParam kSkiaGraphite_ValidationEnabled[] = {
    {"dawn_skip_validation", "false"}};
const FeatureEntry::FeatureParam kSkiaGraphite_ValidationDisabled[] = {
    {"dawn_skip_validation", "true"}};
const FeatureEntry::FeatureParam kSkiaGraphite_DebugLabelsEnabled[] = {
    {"dawn_backend_debug_labels", "true"}};

const FeatureEntry::FeatureVariation kSkiaGraphiteVariations[] = {
    {"dawn frontend validation enabled", kSkiaGraphite_ValidationEnabled,
     nullptr},
    {"dawn frontend validation disabled", kSkiaGraphite_ValidationDisabled,
     nullptr},
    {"dawn debug labels enabled", kSkiaGraphite_DebugLabelsEnabled, nullptr},
};


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::FeatureParam kContextualCueingEnabledNoEngagementCap[] = {
    {"BackoffTime", "0h"},
    {"BackoffMultiplierBase", "0.0"},
    {"NudgeCapTime", "0h"},
    {"NudgeCapTimePerDomain", "0h"},
    {"MinPageCountBetweenNudges", "0"},
    {"MinTimeBetweenNudges", "0h"}};
const FeatureEntry::FeatureVariation kContextualCueingEnabledOptions[] = {
    {"no engagement caps", kContextualCueingEnabledNoEngagementCap, nullptr},
};
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)

#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
const FeatureEntry::FeatureParam
    kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserOnly[] = {
        {"enabled-processes", "browser-only"}};
const FeatureEntry::FeatureParam
    kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserAndRenderer[] = {
        {"enabled-processes", "browser-and-renderer"}};
const FeatureEntry::FeatureParam
    kPartitionAllocWithAdvancedChecksEnabledProcesses_NonRenderer[] = {
        {"enabled-processes", "non-renderer"}};
const FeatureEntry::FeatureParam
    kPartitionAllocWithAdvancedChecksEnabledProcesses_AllProcesses[] = {
        {"enabled-processes", "all-processes"}};
const FeatureEntry::FeatureVariation
    kPartitionAllocWithAdvancedChecksEnabledProcessesOptions[] = {
        {"on browser process only",
         kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserOnly,
         nullptr},
        {"on browser and renderer processes",
         kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserAndRenderer,
         nullptr},
        {"on non renderer processes",
         kPartitionAllocWithAdvancedChecksEnabledProcesses_NonRenderer,
         nullptr},
        {"on all processes",
         kPartitionAllocWithAdvancedChecksEnabledProcesses_AllProcesses,
         nullptr}};
#endif  // PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

        // PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

const FeatureEntry::FeatureParam kAudioDuckingAttenuation_60[] = {
    {"attenuation", "60"}};
const FeatureEntry::FeatureParam kAudioDuckingAttenuation_70[] = {
    {"attenuation", "70"}};
const FeatureEntry::FeatureParam kAudioDuckingAttenuation_80[] = {
    {"attenuation", "80"}};
const FeatureEntry::FeatureParam kAudioDuckingAttenuation_90[] = {
    {"attenuation", "90"}};
const FeatureEntry::FeatureParam kAudioDuckingAttenuation_100[] = {
    {"attenuation", "100"}};

const FeatureEntry::FeatureVariation kAudioDuckingAttenuationVariations[] = {
    {"attenuation 60", kAudioDuckingAttenuation_60, nullptr},
    {"attenuation 70", kAudioDuckingAttenuation_70, nullptr},
    {"attenuation 80", kAudioDuckingAttenuation_80, nullptr},
    {"attenuation 90", kAudioDuckingAttenuation_90, nullptr},
    {"attenuation 100", kAudioDuckingAttenuation_100, nullptr}};

const char kAccountStoragePrefsThemesAndSearchEnginesFeatures[] =
    // Flags for account storage of prefs.
    "EnablePreferencesAccountStorage,"
    // Flags for account storage of search engines.
    "DisableSyncAutogeneratedSearchEngines,"
    "SeparateLocalAndAccountSearchEngines,"
    // Flags for account storage of themes.
    "SeparateLocalAndAccountThemes,"
    "ThemesBatchUpload";

const FeatureEntry::Choice kAccountStoragePrefsThemesAndSearchEnginesChoices[] =
    {{"Default", "", ""},
     {"Disabled", "disable-features",
      kAccountStoragePrefsThemesAndSearchEnginesFeatures},
     {"Enabled", "enable-features",
      kAccountStoragePrefsThemesAndSearchEnginesFeatures}};



const FeatureEntry::FeatureParam
    kStandardBoundSessionCredentialsForDevelopers[] = {
        {"RequireOriginTrialTokens", "false"},
        {"RefreshQuota", "false"},
        {"CheckSubdomainRegistration", "false"},
        {"OriginTrialFeedback", "true"},
        {"SchemaVersion", "2"}};

const FeatureEntry::FeatureVariation
    kStandardBoundSessionCredentialsVariations[] = {
        {"- For developers", kStandardBoundSessionCredentialsForDevelopers,
         nullptr}};

const FeatureEntry::FeatureParam
    kStandardBoundSessionCredentialsFederatedSessionsForDevelopers[] = {
        {"CheckWellKnown", "false"}};

const FeatureEntry::FeatureVariation
    kStandardBoundSessionCredentialsFederatedSessionsVariations[] = {
        {"- For developers",
         kStandardBoundSessionCredentialsFederatedSessionsForDevelopers,
         nullptr}};

// LINT.IfChange(AutofillVcnEnrollStrikeExpiryTime)
const FeatureEntry::FeatureParam kAutofillVcnEnrollStrikeExpiryTime_120Days[] =
    {{"autofill_vcn_strike_expiry_time_days", "120"}};

const FeatureEntry::FeatureParam kAutofillVcnEnrollStrikeExpiryTime_60Days[] = {
    {"autofill_vcn_strike_expiry_time_days", "60"}};

const FeatureEntry::FeatureParam kAutofillVcnEnrollStrikeExpiryTime_30Days[] = {
    {"autofill_vcn_strike_expiry_time_days", "30"}};

const FeatureEntry::FeatureVariation
    kAutofillVcnEnrollStrikeExpiryTimeOptions[] = {
        {"120 days", kAutofillVcnEnrollStrikeExpiryTime_120Days, nullptr},
        {"60 days", kAutofillVcnEnrollStrikeExpiryTime_60Days, nullptr},
        {"30 days", kAutofillVcnEnrollStrikeExpiryTime_30Days, nullptr}};
// LINT.ThenChange(//ios/chrome/browser/flags/about_flags.mm:AutofillVcnEnrollStrikeExpiryTime)

// Variations of the glic panel reset for the top Chrome button.
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_1s[] = {
    {"glic-panel-reset-delay-ms", "1000"}};
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_2s[] = {
    {"glic-panel-reset-delay-ms", "2000"}};
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_3s[] = {
    {"glic-panel-reset-delay-ms", "3000"}};
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_5s[] = {
    {"glic-panel-reset-delay-ms", "3000"}};
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_10s[] = {
    {"glic-panel-reset-delay-ms", "10000"}};

const FeatureEntry::FeatureVariation
    kGlicPanelResetTopChromeButtonVariations[] = {
        {"Reset on open - 1s", kGlicPanelResetTopChromeButtonOnOpen_1s,
         nullptr},
        {"Reset on open - 2s", kGlicPanelResetTopChromeButtonOnOpen_2s,
         nullptr},
        {"Reset on open - 3s", kGlicPanelResetTopChromeButtonOnOpen_3s,
         nullptr},
        {"Reset on open - 5s", kGlicPanelResetTopChromeButtonOnOpen_5s,
         nullptr},
        {"Reset on open - 10s", kGlicPanelResetTopChromeButtonOnOpen_10s,
         nullptr}};

const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_0h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "0"},
};

const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_5min[] = {
    {"glic-panel-reset-session-timeout-delay-h", "0.084"},
};

const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_30min[] = {
    {"glic-panel-reset-session-timeout-delay-h", "0.5"},
};

const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_1h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "1"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_2h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "2"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_4h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "4"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_12h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "12"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_24h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "24"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_48h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "48"},
};

const FeatureEntry::FeatureVariation
    kGlicPanelResetOnSessionTimeoutVariations[] = {
        {"Always Restart (0min)", kGlicPanelResetOnSessionTimeout_0h, nullptr},
        {"Reset after 5min", kGlicPanelResetOnSessionTimeout_5min, nullptr},
        {"Reset after 30min", kGlicPanelResetOnSessionTimeout_30min, nullptr},
        {"Reset after 1h", kGlicPanelResetOnSessionTimeout_1h, nullptr},
        {"Reset after 2h", kGlicPanelResetOnSessionTimeout_2h, nullptr},
        {"Reset after 4h", kGlicPanelResetOnSessionTimeout_4h, nullptr},
        {"Reset after 12h", kGlicPanelResetOnSessionTimeout_12h, nullptr},
        {"Reset after 24h", kGlicPanelResetOnSessionTimeout_24h, nullptr},
        {"Reset after 48h", kGlicPanelResetOnSessionTimeout_48h, nullptr}};

// Variations on pre-warming delays.
const FeatureEntry::FeatureParam kGlicWarmingShorterDelays[] = {
    {"glic-warming-delay-ms", "5000"},
    {"glic-panel-reset-delay-ms", "2000"}};

const FeatureEntry::FeatureVariation kGlicWarmingVariations[] = {
    {"with shorter delays", kGlicWarmingShorterDelays, nullptr}};

const char kGlicEntrypointVariationsShowLabel[] =
    "glic-entrypoint-variations-show-label";
const char kGlicEntrypointVariationsAltIcon[] =
    "glic-entrypoint-variations-alt-icon";
const char kGlicEntrypointVariationsHighlightNudge[] =
    "glic-entrypoint-variations-highlight-nudge";
const FeatureEntry::FeatureParam kGlicEntrypointVariationsHighlightOnly[] = {
    {kGlicEntrypointVariationsHighlightNudge, "true"},
    {kGlicEntrypointVariationsAltIcon, "false"},
    {kGlicEntrypointVariationsShowLabel, "false"},
};
const FeatureEntry::FeatureParam kGlicEntrypointVariationsLabelOnly[] = {
    {kGlicEntrypointVariationsHighlightNudge, "false"},
    {kGlicEntrypointVariationsAltIcon, "false"},
    {kGlicEntrypointVariationsShowLabel, "true"},
};
const FeatureEntry::FeatureParam kGlicEntrypointVariationsLabelAndHighlight[] =
    {
        {kGlicEntrypointVariationsHighlightNudge, "true"},
        {kGlicEntrypointVariationsAltIcon, "false"},
        {kGlicEntrypointVariationsShowLabel, "true"},
};
const FeatureEntry::FeatureParam kGlicEntrypointVariationsLabelAndIcon[] = {
    {kGlicEntrypointVariationsHighlightNudge, "false"},
    {kGlicEntrypointVariationsAltIcon, "true"},
    {kGlicEntrypointVariationsShowLabel, "true"},
};
const FeatureEntry::FeatureParam
    kGlicEntrypointVariationsLabelAndIconAndHighlight[] = {
        {kGlicEntrypointVariationsHighlightNudge, "true"},
        {kGlicEntrypointVariationsAltIcon, "true"},
        {kGlicEntrypointVariationsShowLabel, "true"},
};

const FeatureEntry::FeatureVariation kGlicEntrypointVariations[] = {
    {"highlight nudge only", kGlicEntrypointVariationsHighlightOnly, nullptr},
    {"label only", kGlicEntrypointVariationsLabelOnly, nullptr},
    {"label, highlight nudge", kGlicEntrypointVariationsLabelAndHighlight,
     nullptr},
    {"label, alt icon", kGlicEntrypointVariationsLabelAndIcon, nullptr},
    {"label, icon, highlight nudge",
     kGlicEntrypointVariationsLabelAndIconAndHighlight, nullptr},
};

const FeatureEntry::FeatureParam kGlicButtonPressedStateForceSolidIcon[] = {
    {"glic-button-pressed-force-solid-icon", "true"}};

const FeatureEntry::FeatureVariation kGlicButtonPressedStateVariations[] = {
    {"force solid color icon when pressed",
     kGlicButtonPressedStateForceSolidIcon, nullptr}};

const FeatureEntry::FeatureParam kGlicButtonAltLabelVariant0[] = {
    {"glic-button-alt-label-variant", "0"}};
const FeatureEntry::FeatureParam kGlicButtonAltLabelVariant1[] = {
    {"glic-button-alt-label-variant", "1"}};
const FeatureEntry::FeatureParam kGlicButtonAltLabelVariant2[] = {
    {"glic-button-alt-label-variant", "2"}};

const FeatureEntry::FeatureVariation kGlicButtonAltLabelVariations[] = {
    {"A", kGlicButtonAltLabelVariant0, nullptr},
    {"B", kGlicButtonAltLabelVariant1, nullptr},
    {"C", kGlicButtonAltLabelVariant2, nullptr}};

const FeatureEntry::FeatureParam kGlicTrustFirstOnboardingArm1Params[] = {
    {"arm", "1"}};
const FeatureEntry::FeatureParam kGlicTrustFirstOnboardingArm2Params[] = {
    {"arm", "2"}};
const FeatureEntry::FeatureParam kGlicTrustFirstOnboardingArm3Params[] = {
    {"arm", "3"}};

const FeatureEntry::FeatureVariation kGlicTrustFirstOnboardingVariations[] = {
    {"- Arm 1: Start Chat", kGlicTrustFirstOnboardingArm1Params, nullptr},
    {"- Arm 2: Welcome Screen", kGlicTrustFirstOnboardingArm2Params, nullptr},
    {"- Arm 3: In-Flow opt-in", kGlicTrustFirstOnboardingArm3Params, nullptr},
};

const FeatureEntry::Choice kGlicSetG1ForMultiInstance[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {"Force G1 status", switches::kGlicForceG1StatusForMultiInstance, "true"},
    {"Force non-G1 status", switches::kGlicForceG1StatusForMultiInstance,
     "false"},
};

const FeatureEntry::FeatureParam kGlicGuestUrlPresetTypeAutopush[] = {
    {"glic-guest-url-preset-type", "0"}};
const FeatureEntry::FeatureParam kGlicGuestUrlPresetTypeStaging[] = {
    {"glic-guest-url-preset-type", "1"}};
const FeatureEntry::FeatureParam kGlicGuestUrlPresetTypePreprod[] = {
    {"glic-guest-url-preset-type", "2"}};
const FeatureEntry::FeatureParam kGlicGuestUrlPresetTypeProd[] = {
    {"glic-guest-url-preset-type", "3"}};

const FeatureEntry::FeatureVariation kGlicGuestUrlPresetTypes[] = {
    {"Auto-push", kGlicGuestUrlPresetTypeAutopush, nullptr},
    {"Staging", kGlicGuestUrlPresetTypeStaging, nullptr},
    {"Pre-prod", kGlicGuestUrlPresetTypePreprod, nullptr},
    {"Prod", kGlicGuestUrlPresetTypeProd, nullptr}};

const FeatureEntry::FeatureParam kAutofillShowTypePredictionsAsTitle[] = {
    {"as-title", "true"}};
const FeatureEntry::FeatureVariation kAutofillShowTypePredictionsVariations[] =
    {{"- show predictions as title", kAutofillShowTypePredictionsAsTitle,
      nullptr}};

const FeatureEntry::FeatureParam
    kInvalidateChoiceOnRestoreIsRetroactiveOption[] = {
        {"is_retroactive", "true"}};
const FeatureEntry::FeatureVariation
    kInvalidateSearchEngineChoiceOnRestoreVariations[] = {
        {"(retroactive)", kInvalidateChoiceOnRestoreIsRetroactiveOption,
         nullptr}};

const FeatureEntry::FeatureParam kAILangsParam[] = {{"langs", "*"}};

const FeatureEntry::FeatureVariation kAILangsVariation[] = {
    {"Multilingual", kAILangsParam, nullptr},
};



const FeatureEntry::FeatureParam kLensOverlayEduActionChipAllPages[] = {
    {"url-allow-filters", "[\"*\"]"},
    {"url-path-forced-allowed-match-patterns", "[\".\"]"},
    {"disabled-by-glic", "false"},
};

const FeatureEntry::FeatureParam kLensOverlayEduActionChipHomework[] = {
    {"url-allow-filters", "[\"*\"]"},
    {"url-path-match-allow-filters", "[\"(?i)homework\"]"},
    {"disabled-by-glic", "false"},
};

const FeatureEntry::FeatureVariation kLensOverlayEduActionChipVariations[] = {
    {"trigger on \"homework\"", kLensOverlayEduActionChipHomework, nullptr},
    {"force trigger all pages", kLensOverlayEduActionChipAllPages, nullptr},
};

const FeatureEntry::FeatureParam kLensOverlayEntrypointLabelAlt1[] = {
    {"id", "1"},
};

const FeatureEntry::FeatureParam kLensOverlayEntrypointLabelAlt2[] = {
    {"id", "2"},
};

const FeatureEntry::FeatureParam kLensOverlayEntrypointLabelAlt3[] = {
    {"id", "3"},
};

const FeatureEntry::FeatureVariation
    kLensOverlayEntrypointLabelAltVariations[] = {
        {"Ask Google about this page", kLensOverlayEntrypointLabelAlt1,
         nullptr},
        {"Ask Google Lens about this page", kLensOverlayEntrypointLabelAlt2,
         nullptr},
        {"Search this page with Google Lens", kLensOverlayEntrypointLabelAlt3,
         nullptr},
};

const FeatureEntry::FeatureParam kEnableNtpBrowserPromosVariationSimple[] = {
    {"promo-type", "simple"}};

const FeatureEntry::FeatureParam kEnableNtpBrowserPromosVariationSetupList[] = {
    {"promo-type", "setuplist"}};

const FeatureEntry::FeatureVariation kEnableNtpBrowserPromosVariations[] = {
    {"Single-promo", kEnableNtpBrowserPromosVariationSimple},
    {"Setup List", kEnableNtpBrowserPromosVariationSetupList},
};

// LINT.IfChange(DataSharingVersioningChoices)
const FeatureEntry::Choice kDataSharingVersioningStateChoices[] = {
    {"Default", "", ""},
    {flag_descriptions::kDataSharingSharedDataTypesEnabled,
     switches::kEnableFeatures, "SharedDataTypesKillSwitch"},
    {flag_descriptions::kDataSharingSharedDataTypesEnabledWithUi,
     switches::kEnableFeatures,
     "SharedDataTypesKillSwitch,DataSharingEnableUpdateChromeUI"},
    {"Disabled", switches::kDisableFeatures,
     "SharedDataTypesKillSwitch, DataSharingEnableUpdateChromeUI"},
};
// LINT.ThenChange(//ios/chrome/browser/flags/about_flags.mm:DataSharingVersioningChoices)

const FeatureEntry::FeatureParam
    kDiskCacheBackendExperimentVariations_Default[] = {{"backend", "default"}};
const FeatureEntry::FeatureParam
    kDiskCacheBackendExperimentVariations_Simple[] = {{"backend", "simple"}};
// Block file backend is not supported on Android to reduce the binary size.
const FeatureEntry::FeatureParam
    kDiskCacheBackendExperimentVariations_Blockfile[] = {
        {"backend", "blockfile"}};
const FeatureEntry::FeatureParam kDiskCacheBackendExperimentVariations_Sql[] = {
    {"backend", "sql"}};

const FeatureEntry::FeatureVariation kDiskCacheBackendExperimentVariations[] = {
    {"default backend", kDiskCacheBackendExperimentVariations_Default, nullptr},
    {"simple backend", kDiskCacheBackendExperimentVariations_Simple, nullptr},
    {"blockfile backend", kDiskCacheBackendExperimentVariations_Blockfile,
     nullptr},
    {"experimental sql backend", kDiskCacheBackendExperimentVariations_Sql,
     nullptr}};

const FeatureEntry::FeatureParam
    kSafetyHubDisruptiveNotificationRevocationVariations_RevokeAll[] = {
        {"shadow_run", "false"},
        {"max_engagement_score", "100.0"},
        {"min_notification_count", "0"},
        {"waiting_time_as_proposed", "0d"},
        {"waiting_for_metrics_days", "0"}};
const FeatureEntry::FeatureParam
    kSafetyHubDisruptiveNotificationRevocationVariations_Moderate[] = {
        {"shadow_run", "false"},
        {"max_engagement_score", "0.0"},
        {"min_notification_count", "4"},
        {"waiting_time_as_proposed", "4d"},
        {"waiting_for_metrics_days", "0"}};
const FeatureEntry::FeatureVariation
    kSafetyHubDisruptiveNotificationRevocationVariations[] = {
        {"- Revoke all for testing",
         kSafetyHubDisruptiveNotificationRevocationVariations_RevokeAll,
         nullptr},
        {"- Moderate",
         kSafetyHubDisruptiveNotificationRevocationVariations_Moderate,
         nullptr},
};





constexpr char kWebiumFlag[] = "webium";
constexpr char kWebiumFeatures[] =
    "Webium,AttachUnownedInnerWebContents,ExtensionsMenuAccessControl";

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::FeatureParam kNtpCustomizeChromeAutoOpenOnEveryNTP[] = {
    {"max_customize_chrome_auto_shown_count", "5"},
    {"max_customize_chrome_auto_shown_session_count", "5"}};
const FeatureEntry::FeatureParam kNtpCustomizeChromeAutoOpenOnFirstNTPOnly[] = {
    {"max_customize_chrome_auto_shown_count", "5"},
    {"max_customize_chrome_auto_shown_session_count", "1"}};
const FeatureEntry::FeatureParam kNtpCustomizeChromeAutoOpenIPHOnly[] = {
    {"max_customize_chrome_auto_shown_count", "0"},
    {"max_customize_chrome_auto_shown_session_count", "0"}};
const FeatureEntry::FeatureVariation kNtpCustomizeChromeAutoOpenVariations[] = {
    {"- On every NTP", kNtpCustomizeChromeAutoOpenOnEveryNTP, nullptr},
    {"- First NTP only", kNtpCustomizeChromeAutoOpenOnFirstNTPOnly, nullptr},
    {"- IPH only", kNtpCustomizeChromeAutoOpenIPHOnly, nullptr},
};
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
        // BUILDFLAG(IS_CHROMEOS)

const FeatureEntry::FeatureParam kPolicyRegistrationDelay2m[] = {
    {"PolicyRegistrationDelay", "2m"}};
const FeatureEntry::FeatureParam kPolicyRegistrationDelay1h[] = {
    {"PolicyRegistrationDelay", "1h"}};
const FeatureEntry::FeatureParam kPolicyRegistrationDelay6h[] = {
    {"PolicyRegistrationDelay", "6h"}};
const FeatureEntry::FeatureParam kPolicyRegistrationDelay12h[] = {
    {"PolicyRegistrationDelay", "12h"}};
const FeatureEntry::FeatureParam kPolicyRegistrationDelay24h[] = {
    {"PolicyRegistrationDelay", "24h"}};

const FeatureEntry::FeatureVariation kPolicyRegistrationDelayVariations[] = {
    {"2 minutes", kPolicyRegistrationDelay2m, nullptr},
    {"1 hour", kPolicyRegistrationDelay1h, nullptr},
    {"6 hours", kPolicyRegistrationDelay6h, nullptr},
    {"12 hours", kPolicyRegistrationDelay12h, nullptr},
    {"24 hours", kPolicyRegistrationDelay24h, nullptr},
};

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
const FeatureEntry::FeatureParam kPolicyDisclaimerRegistrationRetryDelay2m[] = {
    {"PolicyDisclaimerRegistrationRetryDelay", "2m"}};
const FeatureEntry::FeatureParam kPolicyDisclaimerRegistrationRetryDelay1h[] = {
    {"PolicyDisclaimerRegistrationRetryDelay", "1h"}};
const FeatureEntry::FeatureParam kPolicyDisclaimerRegistrationRetryDelay6h[] = {
    {"PolicyDisclaimerRegistrationRetryDelay", "6h"}};
const FeatureEntry::FeatureParam kPolicyDisclaimerRegistrationRetryDelay12h[] =
    {{"PolicyDisclaimerRegistrationRetryDelay", "12h"}};
const FeatureEntry::FeatureParam kPolicyDisclaimerRegistrationRetryDelay24h[] =
    {{"PolicyDisclaimerRegistrationRetryDelay", "24h"}};

const FeatureEntry::FeatureVariation
    kPolicyDisclaimerRegistrationRetryDelayVariations[] = {
        {"Enabled - Retry every 2 minutes",
         kPolicyDisclaimerRegistrationRetryDelay2m, nullptr},
        {"Enabled - Retry every 1 hour",
         kPolicyDisclaimerRegistrationRetryDelay1h, nullptr},
        {"Enabled - Retry every 6 hours",
         kPolicyDisclaimerRegistrationRetryDelay6h, nullptr},
        {"Enabled - Retry every 12 hours",
         kPolicyDisclaimerRegistrationRetryDelay12h, nullptr},
        {"Enabled - Retry every 24 hours",
         kPolicyDisclaimerRegistrationRetryDelay24h, nullptr},
};

const FeatureEntry::FeatureParam
    kOAuthMultiloginCookieBindingWithoutEnforcement[] = {{"enforced", "false"}};
const FeatureEntry::FeatureParam
    kOAuthMultiloginCookieBindingWithEnforcement[] = {{"enforced", "true"}};

const FeatureEntry::FeatureVariation
    kOAuthMultiloginCookieBindingEnforcementVariations[] = {
        {"without enforcement", kOAuthMultiloginCookieBindingWithoutEnforcement,
         nullptr},
        {"with enforcement", kOAuthMultiloginCookieBindingWithEnforcement,
         nullptr},
};
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::FeatureParam kProjectsPanelWithoutThreadsVariation[] = {
    {"include_threads_in_projects_panel", "false"}};
const FeatureEntry::FeatureParam kProjectsPanelWithThreadsVariation[] = {
    {"include_threads_in_projects_panel", "true"}};

const FeatureEntry::FeatureVariation kProjectsPanelVariations[] = {
    {"without threads", kProjectsPanelWithoutThreadsVariation, nullptr},
    {"with threads", kProjectsPanelWithThreadsVariation, nullptr}};
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
        // BUILDFLAG(IS_CHROMEOS)


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
const FeatureEntry::FeatureParam kProfilePickerTextVariation1[] = {
    {"profile-picker-variation", "keep-work-and-life-separate"}};
const FeatureEntry::FeatureParam kProfilePickerTextVariation2[] = {
    {"profile-picker-variation", "got-another-google-account"}};
const FeatureEntry::FeatureParam kProfilePickerTextVariation3[] = {
    {"profile-picker-variation", "keep-tasks-separate"}};
const FeatureEntry::FeatureParam kProfilePickerTextVariation4[] = {
    {"profile-picker-variation", "sharing-a-computer"}};
const FeatureEntry::FeatureParam kProfilePickerTextVariation5[] = {
    {"profile-picker-variation", "keep-everything-in-chrome"}};

const FeatureEntry::FeatureVariation kProfilePickerTextVariations[] = {
    {"V1: Keep work and life separate", kProfilePickerTextVariation1, nullptr},
    {"V2: Got another Google Account?", kProfilePickerTextVariation2, nullptr},
    {"V3: Keep school, side projects, and other tasks separate",
     kProfilePickerTextVariation3, nullptr},
    {"V4: Sharing a computer?", kProfilePickerTextVariation4, nullptr},
    {"V5: Keep everything in Chrome", kProfilePickerTextVariation5, nullptr},
};

const FeatureEntry::FeatureParam kDisableU18FeedbackDesktopForced[] = {
    {"state", "forced"}};
const FeatureEntry::FeatureVariation kDisableU18FeedbackDesktopVariations[] = {
    {"Forced", kDisableU18FeedbackDesktopForced, nullptr},
};
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)


// LINT.IfChange(ContextualTasksArms)
const FeatureEntry::FeatureParam kArm1FullBundleWithExpandoButton[] = {
    {"ContextualTasksExpandButtonOptions", "side-panel-expand-button"}};
const FeatureEntry::FeatureParam
    kArm2FullBundleNoAutoSidePanelOpenWithExpandoButton[] = {
        {"ContextualTasksExpandButtonOptions", "side-panel-expand-button"},
        {"ContextualTasksOpenSidePanelOnLinkClicked", "false"}};
const FeatureEntry::FeatureParam
    kArm3FullBundleWithoutLensMigrationWithExpandoButton[] = {
        {"ContextualTasksExpandButtonOptions", "side-panel-expand-button"},
        {"ContextualTasksEnableLensInContextualTasks", "false"}};
const FeatureEntry::FeatureParam
    kArm4FullBundleNoAutoAddedContextInSidePanelWithExpandoButton[] = {
        {"ContextualTasksExpandButtonOptions", "side-panel-expand-button"},
        {"ContextualTasksTabAutoSuggestionChipEnabled", "false"}};
const FeatureEntry::FeatureParam kArm5FullBundleWithCloseToExpandButton[] = {
    {"ContextualTasksExpandButtonOptions", "toolbar-close-button"}};
const FeatureEntry::FeatureParam
    kArm6FullBundleWithoutLensMigrationWithCloseToExpandButton[] = {
        {"ContextualTasksExpandButtonOptions", "toolbar-close-button"},
        {"ContextualTasksEnableLensInContextualTasks", "false"}};
const FeatureEntry::FeatureParam
    kArm7FullBundleNoAutoAddedContextInSidePanelWithCloseToExpandButton[] = {
        {"ContextualTasksExpandButtonOptions", "toolbar-close-button"},
        {"ContextualTasksTabAutoSuggestionChipEnabled", "false"}};

const FeatureEntry::FeatureVariation kContextualTasksVariations[] = {
    {"Arm 1: Full bundle with expando button", kArm1FullBundleWithExpandoButton,
     nullptr},
    {"Arm 2: Full bundle, no auto side panel open, expando button",
     kArm2FullBundleNoAutoSidePanelOpenWithExpandoButton, nullptr},
    {"Arm 3: Full bundle, without Lens migration, expando button",
     kArm3FullBundleWithoutLensMigrationWithExpandoButton, nullptr},
    {"Arm 4: Full bundle, No auto added context in side panel, expando button",
     kArm4FullBundleNoAutoAddedContextInSidePanelWithExpandoButton, nullptr},
    {"Arm 5: Full bundle with close to expand button",
     kArm5FullBundleWithCloseToExpandButton, nullptr},
    {"Arm 6: Full bundle, without Lens migration, close to expand button",
     kArm6FullBundleWithoutLensMigrationWithCloseToExpandButton, nullptr},
    {"Arm 7: Full bundle, No auto added context in side panel, close to expand "
     "button",
     kArm7FullBundleNoAutoAddedContextInSidePanelWithCloseToExpandButton,
     nullptr}};
// LINT.ThenChange(chrome/browser/contextual_tasks/contextual_tasks_side_panel_coordinator.cc)

const FeatureEntry::FeatureParam kTaskScopedSidePanel[] = {
    {"ContextualTasksTaskScopedSidePanel", "true"}};
const FeatureEntry::FeatureParam kTabScopedSidePanel[] = {
    {"ContextualTasksTaskScopedSidePanel", "false"}};

const FeatureEntry::FeatureVariation kContextualTaskContextVariations[] = {
    {" - Task scoped side panel", kTaskScopedSidePanel, nullptr},
    {" - Tab scoped side panel", kTabScopedSidePanel, nullptr}};




const FeatureEntry::FeatureParam
    kDeviceBoundSessionsForRestrictedSitesExperimentIdFromFlags[] = {
        {"Value", "set_from_flags"}};
const FeatureEntry::FeatureVariation
    kDeviceBoundSessionsForRestrictedSitesExperimentIdVariations[] = {
        {"- set_from_flags",
         kDeviceBoundSessionsForRestrictedSitesExperimentIdFromFlags, nullptr}};

const FeatureEntry::FeatureParam
    kCastStreamingExponentialVideoBitrateAlgorithmDefault[] = {
        {"window_size", "30"},
        {"drop_threshold", "1"},
        {"increase_factor", "1.05"},
        {"decrease_factor", "0.9"},
        {"dynamic_window_multiplier", "0.0"},
};

const FeatureEntry::FeatureParam
    kCastStreamingExponentialVideoBitrateAlgorithmAggressive[] = {
        {"window_size", "30"},
        {"drop_threshold", "1"},
        {"increase_factor", "1.1"},
        {"decrease_factor", "0.9"},
        {"dynamic_window_multiplier", "0.0"},
};

const FeatureEntry::FeatureParam
    kCastStreamingExponentialVideoBitrateAlgorithmConservative[] = {
        {"window_size", "30"},
        {"drop_threshold", "1"},
        {"increase_factor", "1.05"},
        {"decrease_factor", "0.8"},
        {"dynamic_window_multiplier", "0.0"},
};

const FeatureEntry::FeatureParam
    kCastStreamingExponentialVideoBitrateAlgorithmLargeWindow[] = {
        {"window_size", "60"},
        {"drop_threshold", "2"},
        {"increase_factor", "1.05"},
        {"decrease_factor", "0.9"},
        {"dynamic_window_multiplier", "0.0"},
};

const FeatureEntry::FeatureVariation
    kCastStreamingExponentialVideoBitrateAlgorithmVariations[] = {
        {"Default (30, 1, 1.05, 0.9)",
         kCastStreamingExponentialVideoBitrateAlgorithmDefault, nullptr},
        {"Aggressive Increase (1.1)",
         kCastStreamingExponentialVideoBitrateAlgorithmAggressive, nullptr},
        {"Conservative Decrease (0.8)",
         kCastStreamingExponentialVideoBitrateAlgorithmConservative, nullptr},
        {"Large Window (60)",
         kCastStreamingExponentialVideoBitrateAlgorithmLargeWindow, nullptr},
};

const FeatureEntry::FeatureParam
    kPermissionsGestureGatedPromptsMuteNotifications[] = {
        {"mute_notifications", "true"}};
const FeatureEntry::FeatureParam
    kPermissionsGestureGatedPromptsMuteGeolocation[] = {
        {"mute_geolocation", "true"}};
const FeatureEntry::FeatureParam kPermissionsGestureGatedPromptsMuteBoth[] = {
    {"mute_notifications", "true"},
    {"mute_geolocation", "true"}};

const FeatureEntry::FeatureVariation
    kPermissionsGestureGatedPromptsVariations[] = {
        {"Mute Notifications", kPermissionsGestureGatedPromptsMuteNotifications,
         nullptr},
        {"Mute Geolocation", kPermissionsGestureGatedPromptsMuteGeolocation,
         nullptr},
        {"Mute Both", kPermissionsGestureGatedPromptsMuteBoth, nullptr},
};

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
const FeatureEntry::FeatureParam kSearchEngineExplicitChoiceDialogEscapable[] =
    {{"escapable", "true"}};

const FeatureEntry::FeatureVariation
    kSearchEngineExplicitChoiceDialogVariations[] = {
        {"Escapable", kSearchEngineExplicitChoiceDialogEscapable, nullptr},
};
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
const FeatureEntry::FeatureParam kSigninPromoOnAvatarPillShortDelays[] = {
    {"startup_delay_for_promo_show", "3s"},
    {"delay_for_next_promo_allowed", "15s"},
};

const FeatureEntry::FeatureVariation kSigninPromoOnAvatarPillVariation[] = {
    {"Short delays (for testing)", kSigninPromoOnAvatarPillShortDelays,
     nullptr},
};
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

// RECORDING USER METRICS FOR FLAGS:
// -----------------------------------------------------------------------------
// The first line of the entry is the internal name.
//
// To add a new entry, add to the end of kFeatureEntries. There are two
// distinct types of entries:
// . SINGLE_VALUE: entry is either on or off. Use the SINGLE_VALUE_TYPE
//   macro for this type supplying the command line to the macro.
// . MULTI_VALUE: a list of choices, the first of which should correspond to a
//   deactivated state for this lab (i.e. no command line option). To specify
//   this type of entry use the macro MULTI_VALUE_TYPE supplying it the
//   array of choices.
// See the documentation of FeatureEntry for details on the fields.
//
// Usage of about:flags is logged on startup via the "Launch.FlagsAtStartup"
// UMA histogram. This histogram shows the number of startups with a given flag
// enabled. If you'd like to see user counts instead, make sure to switch to
// "count users" view on the dashboard. When adding new entries, the enum
// "LoginCustomFlags" must be updated in histograms/enums.xml. See note in
// enums.xml and don't forget to run AboutFlagsHistogramTest unit test to
// calculate and verify checksum.
//
// When adding a new choice, add it to the end of the list.
const FeatureEntry kFeatureEntries[] = {
// Include generated flags for flag unexpiry; see //docs/flag_expiry.md and
// //tools/flags/generate_unexpire_flags.py.
#include "build/chromeos_buildflags.h"
#include "chrome/browser/unexpire_flags_gen.inc"
    {switches::kEnableBenchmarking, flag_descriptions::kEnableBenchmarkingName,
     flag_descriptions::kEnableBenchmarkingDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableBenchmarkingChoices)},
    {"ignore-gpu-blocklist", flag_descriptions::kIgnoreGpuBlocklistName,
     flag_descriptions::kIgnoreGpuBlocklistDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kIgnoreGpuBlocklist)},
    {"enable-accessibility-on-screen-mode",
     flag_descriptions::kAccessibilityOnScreenModeName,
     flag_descriptions::kAccessibilityOnScreenModeDescription, kOsAll,
     FEATURE_VALUE_TYPE(::features::kAccessibilityOnScreenMode)},
    {"disable-accelerated-2d-canvas",
     flag_descriptions::kAccelerated2dCanvasName,
     flag_descriptions::kAccelerated2dCanvasDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAccelerated2dCanvas)},
    {"overlay-strategies", flag_descriptions::kOverlayStrategiesName,
     flag_descriptions::kOverlayStrategiesDescription, kOsAll,
     MULTI_VALUE_TYPE(kOverlayStrategiesChoices)},
    {"tint-composited-content", flag_descriptions::kTintCompositedContentName,
     flag_descriptions::kTintCompositedContentDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kTintCompositedContent)},
    {"show-overdraw-feedback", flag_descriptions::kShowOverdrawFeedbackName,
     flag_descriptions::kShowOverdrawFeedbackDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kShowOverdrawFeedback)},
    {"ui-disable-partial-swap", flag_descriptions::kUiPartialSwapName,
     flag_descriptions::kUiPartialSwapDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kUIDisablePartialSwap)},
    {"webrtc-hw-decoding", flag_descriptions::kWebrtcHwDecodingName,
     flag_descriptions::kWebrtcHwDecodingDescription, kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kWebRtcHWDecoding)},
    {"webrtc-hw-encoding", flag_descriptions::kWebrtcHwEncodingName,
     flag_descriptions::kWebrtcHwEncodingDescription, kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kWebRtcHWEncoding)},
    {"webrtc-pqc-for-dtls", flag_descriptions::kWebRtcPqcForDtlsName,
     flag_descriptions::kWebRtcPqcForDtlsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kWebRtcPqcForDtls)},
    {"enable-webrtc-allow-input-volume-adjustment",
     flag_descriptions::kWebRtcAllowInputVolumeAdjustmentName,
     flag_descriptions::kWebRtcAllowInputVolumeAdjustmentDescription,
     kOsWin | kOsMac | kOsLinux,
     FEATURE_VALUE_TYPE(features::kWebRtcAllowInputVolumeAdjustment)},
    {"enable-webrtc-apm-downmix-capture-audio-method",
     flag_descriptions::kWebRtcApmDownmixCaptureAudioMethodName,
     flag_descriptions::kWebRtcApmDownmixCaptureAudioMethodDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kWebRtcApmDownmixCaptureAudioMethod,
         kWebRtcApmDownmixMethodVariations,
         "WebRtcApmDownmixCaptureAudioMethod")},
    {"enable-webrtc-hide-local-ips-with-mdns",
     flag_descriptions::kWebrtcHideLocalIpsWithMdnsName,
     flag_descriptions::kWebrtcHideLocalIpsWithMdnsDecription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebRtcHideLocalIpsWithMdns)},
    {"enable-webrtc-use-min-max-vea-dimensions",
     flag_descriptions::kWebrtcUseMinMaxVEADimensionsName,
     flag_descriptions::kWebrtcUseMinMaxVEADimensionsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kWebRtcUseMinMaxVEADimensions)},
#if defined(WEBRTC_USE_PIPEWIRE)
    {"enable-webrtc-pipewire-camera",
     flag_descriptions::kWebrtcPipeWireCameraName,
     flag_descriptions::kWebrtcPipeWireCameraDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWebRtcPipeWireCamera)},
#endif  // defined(WEBRTC_USE_PIPEWIRE)
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"web-hid-in-web-view", flag_descriptions::kEnableWebHidInWebViewName,
     flag_descriptions::kEnableWebHidInWebViewDescription, kOsAll,
     FEATURE_VALUE_TYPE(extensions_features::kEnableWebHidInWebView)},
    {"extensions-on-chrome-urls",
     flag_descriptions::kExtensionsOnChromeUrlsName,
     flag_descriptions::kExtensionsOnChromeUrlsDescription, kOsAll,
     SINGLE_VALUE_TYPE(extensions::switches::kExtensionsOnChromeURLs)},
    {"extensions-on-extension-urls",
     flag_descriptions::kExtensionsOnExtensionUrlsName,
     flag_descriptions::kExtensionsOnExtensionUrlsDescription, kOsAll,
     SINGLE_VALUE_TYPE(extensions::switches::kExtensionsOnExtensionURLs)},
    {"web-request-security-info",
     flag_descriptions::kWebRequestSecurityInfoName,
     flag_descriptions::kWebRequestSecurityInfoDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kWebRequestSecurityInfo)},
#endif  // ENABLE_EXTENSIONS
    {"show-autofill-type-predictions",
     flag_descriptions::kShowAutofillTypePredictionsName,
     flag_descriptions::kShowAutofillTypePredictionsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::features::debug::kAutofillShowTypePredictions,
         kAutofillShowTypePredictionsVariations,
         "AutofillShowTypePredictions")},
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"autofill-at-memory", flag_descriptions::kAutofillAtMemoryName,
     flag_descriptions::kAutofillAtMemoryDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillAtMemory)},
#endif
    {"autofill-more-prominent-popup",
     flag_descriptions::kAutofillMoreProminentPopupName,
     flag_descriptions::kAutofillMoreProminentPopupDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillMoreProminentPopup)},
    {"autofill-payments-field-swapping",
     flag_descriptions::kAutofillPaymentsFieldSwappingName,
     flag_descriptions::kAutofillPaymentsFieldSwappingDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillPaymentsFieldSwapping)},
    {"autofill-show-bubbles-based-on-priorities",
     flag_descriptions::kAutofillShowBubblesBasedOnPrioritiesName,
     flag_descriptions::kAutofillShowBubblesBasedOnPrioritiesDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillShowBubblesBasedOnPriorities)},
    {"backdrop-filter-mirror-edge",
     flag_descriptions::kBackdropFilterMirrorEdgeName,
     flag_descriptions::kBackdropFilterMirrorEdgeDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kBackdropFilterMirrorEdgeMode)},
    {"smooth-scrolling", flag_descriptions::kSmoothScrollingName,
     flag_descriptions::kSmoothScrollingDescription,
     // Mac has a separate implementation with its own setting to disable.
     kOsLinux | kOsCrOS | kOsWin | kOsAndroid,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableSmoothScrolling,
                               switches::kDisableSmoothScrolling)},
    {"fractional-scroll-offsets",
     flag_descriptions::kFractionalScrollOffsetsName,
     flag_descriptions::kFractionalScrollOffsetsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFractionalScrollOffsets)},
#if defined(USE_AURA)
    {"overlay-scrollbars", flag_descriptions::kOverlayScrollbarsName,
     flag_descriptions::kOverlayScrollbarsDescription,
     // Uses the system preference on Mac (a different implementation).
     // On Android, this is always enabled.
     kOsAura, FEATURE_VALUE_TYPE(features::kOverlayScrollbar)},
    {"overlay-scrollbars-flash-when-mouse-enter",
     flag_descriptions::kOverlayScrollbarsFlashWhenMouseEnterName,
     flag_descriptions::kOverlayScrollbarsFlashWhenMouseEnterDescription,
     kOsAura,
     FEATURE_VALUE_TYPE(features::kOverlayScrollbarFlashWhenMouseEnter)},
    {"overlay-scrollbars-flash-once-visible-on-viewport",
     flag_descriptions::kOverlayScrollbarsFlashOnceVisibleOnViewportName,
     flag_descriptions::kOverlayScrollbarsFlashOnceVisibleOnViewportDescription,
     kOsAura,
     FEATURE_VALUE_TYPE(
         features::kOverlayScrollbarFlashOnlyOnceVisibleOnViewport)},
#endif  // USE_AURA
#if BUILDFLAG(ENABLE_JXL_DECODER)
    {"enable-jxl-image-format", flag_descriptions::kJxlImageFormatName,
     flag_descriptions::kJxlImageFormatDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kJXLImageFormat)},
#endif  // BUILDFLAG(ENABLE_JXL_DECODER)
    {"enable-rusty-bmp", flag_descriptions::kRustyBmpName,
     flag_descriptions::kRustyBmpDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kRustyBmpFeature)},
    {"soft-navigation-heuristics",
     flag_descriptions::kSoftNavigationHeuristicsName,
     flag_descriptions::kSoftNavigationHeuristicsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kSoftNavigationHeuristics)},
    {"enable-quic", flag_descriptions::kQuicName,
     flag_descriptions::kQuicDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableQuic, switches::kDisableQuic)},
    {"webtransport-developer-mode",
     flag_descriptions::kWebTransportDeveloperModeName,
     flag_descriptions::kWebTransportDeveloperModeDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kWebTransportDeveloperMode)},
    {"structured-dns-errors", flag_descriptions::kStructuredDnsErrorsName,
     flag_descriptions::kStructuredDnsErrorsDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kUseStructuredDnsErrors)},
    {"disable-javascript-harmony-shipping",
     flag_descriptions::kJavascriptHarmonyShippingName,
     flag_descriptions::kJavascriptHarmonyShippingDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableJavaScriptHarmonyShipping)},
    {"enable-javascript-harmony", flag_descriptions::kJavascriptHarmonyName,
     flag_descriptions::kJavascriptHarmonyDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kJavaScriptHarmony)},
    {"enable-experimental-webassembly-features",
     flag_descriptions::kExperimentalWebAssemblyFeaturesName,
     flag_descriptions::kExperimentalWebAssemblyFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableExperimentalWebAssemblyFeatures)},
    {"enable-experimental-webassembly-shared-everything",
     flag_descriptions::kExperimentalWebAssemblySharedEverythingName,
     flag_descriptions::kExperimentalWebAssemblySharedEverythingDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         features::kEnableExperimentalWebAssemblySharedEverything)},
    {"enable-experimental-webassembly-stack-switching",
     flag_descriptions::kEnableWasmStackSwitchingName,
     flag_descriptions::kEnableWasmStackSwitchingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssemblyStackSwitching)},
    {"enable-webassembly-baseline", flag_descriptions::kEnableWasmBaselineName,
     flag_descriptions::kEnableWasmBaselineDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssemblyBaseline)},
    {"enable-webassembly-lazy-compilation",
     flag_descriptions::kEnableWasmLazyCompilationName,
     flag_descriptions::kEnableWasmLazyCompilationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssemblyLazyCompilation)},
    {"enable-webassembly-tiering", flag_descriptions::kEnableWasmTieringName,
     flag_descriptions::kEnableWasmTieringDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssemblyTiering)},
    {"enable-future-v8-vm-features", flag_descriptions::kV8VmFutureName,
     flag_descriptions::kV8VmFutureDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kV8VmFuture)},
    {"enable-gpu-rasterization", flag_descriptions::kGpuRasterizationName,
     flag_descriptions::kGpuRasterizationDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableGpuRasterizationChoices)},
    {"fallback-to-sw-if-gles3-not-supported",
     flag_descriptions::kFallbackToSWIfGLES3NotSupportedName,
     flag_descriptions::kFallbackToSWIfGLES3NotSupportedDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFallbackToSWIfGLES3NotSupported)},
    {"enable-experimental-web-platform-features",
     flag_descriptions::kExperimentalWebPlatformFeaturesName,
     flag_descriptions::kExperimentalWebPlatformFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableExperimentalWebPlatformFeatures)},
    {"top-chrome-touch-ui", flag_descriptions::kTopChromeTouchUiName,
     flag_descriptions::kTopChromeTouchUiDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kTopChromeTouchUiChoices)},
    {
        "disable-accelerated-video-decode",
        flag_descriptions::kAcceleratedVideoDecodeName,
        flag_descriptions::kAcceleratedVideoDecodeDescription,
        kOsMac | kOsWin | kOsCrOS | kOsAndroid | kOsLinux,
        SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAcceleratedVideoDecode),
    },
    {
        "disable-accelerated-video-encode",
        flag_descriptions::kAcceleratedVideoEncodeName,
        flag_descriptions::kAcceleratedVideoEncodeDescription,
        kOsMac | kOsWin | kOsCrOS | kOsAndroid,
        SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAcceleratedVideoEncode),
    },

    {"enable-show-autofill-signatures",
     flag_descriptions::kShowAutofillSignaturesName,
     flag_descriptions::kShowAutofillSignaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(autofill::switches::kShowAutofillSignatures)},
    {"wallet-service-use-sandbox",
     flag_descriptions::kWalletServiceUseSandboxName,
     flag_descriptions::kWalletServiceUseSandboxDescription,
     kOsAndroid | kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(
         autofill::switches::kWalletServiceUseSandbox,
         "1",
         autofill::switches::kWalletServiceUseSandbox,
         "0")},
    {"enable-web-bluetooth", flag_descriptions::kWebBluetoothName,
     flag_descriptions::kWebBluetoothDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWebBluetooth)},
    {"enable-web-bluetooth-new-permissions-backend",
     flag_descriptions::kWebBluetoothNewPermissionsBackendName,
     flag_descriptions::kWebBluetoothNewPermissionsBackendDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebBluetoothNewPermissionsBackend)},
    {"enable-webusb-device-detection",
     flag_descriptions::kWebUsbDeviceDetectionName,
     flag_descriptions::kWebUsbDeviceDetectionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebUsbDeviceDetection)},
#if defined(USE_AURA)
    {"overscroll-history-navigation",
     flag_descriptions::kOverscrollHistoryNavigationName,
     flag_descriptions::kOverscrollHistoryNavigationDescription, kOsAura,
     FEATURE_VALUE_TYPE(features::kOverscrollHistoryNavigation)},
    {"pull-to-refresh", flag_descriptions::kPullToRefreshName,
     flag_descriptions::kPullToRefreshDescription, kOsAura,
     MULTI_VALUE_TYPE(kPullToRefreshChoices)},
#endif  // USE_AURA
    {"enable-touch-drag-drop", flag_descriptions::kTouchDragDropName,
     flag_descriptions::kTouchDragDropDescription, kOsWin | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kTouchDragAndDrop)},
    {"touch-selection-strategy", flag_descriptions::kTouchSelectionStrategyName,
     flag_descriptions::kTouchSelectionStrategyDescription,
     kOsAndroid,  // TODO(mfomitchev): Add CrOS/Win/Linux support soon.
     MULTI_VALUE_TYPE(kTouchTextSelectionStrategyChoices)},
    {"enable-webgl-developer-extensions",
     flag_descriptions::kWebglDeveloperExtensionsName,
     flag_descriptions::kWebglDeveloperExtensionsDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebGLDeveloperExtensions)},
    {"enable-webgl-draft-extensions",
     flag_descriptions::kWebglDraftExtensionsName,
     flag_descriptions::kWebglDraftExtensionsDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebGLDraftExtensions)},
    {"enable-zero-copy", flag_descriptions::kZeroCopyName,
     flag_descriptions::kZeroCopyDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(blink::switches::kEnableZeroCopy,
                               blink::switches::kDisableZeroCopy)},
    {"enable-vulkan", flag_descriptions::kEnableVulkanName,
     flag_descriptions::kEnableVulkanDescription, kOsLinux | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kVulkan)},
    {"force-enable-webgpu-interop", flag_descriptions::kWebGpuInteropName,
     flag_descriptions::kkWebGpuInteropDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kForceEnableWebGpuInterop)},
    {"default-angle-vulkan", flag_descriptions::kDefaultAngleVulkanName,
     flag_descriptions::kDefaultAngleVulkanDescription, kOsLinux | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kDefaultANGLEVulkan)},
    {"vulkan-from-angle", flag_descriptions::kVulkanFromAngleName,
     flag_descriptions::kVulkanFromAngleDescription, kOsLinux | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kVulkanFromANGLE)},

    {"in-product-help-demo-mode-choice",
     flag_descriptions::kInProductHelpDemoModeChoiceName,
     flag_descriptions::kInProductHelpDemoModeChoiceDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         feature_engagement::kIPHDemoMode,
         feature_engagement::kIPHDemoModeChoiceVariations,
         "IPH_DemoMode")},
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
    {"mojo-use-eventfd", flag_descriptions::kMojoUseEventFdName,
     flag_descriptions::kMojoUseEventFdDescription,
     kOsCrOS | kOsLinux | kOsAndroid,
     FEATURE_VALUE_TYPE(mojo::core::kMojoUseEventFd)},
#endif

    {"enable-isolated-web-apps", flag_descriptions::kEnableIsolatedWebAppsName,
     flag_descriptions::kEnableIsolatedWebAppsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kIsolatedWebApps)},
    {"direct-sockets-in-service-workers",
     flag_descriptions::kDirectSocketsInServiceWorkersName,
     flag_descriptions::kDirectSocketsInServiceWorkersDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDirectSocketsInServiceWorkers)},
    {"direct-sockets-in-shared-workers",
     flag_descriptions::kDirectSocketsInSharedWorkersName,
     flag_descriptions::kDirectSocketsInSharedWorkersDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDirectSocketsInSharedWorkers)},
    {"enable-isolated-web-app-allowlist",
     flag_descriptions::kEnableIsolatedWebAppAllowlistName,
     flag_descriptions::kEnableIsolatedWebAppAllowlistDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kIsolatedWebAppManagedAllowlist)},
    {"enable-isolated-web-app-dev-mode",
     flag_descriptions::kEnableIsolatedWebAppDevModeName,
     flag_descriptions::kEnableIsolatedWebAppDevModeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kIsolatedWebAppDevMode)},
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"enable-iwa-key-distribution-component",
     flag_descriptions::kEnableIwaKeyDistributionComponentName,
     flag_descriptions::kEnableIwaKeyDistributionComponentDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(component_updater::kIwaKeyDistributionComponent)},
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"iwa-key-distribution-component-exp-cohort",
     flag_descriptions::kIwaKeyDistributionComponentExpCohortName,
     flag_descriptions::kIwaKeyDistributionComponentExpCohortDescription,
     kOsDesktop,
     STRING_VALUE_TYPE(component_updater::kIwaKeyDistributionComponentExpCohort,
                       "")},
    {"enable-unframed-iwa", flag_descriptions::kEnableUnframedIwaName,
     flag_descriptions::kEnableUnframedIwaDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kUnframedIwa)},

    {"enable-controlled-frame", flag_descriptions::kEnableControlledFrameName,
     flag_descriptions::kEnableControlledFrameDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kControlledFrame)},

    {"isolate-origins", flag_descriptions::kIsolateOriginsName,
     flag_descriptions::kIsolateOriginsDescription, kOsAll,
     ORIGIN_LIST_VALUE_TYPE(switches::kIsolateOrigins, "")},
    {about_flags::kSiteIsolationTrialOptOutInternalName,
     flag_descriptions::kSiteIsolationOptOutName,
     flag_descriptions::kSiteIsolationOptOutDescription, kOsAll,
     MULTI_VALUE_TYPE(kSiteIsolationOptOutChoices)},
    {"allow-insecure-localhost", flag_descriptions::kAllowInsecureLocalhostName,
     flag_descriptions::kAllowInsecureLocalhostDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kAllowInsecureLocalhost)},
    {"text-based-audio-descriptions",
     flag_descriptions::kTextBasedAudioDescriptionName,
     flag_descriptions::kTextBasedAudioDescriptionDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kTextBasedAudioDescription)},
    {"enable-desktop-pwas-app-title",
     flag_descriptions::kDesktopPWAsAppTitleName,
     flag_descriptions::kDesktopPWAsAppTitleDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebAppEnableAppTitle)},
    {"tab-strip-declutter", flag_descriptions::kTabStripDeclutterName,
     flag_descriptions::kTabStripDeclutterDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kTabStripDeclutter)},
    {"detached-tabs", flag_descriptions::kDetachedTabsName,
     flag_descriptions::kDetachedTabsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDetachedTabs)},
    {"enable-desktop-pwas-elided-extensions-menu",
     flag_descriptions::kDesktopPWAsElidedExtensionsMenuName,
     flag_descriptions::kDesktopPWAsElidedExtensionsMenuDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDesktopPWAsElidedExtensionsMenu)},
    {"enable-desktop-pwas-tab-strip",
     flag_descriptions::kDesktopPWAsTabStripName,
     flag_descriptions::kDesktopPWAsTabStripDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDesktopPWAsTabStrip)},
    {"enable-desktop-pwas-tab-strip-settings",
     flag_descriptions::kDesktopPWAsTabStripSettingsName,
     flag_descriptions::kDesktopPWAsTabStripSettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDesktopPWAsTabStripSettings)},
    {"enable-desktop-pwas-tab-strip-customizations",
     flag_descriptions::kDesktopPWAsTabStripCustomizationsName,
     flag_descriptions::kDesktopPWAsTabStripCustomizationsDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDesktopPWAsTabStripCustomizations)},
    {"enable-sub-apps", flag_descriptions::kSubAppsName,
     flag_descriptions::kSubAppsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kSubApps)},
    // TODO(crbug.com/466441366): Remove "borderless".
    {"enable-desktop-pwas-borderless",
     flag_descriptions::kDesktopPWAsBorderlessName,
     flag_descriptions::kDesktopPWAsBorderlessDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebAppBorderless)},
    {"enable-desktop-pwas-additional-windowing-controls",
     flag_descriptions::kDesktopPWAsAdditionalWindowingControlsName,
     flag_descriptions::kDesktopPWAsAdditionalWindowingControlsDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         blink::features::kDesktopPWAsAdditionalWindowingControls)},
    {"record-web-app-debug-info", flag_descriptions::kRecordWebAppDebugInfoName,
     flag_descriptions::kRecordWebAppDebugInfoDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kRecordWebAppDebugInfo)},
    {"use-sync-sandbox", flag_descriptions::kSyncSandboxName,
     flag_descriptions::kSyncSandboxDescription, kOsAll,
     SINGLE_VALUE_TYPE_AND_VALUE(
         syncer::kSyncServiceURL,
         "https://chrome-sync.sandbox.google.com/chrome-sync/alpha")},
    {"media-router-cast-allow-all-ips",
     flag_descriptions::kMediaRouterCastAllowAllIPsName,
     flag_descriptions::kMediaRouterCastAllowAllIPsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kCastAllowAllIPsFeature)},
    {"allow-all-sites-to-initiate-mirroring",
     flag_descriptions::kAllowAllSitesToInitiateMirroringName,
     flag_descriptions::kAllowAllSitesToInitiateMirroringDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kAllowAllSitesToInitiateMirroring)},
    {"media-route-dial-provider",
     flag_descriptions::kDialMediaRouteProviderName,
     flag_descriptions::kDialMediaRouteProviderDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kDialMediaRouteProvider)},
    {"cast-message-logging", flag_descriptions::kCastMessageLoggingName,
     flag_descriptions::kCastMessageLoggingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kCastMessageLogging)},

    {"cast-streaming-exponential-video-bitrate-algorithm",
     flag_descriptions::kCastStreamingExponentialVideoBitrateAlgorithmName,
     flag_descriptions::
         kCastStreamingExponentialVideoBitrateAlgorithmDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         media::kCastStreamingExponentialVideoBitrateAlgorithm,
         kCastStreamingExponentialVideoBitrateAlgorithmVariations,
         "CastStreamingExponentialVideoBitrateAlgorithm")},

    {"cast-streaming-hardware-h264",
     flag_descriptions::kCastStreamingHardwareH264Name,
     flag_descriptions::kCastStreamingHardwareH264Description, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(
         switches::kCastStreamingForceEnableHardwareH264,
         switches::kCastStreamingForceDisableHardwareH264)},

    {"cast-streaming-hardware-hevc",
     flag_descriptions::kCastStreamingHardwareHevcName,
     flag_descriptions::kCastStreamingHardwareHevcDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingHardwareHevc)},

    {"cast-streaming-hardware-vp8",
     flag_descriptions::kCastStreamingHardwareVp8Name,
     flag_descriptions::kCastStreamingHardwareVp8Description, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(
         switches::kCastStreamingForceEnableHardwareVp8,
         switches::kCastStreamingForceDisableHardwareVp8)},

    {"cast-streaming-hardware-vp9",
     flag_descriptions::kCastStreamingHardwareVp9Name,
     flag_descriptions::kCastStreamingHardwareVp9Description, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(
         switches::kCastStreamingForceEnableHardwareVp9,
         switches::kCastStreamingForceDisableHardwareVp9)},

    {"cast-streaming-media-video-encoder",
     flag_descriptions::kCastStreamingMediaVideoEncoderName,
     flag_descriptions::kCastStreamingMediaVideoEncoderDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingMediaVideoEncoder)},

    {"cast-streaming-performance-overlay",
     flag_descriptions::kCastStreamingPerformanceOverlayName,
     flag_descriptions::kCastStreamingPerformanceOverlayDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingPerformanceOverlay)},

    {"enable-cast-streaming-av1", flag_descriptions::kCastStreamingAv1Name,
     flag_descriptions::kCastStreamingAv1Description, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingAv1)},

#if BUILDFLAG(IS_MAC)
    {"enable-cast-streaming-mac-hardware-h264",
     flag_descriptions::kCastStreamingMacHardwareH264Name,
     flag_descriptions::kCastStreamingMacHardwareH264Description, kOsMac,
     FEATURE_VALUE_TYPE(media::kCastStreamingMacHardwareH264)},
#endif


    {"enable-cast-streaming-vp8", flag_descriptions::kCastStreamingVp8Name,
     flag_descriptions::kCastStreamingVp8Description, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingVp8)},

    {"enable-cast-streaming-vp9", flag_descriptions::kCastStreamingVp9Name,
     flag_descriptions::kCastStreamingVp9Description, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingVp9)},

    {"enable-cast-streaming-with-hidpi",
     flag_descriptions::kCastEnableStreamingWithHiDPIName,
     flag_descriptions::kCastEnableStreamingWithHiDPIDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(mirroring::features::kCastEnableStreamingWithHiDPI)},


#if BUILDFLAG(IS_MAC)
    {"mac-catap-loopback-audio-for-cast",
     flag_descriptions::kMacCatapLoopbackAudioForCastName,
     flag_descriptions::kMacCatapLoopbackAudioForCastDescription, kOsMac,
     FEATURE_VALUE_TYPE(media::kMacCatapLoopbackAudioForCast)},

    {"mac-catap-loopback-audio-for-screen-share",
     flag_descriptions::kMacCatapLoopbackAudioForScreenShareName,
     flag_descriptions::kMacCatapLoopbackAudioForScreenShareDescription, kOsMac,
     FEATURE_VALUE_TYPE(media::kMacCatapLoopbackAudioForScreenShare)},

    {"use-sc-content-sharing-picker",
     flag_descriptions::kUseSCContentSharingPickerName,
     flag_descriptions::kUseSCContentSharingPickerDescription, kOsMac,
     FEATURE_VALUE_TYPE(media::kUseSCContentSharingPicker)},
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_LINUX)
    {"pulseaudio-loopback-for-cast",
     flag_descriptions::kPulseaudioLoopbackForCastName,
     flag_descriptions::kPulseaudioLoopbackForCastDescription, kOsLinux,
     FEATURE_VALUE_TYPE(media::kPulseaudioLoopbackForCast)},

    {"pulseaudio-loopback-for-screen-share",
     flag_descriptions::kPulseaudioLoopbackForScreenShareName,
     flag_descriptions::kPulseaudioLoopbackForScreenShareDescription, kOsLinux,
     FEATURE_VALUE_TYPE(media::kPulseaudioLoopbackForScreenShare)},

    {"wayland-session-management",
     flag_descriptions::kWaylandSessionManagementName,
     flag_descriptions::kWaylandSessionManagementDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWaylandSessionManagement)},
#endif  // BUILDFLAG(IS_LINUX)

#if BUILDFLAG(ENABLE_VR)
#if BUILDFLAG(ENABLE_OPENXR)
    {"openxr-spatial-entities", flag_descriptions::kOpenXrSpatialEntitiesName,
     flag_descriptions::kOpenXrSpatialEntitiesDescription, kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kOpenXrSpatialEntities)},
    {"spatial-entities-depth-hit-test",
     flag_descriptions::kSpatialEntitesDepthHitTestName,
     flag_descriptions::kSpatialEntitesDepthHitTestDescription,
     kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kSpatialEntitesDepthHitTest)},
#endif  // BUILDFLAG(ENABLE_OPENXR)
    {"webxr-projection-layers", flag_descriptions::kWebXrLayersName,
     flag_descriptions::kWebXrLayersDescription, kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kWebXRLayers)},
    {"webxr-webgpu-binding", flag_descriptions::kWebXrWebGpuBindingName,
     flag_descriptions::kWebXrWebGpuBindingDescription, kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kWebXRWebGPUBinding)},
    {"webxr-incubations", flag_descriptions::kWebXrIncubationsName,
     flag_descriptions::kWebXrIncubationsDescription, kOsAll,
     FEATURE_VALUE_TYPE(device::features::kWebXRIncubations)},
    {"webxr-internals", flag_descriptions::kWebXrInternalsName,
     flag_descriptions::kWebXrInternalsDescription, kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kWebXrInternals)},
    {"webxr-plane-detection", flag_descriptions::kWebXrPlaneDetectionName,
     flag_descriptions::kWebXrPlaneDetectionDescription, kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kWebXRPlaneDetection)},
    {"webxr-runtime", flag_descriptions::kWebXrForceRuntimeName,
     flag_descriptions::kWebXrForceRuntimeDescription, kOsDesktop | kOsAndroid,
     MULTI_VALUE_TYPE(kWebXrForceRuntimeChoices)},
    {"webxr-hand-anonymization",
     flag_descriptions::kWebXrHandAnonymizationStrategyName,
     flag_descriptions::kWebXrHandAnonymizationStrategyDescription,
     kOsDesktop | kOsAndroid, MULTI_VALUE_TYPE(KWebXrHandAnonymizationChoices)},
#endif  // ENABLE_VR
    {"system-keyboard-lock", flag_descriptions::kSystemKeyboardLockName,
     flag_descriptions::kSystemKeyboardLockDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSystemKeyboardLock)},
    {"disallow-doc-written-script-loads",
     flag_descriptions::kDisallowDocWrittenScriptsUiName,
     flag_descriptions::kDisallowDocWrittenScriptsUiDescription, kOsAll,
     // NOTE: if we want to add additional experiment entries for other
     // features controlled by kBlinkSettings, we'll need to add logic to
     // merge the flag values.
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(
         blink::switches::kBlinkSettings,
         "disallowFetchForDocWrittenScriptsInMainFrame=true",
         blink::switches::kBlinkSettings,
         "disallowFetchForDocWrittenScriptsInMainFrame=false")},
#if defined(TOOLKIT_VIEWS)
    {"enable-autofill-credit-card-upload",
     flag_descriptions::kAutofillCreditCardUploadName,
     flag_descriptions::kAutofillCreditCardUploadDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillUpstream)},
#endif  // defined(TOOLKIT_VIEWS) || BUILDFLAG(IS_ANDROID)
    {"force-ui-direction", flag_descriptions::kForceUiDirectionName,
     flag_descriptions::kForceUiDirectionDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceUIDirectionChoices)},
    {"force-text-direction", flag_descriptions::kForceTextDirectionName,
     flag_descriptions::kForceTextDirectionDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceTextDirectionChoices)},
    {"enable-tls13-early-data", flag_descriptions::kEnableTLS13EarlyDataName,
     flag_descriptions::kEnableTLS13EarlyDataDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kEnableTLS13EarlyData)},
    {"tls-trust-anchor-ids", flag_descriptions::kTLSTrustAnchorIDsName,
     flag_descriptions::kTLSTrustAnchorIDsDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kTLSTrustAnchorIDs)},
    {"enable-force-dark", flag_descriptions::kAutoWebContentsDarkModeName,
     flag_descriptions::kAutoWebContentsDarkModeDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kForceWebContentsDarkMode)},
    {"enable-experimental-accessibility-language-detection",
     flag_descriptions::kExperimentalAccessibilityLanguageDetectionName,
     flag_descriptions::kExperimentalAccessibilityLanguageDetectionDescription,
     kOsAll,
     SINGLE_VALUE_TYPE(
         ::switches::kEnableExperimentalAccessibilityLanguageDetection)},
    {"enable-experimental-accessibility-language-detection-dynamic",
     flag_descriptions::kExperimentalAccessibilityLanguageDetectionDynamicName,
     flag_descriptions::
         kExperimentalAccessibilityLanguageDetectionDynamicDescription,
     kOsAll,
     SINGLE_VALUE_TYPE(
         ::switches::kEnableExperimentalAccessibilityLanguageDetectionDynamic)},

    {"enable-cros-touch-text-editing-redesign",
     flag_descriptions::kTouchTextEditingRedesignName,
     flag_descriptions::kTouchTextEditingRedesignDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kTouchTextEditingRedesign)},
#if BUILDFLAG(IS_MAC)
    {"enable-retry-capture-device-enumeration-on-crash",
     flag_descriptions::kRetryGetVideoCaptureDeviceInfosName,
     flag_descriptions::kRetryGetVideoCaptureDeviceInfosDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kRetryGetVideoCaptureDeviceInfos)},
#endif  // BUILDFLAG(IS_MAC)
    {"enable-web-payments-experimental-features",
     flag_descriptions::kWebPaymentsExperimentalFeaturesName,
     flag_descriptions::kWebPaymentsExperimentalFeaturesDescription, kOsAll,
     FEATURE_VALUE_TYPE(payments::features::kWebPaymentsExperimentalFeatures)},
    {"enable-debug-for-store-billing",
     flag_descriptions::kAppStoreBillingDebugName,
     flag_descriptions::kAppStoreBillingDebugDescription, kOsAll,
     FEATURE_VALUE_TYPE(payments::features::kAppStoreBillingDebug)},
    {"enable-secure-payment-confirmation-browser-bound-key",
     flag_descriptions::kSecurePaymentConfirmationBrowserBoundKeysName,
     flag_descriptions::kSecurePaymentConfirmationBrowserBoundKeysDescription,
     kOsAndroid | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(
         blink::features::kSecurePaymentConfirmationBrowserBoundKeys)},
    {"fill-on-account-select", flag_descriptions::kFillOnAccountSelectName,
     flag_descriptions::kFillOnAccountSelectDescription, kOsAll,
     FEATURE_VALUE_TYPE(password_manager::features::kFillOnAccountSelect)},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {"first-run-desktop-choice-screen-refresh",
     flag_descriptions::kFirstRunDesktopChoiceScreenRefreshName,
     flag_descriptions::kFirstRunDesktopChoiceScreenRefreshDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kFirstRunDesktopChoiceScreenRefresh)},
    {"first-run-desktop-refresh",
     flag_descriptions::kFirstRunDesktopRefreshName,
     flag_descriptions::kFirstRunDesktopRefreshDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kFirstRunDesktopRefresh)},
    {"first-run-desktop-revamp", flag_descriptions::kFirstRunDesktopRevampName,
     flag_descriptions::kFirstRunDesktopRevampDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kFirstRunDesktopRevamp)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)





    {"most-visited-tiles-new-scoring",
     flag_descriptions::kMostVisitedTilesNewScoringName,
     flag_descriptions::kMostVisitedTilesNewScoringDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(history::kMostVisitedTilesNewScoring,
                                    kMostVisitedTilesNewScoringVariations,
                                    "MostVisitedTilesNewScoring")},

    {"omnibox-local-history-zero-suggest-beyond-ntp",
     flag_descriptions::kOmniboxLocalHistoryZeroSuggestBeyondNTPName,
     flag_descriptions::kOmniboxLocalHistoryZeroSuggestBeyondNTPDescription,
     kOsAll, FEATURE_VALUE_TYPE(omnibox::kLocalHistoryZeroSuggestBeyondNTP)},

    {"omnibox-zero-suggest-prefetch-debouncing",
     flag_descriptions::kOmniboxZeroSuggestPrefetchDebouncingName,
     flag_descriptions::kOmniboxZeroSuggestPrefetchDebouncingDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kZeroSuggestPrefetchDebouncing,
         kOmniboxZeroSuggestPrefetchDebouncingVariations,
         "OmniboxZeroSuggestPrefetchDebouncing")},

    {"omnibox-zero-suggest-prefetching-on-srp",
     flag_descriptions::kOmniboxZeroSuggestPrefetchingOnSRPName,
     flag_descriptions::kOmniboxZeroSuggestPrefetchingOnSRPDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kZeroSuggestPrefetchingOnSRP)},

    {"omnibox-zero-suggest-prefetching-on-web",
     flag_descriptions::kOmniboxZeroSuggestPrefetchingOnWebName,
     flag_descriptions::kOmniboxZeroSuggestPrefetchingOnWebDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kZeroSuggestPrefetchingOnWeb)},

    {"omnibox-ml-log-url-scoring-signals",
     flag_descriptions::kOmniboxMlLogUrlScoringSignalsName,
     flag_descriptions::kOmniboxMlLogUrlScoringSignalsDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kLogUrlScoringSignals)},
    {"omnibox-ml-url-piecewise-mapped-search-blending",
     flag_descriptions::kOmniboxMlUrlPiecewiseMappedSearchBlendingName,
     flag_descriptions::kOmniboxMlUrlPiecewiseMappedSearchBlendingDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kMlUrlPiecewiseMappedSearchBlending,
         kMlUrlPiecewiseMappedSearchBlendingVariations,
         "MlUrlPiecewiseMappedSearchBlending")},
    {"omnibox-ml-url-score-caching",
     flag_descriptions::kOmniboxMlUrlScoreCachingName,
     flag_descriptions::kOmniboxMlUrlScoreCachingDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kMlUrlScoreCaching)},
    {"omnibox-ml-url-scoring", flag_descriptions::kOmniboxMlUrlScoringName,
     flag_descriptions::kOmniboxMlUrlScoringDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kMlUrlScoring,
                                    kOmniboxMlUrlScoringVariations,
                                    "MlUrlScoring")},
    {"omnibox-ml-url-search-blending",
     flag_descriptions::kOmniboxMlUrlSearchBlendingName,
     flag_descriptions::kOmniboxMlUrlSearchBlendingDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kMlUrlSearchBlending,
                                    kMlUrlSearchBlendingVariations,
                                    "MlUrlScoring")},
    {"omnibox-ml-url-scoring-model",
     flag_descriptions::kOmniboxMlUrlScoringModelName,
     flag_descriptions::kOmniboxMlUrlScoringModelDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kUrlScoringModel,
                                    kUrlScoringModelVariations,
                                    "MlUrlScoring")},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
    {"contextual-search-box-uses-contextual-search-provider",
     flag_descriptions::kContextualSearchBoxUsesContextualSearchProviderName,
     flag_descriptions::
         kContextualSearchBoxUsesContextualSearchProviderDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::ContextualSearch::
                            kContextualSearchBoxUsesContextualSearchProvider)},

    {"contextual-search-open-lens-action-uses-thumbnail",
     flag_descriptions::kContextualSearchOpenLensActionUsesThumbnailName,
     flag_descriptions::kContextualSearchOpenLensActionUsesThumbnailDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::ContextualSearch::
                            kContextualSearchOpenLensActionUsesThumbnail)},

    {"contextual-suggestions-ablate-others-when-present",
     flag_descriptions::kContextualSuggestionsAblateOthersWhenPresentName,
     flag_descriptions::
         kContextualSuggestionsAblateOthersWhenPresentDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::ContextualSearch::
             kContextualSuggestionsAblateOthersWhenPresent,
         kContextualSuggestionsAblateOthersWhenPresentVariations,
         "ContextualSuggestionsAblateOthersWhenPresent")},

    {"enable-force-download-to-onedrive",
     flag_descriptions::kEnableForceDownloadToOneDriveName,
     flag_descriptions::kEnableForceDownloadToOneDriveDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         enterprise_data_protection::kEnableForceDownloadToOneDrive)},

    {"omnibox-contextual-search-on-focus-suggestions",
     flag_descriptions::kOmniboxContextualSearchOnFocusSuggestionsName,
     flag_descriptions::kOmniboxContextualSearchOnFocusSuggestionsDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::ContextualSearch::
             kOmniboxContextualSearchOnFocusSuggestions,
         kOmniboxContextualSearchOnFocusSuggestionsVariations,
         "OmniboxContextualSearchOnFocusSuggestions")},

    {"omnibox-contextual-suggestions",
     flag_descriptions::kOmniboxContextualSuggestionsName,
     flag_descriptions::kOmniboxContextualSuggestionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::ContextualSearch::
                            kOmniboxContextualSuggestions)},

    {"lens-overlay-omnibox-entry-point",
     flag_descriptions::kLensOverlayOmniboxEntryPointName,
     flag_descriptions::kLensOverlayOmniboxEntryPointDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayOmniboxEntryPoint)},

    {"ai-mode-omnibox-entry-point",
     flag_descriptions::kAiModeOmniboxEntryPointName,
     flag_descriptions::kAiModeOmniboxEntryPointDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kAiModeOmniboxEntryPoint,
                                    kOmniboxAiModeEntryPointVariations,
                                    "OmniboxAiModeEntryPointVariations")},

    {"hide-aim-omnibox-entrypoint-on-user-input",
     flag_descriptions::kHideAimOmniboxEntrypointOnUserInputName,
     flag_descriptions::kHideAimOmniboxEntrypointOnUserInputDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(omnibox::kHideAimEntrypointOnUserInput)},

    {"omnibox-toolbelt", flag_descriptions::kOmniboxToolbeltName,
     flag_descriptions::kOmniboxToolbeltDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::Toolbelt::kOmniboxToolbelt,
         kOmniboxToolbeltVariations,
         "OmniboxToolbelt")},

    {"omnibox-allow-ai-mode-matches",
     flag_descriptions::kOmniboxAllowAiModeMatchesName,
     flag_descriptions::kOmniboxAllowAiModeMatchesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::AiMode::kAllowAiModeMatches)},

    {"omnibox-drive-suggestions-no-sync-requirement",
     flag_descriptions::kOmniboxDriveSuggestionsNoSyncRequirementName,
     flag_descriptions::kOmniboxDriveSuggestionsNoSyncRequirementDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kDocumentProviderNoSyncRequirement)},
    {"omnibox-force-allowed-to-be-default",
     flag_descriptions::kOmniboxForceAllowedToBeDefaultName,
     flag_descriptions::kOmniboxForceAllowedToBeDefaultDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::ForceAllowedToBeDefault::
                            kForceAllowedToBeDefault)},
    {"omnibox-rich-autocompletion-promising",
     flag_descriptions::kOmniboxRichAutocompletionPromisingName,
     flag_descriptions::kOmniboxRichAutocompletionPromisingDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kRichAutocompletion,
         kOmniboxRichAutocompletionPromisingVariations,
         "OmniboxBundledExperimentV1")},
    {"omnibox-starter-pack-expansion",
     flag_descriptions::kOmniboxStarterPackExpansionName,
     flag_descriptions::kOmniboxStarterPackExpansionDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kStarterPackExpansion,
                                    kOmniboxStarterPackExpansionVariations,
                                    "StarterPackExpansion")},

    {"omnibox-starter-pack-iph", flag_descriptions::kOmniboxStarterPackIPHName,
     flag_descriptions::kOmniboxStarterPackIPHDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kStarterPackIPH)},

    {"omnibox-focus-triggers-web-and-srp-zero-suggest",
     flag_descriptions::kOmniboxFocusTriggersWebAndSRPZeroSuggestName,
     flag_descriptions::kOmniboxFocusTriggersWebAndSRPZeroSuggestDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kFocusTriggersWebAndSRPZeroSuggest)},

    {"omnibox-show-popup-on-mouse-released",
     flag_descriptions::kOmniboxShowPopupOnMouseReleasedName,
     flag_descriptions::kOmniboxShowPopupOnMouseReleasedDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kShowPopupOnMouseReleased)},

    {"omnibox-hide-suggestion-group-headers",
     flag_descriptions::kOmniboxHideSuggestionGroupHeadersName,
     flag_descriptions::kOmniboxHideSuggestionGroupHeadersDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(omnibox::kHideSuggestionGroupHeaders)},

    {"omnibox-hide-contextual-group-headers",
     flag_descriptions::kOmniboxHideContextualGroupHeadersName,
     flag_descriptions::kOmniboxHideContextualGroupHeadersDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(omnibox::kHideContextualGroupHeaders)},

    {"omnibox-url-suggestions-on-focus",
     flag_descriptions::kOmniboxUrlSuggestionsOnFocus,
     flag_descriptions::kOmniboxUrlSuggestionsOnFocusDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::OmniboxUrlSuggestionsOnFocus::
             kOmniboxUrlSuggestionsOnFocus,
         kOmniboxUrlSuggestionsOnFocusVariations,
         "OmniboxUrlSuggestionsOnFocus")},

    {"omnibox-zps-suggestion-limit",
     flag_descriptions::kOmniboxZpsSuggestionLimit,
     flag_descriptions::kOmniboxZpsSuggestionLimitDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::OmniboxZpsSuggestionLimit::
             kOmniboxZpsSuggestionLimit,
         kOmniboxZpsSuggestionLimitVariations,
         "OmniboxZpsSuggestionLimit")},

    {"omnibox-enterprise-search-aggregator",
     flag_descriptions::kOmniboxSearchAggregatorName,
     flag_descriptions::kOmniboxSearchAggregatorDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::SearchAggregatorProvider::
                            kSearchAggregatorProvider)},

    {"omnibox-adjust-indentation",
     flag_descriptions::kOmniboxAdjustIndentationName,
     flag_descriptions::kOmniboxAdjustIndentationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         omnibox_feature_configs::AdjustOmniboxIndent::kAdjustOmniboxIndent)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

    {"aim-server-eligibility",
     flag_descriptions::kOmniboxAimServerEligibilityName,
     flag_descriptions::kOmniboxAimServerEligibilityDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kAimServerEligibilityEnabled)},

    {"aim-server-eligibility-include-client-locale",
     flag_descriptions::kAimServerEligibilityIncludeClientLocaleName,
     flag_descriptions::kAimServerEligibilityIncludeClientLocaleDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kAimServerEligibilityIncludeClientLocale,
         kAimServerEligibilityIncludeClientLocaleVariations,
         "AimServerEligibilityIncludeClientLocale")},

    {"aim-use-pec-api", flag_descriptions::kAimUsePecApiName,
     flag_descriptions::kAimUsePecApiDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kAimUsePecApi)},


    {"omnibox-on-device-tail-suggestions",
     flag_descriptions::kOmniboxOnDeviceTailSuggestionsName,
     flag_descriptions::kOmniboxOnDeviceTailSuggestionsDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kOnDeviceTailModel)},



    {"force-color-profile", flag_descriptions::kForceColorProfileName,
     flag_descriptions::kForceColorProfileDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceColorProfileChoices)},

    {"forced-colors", flag_descriptions::kForcedColorsName,
     flag_descriptions::kForcedColorsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kForcedColors)},

    {"hdr-agtm", flag_descriptions::kHdrAgtmName,
     flag_descriptions::kHdrAgtmDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kHdrAgtm)},

    {"memlog", flag_descriptions::kMemlogName,
     flag_descriptions::kMemlogDescription, kOsAll,
     MULTI_VALUE_TYPE(kMemlogModeChoices)},

    {"memlog-sampling-rate", flag_descriptions::kMemlogSamplingRateName,
     flag_descriptions::kMemlogSamplingRateDescription, kOsAll,
     MULTI_VALUE_TYPE(kMemlogSamplingRateChoices)},

    {"memlog-stack-mode", flag_descriptions::kMemlogStackModeName,
     flag_descriptions::kMemlogStackModeDescription, kOsAll,
     MULTI_VALUE_TYPE(kMemlogStackModeChoices)},

    {"omnibox-max-zero-suggest-matches",
     flag_descriptions::kOmniboxMaxZeroSuggestMatchesName,
     flag_descriptions::kOmniboxMaxZeroSuggestMatchesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kMaxZeroSuggestMatches,
                                    kMaxZeroSuggestMatchesVariations,
                                    "OmniboxBundledExperimentV1")},

    {"omnibox-ui-max-autocomplete-matches",
     flag_descriptions::kOmniboxUIMaxAutocompleteMatchesName,
     flag_descriptions::kOmniboxUIMaxAutocompleteMatchesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kUIExperimentMaxAutocompleteMatches,
         kOmniboxUIMaxAutocompleteMatchesVariations,
         "OmniboxBundledExperimentV1")},

    {"omnibox-mia-zps", flag_descriptions::kOmniboxMiaZps,
     flag_descriptions::kOmniboxMiaZpsDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::MiaZPS::kOmniboxMiaZPS)
    },

    {"omnibox-dynamic-max-autocomplete",
     flag_descriptions::kOmniboxDynamicMaxAutocompleteName,
     flag_descriptions::kOmniboxDynamicMaxAutocompleteDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kDynamicMaxAutocomplete,
                                    kOmniboxDynamicMaxAutocompleteVariations,
                                    "OmniboxBundledExperimentV1")},

    {"omnibox-grouping-framework-non-zps",
     flag_descriptions::kOmniboxGroupingFrameworkNonZPSName,
     flag_descriptions::kOmniboxGroupingFrameworkDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kGroupingFrameworkForNonZPS)},

    {"omnibox-calc-provider", flag_descriptions::kOmniboxCalcProviderName,
     flag_descriptions::kOmniboxCalcProviderDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::CalcProvider::kCalcProvider)},

    {"optimization-guide-debug-logs",
     flag_descriptions::kOptimizationGuideDebugLogsName,
     flag_descriptions::kOptimizationGuideDebugLogsDescription, kOsAll,
     SINGLE_VALUE_TYPE(optimization_guide::switches::kDebugLoggingEnabled)},

    {"optimization-guide-on-device-model",
     flag_descriptions::kOptimizationGuideOnDeviceModelName,
     flag_descriptions::kOptimizationGuideOnDeviceModelDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         optimization_guide::features::kOnDeviceModelPerformanceParams,
         kOptimizationGuideOnDeviceModelVariations,
         "OptimizationGuideOnDeviceModel")},

    {"optimization-guide-on-device-model-android",
     flag_descriptions::kOptimizationGuideOnDeviceModelAndroidName,
     flag_descriptions::kOptimizationGuideOnDeviceModelAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         optimization_guide::features::kOptimizationGuideOnDeviceModel)},

    {"text-safety-classifier", flag_descriptions::kTextSafetyClassifierName,
     flag_descriptions::kTextSafetyClassifierDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         optimization_guide::features::kTextSafetyClassifier,
         kTextSafetyClassifierVariations,
         "TextSafetyClassifier")},

    {"organic-repeatable-queries",
     flag_descriptions::kOrganicRepeatableQueriesName,
     flag_descriptions::kOrganicRepeatableQueriesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(history::kOrganicRepeatableQueries,
                                    kOrganicRepeatableQueriesVariations,
                                    "OrganicRepeatableQueries")},

    {"omnibox-num-ntp-zps-recent-searches",
     flag_descriptions::kOmniboxNumNtpZpsRecentSearchesName,
     flag_descriptions::kOmniboxNumNtpZpsRecentSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumNtpZpsRecentSearches,
                                    kNumNtpZpsRecentSearches,
                                    "PowerTools")},
    {"omnibox-num-ntp-zps-trending-searches",
     flag_descriptions::kOmniboxNumNtpZpsTrendingSearchesName,
     flag_descriptions::kOmniboxNumNtpZpsTrendingSearchesDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumNtpZpsTrendingSearches,
                                    kNumNtpZpsTrendingSearches,
                                    "PowerTools")},
    {"omnibox-num-web-zps-recent-searches",
     flag_descriptions::kOmniboxNumWebZpsRecentSearchesName,
     flag_descriptions::kOmniboxNumWebZpsRecentSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumWebZpsRecentSearches,
                                    kNumWebZpsRecentSearches,
                                    "PowerTools")},
    {"omnibox-num-web-zps-related-searches",
     flag_descriptions::kOmniboxNumWebZpsRelatedSearchesName,
     flag_descriptions::kOmniboxNumWebZpsRelatedSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumWebZpsRelatedSearches,
                                    kNumWebZpsRelatedSearches,
                                    "PowerTools")},
    {"omnibox-num-web-zps-most-visited-urls",
     flag_descriptions::kOmniboxNumWebZpsMostVisitedUrlsName,
     flag_descriptions::kOmniboxNumWebZpsMostVisitedUrlsDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumWebZpsMostVisitedUrls,
                                    kNumWebZpsMostVisitedUrls,
                                    "PowerTools")},
    {"omnibox-num-srp-zps-recent-searches",
     flag_descriptions::kOmniboxNumSrpZpsRecentSearchesName,
     flag_descriptions::kOmniboxNumSrpZpsRecentSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumSrpZpsRecentSearches,
                                    kNumSrpZpsRecentSearches,
                                    "PowerTools")},
    {"omnibox-num-srp-zps-related-searches",
     flag_descriptions::kOmniboxNumSrpZpsRelatedSearchesName,
     flag_descriptions::kOmniboxNumSrpZpsRelatedSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumSrpZpsRelatedSearches,
                                    kNumSrpZpsRelatedSearches,
                                    "PowerTools")},
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
    {"history-embeddings", flag_descriptions::kHistoryEmbeddingsName,
     flag_descriptions::kHistoryEmbeddingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(history_embeddings::kHistoryEmbeddings)},
    {"history-embeddings-answers",
     flag_descriptions::kHistoryEmbeddingsAnswersName,
     flag_descriptions::kHistoryEmbeddingsAnswersDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(history_embeddings::kHistoryEmbeddingsAnswers)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

    {"history-journeys", flag_descriptions::kJourneysName,
     flag_descriptions::kJourneysDescription, kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(history_clusters::internal::kJourneys,
                                    kJourneysVariations,
                                    "HistoryJourneys")},

    {"annotated-page-content-extraction",
     flag_descriptions::kAnnotatedPageContentExtractionName,
     flag_descriptions::kAnnotatedPageContentExtractionDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(
         page_content_annotations::features::kAnnotatedPageContentExtraction)},

    {"extract-related-searches-from-prefetched-zps-response",
     flag_descriptions::kExtractRelatedSearchesFromPrefetchedZPSResponseName,
     flag_descriptions::
         kExtractRelatedSearchesFromPrefetchedZPSResponseDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(page_content_annotations::features::
                            kExtractRelatedSearchesFromPrefetchedZPSResponse)},

    {"page-content-annotations", flag_descriptions::kPageContentAnnotationsName,
     flag_descriptions::kPageContentAnnotationsDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         page_content_annotations::features::kPageContentAnnotations,
         kPageContentAnnotationsVariations,
         "PageContentAnnotations")},

    {"page-content-annotations-remote-page-metadata",
     flag_descriptions::kPageContentAnnotationsRemotePageMetadataName,
     flag_descriptions::kPageContentAnnotationsRemotePageMetadataDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         page_content_annotations::features::kRemotePageMetadata,
         kRemotePageMetadataVariations,
         "RemotePageMetadata")},

    {"page-content-cache", flag_descriptions::kPageContentCacheName,
     flag_descriptions::kPageContentCacheDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(page_content_annotations::features::kPageContentCache)},


    {"mbi-mode", flag_descriptions::kMBIModeName,
     flag_descriptions::kMBIModeDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kMBIMode,
                                    kMBIModeVariations,
                                    "MBIMode")},


    {"tab-groups-focusing", flag_descriptions::kTabGroupsFocusingName,
     flag_descriptions::kTabGroupsFocusingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kTabGroupsFocusing,
                                    kTabGroupsFocusingVariations,
                                    "TabGroupsFocusing")},

    {"vertical-tabs", flag_descriptions::kVerticalTabsName,
     flag_descriptions::kVerticalTabsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(tabs::kVerticalTabs)},

    {"side-panel-flyover-animation",
     flag_descriptions::kSidePanelFlyoverAnimationName,
     flag_descriptions::kSidePanelFlyoverAnimationDescription,
     kOsWin | kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kSidePanelFlyoverAnimation)},



    {"by-date-history-in-side-panel",
     flag_descriptions::kByDateHistoryInSidePanelName,
     flag_descriptions::kByDateHistoryInSidePanelDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kByDateHistoryInSidePanel)},

#if BUILDFLAG(IS_MAC)
    {"ca-display-link-in-browser",
     flag_descriptions::kCADisplayLinkInBrowserName,
     flag_descriptions::kCADisplayLinkInBrowserDescription, kOsMac,
     FEATURE_VALUE_TYPE(display::features::kCADisplayLinkInBrowser)},
#endif


    {"shopping-list", commerce::flag_descriptions::kShoppingListName,
     commerce::flag_descriptions::kShoppingListDescription,
     kOsAndroid | kOsDesktop, FEATURE_VALUE_TYPE(commerce::kShoppingList)},

    {"shopping-alternate-server",
     commerce::flag_descriptions::kShoppingAlternateServerName,
     commerce::flag_descriptions::kShoppingAlternateServerDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kShoppingAlternateServer)},

    {"price-tracking-subscription-service-locale-key",
     commerce::flag_descriptions::
         kPriceTrackingSubscriptionServiceLocaleKeyName,
     commerce::flag_descriptions::
         kPriceTrackingSubscriptionServiceLocaleKeyDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kPriceTrackingSubscriptionServiceLocaleKey)},

    {"price-tracking-subscription-service-product-version",
     commerce::flag_descriptions::
         kPriceTrackingSubscriptionServiceProductVersionName,
     commerce::flag_descriptions::
         kPriceTrackingSubscriptionServiceProductVersionDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(
         commerce::kPriceTrackingSubscriptionServiceProductVersion)},

    {"composebox-uses-chrome-compose-client",
     flag_descriptions::kNtpComposeboxUsesChromeComposeClientName,
     flag_descriptions::kNtpComposeboxUsesChromeComposeClientDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(omnibox::kComposeboxUsesChromeComposeClient)},

    {"ntp-alpha-background-collections",
     flag_descriptions::kNtpAlphaBackgroundCollectionsName,
     flag_descriptions::kNtpAlphaBackgroundCollectionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpAlphaBackgroundCollections)},

    {"ntp-background-image-error-detection",
     flag_descriptions::kNtpBackgroundImageErrorDetectionName,
     flag_descriptions::kNtpBackgroundImageErrorDetectionDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpBackgroundImageErrorDetection)},

    {"ntp-calendar-module", flag_descriptions::kNtpCalendarModuleName,
     flag_descriptions::kNtpCalendarModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpCalendarModule,
                                    kNtpCalendarModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-composebox", flag_descriptions::kNtpComposeboxName,
     flag_descriptions::kNtpComposeboxDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_composebox::kNtpComposebox,
                                    kNtpComposeboxVariations,
                                    "NtpComposebox")},

    {"ntp-realbox-next", flag_descriptions::kNtpRealboxNextName,
     flag_descriptions::kNtpRealboxNextDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_realbox::kNtpRealboxNext,
                                    kNtpRealboxNextVariations,
                                    "NtpRealboxNext")},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
    {"ntp-customize-chrome-auto-open",
     flag_descriptions::kNtpCustomizeChromeAutoOpenName,
     flag_descriptions::kNtpCustomizeChromeAutoOpenDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpCustomizeChromeAutoOpen,
                                    kNtpCustomizeChromeAutoOpenVariations,
                                    "NtpCustomizeChromeAutoOpen")},
#endif

    {"ntp-drive-module", flag_descriptions::kNtpDriveModuleName,
     flag_descriptions::kNtpDriveModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpDriveModule,
                                    kNtpDriveModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-drive-module-segmentation",
     flag_descriptions::kNtpDriveModuleSegmentationName,
     flag_descriptions::kNtpDriveModuleSegmentationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpDriveModuleSegmentation)},

#if !defined(OFFICIAL_BUILD)
    {"ntp-dummy-modules", flag_descriptions::kNtpDummyModulesName,
     flag_descriptions::kNtpDummyModulesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpDummyModules)},
#endif

    {"ntp-feature-optimization-module-removal",
     flag_descriptions::kNtpFeatureOptimizationModuleRemovalName,
     flag_descriptions::kNtpFeatureOptimizationModuleRemovalDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ntp_features::kNtpFeatureOptimizationModuleRemoval,
         kNtpFeatureOptimizationModuleRemovalVariations,
         "NtpFeatureOptimizationModuleRemoval")},

    {"ntp-feature-optimization-shortcuts-removal",
     flag_descriptions::kNtpFeatureOptimizationShortcutsRemovalName,
     flag_descriptions::kNtpFeatureOptimizationShortcutsRemovalDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ntp_features::kNtpFeatureOptimizationShortcutsRemoval,
         kNtpFeatureOptimizationShortcutsRemovalVariations,
         "NtpFeatureOptimizationShortcutsRemoval")},

    {"ntp-feature-optimization-dismiss-modules-removal",
     flag_descriptions::kNtpFeatureOptimizationDismissModulesRemovalName,
     flag_descriptions::kNtpFeatureOptimizationDismissModulesRemovalDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         ntp_features::kNtpFeatureOptimizationDismissModulesRemoval)},

    {"ntp-footer", flag_descriptions::kNtpFooterName,
     flag_descriptions::kNtpFooterDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpFooter)},

    {"ntp-middle-slot-promo-dismissal",
     flag_descriptions::kNtpMiddleSlotPromoDismissalName,
     flag_descriptions::kNtpMiddleSlotPromoDismissalDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpMiddleSlotPromoDismissal,
                                    kNtpMiddleSlotPromoDismissalVariations,
                                    "DesktopNtpModules")},

    {"ntp-module-sign-in-requirement",
     flag_descriptions::kNtpModuleSignInRequirementName,
     flag_descriptions::kNtpModuleSignInRequirementDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpModuleSignInRequirement)},

    {"ntp-next-features", flag_descriptions::kNtpNextFeaturesName,
     flag_descriptions::kNtpNextFeaturesDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpNextFeatures,
                                    kNtpNextVariations,
                                    "NtpNextFeatures")},

    {"ntp-modules-drag-and-drop", flag_descriptions::kNtpModulesDragAndDropName,
     flag_descriptions::kNtpModulesDragAndDropDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpModulesDragAndDrop)},

    {"ntp-ogb-async-bar-parts",
     flag_descriptions::kNtpOneGoogleBarAsyncBarPartsName,
     flag_descriptions::kNtpOneGoogleBarAsyncBarPartsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpOneGoogleBarAsyncBarParts)},

    {"ntp-outlook-calendar-module",
     flag_descriptions::kNtpOutlookCalendarModuleName,
     flag_descriptions::kNtpOutlookCalendarModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpOutlookCalendarModule,
                                    kNtpOutlookCalendarModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-realbox-cr23-theming", flag_descriptions::kNtpRealboxCr23ThemingName,
     flag_descriptions::kNtpRealboxCr23ThemingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kRealboxCr23Theming,
                                    kNtpRealboxCr23ThemingVariations,
                                    "NtpRealboxCr23Theming")},

    {"ntp-safe-browsing-module", flag_descriptions::kNtpSafeBrowsingModuleName,
     flag_descriptions::kNtpSafeBrowsingModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpSafeBrowsingModule,
                                    kNtpSafeBrowsingModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-sharepoint-module", flag_descriptions::kNtpSharepointModuleName,
     flag_descriptions::kNtpSharepointModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpSharepointModule,
                                    kNtpSharepointModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-tab-groups-module", flag_descriptions::kNtpTabGroupsModuleName,
     flag_descriptions::kNtpTabGroupsModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpTabGroupsModule,
                                    kNtpTabGroupsModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-tab-groups-module-zero-state",
     flag_descriptions::kNtpTabGroupsModuleZeroStateName,
     flag_descriptions::kNtpTabGroupsModuleZeroStateDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpTabGroupsModuleZeroState)},

    {"ntp-wallpaper-search-button",
     flag_descriptions::kNtpWallpaperSearchButtonName,
     flag_descriptions::kNtpWallpaperSearchButtonDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpWallpaperSearchButton)},

    {"ntp-wallpaper-search-button-animation",
     flag_descriptions::kNtpWallpaperSearchButtonAnimationName,
     flag_descriptions::kNtpWallpaperSearchButtonAnimationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpWallpaperSearchButtonAnimation)},

    {"ntp-microsoft-authentication-module",
     flag_descriptions::kNtpMicrosoftAuthenticationModuleName,
     flag_descriptions::kNtpMicrosoftAuthenticationModuleDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpMicrosoftAuthenticationModule)},


#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
    {"chrome-wide-echo-cancellation",
     flag_descriptions::kChromeWideEchoCancellationName,
     flag_descriptions::kChromeWideEchoCancellationDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(media::kChromeWideEchoCancellation)},
#endif  // BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)

#if BUILDFLAG(DCHECK_IS_CONFIGURABLE)
    {"dcheck-is-fatal", flag_descriptions::kDcheckIsFatalName,
     flag_descriptions::kDcheckIsFatalDescription, kOsWin,
     FEATURE_VALUE_TYPE(base::kDCheckIsFatalFeature)},
#endif  // BUILDFLAG(DCHECK_IS_CONFIGURABLE)

    {"enable-pixel-canvas-recording",
     flag_descriptions::kEnablePixelCanvasRecordingName,
     flag_descriptions::kEnablePixelCanvasRecordingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kEnablePixelCanvasRecording)},


    {"enable-parallel-downloading", flag_descriptions::kParallelDownloadingName,
     flag_descriptions::kParallelDownloadingDescription, kOsAll,
     FEATURE_VALUE_TYPE(download::features::kParallelDownloading)},
    {"download-notification-service-unified-api",
     flag_descriptions::kDownloadNotificationServiceUnifiedAPIName,
     flag_descriptions::kDownloadNotificationServiceUnifiedAPIDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         download::features::kDownloadNotificationServiceUnifiedAPI)},

    {"enable-network-logging-to-file",
     flag_descriptions::kEnableNetworkLoggingToFileName,
     flag_descriptions::kEnableNetworkLoggingToFileDescription, kOsAll,
     SINGLE_VALUE_TYPE(net::switches::kLogNetLog)},

    {"web-authentication-permit-enterprise-attestation",
     flag_descriptions::kWebAuthenticationPermitEnterpriseAttestationName,
     flag_descriptions::
         kWebAuthenticationPermitEnterpriseAttestationDescription,
     kOsAll,
     ORIGIN_LIST_VALUE_TYPE(
         webauthn::switches::kPermitEnterpriseAttestationOriginList,
         "")},

    {"exclude-pip-from-screen-capture",
     flag_descriptions::kExcludePipFromScreenCaptureName,
     flag_descriptions::kExcludePipFromScreenCaptureDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kExcludePipFromScreenCapture)},

#if BUILDFLAG(ENABLE_PDF)
    {"accessible-pdf-form", flag_descriptions::kAccessiblePDFFormName,
     flag_descriptions::kAccessiblePDFFormDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kAccessiblePDFForm)},

    {"pdf-oopif", flag_descriptions::kPdfOopifName,
     flag_descriptions::kPdfOopifDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfOopif)},

    {"pdf-portfolio", flag_descriptions::kPdfPortfolioName,
     flag_descriptions::kPdfPortfolioDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfPortfolio)},

    {"pdf-use-skia-renderer", flag_descriptions::kPdfUseSkiaRendererName,
     flag_descriptions::kPdfUseSkiaRendererDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfUseSkiaRenderer)},

#if BUILDFLAG(ENABLE_PDF_INK2)
    {"pdf-ink2", flag_descriptions::kPdfInk2Name,
     flag_descriptions::kPdfInk2Description, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome_pdf::features::kPdfInk2,
                                    kPdfInk2Variations,
                                    "PdfInk2")},
#endif  // BUILDFLAG(ENABLE_PDF_INK2)

#if BUILDFLAG(ENABLE_PDF_SAVE_TO_DRIVE)
    {"pdf-save-to-drive", flag_descriptions::kPdfSaveToDriveName,
     flag_descriptions::kPdfSaveToDriveDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfSaveToDrive)},
#endif  // BUILDFLAG(ENABLE_PDF_SAVE_TO_DRIVE)

#endif  // BUILDFLAG(ENABLE_PDF)


#if BUILDFLAG(ENABLE_PRINTING)
#if BUILDFLAG(IS_LINUX)
    {"cups-ipp-printing-backend",
     flag_descriptions::kCupsIppPrintingBackendName,
     flag_descriptions::kCupsIppPrintingBackendDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(printing::features::kCupsIppPrintingBackend)},
#endif  // BUILDFLAG(IS_LINUX)

#endif  // BUILDFLAG(ENABLE_PRINTING)



    {"gemini-antiscam-protections-metrics-only",
     flag_descriptions::kGeminiAntiscamProtectionsMetricsOnlyName,
     flag_descriptions::kGeminiAntiscamProtectionsMetricsOnlyDescription,
     kOsMac | kOsWin | kOsCrOS | kOsAndroid | kOsLinux,
     FEATURE_VALUE_TYPE(
         safe_browsing::kGeminiAntiscamProtectionForMetricsCollection)},

    {"report-notification-content-detection-data",
     flag_descriptions::kReportNotificationContentDetectionDataName,
     flag_descriptions::kReportNotificationContentDetectionDataDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         safe_browsing::kReportNotificationContentDetectionData,
         kReportNotificationContentDetectionDataVariations,
         "ReportNotificationContentDetectionData")},

    {"report-unsafe-site", flag_descriptions::kReportUnsafeSiteName,
     flag_descriptions::kReportUnsafeSiteDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReportUnsafeSite)},

    {"show-warnings-for-suspicious-notifications",
     flag_descriptions::kShowWarningsForSuspiciousNotificationsName,
     flag_descriptions::kShowWarningsForSuspiciousNotificationsDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         safe_browsing::kShowWarningsForSuspiciousNotifications,
         kShowWarningsForSuspiciousNotificationsVariations,
         "ShowWarningsForSuspiciousNotifications")},

    {"unsafely-treat-insecure-origin-as-secure",
     flag_descriptions::kTreatInsecureOriginAsSecureName,
     flag_descriptions::kTreatInsecureOriginAsSecureDescription, kOsAll,
     ORIGIN_LIST_VALUE_TYPE(
         network::switches::kUnsafelyTreatInsecureOriginAsSecure,
         "")},

    {"disable-process-reuse", flag_descriptions::kDisableProcessReuse,
     flag_descriptions::kDisableProcessReuseDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDisableProcessReuse)},


    {"enable-headless-live-caption",
     flag_descriptions::kEnableHeadlessLiveCaptionName,
     flag_descriptions::kEnableHeadlessLiveCaptionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kHeadlessLiveCaption)},

    {"enable-media-link-helpers",
     flag_descriptions::kEnableMediaLinkHelpersName,
     flag_descriptions::kEnableMediaLinkHelpersDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kMediaLinkHelpers)},

    {"enable-headless-live-caption-early-start",
     flag_descriptions::kHeadlessCaptionEarlyStartName,
     flag_descriptions::kHeadlessCaptionEarlyStartDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kHeadlessCaptionEarlyStart)},


    {"read-anything-read-aloud-phrase-highlighting",
     flag_descriptions::kReadAnythingReadAloudPhraseHighlightingName,
     flag_descriptions::kReadAnythingReadAloudPhraseHighlightingDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingReadAloudPhraseHighlighting)},

    {"read-anything-images-via-algorithm",
     flag_descriptions::kReadAnythingImagesViaAlgorithmName,
     flag_descriptions::kReadAnythingImagesViaAlgorithmDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingImagesViaAlgorithm)},

    {"read-anything-docs-integration",
     flag_descriptions::kReadAnythingDocsIntegrationName,
     flag_descriptions::kReadAnythingDocsIntegrationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingDocsIntegration)},

    {"read-anything-docs-load-more-button",
     flag_descriptions::kReadAnythingDocsLoadMoreButtonName,
     flag_descriptions::kReadAnythingDocsLoadMoreButtonDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingDocsLoadMoreButton)},

    {"enable-auto-disable-accessibility",
     flag_descriptions::kEnableAutoDisableAccessibilityName,
     flag_descriptions::kEnableAutoDisableAccessibilityDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kAutoDisableAccessibility)},

    {"image-descriptions-alternative-routing",
     flag_descriptions::kImageDescriptionsAlternateRoutingName,
     flag_descriptions::kImageDescriptionsAlternateRoutingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kImageDescriptionsAlternateRouting)},







    {"boundary-event-dispatch-tracks-node-removal",
     flag_descriptions::kBoundaryEventDispatchTracksNodeRemovalName,
     flag_descriptions::kBoundaryEventDispatchTracksNodeRemovalDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kBoundaryEventDispatchTracksNodeRemoval)},

    // Should only be available if kResamplingScrollEvents is on, and using
    // linear resampling.
    {"enable-resampling-scroll-events-experimental-prediction",
     flag_descriptions::kEnableResamplingScrollEventsExperimentalPredictionName,
     flag_descriptions::
         kEnableResamplingScrollEventsExperimentalPredictionDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ::features::kResamplingScrollEventsExperimentalPrediction,
         kResamplingScrollEventsExperimentalPredictionVariations,
         "ResamplingScrollEventsExperimentalLatency")},

    {"happiness-tracking-surveys-for-desktop-demo",
     flag_descriptions::kHappinessTrackingSurveysForDesktopDemoName,
     flag_descriptions::kHappinessTrackingSurveysForDesktopDemoDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kHappinessTrackingSurveysForDesktopDemo,
         kHappinessTrackingSurveysForDesktopDemoVariations,
         "HappinessTrackingSurveysForDesktopDemo")},




    {"enable-gamepad-multitouch",
     flag_descriptions::kEnableGamepadMultitouchName,
     flag_descriptions::kEnableGamepadMultitouchDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kEnableGamepadMultitouch)},

    {"sharing-desktop-screenshots",
     flag_descriptions::kSharingDesktopScreenshotsName,
     flag_descriptions::kSharingDesktopScreenshotsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(sharing_hub::kDesktopScreenshots)},


    {"enable-gpu-service-logging",
     flag_descriptions::kEnableGpuServiceLoggingName,
     flag_descriptions::kEnableGpuServiceLoggingDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableGPUServiceLogging)},

    {"hardware-media-key-handling",
     flag_descriptions::kHardwareMediaKeyHandling,
     flag_descriptions::kHardwareMediaKeyHandlingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kHardwareMediaKeyHandling)},



    {"file-handling-icons", flag_descriptions::kFileHandlingIconsName,
     flag_descriptions::kFileHandlingIconsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kFileHandlingIcons)},

    {"strict-origin-isolation", flag_descriptions::kStrictOriginIsolationName,
     flag_descriptions::kStrictOriginIsolationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kStrictOriginIsolation)},



    {"enable-fenced-frames-developer-mode",
     flag_descriptions::kEnableFencedFramesDeveloperModeName,
     flag_descriptions::kEnableFencedFramesDeveloperModeDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kFencedFramesDefaultMode)},

    {"enable-unsafe-webgpu", flag_descriptions::kUnsafeWebGPUName,
     flag_descriptions::kUnsafeWebGPUDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableUnsafeWebGPU)},

    {"force-high-performance-gpu",
     flag_descriptions::kForceHighPerformanceGPUName,
     flag_descriptions::kForceHighPerformanceGPUDescription, kOsWin,
     SINGLE_VALUE_TYPE(switches::kForceHighPerformanceGPU)},

    {"enable-webgpu-developer-features",
     flag_descriptions::kWebGpuDeveloperFeaturesName,
     flag_descriptions::kWebGpuDeveloperFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebGPUDeveloperFeatures)},


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"enable-network-service-sandbox",
     flag_descriptions::kEnableNetworkServiceSandboxName,
     flag_descriptions::kEnableNetworkServiceSandboxDescription,
     kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(sandbox::policy::features::kNetworkServiceSandbox)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
    {"use-out-of-process-video-decoding",
     flag_descriptions::kUseOutOfProcessVideoDecodingName,
     flag_descriptions::kUseOutOfProcessVideoDecodingDescription,
     kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(media::kUseOutOfProcessVideoDecoding)},
    {"use-shared-image-in-oop-vd",
     flag_descriptions::kUseSharedImageInOOPVDName,
     flag_descriptions::kUseSharedImageInOOPVDDescription, kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(media::kUseSharedImageInOOPVDProcess)},
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)




    {"element-capture-cross-tab",
     flag_descriptions::kCrossTabElementCaptureName,
     flag_descriptions::kCrossTabElementCaptureDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kElementCaptureOfOtherTabs)},

    {"device-posture", flag_descriptions::kDevicePostureName,
     flag_descriptions::kDevicePostureDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kDevicePosture)},

    {"viewport-segments", flag_descriptions::kViewportSegmentsName,
     flag_descriptions::kViewportSegmentsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kViewportSegments)},

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {"enable-location-provider-manager",
     flag_descriptions::kLocationProviderManagerName,
     flag_descriptions::kLocationProviderManagerDescription, kOsMac | kOsWin,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kLocationProviderManager,
                                    kLocationProviderManagerVariations,
                                    "LocationProviderManager")},
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

    {"mute-notification-snooze-action",
     flag_descriptions::kMuteNotificationSnoozeActionName,
     flag_descriptions::kMuteNotificationSnoozeActionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMuteNotificationSnoozeAction)},

    {"notification-one-tap-unsubscribe-on-desktop",
     flag_descriptions::kNotificationOneTapUnsubscribeOnDesktopName,
     flag_descriptions::kNotificationOneTapUnsubscribeOnDesktopDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kNotificationOneTapUnsubscribeOnDesktop)},

#if BUILDFLAG(IS_MAC)
    {"enable-new-mac-notification-api",
     flag_descriptions::kNewMacNotificationAPIName,
     flag_descriptions::kNewMacNotificationAPIDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kNewMacNotificationAPI)},
#endif


    {"heavy-ad-privacy-mitigations",
     flag_descriptions::kHeavyAdPrivacyMitigationsName,
     flag_descriptions::kHeavyAdPrivacyMitigationsDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         heavy_ad_intervention::features::kHeavyAdPrivacyMitigations)},




#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
    {"run-video-capture-service-in-browser",
     flag_descriptions::kRunVideoCaptureServiceInBrowserProcessName,
     flag_descriptions::kRunVideoCaptureServiceInBrowserProcessDescription,
     kOsWin | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kRunVideoCaptureServiceInBrowserProcess)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
    {"double-buffer-compositing",
     flag_descriptions::kDoubleBufferCompositingName,
     flag_descriptions::kDoubleBufferCompositingDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kDoubleBufferCompositing)},



    {"enable-experimental-cookie-features",
     flag_descriptions::kEnableExperimentalCookieFeaturesName,
     flag_descriptions::kEnableExperimentalCookieFeaturesDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableExperimentalCookieFeaturesChoices)},

    {"enable-extension-install-policy-fetching",
     flag_descriptions::kEnableExtensionInstallPolicyFetchingName,
     flag_descriptions::kEnableExtensionInstallPolicyFetchingDescription,
     kOsWin | kOsMac | kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(
         policy::features::kEnableExtensionInstallPolicyFetching)},

    {"canvas-2d-layers", flag_descriptions::kCanvas2DLayersName,
     flag_descriptions::kCanvas2DLayersDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableCanvas2DLayers)},

    {"web-machine-learning-neural-network",
     flag_descriptions::kWebMachineLearningNeuralNetworkName,
     flag_descriptions::kWebMachineLearningNeuralNetworkDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         webnn::mojom::features::kWebMachineLearningNeuralNetwork)},

    {"experimental-web-machine-learning-neural-network",
     flag_descriptions::kExperimentalWebMachineLearningNeuralNetworkName,
     flag_descriptions::kExperimentalWebMachineLearningNeuralNetworkDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         webnn::mojom::features::kExperimentalWebMachineLearningNeuralNetwork)},

#if BUILDFLAG(IS_MAC)
    {"webnn-coreml", flag_descriptions::kWebNNCoreMLName,
     flag_descriptions::kWebNNCoreMLDescription, kOsMac,
     FEATURE_VALUE_TYPE(webnn::mojom::features::kWebNNCoreML)},

    {"webnn-coreml-explicit-gpu-or-npu",
     flag_descriptions::kWebNNCoreMLExplicitGPUOrNPUName,
     flag_descriptions::kWebNNCoreMLExplicitGPUOrNPUDescription, kOsMac,
     FEATURE_VALUE_TYPE(webnn::mojom::features::kWebNNCoreMLExplicitGPUOrNPU)},
#endif  // BUILDFLAG(IS_MAC)



    {"left-hand-side-activity-indicators",
     flag_descriptions::kLeftHandSideActivityIndicatorsName,
     flag_descriptions::kLeftHandSideActivityIndicatorsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         content_settings::features::kLeftHandSideActivityIndicators)},

    {"privacy-policy-insights", flag_descriptions::kPrivacyPolicyInsightsName,
     flag_descriptions::kPrivacyPolicyInsightsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(page_info::kPrivacyPolicyInsights)},




    {"pwa-update-dialog-for-icon",
     flag_descriptions::kPwaUpdateDialogForAppIconName,
     flag_descriptions::kPwaUpdateDialogForAppIconDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kPwaUpdateDialogForIcon)},

#if BUILDFLAG(ENABLE_OOP_PRINTING)
    {"enable-oop-print-drivers", flag_descriptions::kEnableOopPrintDriversName,
     flag_descriptions::kEnableOopPrintDriversDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(printing::features::kEnableOopPrintDrivers)},
#endif


    {"privacy-sandbox-internals",
     flag_descriptions::kPrivacySandboxInternalsName,
     flag_descriptions::kPrivacySandboxInternalsDescription, kOsAll,
     FEATURE_VALUE_TYPE(privacy_sandbox::kPrivacySandboxInternalsDevUI)},

    {"sct-auditing", flag_descriptions::kSCTAuditingName,
     flag_descriptions::kSCTAuditingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kSCTAuditing,
                                    kSCTAuditingVariations,
                                    "SCTAuditingVariations")},


    {"prerender-early-document-lifecycle-update",
     flag_descriptions::kPrerender2EarlyDocumentLifecycleUpdateName,
     flag_descriptions::kPrerender2EarlyDocumentLifecycleUpdateDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kPrerender2EarlyDocumentLifecycleUpdate)},

    {"trees-in-viz", flag_descriptions::kTreesInVizName,
     flag_descriptions::kTreesInVizDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kTreesInViz)},

    {"omnibox-search-prefetch",
     flag_descriptions::kEnableOmniboxSearchPrefetchName,
     flag_descriptions::kEnableOmniboxSearchPrefetchDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(kSearchPrefetchServicePrefetching,
                                    kSearchPrefetchServicePrefetchingVariations,
                                    "SearchSuggestionPrefetch")},
    {"omnibox-search-client-prefetch",
     flag_descriptions::kEnableOmniboxClientSearchPrefetchName,
     flag_descriptions::kEnableOmniboxClientSearchPrefetchDescription, kOsAll,
     FEATURE_VALUE_TYPE(kSearchNavigationPrefetch)},



#if BUILDFLAG(ENABLE_PDF)
    {"pdf-xfa-forms", flag_descriptions::kPdfXfaFormsName,
     flag_descriptions::kPdfXfaFormsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfXfaSupport)},
#endif  // BUILDFLAG(ENABLE_PDF)

    {"enable-managed-configuration-web-api",
     flag_descriptions::kEnableManagedConfigurationWebApiName,
     flag_descriptions::kEnableManagedConfigurationWebApiDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(blink::features::kManagedConfiguration)},


    {"enable-global-vaapi-lock", flag_descriptions::kGlobalVaapiLockName,
     flag_descriptions::kGlobalVaapiLockDescription, kOsCrOS | kOsLinux,
     FEATURE_VALUE_TYPE(media::kGlobalVaapiLock)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
    {
        "ui-debug-tools",
        flag_descriptions::kUIDebugToolsName,
        flag_descriptions::kUIDebugToolsDescription,
        kOsWin | kOsLinux | kOsMac,
        FEATURE_VALUE_TYPE(features::kUIDebugTools),
    },

#endif

    {"fedcm-autofill", flag_descriptions::kFedCmAutofillName,
     flag_descriptions::kFedCmAutofillDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmAutofill)},

    {"fedcm-delegation", flag_descriptions::kFedCmDelegationName,
     flag_descriptions::kFedCmDelegationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmDelegation)},

    {"email-verification-protocol",
     flag_descriptions::kEmailVerificationProtocolName,
     flag_descriptions::kEmailVerificationProtocolDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kEmailVerificationProtocol)},

    {"fedcm-error-attribute", flag_descriptions::kFedCmErrorAttributeName,
     flag_descriptions::kFedCmErrorAttributeDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmErrorAttribute)},

    {"fedcm-idp-registration", flag_descriptions::kFedCmIdPRegistrationName,
     flag_descriptions::kFedCmIdPRegistrationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kFedCmIdPRegistration)},

    {"fedcm-in-authenticator", flag_descriptions::kFedCmInAuthenticatorName,
     flag_descriptions::kFedCmInAuthenticatorDescription, kOsAll,
     FEATURE_VALUE_TYPE(device::kFedCmInAuthenticator)},

    {"fedcm-lightweight-mode", flag_descriptions::kFedCmLightweightModeName,
     flag_descriptions::kFedCmLightweightModeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kFedCmLightweightMode)},

    {"fedcm-metrics-endpoint", flag_descriptions::kFedCmMetricsEndpointName,
     flag_descriptions::kFedCmMetricsEndpointDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmMetricsEndpoint)},

    {"fedcm-nonce-in-params", flag_descriptions::kFedCmNonceInParamsName,
     flag_descriptions::kFedCmNonceInParamsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmNonceInParams)},

    {"fedcm-well-known-endpoint-validation",
     flag_descriptions::kFedCmWellKnownEndpointValidationName,
     flag_descriptions::kFedCmWellKnownEndpointValidationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmWellKnownEndpointValidation)},

    {"fedcm-without-well-known-enforcement",
     flag_descriptions::kFedCmWithoutWellKnownEnforcementName,
     flag_descriptions::kFedCmWithoutWellKnownEnforcementDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmWithoutWellKnownEnforcement)},

    {"fedcm-segmentation-platform",
     flag_descriptions::kFedCmSegmentationPlatformName,
     flag_descriptions::kFedCmSegmentationPlatformDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         segmentation_platform::features::kSegmentationPlatformFedCmUser)},

    {"fedcm-navigation-interception",
     flag_descriptions::kFedCmNavigationInterceptionName,
     flag_descriptions::kFedCmNavigationInterceptionDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmNavigationInterception)},

    {"web-identity-digital-credentials",
     flag_descriptions::kWebIdentityDigitalCredentialsName,
     flag_descriptions::kWebIdentityDigitalCredentialsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kWebIdentityDigitalCredentials,
         kWebIdentityDigitalIdentityCredentialVariations,
         "WebIdentityDigitalCredentials")},

    {"web-identity-digital-credentials-creation",
     flag_descriptions::kWebIdentityDigitalCredentialsCreationName,
     flag_descriptions::kWebIdentityDigitalCredentialsCreationDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(features::kWebIdentityDigitalCredentialsCreation)},


    {"lens-enable-raw-file-media-types",
     flag_descriptions::kLensEnableSendRawFileMediaTypesName,
     flag_descriptions::kLensEnableSendRawFileMediaTypesDescription, kOsAll,
     FEATURE_VALUE_TYPE(lens::features::kLensSendRawFileMediaTypes)},

    {"lens-enable-urls-in-composeboxes",
     flag_descriptions::kLensEnableSendUrlsInComposeboxesName,
     flag_descriptions::kLensEnableSendUrlsInComposeboxesDescription, kOsAll,
     FEATURE_VALUE_TYPE(lens::features::kLensSendUrlsInComposeboxes)},

    {flag_descriptions::kEnableLensStandaloneFlagId,
     flag_descriptions::kEnableLensStandaloneName,
     flag_descriptions::kEnableLensStandaloneDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensStandalone)},

    {"enable-lens-overlay", flag_descriptions::kLensOverlayName,
     flag_descriptions::kLensOverlayDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(lens::features::kLensOverlay,
                                    kLensOverlayVariations,
                                    "LensOverlay")},


    {"bind-cookies-to-port", flag_descriptions::kBindCookiesToPortName,
     flag_descriptions::kBindCookiesToPortDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kEnablePortBoundCookies)},

    {"bind-cookies-to-scheme", flag_descriptions::kBindCookiesToSchemeName,
     flag_descriptions::kBindCookiesToSchemeDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kEnableSchemeBoundCookies)},

    {"align-wakeups", flag_descriptions::kAlignWakeUpsName,
     flag_descriptions::kAlignWakeUpsDescription, kOsAll,
     FEATURE_VALUE_TYPE(base::kAlignWakeUps)},

#if BUILDFLAG(ENABLE_VALIDATING_COMMAND_DECODER)
    {"use-passthrough-command-decoder",
     flag_descriptions::kUsePassthroughCommandDecoderName,
     flag_descriptions::kUsePassthroughCommandDecoderDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDefaultPassthroughCommandDecoder)},
#endif  // BUILDFLAG(ENABLE_VALIDATING_COMMAND_DECODER)

    {"use-primary-and-tonal-buttons-for-promos",
     flag_descriptions::kUsePrimaryAndTonalButtonsForPromosName,
     flag_descriptions::kUsePrimaryAndTonalButtonsForPromosDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(switches::kUsePrimaryAndTonalButtonsForPromos)},

#if BUILDFLAG(ENABLE_SWIFTSHADER)
    {"enable-unsafe-swiftshader",
     flag_descriptions::kEnableUnsafeSwiftShaderName,
     flag_descriptions::kEnableUnsafeSwiftShaderDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableUnsafeSwiftShader)},
#endif  // BUILDFLAG(ENABLE_SWIFTSHADER)

    // The entry in kFeatureEntries
    {"policy-registration-delay",
     flag_descriptions::kPolicyRegistrationDelayName,
     flag_descriptions::kPolicyRegistrationDelayDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         policy::features::kCustomPolicyRegistrationDelay,
         kPolicyRegistrationDelayVariations,
         "CustomPolicyRegistrationDelay")},



    {"prerender2", flag_descriptions::kPrerender2Name,
     flag_descriptions::kPrerender2Description, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kPrerender2)},

    {"prerender2-reuse-host", flag_descriptions::kPrerender2ReuseHostName,
     flag_descriptions::kPrerender2ReuseHostDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kPrerender2ReuseHost)},

    {"prerender-until-script", flag_descriptions::kPrerenderUntilScriptName,
     flag_descriptions::kPrerenderUntilScriptDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kPrerenderUntilScript)},

    {"prerender-activation-by-form-submission",
     flag_descriptions::kPrerenderActivationByFormSubmissionName,
     flag_descriptions::kPrerenderActivationByFormSubmissiontDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kPrerenderActivationByFormSubmission)},


    {"test-third-party-cookie-phaseout",
     flag_descriptions::kTestThirdPartyCookiePhaseoutName,
     flag_descriptions::kTestThirdPartyCookiePhaseoutDescription, kOsAll,
     SINGLE_VALUE_TYPE(network::switches::kTestThirdPartyCookiePhaseout)},

    {"tpcd-heuristics-grants", flag_descriptions::kTpcdHeuristicsGrantsName,
     flag_descriptions::kTpcdHeuristicsGrantsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         content_settings::features::kTpcdHeuristicsGrants,
         kTpcdHeuristicsGrantsVariations,
         "TpcdHeuristicsGrants")},

    {"tpcd-metadata-grants", flag_descriptions::kTpcdMetadataGrantsName,
     flag_descriptions::kTpcdMetadataGrantsDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kTpcdMetadataGrants)},


    {"https-first-balanced-mode",
     flag_descriptions::kHttpsFirstBalancedModeName,
     flag_descriptions::kHttpsFirstBalancedModeDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstBalancedMode)},

    {"https-first-dialog-ui", flag_descriptions::kHttpsFirstDialogUiName,
     flag_descriptions::kHttpsFirstDialogUiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(security_interstitials::features::kHttpsFirstDialogUi)},

    {"https-first-mode-v2-for-engaged-sites",
     flag_descriptions::kHttpsFirstModeV2ForEngagedSitesName,
     flag_descriptions::kHttpsFirstModeV2ForEngagedSitesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstModeV2ForEngagedSites)},

    {"https-upgrades", flag_descriptions::kHttpsUpgradesName,
     flag_descriptions::kHttpsUpgradesDescription, kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsUpgrades)},

    {"https-first-mode-incognito",
     flag_descriptions::kHttpsFirstModeIncognitoName,
     flag_descriptions::kHttpsFirstModeIncognitoDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstModeIncognito)},

    {"https-first-mode-incognito-new-settings",
     flag_descriptions::kHttpsFirstModeIncognitoNewSettingsName,
     flag_descriptions::kHttpsFirstModeIncognitoNewSettingsDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstModeIncognitoNewSettings)},

    {"https-first-mode-for-typically-secure-users",
     flag_descriptions::kHttpsFirstModeForTypicallySecureUsersName,
     flag_descriptions::kHttpsFirstModeForTypicallySecureUsersDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstModeV2ForTypicallySecureUsers)},

    {"enable-drdc", flag_descriptions::kEnableDrDcName,
     flag_descriptions::kEnableDrDcDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kEnableDrDc)},


#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"experimental-omnibox-labs",
     flag_descriptions::kExperimentalOmniboxLabsName,
     flag_descriptions::kExperimentalOmniboxLabsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kExperimentalOmniboxLabs)},

    {kExtensionAiDataInternalName,
     flag_descriptions::kExtensionAiDataCollectionName,
     flag_descriptions::kExtensionAiDataCollectionDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kExtensionAiDataCollection)},

    {"extensions-collapse-main-menu",
     flag_descriptions::kExtensionsCollapseMainMenuName,
     flag_descriptions::kExtensionsCollapseMainMenuDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kExtensionsCollapseMainMenu)},

    {"extensions-menu-access-control",
     flag_descriptions::kExtensionsMenuAccessControlName,
     flag_descriptions::kExtensionsMenuAccessControlDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kExtensionsMenuAccessControl)},

    {"extensions-toolbar-zero-state-variation",
     flag_descriptions::kExtensionsToolbarZeroStateName,
     flag_descriptions::kExtensionsToolbarZeroStateDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kExtensionsToolbarZeroStateChoices)},

    {"iph-extensions-menu-feature",
     flag_descriptions::kIPHExtensionsMenuFeatureName,
     flag_descriptions::kIPHExtensionsMenuFeatureDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(feature_engagement::kIPHExtensionsMenuFeature)},

    {"iph-extensions-request-access-button-feature",
     flag_descriptions::kIPHExtensionsRequestAccessButtonFeatureName,
     flag_descriptions::kIPHExtensionsRequestAccessButtonFeatureDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         feature_engagement::kIPHExtensionsRequestAccessButtonFeature)},

    {"extension-manifest-v2-deprecation-disabled",
     flag_descriptions::kExtensionManifestV2DeprecationDisabledName,
     flag_descriptions::kExtensionManifestV2DeprecationDisabledDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kExtensionManifestV2Disabled)},

    {"extension-manifest-v2-deprecation-unsupported",
     flag_descriptions::kExtensionManifestV2DeprecationUnsupportedName,
     flag_descriptions::kExtensionManifestV2DeprecationUnsupportedDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kExtensionManifestV2Unsupported)},
#endif  // ENABLE_EXTENSIONS

    {"region-capture-cross-tab", flag_descriptions::kCrossTabRegionCaptureName,
     flag_descriptions::kCrossTabRegionCaptureDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kRegionCaptureOfOtherTabs)},

    {"skia-graphite", flag_descriptions::kSkiaGraphiteName,
     flag_descriptions::kSkiaGraphiteDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kSkiaGraphite,
                                    kSkiaGraphiteVariations,
                                    "SkiaGraphite")},

    {"skia-graphite-precompilation",
     flag_descriptions::kSkiaGraphitePrecompilationName,
     flag_descriptions::kSkiaGraphitePrecompilationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kSkiaGraphitePrecompilation)},

    {"enable-tab-audio-muting", flag_descriptions::kTabAudioMutingName,
     flag_descriptions::kTabAudioMutingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kEnableTabMuting)},

    {"customize-chrome-side-panel-extensions-card",
     flag_descriptions::kCustomizeChromeSidePanelExtensionsCardName,
     flag_descriptions::kCustomizeChromeSidePanelExtensionsCardDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kCustomizeChromeSidePanelExtensionsCard)},

    {"customize-chrome-wallpaper-search",
     flag_descriptions::kCustomizeChromeWallpaperSearchName,
     flag_descriptions::kCustomizeChromeWallpaperSearchDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kCustomizeChromeWallpaperSearch)},

    {"customize-chrome-wallpaper-search-button",
     flag_descriptions::kCustomizeChromeWallpaperSearchButtonName,
     flag_descriptions::kCustomizeChromeWallpaperSearchButtonDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kCustomizeChromeWallpaperSearchButton)},

    {"customize-chrome-wallpaper-search-inspiration-card",
     flag_descriptions::kCustomizeChromeWallpaperSearchInspirationCardName,
     flag_descriptions::
         kCustomizeChromeWallpaperSearchInspirationCardDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         ntp_features::kCustomizeChromeWallpaperSearchInspirationCard)},

    {"wallpaper-search-settings-visibility",
     flag_descriptions::kWallpaperSearchSettingsVisibilityName,
     flag_descriptions::kWallpaperSearchSettingsVisibilityDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(optimization_guide::features::internal::
                            kWallpaperSearchSettingsVisibility)},



#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_CHROMEOS)
    {"auto-picture-in-picture-for-video-playback",
     flag_descriptions::kAutoPictureInPictureForVideoPlaybackName,
     flag_descriptions::kAutoPictureInPictureForVideoPlaybackDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media::kAutoPictureInPictureForVideoPlayback)},

    {"document-picture-in-picture-animate-resize",
     flag_descriptions::kDocumentPictureInPictureAnimateResizeName,
     flag_descriptions::kDocumentPictureInPictureAnimateResizeDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media::kDocumentPictureInPictureAnimateResize)},

    {"browser-initiated-automatic-picture-in-picture",
     flag_descriptions::kBrowserInitiatedAutomaticPictureInPictureName,
     flag_descriptions::kBrowserInitiatedAutomaticPictureInPictureDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         blink::features::kBrowserInitiatedAutomaticPictureInPicture)},

    {"picture-in-picture-show-window-animation",
     flag_descriptions::kPictureInPictureShowWindowAnimationName,
     flag_descriptions::kPictureInPictureShowWindowAnimationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media::kPictureInPictureShowWindowAnimation)},

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_CHROMEOS)


    {"document-patching", flag_descriptions::kDocumentPatchingName,
     flag_descriptions::kDocumentPatchingDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kDocumentPatching)},

    {"route-matching", flag_descriptions::kRouteMatchingName,
     flag_descriptions::kRouteMatchingDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kRouteMatching)},

    {"dse-preload2", flag_descriptions::kDsePreload2Name,
     flag_descriptions::kDsePreload2Description, kOsAll,
     FEATURE_VALUE_TYPE(features::kDsePreload2)},
    {"dse-preload2-on-press", flag_descriptions::kDsePreload2OnPressName,
     flag_descriptions::kDsePreload2OnPressDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDsePreload2OnPress)},

    {"http-cache-custom-backend",
     flag_descriptions::kHttpCacheCustomBackendName,
     flag_descriptions::kHttpCacheCustomBackendDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(net::features::kDiskCacheBackendExperiment,
                                    kDiskCacheBackendExperimentVariations,
                                    "DiskCacheBackendExperiment")},

    {"audio-ducking", flag_descriptions::kAudioDuckingName,
     flag_descriptions::kAudioDuckingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(media::kAudioDucking,
                                    kAudioDuckingAttenuationVariations,
                                    "AudioDucking")},



    {"main-node-annotations", flag_descriptions::kMainNodeAnnotationsName,
     flag_descriptions::kMainNodeAnnotationsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMainNodeAnnotations)},

    {"origin-agent-cluster-default",
     flag_descriptions::kOriginAgentClusterDefaultName,
     flag_descriptions::kOriginAgentClusterDefaultDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kOriginAgentClusterDefaultEnabled)},

    {"origin-keyed-processes-by-default",
     flag_descriptions::kOriginKeyedProcessesByDefaultName,
     flag_descriptions::kOriginKeyedProcessesByDefaultDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kOriginKeyedProcessesByDefault)},

    {"collaboration-messaging", flag_descriptions::kCollaborationMessagingName,
     flag_descriptions::kCollaborationMessagingDescription, kOsAll,
     FEATURE_VALUE_TYPE(collaboration::features::kCollaborationMessaging)},

    {"enable-isolated-sandboxed-iframes",
     flag_descriptions::kIsolatedSandboxedIframesName,
     flag_descriptions::kIsolatedSandboxedIframesDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         blink::features::kIsolateSandboxedIframes,
         kIsolateSandboxedIframesGroupingVariations,
         "IsolateSandboxedIframes" /* trial name */)},

    {"reduce-transfer-size-updated-ipc",
     flag_descriptions::kReduceTransferSizeUpdatedIPCName,
     flag_descriptions::kReduceTransferSizeUpdatedIPCDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kReduceTransferSizeUpdatedIPC)},

#if BUILDFLAG(IS_LINUX)
    {"reduce-user-agent-data-linux-platform-version",
     flag_descriptions::kReduceUserAgentDataLinuxPlatformVersionName,
     flag_descriptions::kReduceUserAgentDataLinuxPlatformVersionDescription,
     kOsLinux,
     FEATURE_VALUE_TYPE(
         blink::features::kReduceUserAgentDataLinuxPlatformVersion)},
#endif  // BUILDFLAG(IS_LINUX)



    {"omit-cors-client-cert", flag_descriptions::kOmitCorsClientCertName,
     flag_descriptions::kOmitCorsClientCertDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kOmitCorsClientCert)},



    {"safe-browsing-local-lists-use-sbv5",
     flag_descriptions::kSafeBrowsingLocalListsUseSBv5Name,
     flag_descriptions::kSafeBrowsingLocalListsUseSBv5Description, kOsAll,
     FEATURE_VALUE_TYPE(safe_browsing::kLocalListsUseSBv5)},

    {"xslt", flag_descriptions::kXSLTName, flag_descriptions::kXSLTDescription,
     kOsAll, FEATURE_VALUE_TYPE(blink::features::kXSLT)},

#if BUILDFLAG(ENABLE_SYMPHONIA)
    {"symphonia-audio-decoding", flag_descriptions::kSymphoniaAudioDecodingName,
     flag_descriptions::kSymphoniaAudioDecodingDescription, kOsAll,
     FEATURE_VALUE_TYPE(media::kSymphoniaAudioDecoding)},
#endif

    {"safety-check-unused-site-permissions",
     flag_descriptions::kSafetyCheckUnusedSitePermissionsName,
     flag_descriptions::kSafetyCheckUnusedSitePermissionsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         content_settings::features::kSafetyCheckUnusedSitePermissions,
         kSafetyCheckUnusedSitePermissionsVariations,
         "SafetyCheckUnusedSitePermissions")},


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
    {"enable-web-bluetooth-confirm-pairing-support",
     flag_descriptions::kWebBluetoothConfirmPairingSupportName,
     flag_descriptions::kWebBluetoothConfirmPairingSupportDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(device::features::kWebBluetoothConfirmPairingSupport)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)

    {"enable-perfetto-system-tracing",
     flag_descriptions::kEnablePerfettoSystemTracingName,
     flag_descriptions::kEnablePerfettoSystemTracingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kEnablePerfettoSystemTracing)},



    {"click-to-call", flag_descriptions::kClickToCallName,
     flag_descriptions::kClickToCallDescription, kOsAll,
     FEATURE_VALUE_TYPE(kClickToCall)},

    {"css-gamut-mapping", flag_descriptions::kCssGamutMappingName,
     flag_descriptions::kCssGamutMappingDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kBakedGamutMapping)},


    {"background-resource-fetch",
     flag_descriptions::kBackgroundResourceFetchName,
     flag_descriptions::kBackgroundResourceFetchDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kBackgroundResourceFetch)},

    {"renderer-side-content-decoding",
     flag_descriptions::kRendererSideContentDecodingName,
     flag_descriptions::kRendererSideContentDecodingDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kRendererSideContentDecoding)},


    {"aim-entry-point-direct-navigation",
     flag_descriptions::kAiModeEntryPointAlwaysNavigatesName,
     flag_descriptions::kAiModeEntryPointAlwaysNavigatesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kAiModeEntryPointAlwaysNavigates)},

    {"webui-omnibox-aim-popup", flag_descriptions::kWebUIOmniboxAimPopupName,
     flag_descriptions::kWebUIOmniboxAimPopupDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::internal::kWebUIOmniboxAimPopup,
                                    kWebUIOmniboxAimPopupVariations,
                                    "WebUIOmniboxAimPopupVariations")},

    {"webui-omnibox-aim-popup-disable-animation",
     flag_descriptions::kWebUIOmniboxAimPopupDisableAnimationName,
     flag_descriptions::kWebUIOmniboxAimPopupDisableAnimationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kWebUIOmniboxAimPopupDisableAnimation)},

    {"webui-omnibox-full-popup", flag_descriptions::kWebUIOmniboxFullPopupName,
     flag_descriptions::kWebUIOmniboxFullPopupDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kWebUIOmniboxFullPopup)},

    {"webui-omnibox-popup", flag_descriptions::kWebUIOmniboxPopupName,
     flag_descriptions::kWebUIOmniboxPopupDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kWebUIOmniboxPopup)},

    {"webui-omnibox-popup-debug",
     flag_descriptions::kWebUIOmniboxPopupDebugName,
     flag_descriptions::kWebUIOmniboxPopupDebugDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kWebUIOmniboxPopupDebug,
                                    kWebUIOmniboxPopupDebugVariations,
                                    "WebUIOmniboxPopupDebugVariations")},

    {"webui-omnibox-popup-selection-control",
     flag_descriptions::kWebUIOmniboxPopupSelectionControlName,
     flag_descriptions::kWebUIOmniboxPopupSelectionControlDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kWebUIOmniboxPopupSelectionControl)},



    {"group-promo-prototype", flag_descriptions::kGroupPromoPrototypeName,
     flag_descriptions::kGroupPromoPrototypeDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         visited_url_ranking::features::kGroupSuggestionService,
         kGroupSuggestionVariations,
         "GroupPromoPrototype")},


    {"use-dmsaa-for-tiles", flag_descriptions::kUseDMSAAForTilesName,
     flag_descriptions::kUseDMSAAForTilesDescription, kOsAll,
     FEATURE_VALUE_TYPE(::features::kUseDMSAAForTiles)},



    {"sync-autofill-wallet-credential-data",
     flag_descriptions::kSyncAutofillWalletCredentialDataName,
     flag_descriptions::kSyncAutofillWalletCredentialDataDescription, kOsAll,
     FEATURE_VALUE_TYPE(syncer::kSyncAutofillWalletCredentialData)},



    {"enable-preferences-account-storage",
     flag_descriptions::kEnablePreferencesAccountStorageName,
     flag_descriptions::kEnablePreferencesAccountStorageDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(switches::kEnablePreferencesAccountStorage)},


    {"enable-compression-dictionary-transport",
     flag_descriptions::kCompressionDictionaryTransportName,
     flag_descriptions::kCompressionDictionaryTransportDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kCompressionDictionaryTransport)},

    {"enable-compression-dictionary-ttl",
     flag_descriptions::kCompressionDictionaryTTLName,
     flag_descriptions::kCompressionDictionaryTTLDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kCompressionDictionaryTTL)},




    {"cast-mirroring-target-playout-delay",
     flag_descriptions::kCastMirroringTargetPlayoutDelayName,
     flag_descriptions::kCastMirroringTargetPlayoutDelayDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kCastMirroringTargetPlayoutDelayChoices)},


    {"enable-process-per-site-up-to-main-frame-threshold",
     flag_descriptions::kEnableProcessPerSiteUpToMainFrameThresholdName,
     flag_descriptions::kEnableProcessPerSiteUpToMainFrameThresholdDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kProcessPerSiteUpToMainFrameThreshold)},

    {"get-display-media-confers-activation",
     flag_descriptions::kGetDisplayMediaConfersActivationName,
     flag_descriptions::kGetDisplayMediaConfersActivationDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(media::kGetDisplayMediaConfersActivation)},

    {"glass-toolbar", flag_descriptions::kGlassToolbarName,
     flag_descriptions::kGlassToolbarDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kGlassToolbar)},

    {"render-document", flag_descriptions::kRenderDocumentName,
     flag_descriptions::kRenderDocumentDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kRenderDocument,
                                    kRenderDocumentVariations,
                                    "RenderDocument")},

    {"default-site-instance-groups",
     flag_descriptions::kDefaultSiteInstanceGroupsName,
     flag_descriptions::kDefaultSiteInstanceGroupsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDefaultSiteInstanceGroups)},

#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"cws-info-fast-check", flag_descriptions::kCWSInfoFastCheckName,
     flag_descriptions::kCWSInfoFastCheckDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions::kCWSInfoFastCheck)},

    {"extension-disable-unsupported-developer-mode-extensions",
     flag_descriptions::kExtensionDisableUnsupportedDeveloperName,
     flag_descriptions::kExtensionDisableUnsupportedDeveloperDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         extensions_features::kExtensionDisableUnsupportedDeveloper)},
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

    {"autofill-enable-cvc-storage-and-filling",
     flag_descriptions::kAutofillEnableCvcStorageAndFillingName,
     flag_descriptions::kAutofillEnableCvcStorageAndFillingDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableCvcStorageAndFilling)},


    {"privacy-sandbox-enrollment-overrides",
     flag_descriptions::kPrivacySandboxEnrollmentOverridesName,
     flag_descriptions::kPrivacySandboxEnrollmentOverridesDescription, kOsAll,
     ORIGIN_LIST_VALUE_TYPE(privacy_sandbox::kPrivacySandboxEnrollmentOverrides,
                            "")},


    {"autofill-enable-prefetching-risk-data-for-retrieval",
     flag_descriptions::kAutofillEnablePrefetchingRiskDataForRetrievalName,
     flag_descriptions::
         kAutofillEnablePrefetchingRiskDataForRetrievalDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnablePrefetchingRiskDataForRetrieval)},

    {"rcaps-dynamic-profile-country",
     flag_descriptions::kRcapsDynamicProfileCountryName,
     flag_descriptions::kRcapsDynamicProfileCountryDescription, kOsAll,
     FEATURE_VALUE_TYPE(switches::kDynamicProfileCountry)},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {"enable-generic-oidc-auth-profile-management",
     flag_descriptions::kEnableGenericOidcAuthProfileManagementName,
     flag_descriptions::kEnableGenericOidcAuthProfileManagementDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(profile_management::features::
                            kEnableGenericOidcAuthProfileManagement)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
    {"enable-user-navigation-capturing-pwa",
     flag_descriptions::kPwaNavigationCapturingName,
     flag_descriptions::kPwaNavigationCapturingDescription,
     kOsLinux | kOsMac | kOsWin | kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kPwaNavigationCapturing,
                                    kPwaNavigationCapturingVariations,
                                    "PwaNavigationCapturing")},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_CHROMEOS)

    {"protected-audience-debug-token",
     flag_descriptions::kProtectedAudiencesConsentedDebugTokenName,
     flag_descriptions::kProtectedAudiencesConsentedDebugTokenDescription,
     kOsAll,
     STRING_VALUE_TYPE(switches::kProtectedAudiencesConsentedDebugToken, "")},

    {"deprecate-unload", flag_descriptions::kDeprecateUnloadName,
     flag_descriptions::kDeprecateUnloadDescription, kOsAll | kDeprecated,
     FEATURE_VALUE_TYPE(network::features::kDeprecateUnload)},

    {"autofill-enable-fpan-risk-based-authentication",
     flag_descriptions::kAutofillEnableFpanRiskBasedAuthenticationName,
     flag_descriptions::kAutofillEnableFpanRiskBasedAuthenticationDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableFpanRiskBasedAuthentication)},

    {"ack-copy-output-request-early-for-view-transition",
     flag_descriptions::kAckCopyOutputRequestEarlyForViewTransitionName,
     flag_descriptions::kAckCopyOutputRequestEarlyForViewTransitionDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(features::kAckCopyOutputRequestEarlyForViewTransition)},

#if BUILDFLAG(IS_MAC)
    {"enable-mac-pwas-notification-attribution",
     flag_descriptions::kMacPWAsNotificationAttributionName,
     flag_descriptions::kMacPWAsNotificationAttributionDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kAppShimNotificationAttribution)},

    {"use-adhoc-signing-for-web-app-shims",
     flag_descriptions::kUseAdHocSigningForWebAppShimsName,
     flag_descriptions::kUseAdHocSigningForWebAppShimsDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kUseAdHocSigningForWebAppShims)},
#endif  // BUILDFLAG(IS_MAC)

    {"profiles-reordering", flag_descriptions::kProfilesReorderingName,
     flag_descriptions::kProfilesReorderingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(switches::kProfilesReordering)},



#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
    {"enable-bound-session-credentials",
     flag_descriptions::kEnableBoundSessionCredentialsName,
     flag_descriptions::kEnableBoundSessionCredentialsDescription,
     kOsMac | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kEnableBoundSessionCredentials)},
    {"enable-bound-session-credentials-software-keys-for-manual-testing",
     flag_descriptions::
         kEnableBoundSessionCredentialsSoftwareKeysForManualTestingName,
     flag_descriptions::
         kEnableBoundSessionCredentialsSoftwareKeysForManualTestingDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(
         unexportable_keys::
             kEnableBoundSessionCredentialsSoftwareKeysForManualTesting)},
#endif  // BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)


#if BUILDFLAG(ENABLE_COMPOSE)
    {"compose-selection-nudge", flag_descriptions::kComposeSelectionNudgeName,
     flag_descriptions::kComposeSelectionNudgeDescription,
     kOsWin | kOsLinux | kOsMac | kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         compose::features::kEnableComposeSelectionNudge,
         kComposeSelectionNudgeVariations,
         "ComposeSelectionNudge")},
#endif

    {"related-website-sets-permission-grants",
     flag_descriptions::kShowRelatedWebsiteSetsPermissionGrantsName,
     flag_descriptions::kShowRelatedWebsiteSetsPermissionGrantsDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         permissions::features::kShowRelatedWebsiteSetsPermissionGrants)},







    {"save-passwords-contextual-ui",
     flag_descriptions::kSavePasswordsContextualUiName,
     flag_descriptions::kSavePasswordsContextualUiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSavePasswordsContextualUi)},


    {"enable-unrestricted-usb", flag_descriptions::kEnableUnrestrictedUsbName,
     flag_descriptions::kEnableUnrestrictedUsbDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kUnrestrictedUsb)},

    {"autofill-enable-vcn-3ds-authentication",
     flag_descriptions::kAutofillEnableVcn3dsAuthenticationName,
     flag_descriptions::kAutofillEnableVcn3dsAuthenticationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableVcn3dsAuthentication)},


    {"link-preview", flag_descriptions::kLinkPreviewName,
     flag_descriptions::kLinkPreviewDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kLinkPreview,
                                    kLinkPreviewTriggerTypeVariations,
                                    "LinkPreview")},

    {"send-tab-to-self-enhanced-handoff",
     flag_descriptions::kSendTabToSelfEnhancedHandoffName,
     flag_descriptions::kSendTabToSelfEnhancedHandoffDescription, kOsAll,
     MULTI_VALUE_TYPE(kSendTabToSelfEnhancedHandoffChoices)},



    {"data-sharing-debug-logs", flag_descriptions::kDataSharingDebugLogsName,
     flag_descriptions::kDataSharingDebugLogsDescription, kOsAll,
     SINGLE_VALUE_TYPE(data_sharing::kDataSharingDebugLoggingEnabled)},

    {"autofill-shared-storage-server-card-data",
     flag_descriptions::kAutofillSharedStorageServerCardDataName,
     flag_descriptions::kAutofillSharedStorageServerCardDataDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillSharedStorageServerCardData)},




    {"autofill-enable-card-benefits-for-american-express",
     flag_descriptions::kAutofillEnableCardBenefitsForAmericanExpressName,
     flag_descriptions::
         kAutofillEnableCardBenefitsForAmericanExpressDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableCardBenefitsForAmericanExpress)},

    {"autofill-enable-card-benefits-sync",
     flag_descriptions::kAutofillEnableCardBenefitsSyncName,
     flag_descriptions::kAutofillEnableCardBenefitsSyncDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableCardBenefitsSync)},

    {"enable-standard-device-bound-session-credentials",
     flag_descriptions::kEnableStandardBoundSessionCredentialsName,
     flag_descriptions::kEnableStandardBoundSessionCredentialsDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(net::features::kDeviceBoundSessions,
                                    kStandardBoundSessionCredentialsVariations,
                                    "standard-device-bound-sessions")},
    {"enable-standard-device-bound-session-persistence",
     flag_descriptions::kEnableStandardBoundSessionPersistenceName,
     flag_descriptions::kEnableStandardBoundSessionPersistenceDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(net::features::kPersistDeviceBoundSessions)},
    {"enable-standard-device-bound-session-credentials-federated-sessions",
     flag_descriptions::
         kEnableStandardBoundSessionCredentialsFederatedSessionsName,
     flag_descriptions::
         kEnableStandardBoundSessionCredentialsFederatedSessionsDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         net::features::kDeviceBoundSessionsFederatedRegistration,
         kStandardBoundSessionCredentialsFederatedSessionsVariations,
         "standard-device-bound-sessions-federated-sessions")},
    {"enable-standard-device-bound-session-devtools-debugging",
     flag_descriptions::kEnableStandardBoundSessionDevToolsDebuggingName,
     flag_descriptions::kEnableStandardBoundSessionDevToolsDebuggingDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kDeviceBoundSessionsDevTools)},
    {"enable-standard-device-bound-session-google",
     flag_descriptions::kEnableStandardBoundSessionsGoogleName,
     flag_descriptions::kEnableStandardBoundSessionsGoogleDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(net::features::kDeviceBoundSessionsForRestrictedSites)},
    {"enable-standard-device-bound-session-google-experiment-id",
     flag_descriptions::kEnableStandardBoundSessionsGoogleExperimentIdName,
     flag_descriptions::
         kEnableStandardBoundSessionsGoogleExperimentIdDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         net::features::kDeviceBoundSessionsForRestrictedSitesExperimentId,
         kDeviceBoundSessionsForRestrictedSitesExperimentIdVariations,
         "DeviceBoundSessionsForRestrictedSitesExperimentIdVariations")},

    {"responsive-iframes", flag_descriptions::kResponsiveIframesName,
     flag_descriptions::kResponsiveIframesDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kResponsiveIframes)},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {"replace-sync-promos-with-sign-in-promos-desktop",
     flag_descriptions::kReplaceSyncPromosWithSignInPromosName,
     flag_descriptions::kReplaceSyncPromosWithSignInPromosDescription,
     kOsDesktop, MULTI_VALUE_TYPE(kReplaceSyncPromosWithSignInPromosChoices)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

    {"pwm-show-suggestions-on-autofocus",
     flag_descriptions::kPasswordManagerShowSuggestionsOnAutofocusName,
     flag_descriptions::kPasswordManagerShowSuggestionsOnAutofocusDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kShowSuggestionsOnAutofocus)},

    {"password-save-in-context-error-resolution-on-desktop",
     flag_descriptions::kPasswordSaveInContextErrorResolutionOnDesktopName,
     flag_descriptions::
         kPasswordSaveInContextErrorResolutionOnDesktopDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(password_manager::features::
                            kPasswordSaveInContextErrorResolutionOnDesktop)},


    {"optimization-guide-enable-dogfood-logging",
     flag_descriptions::kOptimizationGuideEnableDogfoodLoggingName,
     flag_descriptions::kOptimizationGuideEnableDogfoodLoggingDescription,
     kOsAll,
     SINGLE_VALUE_TYPE(
         optimization_guide::switches::kEnableModelQualityDogfoodLogging)},

    {"hybrid-passkeys-in-context-menu",
     flag_descriptions::kWebAuthnUsePasskeyFromAnotherDeviceInContextMenuName,
     flag_descriptions::
         kWebAuthnUsePasskeyFromAnotherDeviceInContextMenuDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(password_manager::features::
                            kWebAuthnUsePasskeyFromAnotherDeviceInContextMenu)},



    {"prompt-api-for-gemini-nano",
     flag_descriptions::kPromptAPIForGeminiNanoName,
     flag_descriptions::kPromptAPIForGeminiNanoDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kAIPromptAPI,
                                    kAILangsVariation,
                                    "kAIPromptAPI"),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"prompt-api-for-gemini-nano-multimodal-input",
     flag_descriptions::kPromptAPIForGeminiNanoMultimodalInputName,
     flag_descriptions::kPromptAPIForGeminiNanoMultimodalInputDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kAIPromptAPIMultimodalInput),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"writer-api-for-gemini-nano",
     flag_descriptions::kWriterAPIForGeminiNanoName,
     flag_descriptions::kWriterAPIForGeminiNanoDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kAIWriterAPI,
                                    kAILangsVariation,
                                    "kAIWriterAPI"),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"rewriter-api-for-gemini-nano",
     flag_descriptions::kRewriterAPIForGeminiNanoName,
     flag_descriptions::kRewriterAPIForGeminiNanoDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kAIRewriterAPI,
                                    kAILangsVariation,
                                    "kAIRewriterAPI"),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"proofreader-api-for-gemini-nano",
     flag_descriptions::kProofreaderAPIForGeminiNanoName,
     flag_descriptions::kProofreaderAPIForGeminiNanoDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kAIProofreadingAPI),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"summarizer-api-performance-preference",
     flag_descriptions::kSummarizerAPIWithPerformancePreferenceName,
     flag_descriptions::kSummarizerAPIWithPerformancePreferenceDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kAISummarizationPerformancePreference),
     flag_descriptions::kSummarizerAPIWithPerformancePreferenceLink},

    {"summarizer-api-for-gemini-nano",
     flag_descriptions::kSummarizerAPIForGeminiNanoName,
     flag_descriptions::kSummarizerAPIForGeminiNanoDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kAISummarizationAPI,
                                    kAILangsVariation,
                                    "kAISummarizationAPI"),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"on-device-model-litert-lm-backend",
     flag_descriptions::kOnDeviceModelLitertLmBackendName,
     flag_descriptions::kOnDeviceModelLitertLmBackendDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         on_device_model::features::kOnDeviceModelLitertLmBackend)},

    {"css-grid-lanes-layout", flag_descriptions::kCSSGridLanesLayoutName,
     flag_descriptions::kCSSGridLanesLayoutDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kCSSGridLanesLayout)},

    {"canvas-2d-hibernation", flag_descriptions::kCanvasHibernationName,
     flag_descriptions::kCanvasHibernationDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kCanvas2DHibernation)},

    {"visited-url-ranking-service-domain-deduplication",
     flag_descriptions::kVisitedURLRankingServiceDeduplicationName,
     flag_descriptions::kVisitedURLRankingServiceDeduplicationDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         visited_url_ranking::features::kVisitedURLRankingDeduplication,
         kVisitedURLRankingDomainDeduplicationVariations,
         "visited-url-ranking-service-domain-deduplication")},

    {"visited-url-ranking-service-history-visibility-score-filter",
     flag_descriptions::
         kVisitedURLRankingServiceHistoryVisibilityScoreFilterName,
     flag_descriptions::
         kVisitedURLRankingServiceHistoryVisibilityScoreFilterDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(visited_url_ranking::features::
                            kVisitedURLRankingHistoryVisibilityScoreFilter)},

    {"autofill-unmask-card-request-timeout",
     flag_descriptions::kAutofillUnmaskCardRequestTimeoutName,
     flag_descriptions::kAutofillUnmaskCardRequestTimeoutDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillUnmaskCardRequestTimeout)},

    {"infinite-tabs-freezing", flag_descriptions::kInfiniteTabsFreezingName,
     flag_descriptions::kInfiniteTabsFreezingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(performance_manager::features::kInfiniteTabsFreezing)},

    {"memory-purge-on-freeze-limit",
     flag_descriptions::kMemoryPurgeOnFreezeLimitName,
     flag_descriptions::kMemoryPurgeOnFreezeLimitDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kMemoryPurgeOnFreezeLimit)},


    {"lens-aim-suggestions", flag_descriptions::kLensAimSuggestionsName,
     flag_descriptions::kLensAimSuggestionsDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(lens::features::kLensAimSuggestions,
                                    kLensAimSuggestionsVariations,
                                    "LensAimSuggestions")},

    {"lens-aim-gradient-suggest-background",
     flag_descriptions::kLensAimSuggestionsGradientBackgroundName,
     flag_descriptions::kLensAimSuggestionsGradientBackgroundDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(lens::features::kLensAimSuggestionsGradientBackground)},


    {"data-sharing", flag_descriptions::kDataSharingName,
     flag_descriptions::kDataSharingDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(data_sharing::features::kDataSharingFeature,
                                    kDatasharingVariations,
                                    "Enabled")},

    {"collaboration-entreprise-v2",
     flag_descriptions::kCollaborationEntrepriseV2Name,
     flag_descriptions::kCollaborationEntrepriseV2Description, kOsAll,
     FEATURE_VALUE_TYPE(data_sharing::features::kCollaborationEntrepriseV2)},

    {"collaboration-shared-tab-group-account-data",
     flag_descriptions::kCollaborationSharedTabGroupAccountDataName,
     flag_descriptions::kCollaborationSharedTabGroupAccountDataDescription,
     kOsAll, FEATURE_VALUE_TYPE(syncer::kSyncSharedTabGroupAccountData)},

    {"data-sharing-join-only", flag_descriptions::kDataSharingJoinOnlyName,
     flag_descriptions::kDataSharingJoinOnlyDescription, kOsAll,
     FEATURE_VALUE_TYPE(data_sharing::features::kDataSharingJoinOnly)},

    {"data-sharing-non-production-environment",
     flag_descriptions::kDataSharingNonProductionEnvironmentName,
     flag_descriptions::kDataSharingNonProductionEnvironmentDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         data_sharing::features::kDataSharingNonProductionEnvironment)},

    // LINT.IfChange(DataSharingVersioning)
    {"shared-data-types-kill-switch",
     flag_descriptions::kDataSharingVersioningStatesName,
     flag_descriptions::kDataSharingVersioningStatesDescription, kOsAll,
     MULTI_VALUE_TYPE(kDataSharingVersioningStateChoices)},
    // LINT.ThenChange(//ios/chrome/browser/flags/about_flags.mm:DataSharingVersioning)

    {"history-sync-alternative-illustration",
     flag_descriptions::kHistorySyncAlternativeIllustrationName,
     flag_descriptions::kHistorySyncAlternativeIllustrationDescription, kOsAll,
     FEATURE_VALUE_TYPE(tab_groups::kUseAlternateHistorySyncIllustration)},


    {"autofill-enable-cvc-storage-and-filling-enhancement",
     flag_descriptions::kAutofillEnableCvcStorageAndFillingEnhancementName,
     flag_descriptions::
         kAutofillEnableCvcStorageAndFillingEnhancementDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableCvcStorageAndFillingEnhancement)},


    {"discount-on-navigation",
     commerce::flag_descriptions::kDiscountOnNavigationName,
     commerce::flag_descriptions::kDiscountOnNavigationDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(commerce::kEnableDiscountInfoApi,
                                    kDiscountsVariations,
                                    "DisocuntOnNavigation")},

    {"devtools-privacy-ui", flag_descriptions::kDevToolsPrivacyUIName,
     flag_descriptions::kDevToolsPrivacyUIDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDevToolsPrivacyUI)},

    {"devtools-live-edit", flag_descriptions::kDevToolsLiveEditName,
     flag_descriptions::kDevToolsLiveEditDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDevToolsLiveEdit)},

    {"permissions-ai-v4", flag_descriptions::kPermissionsAIv4Name,
     flag_descriptions::kPermissionsAIv4Description, kOsAll,
     FEATURE_VALUE_TYPE(permissions::features::kPermissionsAIv4)},

    {"permissions-ai-p92", flag_descriptions::kPermissionsAIP92Name,
     flag_descriptions::kPermissionsAIP92Description, kOsAll,
     FEATURE_VALUE_TYPE(permissions::features::kPermissionsAIP92)},


    {"permissions-gesture-gated-prompts",
     flag_descriptions::kPermissionsGestureGatedPromptsName,
     flag_descriptions::kPermissionsGestureGatedPromptsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         permissions::features::kPermissionsGestureGatedPrompts,
         kPermissionsGestureGatedPromptsVariations,
         "PermissionsGestureGatedPrompts")},


    {"enable-lens-overlay-translate-button",
     flag_descriptions::kLensOverlayTranslateButtonName,
     flag_descriptions::kLensOverlayTranslateButtonDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayTranslateButton)},

    {"enable-lens-overlay-latency-optimizations",
     flag_descriptions::kLensOverlayLatencyOptimizationsName,
     flag_descriptions::kLensOverlayLatencyOptimizationsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayLatencyOptimizations)},

    {"enable-lens-overlay-image-context-menu-actions",
     flag_descriptions::kLensOverlayImageContextMenuActionsName,
     flag_descriptions::kLensOverlayImageContextMenuActionsDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         lens::features::kLensOverlayImageContextMenuActions,
         kLensOverlayImageContextMenuActionsVariations,
         "LensOverlayImageContextMenuActions")},

    {"enable-lens-overlay-updated-visuals",
     flag_descriptions::kLensOverlayUpdatedVisualsName,
     flag_descriptions::kLensOverlayUpdatedVisualsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayVisualSelectionUpdates)},

    {"enable-lens-search-aim-m3", flag_descriptions::kLensSearchAimM3Name,
     flag_descriptions::kLensSearchAimM3Description, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensSearchAimM3)},


    {"enable-segmentation-internals-survey",
     flag_descriptions::kSegmentationSurveyPageName,
     flag_descriptions::kSegmentationSurveyPageDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         segmentation_platform::features::kSegmentationSurveyPage)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-buy-now-pay-later",
     flag_descriptions::kAutofillEnableBuyNowPayLaterName,
     flag_descriptions::kAutofillEnableBuyNowPayLaterDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableBuyNowPayLater)},

    {"autofill-enable-buy-now-pay-later-syncing",
     flag_descriptions::kAutofillEnableBuyNowPayLaterSyncingName,
     flag_descriptions::kAutofillEnableBuyNowPayLaterSyncingDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableBuyNowPayLaterSyncing)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)

    {"autofill-enable-cvc-storage-and-filling-standalone-form-enhancement",
     flag_descriptions::
         kAutofillEnableCvcStorageAndFillingStandaloneFormEnhancementName,
     flag_descriptions::
         kAutofillEnableCvcStorageAndFillingStandaloneFormEnhancementDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::
             kAutofillEnableCvcStorageAndFillingStandaloneFormEnhancement)},

    {"separate-local-and-account-themes",
     flag_descriptions::kSeparateLocalAndAccountThemesName,
     flag_descriptions::kSeparateLocalAndAccountThemesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(syncer::kSeparateLocalAndAccountThemes)},


    {"autofill-enable-card-info-runtime-retrieval",
     flag_descriptions::kAutofillEnableCardInfoRuntimeRetrievalName,
     flag_descriptions::kAutofillEnableCardInfoRuntimeRetrievalDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableCardInfoRuntimeRetrieval)},


    {"password-form-grouped-affiliations",
     flag_descriptions::kPasswordFormGroupedAffiliationsName,
     flag_descriptions::kPasswordFormGroupedAffiliationsDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kPasswordFormGroupedAffiliations)},

    {"password-form-clientside-classifier",
     flag_descriptions::kPasswordFormClientsideClassifierName,
     flag_descriptions::kPasswordFormClientsideClassifierDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kPasswordFormClientsideClassifier)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"contextual-cueing", flag_descriptions::kContextualCueingName,
     flag_descriptions::kContextualCueingDescription, kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(contextual_cueing::kContextualCueing,
                                    kContextualCueingEnabledOptions,
                                    "ContextualCueingEnabledOptions")},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)

    {"separate-local-and-account-search-engines",
     flag_descriptions::kSeparateLocalAndAccountSearchEnginesName,
     flag_descriptions::kSeparateLocalAndAccountSearchEnginesDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(syncer::kSeparateLocalAndAccountSearchEngines)},

#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
    {"partition-alloc-with-advanced-checks",
     flag_descriptions::kPartitionAllocWithAdvancedChecksName,
     flag_descriptions::kPartitionAllocWithAdvancedChecksDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         base::features::kPartitionAllocWithAdvancedChecks,
         kPartitionAllocWithAdvancedChecksEnabledProcessesOptions,
         "PartitionAllocWithAdvancedChecks")},
#endif  //  PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

    {"partition-visited-link-database-with-self-links",
     flag_descriptions::kPartitionVisitedLinkDatabaseWithSelfLinksName,
     flag_descriptions::kPartitionVisitedLinkDatabaseWithSelfLinksDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kPartitionVisitedLinkDatabaseWithSelfLinks)},

    {"predictable-reported-quota",
     flag_descriptions::kPredictableReportedQuotaName,
     flag_descriptions::kPredictableReportedQuotaDescription, kOsAll,
     FEATURE_VALUE_TYPE(storage::features::kStaticStorageQuota)},

    {"prefetch-bookmarkbar-trigger",
     flag_descriptions::kBookmarkBarPrefetchName,
     flag_descriptions::kBookmarkBarPrefetchDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kBookmarkTriggerForPrefetch)},

    {"prefetch-new-tab-page-trigger",
     flag_descriptions::kNewTabPagePrefetchName,
     flag_descriptions::kNewTabPagePrefetchDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kNewTabPageTriggerForPrefetch)},

        // PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

#if BUILDFLAG(IS_MAC)
    {"enable-mac-a11y-api-migration",
     flag_descriptions::kMacAccessibilityAPIMigrationName,
     flag_descriptions::kMacAccessibilityAPIMigrationDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kMacAccessibilityAPIMigration)},
#endif  // BUILDFLAG(IS_MAC)

    {"enable-lens-overlay-translate-languages",
     flag_descriptions::kLensOverlayTranslateLanguagesName,
     flag_descriptions::kLensOverlayTranslateLanguagesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayTranslateLanguages)},

    {"glic", flag_descriptions::kGlicName, flag_descriptions::kGlicDescription,
     kOsAll, FEATURE_VALUE_TYPE(features::kGlic)},
    {"glic-z-order-changes", flag_descriptions::kGlicZOrderChangesName,
     flag_descriptions::kGlicZOrderChangesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicZOrderChanges)},
    {"glic-actor", flag_descriptions::kGlicActorName,
     flag_descriptions::kGlicActorDescription, kOsDesktop | kOsAndroid,
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(switches::kEnableFeatures,
                                         "GlicActor,GlicActorUi",
                                         switches::kDisableFeatures,
                                         "GlicActor,GlicActorUi")},
    {"glic-actor-autofill", flag_descriptions::kGlicActorAutofillName,
     flag_descriptions::kGlicActorAutofillDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicActorAutofill)},
    {"glic-actor-cursor", flag_descriptions::kGlicActorCursorName,
     flag_descriptions::kGlicActorCursorDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicActorUiMagicCursor)},
    {"glic-detached", flag_descriptions::kGlicDetachedName,
     flag_descriptions::kGlicDetachedDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicDetached)},
    {"glic-panel-reset-top-chrome-button",
     flag_descriptions::kGlicPanelResetTopChromeButtonName,
     flag_descriptions::kGlicPanelResetTopChromeButtonDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicPanelResetTopChromeButton,
                                    kGlicPanelResetTopChromeButtonVariations,
                                    "GlicPanelResetTopChromeButton")},
    {"glic-panel-reset-on-start", flag_descriptions::kGlicPanelResetOnStartName,
     flag_descriptions::kGlicPanelResetOnStartDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicPanelResetOnStart)},
    {"glic-tab-restoration", flag_descriptions::kGlicTabRestorationName,
     flag_descriptions::kGlicTabRestorationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicTabRestoration)},
    {"glic-panel-set-position-on-drag",
     flag_descriptions::kGlicPanelSetPositionOnDragName,
     flag_descriptions::kGlicPanelSetPositionOnDragDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicPanelSetPositionOnDrag)},
    {"glic-panel-reset-on-session-timeout",
     flag_descriptions::kGlicPanelResetOnSessionTimeoutName,
     flag_descriptions::kGlicPanelResetOnSessionTimeoutDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicPanelResetOnSessionTimeout,
                                    kGlicPanelResetOnSessionTimeoutVariations,
                                    "GlicPanelResetOnSessionTimeout")},
    {"glic-panel-reset-size-and-location-on-open",
     flag_descriptions::kGlicPanelResetSizeAndLocationName,
     flag_descriptions::kGlicPanelResetSizeAndLocationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicPanelResetSizeAndLocationOnOpen)},
    {"glic-print-menu-item", flag_descriptions::kGlicPrintMenuItemName,
     flag_descriptions::kGlicPrintMenuItemDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kGlicPrintMenuItem)},
    {"glic-pre-warming", flag_descriptions::kGlicWarmingName,
     flag_descriptions::kGlicWarmingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicWarming,
                                    kGlicWarmingVariations,
                                    "GlicWarming")},
    {"glic-side-panel", flag_descriptions::kGlicSidePanelName,
     flag_descriptions::kGlicSidePanelDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicMultiInstance)},
    {"glic-entrypoint-variations",
     flag_descriptions::kGlicEntrypointVariationsName,
     flag_descriptions::kGlicEntrypointVariationsDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicEntrypointVariations,
                                    kGlicEntrypointVariations,
                                    "GlicEntrypointVariations")},
    {"glic-contextual-cue-bubble",
     flag_descriptions::kGlicContextualCueBubbleName,
     flag_descriptions::kGlicContextualCueBubbleDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicContextualCueBubble)},
    {"glic-default-to-last-active-conversation",
     flag_descriptions::kGlicDefaultToLastActiveConversationName,
     flag_descriptions::kGlicDefaultToLastActiveConversationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicDefaultToLastActiveConversation)},
    {"glic-bind-pinned-unbound-tab",
     flag_descriptions::kGlicBindPinnedUnboundTabName,
     flag_descriptions::kGlicBindPinnedUnboundTabDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicBindPinnedUnboundTab)},
    {"glic-button-pressed-state",
     flag_descriptions::kGlicButtonPressedStateName,
     flag_descriptions::kGlicButtonPressedStateDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicButtonPressedState,
                                    kGlicButtonPressedStateVariations,
                                    "GlicButtonPressedState")},
    {"glic-button-alt-label", flag_descriptions::kGlicButtonAltLabelName,
     flag_descriptions::kGlicButtonAltLabelDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicButtonAltLabel,
                                    kGlicButtonAltLabelVariations,
                                    "GlicButtonAltLabel")},
    {"glic-capture-region", flag_descriptions::kGlicCaptureRegionName,
     flag_descriptions::kGlicCaptureRegionDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kGlicCaptureRegion)},
    {"glic-chrome-status-icon", flag_descriptions::kGlicChromeStatusIconName,
     flag_descriptions::kGlicChromeStatusIconDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kGlicChromeStatusIcon)},
    {"glic-daisy-chain-new-tabs", flag_descriptions::kGlicDaisyChainNewTabsName,
     flag_descriptions::kGlicDaisyChainNewTabsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicDaisyChainNewTabs)},
    {"glic-toolbar-height-side-panel",
     flag_descriptions::kGlicUseToolbarHeightSidePanelName,
     flag_descriptions::kGlicUseToolbarHeightSidePanelDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicUseToolbarHeightSidePanel)},
    {"glic-unified-fre-screen", flag_descriptions::kGlicUnifiedFreScreenName,
     flag_descriptions::kGlicUnifiedFreScreenDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicUnifiedFreScreen)},
    {"glic-live-mode-only-glow", flag_descriptions::kGlicLiveModeOnlyGlowName,
     flag_descriptions::kGlicLiveModeOnlyGlowDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicLiveModeOnlyGlow)},
    {"glic-mi-tab-context-menu", flag_descriptions::kGlicMITabContextMenuName,
     flag_descriptions::kGlicMITabContextMenuDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicMITabContextMenu)},
    {"glic-share-image", flag_descriptions::kGlicShareImageName,
     flag_descriptions::kGlicShareImageDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicShareImage)},
    {"glic-trust-first-onboarding",
     flag_descriptions::kGlicTrustFirstOnboardingName,
     flag_descriptions::kGlicTrustFirstOnboardingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicTrustFirstOnboarding,
                                    kGlicTrustFirstOnboardingVariations,
                                    "GlicTrustFirstOnboarding")},
    {"glic-default-tab-context-setting",
     flag_descriptions::kGlicDefaultTabContextSettingName,
     flag_descriptions::kGlicDefaultTabContextSettingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicDefaultTabContextSetting)},

    {"glic-reset-mi-enablement-by-tier",
     flag_descriptions::kGlicResetMultiInstanceEnabledByTierName,
     flag_descriptions::kGlicResetMultiInstanceEnabledByTierDescription,
     kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kGlicResetMultiInstanceEnabledByTier)},

    {"glic-set-g1-for-mi",
     flag_descriptions::kGlicForceG1StatusForMultiInstanceName,
     flag_descriptions::kGlicForceG1StatusForMultiInstanceDescription,
     kOsDesktop, MULTI_VALUE_TYPE(kGlicSetG1ForMultiInstance)},

    {"glic-guest-url-presets", flag_descriptions::kGlicGuestUrlPresetsName,
     flag_descriptions::kGlicGuestUrlPresetsDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicGuestUrlPresets,
                                    kGlicGuestUrlPresetTypes,
                                    "GlicGuestUrlPresets")},

    {"glic-selection-prompt", flag_descriptions::kGlicSelectionPromptName,
     flag_descriptions::kGlicSelectionPromptDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGlicSelectionPrompt)},

    {"glic-disable-actor-safety-checks",
     flag_descriptions::kGlicDisableActorSafetyChecksName,
     flag_descriptions::kGlicDisableActorSafetyChecksDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(actor::switches::kDisableActorSafetyChecks)},


    {"skills", flag_descriptions::kSkillsEnabledName,
     flag_descriptions::kSkillsEnabledDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSkillsEnabled)},


    {"autofill-enable-save-and-fill",
     flag_descriptions::kAutofillEnableSaveAndFillName,
     flag_descriptions::kAutofillEnableSaveAndFillDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableSaveAndFill)},


    {"happy-eyeballs-v3", flag_descriptions::kHappyEyeballsV3Name,
     flag_descriptions::kHappyEyeballsV3Description, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kHappyEyeballsV3)},

    {"policy-promotion-banner-flag",
     flag_descriptions::kEnablePolicyPromotionBannerName,
     flag_descriptions::kEnablePolicyPromotionBannerDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kEnablePolicyPromotionBanner)},
    {"management-promotion-banner-flag",
     flag_descriptions::kEnableManagementPromotionBannerName,
     flag_descriptions::kEnableManagementPromotionBannerDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kEnableManagementPromotionBanner)},

    {"privacy-sandbox-ads-api-ux-enhancements",
     flag_descriptions::kPrivacySandboxAdsApiUxEnhancementsName,
     flag_descriptions::kPrivacySandboxAdsApiUxEnhancementsDescription, kOsAll,
     FEATURE_VALUE_TYPE(privacy_sandbox::kPrivacySandboxAdsApiUxEnhancements)},

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    {"enable-oauth-multilogin-cookies-binding",
     flag_descriptions::kEnableOAuthMultiloginCookiesBindingName,
     flag_descriptions::kEnableOAuthMultiloginCookiesBindingDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kEnableOAuthMultiloginCookiesBinding)},

    {"enable-oauth-multilogin-cookies-binding-server-experiment",
     flag_descriptions::
         kEnableOAuthMultiloginCookiesBindingServerExperimentName,
     flag_descriptions::
         kEnableOAuthMultiloginCookiesBindingServerExperimentDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         switches::kEnableOAuthMultiloginCookiesBindingServerExperiment,
         kOAuthMultiloginCookieBindingEnforcementVariations,
         "EnableOAuthMultiloginCookiesBindingServerExperiment")},

    {"enable-chrome-refresh-token-binding",
     flag_descriptions::kEnableChromeRefreshTokenBindingName,
     flag_descriptions::kEnableChromeRefreshTokenBindingDescription,
     kOsMac | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kEnableChromeRefreshTokenBinding)},
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

    {"three-button-password-save-dialog",
     flag_descriptions::kThreeButtonPasswordSaveDialogName,
     flag_descriptions::kThreeButtonPasswordSaveDialogDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kThreeButtonPasswordSaveDialog)},


#if BUILDFLAG(IS_MAC)
    {"block-root-window-accessible-name-change-event",
     flag_descriptions::kBlockRootWindowAccessibleNameChangeEventName,
     flag_descriptions::kBlockRootWindowAccessibleNameChangeEventDescription,
     kOsMac,
     FEATURE_VALUE_TYPE(::features::kBlockRootWindowAccessibleNameChangeEvent)},
#endif  // BUILDFLAG(IS_MAC)

    {"throttle-main-thread-to-60hz", flag_descriptions::kThrottleMainTo60HzName,
     flag_descriptions::kThrottleMainTo60HzDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kThrottleMainFrameTo60Hz)},



    {"autofill-enable-card-benefits-for-bmo",
     flag_descriptions::kAutofillEnableCardBenefitsForBmoName,
     flag_descriptions::kAutofillEnableCardBenefitsForBmoDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableCardBenefitsForBmo)},



    {"bookmarks-tree-view", flag_descriptions::kBookmarksTreeViewName,
     flag_descriptions::kBookmarksTreeViewDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kBookmarksTreeView)},


#if BUILDFLAG(IS_LINUX)
    {"automatic-usb-detach", flag_descriptions::kAutomaticUsbDetachName,
     flag_descriptions::kAutomaticUsbDetachDescription, kOsAndroid | kOsLinux,
     FEATURE_VALUE_TYPE(features::kAutomaticUsbDetach)},
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX)

    {"enable-lens-overlay-side-panel-open-in-new-tab",
     flag_descriptions::kLensOverlaySidePanelOpenInNewTabName,
     flag_descriptions::kLensOverlaySidePanelOpenInNewTabDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlaySidePanelOpenInNewTab)},

    {"mark-all-credentials-as-leaked",
     flag_descriptions::kMarkAllCredentialsAsLeakedName,
     flag_descriptions::kMarkAllCredentialsAsLeakedDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         password_manager::features::kMarkAllCredentialsAsLeaked)},

    {"account-storage-prefs-themes-search-engines",
     flag_descriptions::kAccountStoragePrefsThemesAndSearchEnginesName,
     flag_descriptions::kAccountStoragePrefsThemesAndSearchEnginesDescription,
     kOsDesktop,
     MULTI_VALUE_TYPE(kAccountStoragePrefsThemesAndSearchEnginesChoices)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-amount-extraction",
     flag_descriptions::kAutofillEnableAmountExtractionName,
     flag_descriptions::kAutofillEnableAmountExtractionDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableAmountExtraction)},
    {"autofill-enable-non-affiliated-loyalty-cards",
     flag_descriptions::kAutofillEnableNonAffiliatedLoyaltyCardsFillingName,
     flag_descriptions::
         kAutofillEnableNonAffiliatedLoyaltyCardsFillingDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableNonAffiliatedLoyaltyCardsFilling)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)

    {"enable-ax-tree-fixing", flag_descriptions::kAXTreeFixingName,
     flag_descriptions::kAXTreeFixingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kAXTreeFixing)},
    {"enable-clipboardchange-event",
     flag_descriptions::kClipboardChangeEventName,
     flag_descriptions::kClipboardChangeEventDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kClipboardChangeEvent)},

    {"devtools-project-settings",
     flag_descriptions::kDevToolsProjectSettingsName,
     flag_descriptions::kDevToolsProjectSettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDevToolsWellKnown)},



    {"drop-input-events-while-paint-holding",
     flag_descriptions::kDropInputEventsWhilePaintHoldingName,
     flag_descriptions::kDropInputEventsWhilePaintHoldingDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kDropInputEventsWhilePaintHolding)},

    {"dbd-revamp-desktop", flag_descriptions::kDbdRevampDesktopName,
     flag_descriptions::kDbdRevampDesktopDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(browsing_data::features::kDbdRevampDesktop)},

    {"default-browser-changed-os-notification",
     flag_descriptions::kDefaultBrowserChangedOsNotificationName,
     flag_descriptions::kDefaultBrowserChangedOsNotificationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(default_browser::kDefaultBrowserChangedOsNotification)},

    {"default-browser-framework",
     flag_descriptions::kDefaultBrowserFrameworkName,
     flag_descriptions::kDefaultBrowserFrameworkDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(default_browser::kDefaultBrowserFramework)},

    {"default-browser-prompt-surfaces",
     flag_descriptions::kDefaultBrowserPromptSurfacesName,
     flag_descriptions::kDefaultBrowserPromptSurfacesDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         default_browser::kDefaultBrowserPromptSurfaces,
         kDefaultBrowserPromptSurfacesVariations,
         "DefaultBrowserPromptSurfaces")},

    {"privacy-sandbox-ad-topics-content-parity",
     flag_descriptions::kPrivacySandboxAdTopicsContentParityName,
     flag_descriptions::kPrivacySandboxAdTopicsContentParityDescription, kOsAll,
     FEATURE_VALUE_TYPE(privacy_sandbox::kPrivacySandboxAdTopicsContentParity)},







    {"iph-autofill-credit-card-benefit-feature",
     flag_descriptions::kIPHAutofillCreditCardBenefitFeatureName,
     flag_descriptions::kIPHAutofillCreditCardBenefitFeatureDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         feature_engagement::kIPHAutofillCreditCardBenefitFeature)},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {"chrome-web-store-navigation-throttle",
     flag_descriptions::kChromeWebStoreNavigationThrottleName,
     flag_descriptions::kChromeWebStoreNavigationThrottleDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(
         enterprise::webstore::kChromeWebStoreNavigationThrottle)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)


    {"supervised-user-block-interstitial-v3",
     flag_descriptions::kSupervisedUserBlockInterstitialV3Name,
     flag_descriptions::kSupervisedUserBlockInterstitialV3Description, kOsAll,
     FEATURE_VALUE_TYPE(supervised_user::kSupervisedUserBlockInterstitialV3)},

    {"supervised-user-emit-log-record-separately",
     flag_descriptions::kSupervisedUserEmitLogRecordSeparatelyName,
     flag_descriptions::kSupervisedUserEmitLogRecordSeparatelyDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         supervised_user::kSupervisedUserEmitLogRecordSeparately)},

    {"supervised-user-merge-device-parental-controls-and-family-link-prefs",
     flag_descriptions::
         kSupervisedUserMergeDeviceParentalControlsAndFamilyLinkPrefsName,
     flag_descriptions::
         kSupervisedUserMergeDeviceParentalControlsAndFamilyLinkPrefsDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         supervised_user::
             kSupervisedUserMergeDeviceParentalControlsAndFamilyLinkPrefs)},

    {"supervised-user-use-url-filtering-service",
     flag_descriptions::kSupervisedUserUseUrlFilteringServiceName,
     flag_descriptions::kSupervisedUserUseUrlFilteringServiceDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         supervised_user::kSupervisedUserUseUrlFilteringService)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-amount-extraction-testing",
     flag_descriptions::kAutofillEnableAmountExtractionTestingName,
     flag_descriptions::kAutofillEnableAmountExtractionTestingDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableAmountExtractionTesting)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)

    {"enable-web-app-predictable-app-updating",
     flag_descriptions::kEnableWebAppPredictableAppUpdatingName,
     flag_descriptions::kEnableWebAppPredictableAppUpdatingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAppPredictableAppUpdating)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
    {"root-scrollbar-follows-browser-theme",
     flag_descriptions::kRootScrollbarFollowsTheme,
     flag_descriptions::kRootScrollbarFollowsThemeDescription,
     kOsLinux | kOsWin,
     FEATURE_VALUE_TYPE(blink::features::kRootScrollbarFollowsBrowserTheme)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)


    {"local-network-access-check",
     flag_descriptions::kLocalNetworkAccessChecksName,
     flag_descriptions::kLocalNetworkAccessChecksDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         network::features::kLocalNetworkAccessChecks,
         kLocalNetworkAccessChecksVariations,
         "LocalNetworkAccessChecks")},
    {"local-network-access-check-webrtc",
     flag_descriptions::kLocalNetworkAccessChecksWebRTCName,
     flag_descriptions::kLocalNetworkAccessChecksWebRTCDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kLocalNetworkAccessChecksWebRTC)},
    {"local-network-access-check-websockets",
     flag_descriptions::kLocalNetworkAccessChecksWebSocketsName,
     flag_descriptions::kLocalNetworkAccessChecksWebSocketsDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         network::features::kLocalNetworkAccessChecksWebSockets)},
    {"local-network-access-check-webtransport",
     flag_descriptions::kLocalNetworkAccessChecksWebTransportName,
     flag_descriptions::kLocalNetworkAccessChecksWebTransportDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         network::features::kLocalNetworkAccessChecksWebTransport)},
    {"local-network-access-check-split-permissions",
     flag_descriptions::kLocalNetworkAccessChecksSplitPermissionsName,
     flag_descriptions::kLocalNetworkAccessChecksSplitPermissionsDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         network::features::kLocalNetworkAccessChecksSplitPermissions)},

    {"tab-capture-infobar-links",
     flag_descriptions::kTabCaptureInfobarLinksName,
     flag_descriptions::kTabCaptureInfobarLinksDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kTabCaptureInfobarLinks)},





    {"enable-lens-search-side-panel-new-feedback",
     flag_descriptions::kLensSearchSidePanelNewFeedbackName,
     flag_descriptions::kLensSearchSidePanelNewFeedbackDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensSearchSidePanelNewFeedback)},

    {"autofill-vcn-enroll-strike-expiry-time",
     flag_descriptions::kAutofillVcnEnrollStrikeExpiryTimeName,
     flag_descriptions::kAutofillVcnEnrollStrikeExpiryTimeDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::features::kAutofillVcnEnrollStrikeExpiryTime,
         kAutofillVcnEnrollStrikeExpiryTimeOptions,
         "AutofillVcnEnrollStrikeExpiryTime")},


    {"autofill-enable-flat-rate-card-benefits-from-curinos",
     flag_descriptions::kAutofillEnableFlatRateCardBenefitsFromCurinosName,
     flag_descriptions::
         kAutofillEnableFlatRateCardBenefitsFromCurinosDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableFlatRateCardBenefitsFromCurinos)},


    {"bundled-security-settings",
     flag_descriptions::kBundledSecuritySettingsName,
     flag_descriptions::kBundledSecuritySettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(safe_browsing::kBundledSecuritySettings)},

    {"invalidate-search-engine-choice-on-device-restore-detection",
     flag_descriptions::
         kInvalidateSearchEngineChoiceOnDeviceRestoreDetectionName,
     flag_descriptions::
         kInvalidateSearchEngineChoiceOnDeviceRestoreDetectionDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         switches::kInvalidateSearchEngineChoiceOnDeviceRestoreDetection,
         kInvalidateSearchEngineChoiceOnRestoreVariations,
         "InvalidateSearchEngineChoiceOnDeviceRestoreDetection")},

    {"block-cross-partition-blob-url-fetching",
     flag_descriptions::kBlockCrossPartitionBlobUrlFetchingName,
     flag_descriptions::kBlockCrossPartitionBlobUrlFetchingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kBlockCrossPartitionBlobUrlFetching)},


    {"web-authentication-immediate-get",
     flag_descriptions::kWebAuthnImmediateGetName,
     flag_descriptions::kWebAuthnImmediateGetDescription, kOsAll,
     FEATURE_VALUE_TYPE(device::kWebAuthnImmediateGet)},

    {"media-playback-while-not-visible-permission-policy",
     flag_descriptions::kMediaPlaybackWhileNotVisiblePermissionPolicyName,
     flag_descriptions::
         kMediaPlaybackWhileNotVisiblePermissionPolicyDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kMediaPlaybackWhileNotVisiblePermissionPolicy)},


    {"open-dragged-links-same-tab",
     flag_descriptions::kOpenDraggedLinksSameTabName,
     flag_descriptions::kOpenDraggedLinksSameTabDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kSupportOpeningDraggedLinksInSameTab)},

    {"enable-secure-payment-confirmation-fallback-ux",
     flag_descriptions::kSecurePaymentConfirmationFallbackName,
     flag_descriptions::kSecurePaymentConfirmationFallbackDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         payments::features::kSecurePaymentConfirmationFallback)},




#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
    {"tab-group-home", tabs::flag_descriptions::kTabGroupHomeName,
     tabs::flag_descriptions::kTabGroupHomeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(tabs::kTabGroupHome)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_CHROMEOS)

    {"discount-autofill", commerce::flag_descriptions::kDiscountAutofillName,
     commerce::flag_descriptions::kDiscountAutofillDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kDiscountAutofill)},





    {"enable-secure-payment-confirmation-ux-refresh",
     flag_descriptions::kSecurePaymentConfirmationUxRefreshName,
     flag_descriptions::kSecurePaymentConfirmationUxRefreshDescription,
     kOsAndroid | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(blink::features::kSecurePaymentConfirmationUxRefresh)},



#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
    {"enable-site-search-allow-user-override-policy",
     flag_descriptions::kEnableSiteSearchAllowUserOverridePolicyName,
     flag_descriptions::kEnableSiteSearchAllowUserOverridePolicyDescription,
     static_cast<unsigned short>(kOsCrOS | kOsLinux | kOsMac | kOsWin),
     FEATURE_VALUE_TYPE(omnibox::kEnableSiteSearchAllowUserOverridePolicy)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/40680264): Remove this flag after regression investigation
    // is finished.
    {
        "new-content-for-checkerboarded-scrolls",
        flag_descriptions::kNewContentForCheckerboardedScrollsName,
        flag_descriptions::kNewContentForCheckerboardedScrollsDescription,
        kOsAll,
        FEATURE_VALUE_TYPE(features::kNewContentForCheckerboardedScrolls),
    },
    {"page-actions-migration", flag_descriptions::kPageActionsMigrationName,
     flag_descriptions::kPageActionsMigrationDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kPageActionsMigration,
                                    kPageActionsMigrationVariations,
                                    "PageActionsMigration")},

    {"field-classification-model-caching",
     flag_descriptions::kFieldClassificationModelCachingName,
     flag_descriptions::kFieldClassificationModelCachingDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kFieldClassificationModelCaching)},

    {"disable-autofill-strike-system",
     flag_descriptions::kDisableAutofillStrikeSystemName,
     flag_descriptions::kDisableAutofillStrikeSystemDescription, kOsAll,
     FEATURE_VALUE_TYPE(strike_database::features::kDisableStrikeSystem)},




    {"web-app-install-element", flag_descriptions::kWebAppInstallElementName,
     flag_descriptions::kWebAppInstallElementDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kInstallElement)},

    {"web-app-installation-api", flag_descriptions::kWebAppInstallationApiName,
     flag_descriptions::kWebAppInstallationApiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebAppInstallation)},

    {"web-app-migration-api", flag_descriptions::kWebAppMigrationApiName,
     flag_descriptions::kWebAppMigrationApiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebAppMigrationApi)},



#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-buy-now-pay-later-for-klarna",
     flag_descriptions::kAutofillEnableBuyNowPayLaterForKlarnaName,
     flag_descriptions::kAutofillEnableBuyNowPayLaterForKlarnaDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableBuyNowPayLaterForKlarna)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)

    {"customize-tab-group-color-palette",
     flag_descriptions::kCustomizeTabGroupColorPaletteName,
     flag_descriptions::kCustomizeTabGroupColorPaletteDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kCustomizeTabGroupColorPalette)},

    {"lens-overlay-permission-bubble-alt",
     flag_descriptions::kLensOverlayPermissionBubbleAltName,
     flag_descriptions::kLensOverlayPermissionBubbleAltDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayPermissionBubbleAlt)},

    {"autofill-enable-downstream-card-awareness-iph",
     flag_descriptions::kAutofillEnableDownstreamCardAwarenessIphName,
     flag_descriptions::kAutofillEnableDownstreamCardAwarenessIphDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableDownstreamCardAwarenessIph)},

    {"enable-ntp-browser-promos",
     flag_descriptions::kEnableNtpBrowserPromosName,
     flag_descriptions::kEnableNtpBrowserPromosDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         user_education::features::kEnableNtpBrowserPromos,
         kEnableNtpBrowserPromosVariations,
         "EnableNtpBrowserPromos")},

    {"enable-devtools-deep-link-via-extensibility-api",
     flag_descriptions::kEnableDevtoolsDeepLinkViaExtensibilityApiName,
     flag_descriptions::kEnableDevtoolsDeepLinkViaExtensibilityApiDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         blink::features::kEnableDevtoolsDeepLinkViaExtensibilityApi)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-buy-now-pay-later-for-externally-linked",
     flag_descriptions::kAutofillEnableBuyNowPayLaterForExternallyLinkedName,
     flag_descriptions::
         kAutofillEnableBuyNowPayLaterForExternallyLinkedDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableBuyNowPayLaterForExternallyLinked)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)






    {"enable-lens-overlay-edu-action-chip",
     flag_descriptions::kLensOverlayEduActionChipName,
     flag_descriptions::kLensOverlayEduActionChipDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(lens::features::kLensOverlayEduActionChip,
                                    kLensOverlayEduActionChipVariations,
                                    "LensOverlayEduActionChip")},

    {"enable-lens-overlay-entrypoint-label-alt",
     flag_descriptions::kLensOverlayEntrypointLabelAltName,
     flag_descriptions::kLensOverlayEntrypointLabelAltDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         lens::features::kLensOverlayEntrypointLabelAlt,
         kLensOverlayEntrypointLabelAltVariations,
         "LensOverlayEntrypointLabelAltVariations")},

    {"safety-hub-disruptive-notification-revocation",
     flag_descriptions::kSafetyHubDisruptiveNotificationRevocationName,
     flag_descriptions::kSafetyHubDisruptiveNotificationRevocationDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kSafetyHubDisruptiveNotificationRevocation,
         kSafetyHubDisruptiveNotificationRevocationVariations,
         "SafetyHubDisruptiveNotificationRevocation")},

    {"safety-hub-unused-permission-revocation-for-all-surfaces",
     flag_descriptions::kSafetyHubUnusedPermissionRevocationForAllSurfacesName,
     flag_descriptions::
         kSafetyHubUnusedPermissionRevocationForAllSurfacesDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         permissions::features::
             kSafetyHubUnusedPermissionRevocationForAllSurfaces)},



    {"bookmark-tab-group-conversion",
     flag_descriptions::kBookmarkTabGroupConversionName,
     flag_descriptions::kBookmarkTabGroupConversionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kBookmarkTabGroupConversion)},

    {"enable-lens-overlay-straight-to-srp",
     flag_descriptions::kLensOverlayStraightToSrpName,
     flag_descriptions::kLensOverlayStraightToSrpDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayStraightToSrp)},



    {kWebiumFlag, flag_descriptions::kWebiumName,
     flag_descriptions::kWebiumDescription, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(switches::kEnableFeatures,
                                         kWebiumFeatures,
                                         switches::kDisableFeatures,
                                         kWebiumFeatures)},


    {"default-search-engine-prewarm",
     flag_descriptions::kDefaultSearchEnginePrewarmName,
     flag_descriptions::kDefaultSearchEnginePrewarmDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kPrewarm)},

    {"apply-clientside-model-predictions-for-password-types",
     flag_descriptions::kApplyClientsideModelPredictionsForPasswordTypesName,
     flag_descriptions::
         kApplyClientsideModelPredictionsForPasswordTypesDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(password_manager::features::
                            kApplyClientsideModelPredictionsForPasswordTypes)},

    {"canvas-draw-element", flag_descriptions::kCanvasDrawElementName,
     flag_descriptions::kCanvasDrawElementDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kCanvasDrawElement)},


#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    {"enforce-management-disclaimer",
     flag_descriptions::kEnforceManagementDisclaimerName,
     flag_descriptions::kEnforceManagementDisclaimerDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         switches::kEnforceManagementDisclaimer,
         kPolicyDisclaimerRegistrationRetryDelayVariations,
         "PolicyDisclaimerRegistrationRetryDelayVariations")},

#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

    {"enable-lens-overlay-force-empty-csb-query",
     flag_descriptions::kLensOverlayForceEmptyCsbQueryName,
     flag_descriptions::kLensOverlayForceEmptyCsbQueryDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayForceEmptyCsbQuery)},


    {"browsing-history-actor-integration-M2",
     flag_descriptions::kBrowsingHistoryActorIntegrationM2Name,
     flag_descriptions::kBrowsingHistoryActorIntegrationM2Description,
     kOsDesktop,
     FEATURE_VALUE_TYPE(history::kBrowsingHistoryActorIntegrationM2)},

    {"browsing-history-actor-integration-M3",
     flag_descriptions::kBrowsingHistoryActorIntegrationM3Name,
     flag_descriptions::kBrowsingHistoryActorIntegrationM3Description,
     kOsDesktop,
     FEATURE_VALUE_TYPE(history::kBrowsingHistoryActorIntegrationM3)},

    {"browsing-history-similar-visits-grouping",
     flag_descriptions::kBrowsingHistorySimilarVisitsGroupingName,
     flag_descriptions::kBrowsingHistorySimilarVisitsGroupingDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(history::kBrowsingHistorySimilarVisitsGrouping)},

    {"autofill-manual-testing-data",
     flag_descriptions::kAutofillManualTestingDataName,
     flag_descriptions::kAutofillManualTestingDataDescription, kOsAll,
     STRING_VALUE_TYPE(autofill::kManualContentImportForTestingFlag, "")},

    {"autofill-enable-support-for-home-and-work",
     flag_descriptions::kAutofillEnableSupportForHomeAndWorkName,
     flag_descriptions::kAutofillEnableSupportForHomeAndWorkDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableSupportForHomeAndWork)},

    {"new-tab-adds-to-active-group",
     flag_descriptions::kNewTabAddsToActiveGroupName,
     flag_descriptions::kNewTabAddsToActiveGroupDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kNewTabAddsToActiveGroup)},


    {"autofill-enable-support-for-name-and-email-profile",
     flag_descriptions::kAutofillEnableSupportForNameAndEmailName,
     flag_descriptions::kAutofillEnableSupportForNameAndEmailDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableSupportForNameAndEmail)},
    {"reintroduce-hybrid-passkey-entry-point",
     flag_descriptions::kAutofillReintroduceHybridPasskeyDropdownItemName,
     flag_descriptions::
         kAutofillReintroduceHybridPasskeyDropdownItemDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(password_manager::features::
                            kAutofillReintroduceHybridPasskeyDropdownItem)},
    {"enable-lens-overlay-text-selection-context-menu-entrypoint",
     flag_descriptions::kLensOverlayTextSelectionContextMenuEntrypointName,
     flag_descriptions::
         kLensOverlayTextSelectionContextMenuEntrypointDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         lens::features::kLensOverlayTextSelectionContextMenuEntrypoint,
         kLensOverlayTextSelectionContextMenuEntrypointVariations,
         "LensOverlayTextSelectionContextMenuEntrypoint")},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
    {"enable-ntp-enterprise-shortcuts",
     flag_descriptions::kEnableNtpEnterpriseShortcutsName,
     flag_descriptions::kEnableNtpEnterpriseShortcutsDescription,
     static_cast<unsigned short>(kOsCrOS | kOsLinux | kOsMac | kOsWin),
     FEATURE_VALUE_TYPE(ntp_tiles::kNtpEnterpriseShortcuts)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||\
        // BUILDFLAG(IS_CHROMEOS)


    {"tab-group-more-entry-points",
     flag_descriptions::kTabGroupMenuMoreEntryPointsName,
     flag_descriptions::kTabGroupMenuMoreEntryPointsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kTabGroupMenuMoreEntryPoints)},

#if BUILDFLAG(IS_MAC)
    {"show-tab-groups-mac-system-menu",
     flag_descriptions::kShowTabGroupsMacSystemMenuName,
     flag_descriptions::kShowTabGroupsMacSystemMenuDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kShowTabGroupsMacSystemMenu)},
#endif  // BUILDFLAG(IS_MAC)



    {"controlled-frame-web-request-security-info",
     flag_descriptions::kControlledFrameWebRequestSecurityInfoName,
     flag_descriptions::kControlledFrameWebRequestSecurityInfoDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         blink::features::kControlledFrameWebRequestSecurityInfo)},
    {"source-specific-multicast-in-direct-sockets",
     flag_descriptions::kSourceSpecificMulticastInDirectSocketsName,
     flag_descriptions::kSourceSpecificMulticastInDirectSocketsDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         blink::features::kSourceSpecificMulticastInDirectSockets)},



    {"enable-cross-device-pref-tracker",
     flag_descriptions::kEnableCrossDevicePrefTrackerName,
     flag_descriptions::kEnableCrossDevicePrefTrackerDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         sync_preferences::features::kEnableCrossDevicePrefTracker)},



#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
    {"contextual-suggestion-ui-improvements",
     flag_descriptions::kContextualSuggestionsUiImprovementsName,
     flag_descriptions::kContextualSuggestionsUiImprovementsDescription,
     kOsDesktop, MULTI_VALUE_TYPE(kContextualSuggestionsUiImprovementsChoices)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

    {"verify-qwacs", flag_descriptions::kVerifyQWACsName,
     flag_descriptions::kVerifyQWACsDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kVerifyQWACs)},

    {"autofill-prefer-buy-now-pay-later-blocklists",
     flag_descriptions::kAutofillPreferBuyNowPayLaterBlocklistsName,
     flag_descriptions::kAutofillPreferBuyNowPayLaterBlocklistsDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillPreferBuyNowPayLaterBlocklists)},

    {"autofill-enable-ai-based-amount-extraction",
     flag_descriptions::kAutofillEnableAiBasedAmountExtractionName,
     flag_descriptions::kAutofillEnableAiBasedAmountExtractionDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableAiBasedAmountExtraction)},

    {"contextual-tasks",
     contextual_tasks::flag_descriptions::kContextualTasksName,
     contextual_tasks::flag_descriptions::kContextualTasksDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(contextual_tasks::kContextualTasks,
                                    kContextualTasksVariations,
                                    "ContextualTasks")},

    {"omnibox-debug-logs", omnibox::flag_descriptions::kOmniboxDebugLogsName,
     omnibox::flag_descriptions::kOmniboxDebugLogsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kOmniboxDebugLogs)},

    {"contextual-tasks-context",
     contextual_tasks::flag_descriptions::kContextualTasksContextName,
     contextual_tasks::flag_descriptions::kContextualTasksContextDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(contextual_tasks::kContextualTasksContext,
                                    kContextualTaskContextVariations,
                                    "ContextualTasks")},

    {"contextual-tasks-suggestions-enabled",
     contextual_tasks::flag_descriptions::
         kContextualTasksSuggestionsEnabledName,
     contextual_tasks::flag_descriptions::
         kContextualTasksSuggestionsEnabledDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(contextual_tasks::kContextualTasksSuggestionsEnabled)},

    {"contextual-tasks-context-library",
     contextual_tasks::flag_descriptions::kContextualTasksContextLibraryName,
     contextual_tasks::flag_descriptions::
         kContextualTasksContextLibraryDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(contextual_tasks::kContextualTasksContextLibrary)},

    {"create-new-tab-group-app-menu-top-level",
     flag_descriptions::kCreateNewTabGroupAppMenuTopLevelName,
     flag_descriptions::kCreateNewTabGroupAppMenuTopLevelDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kCreateNewTabGroupAppMenuTopLevel)},


    {"autofill-enable-buy-now-pay-later-updated-suggestion-second-line-string",
     flag_descriptions::
         kAutofillEnableBuyNowPayLaterUpdatedSuggestionSecondLineStringName,
     flag_descriptions::
         kAutofillEnableBuyNowPayLaterUpdatedSuggestionSecondLineStringDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::
             kAutofillEnableBuyNowPayLaterUpdatedSuggestionSecondLineString)},

    {"cryptography-compliance-cnsa",
     flag_descriptions::kCryptographyComplianceCnsaName,
     flag_descriptions::kCryptographyComplianceCnsaDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kCryptographyComplianceCnsa)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"lens-reinvocation-affordance",
     flag_descriptions::kLensSearchReinvocationAffordanceName,
     flag_descriptions::kLensSearchReinvocationAffordanceDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensSearchReinvocationAffordance)},

    {"lens-search-zero-state-csb",
     flag_descriptions::kLensSearchZeroStateCsbName,
     flag_descriptions::kLensSearchZeroStateCsbDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensSearchZeroStateCsb)},

    {"lens-updated-feedback-entrypoint",
     flag_descriptions::kLensUpdatedFeedbackEntrypointName,
     flag_descriptions::kLensUpdatedFeedbackEntrypointDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensUpdatedFeedbackEntrypoint)},

    {"lens-video-citations", flag_descriptions::kLensVideoCitationsName,
     flag_descriptions::kLensVideoCitationsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensVideoCitations)},
#endif

    {"autofill-prioritize-save-card-over-mandatory-reauth",
     flag_descriptions::kAutofillPrioritizeSaveCardOverMandatoryReauthName,
     flag_descriptions::
         kAutofillPrioritizeSaveCardOverMandatoryReauthDescription,
     kOsMac | kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillPrioritizeSaveCardOverMandatoryReauth)},



    {"variations-seed-corpus", flag_descriptions::kVariationsSeedCorpusName,
     flag_descriptions::kVariationsSeedCorpusDescription, kOsAll,
     STRING_VALUE_TYPE(variations::switches::kVariationsSeedCorpus, "")},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"passkey-unlock-manager", flag_descriptions::kPasskeyUnlockManagerName,
     flag_descriptions::kPasskeyUnlockManagerDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(device::kPasskeyUnlockManager)},

    {"passkey-unlock-error-ui", flag_descriptions::kPasskeyUnlockErrorUiName,
     flag_descriptions::kPasskeyUnlockErrorUiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(device::kPasskeyUnlockErrorUi)},
#endif



    {"block-v8-optimizer-on-unfamiliar-sites",
     flag_descriptions::kBlockV8OptimizerOnUnfamiliarSitesSettingName,
     flag_descriptions::kBlockV8OptimizerOnUnfamiliarSitesSettingDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(content_settings::features::
                            kBlockV8OptimizerOnUnfamiliarSitesSetting)},

    {"service-worker-synthetic-response",
     flag_descriptions::kServiceWorkerSyntheticResponseName,
     flag_descriptions::kServiceWorkerSyntheticResponseDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kServiceWorkerSyntheticResponse)},

    {"user-value-default-browser-strings",
     flag_descriptions::kUserValueDefaultBrowserStringsName,
     flag_descriptions::kUserValueDefaultBrowserStringsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kUserValueDefaultBrowserStrings)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"read-anything-read-aloud-ts-text-segmentation",
     flag_descriptions::kReadAnythingReadAloudTsTextSegmentationName,
     flag_descriptions::kReadAnythingReadAloudTsTextSegmentationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingReadAloudTSTextSegmentation)},
#endif
    {"mdm-errors-for-dasher-accounts-handling",
     flag_descriptions::kHandleMdmErrorsForDasherAccountsName,
     flag_descriptions::kHandleMdmErrorsForDasherAccountsDescription, kOsAll,
     FEATURE_VALUE_TYPE(switches::kHandleMdmErrorsForDasherAccounts)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"disable-u18-feedback-desktop",
     flag_descriptions::kDisableU18FeedbackDesktopName,
     flag_descriptions::kDisableU18FeedbackDesktopDescription,
     kOsWin | kOsMac | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(switches::kDisableU18FeedbackDesktop,
                                    kDisableU18FeedbackDesktopVariations,
                                    "DisableU18FeedbackDesktop")},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"profile-creation-decline-signin-cta-experiment",
     flag_descriptions::kProfileCreationDeclineSigninCTAExperimentName,
     flag_descriptions::kProfileCreationDeclineSigninCTAExperimentDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kProfileCreationDeclineSigninCTAExperiment)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"profile-creation-friction-reduction-experiment-prefill-name-requirement",
     flag_descriptions::
         kProfileCreationFrictionReductionExperimentPrefillNameRequirementName,
     flag_descriptions::
         kProfileCreationFrictionReductionExperimentPrefillNameRequirementDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(
         switches::
             kProfileCreationFrictionReductionExperimentPrefillNameRequirement)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"profile-creation-friction-reduction-experiment-remove-signin-step",
     flag_descriptions::
         kProfileCreationFrictionReductionExperimentRemoveSigninStepName,
     flag_descriptions::
         kProfileCreationFrictionReductionExperimentRemoveSigninStepDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(
         switches::
             kProfileCreationFrictionReductionExperimentRemoveSigninStep)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"profile-creation-friction-reduction-experiment-skip-customize-profile",
     flag_descriptions::
         kProfileCreationFrictionReductionExperimentSkipCustomizeProfileName,
     flag_descriptions::
         kProfileCreationFrictionReductionExperimentSkipCustomizeProfileDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(
         switches::
             kProfileCreationFrictionReductionExperimentSkipCustomizeProfile)},
#endif

    {"search-settings-update", flag_descriptions::kSearchSettingsUpdateName,
     flag_descriptions::kSearchSettingsUpdateDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(switches::kSearchSettingsUpdate)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"show-profile-picker-to-all-users-experiment",
     flag_descriptions::kShowProfilePickerToAllUsersExperimentName,
     flag_descriptions::kShowProfilePickerToAllUsersExperimentDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kShowProfilePickerToAllUsersExperiment)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"open-all-profiles-from-profile-picker-experiment",
     flag_descriptions::kOpenAllProfilesFromProfilePickerExperimentName,
     flag_descriptions::kOpenAllProfilesFromProfilePickerExperimentDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kOpenAllProfilesFromProfilePickerExperiment)},
#endif

    {"profile-signals-reporting-enabled",
     flag_descriptions::kProfileSignalsReportingEnabledName,
     flag_descriptions::kProfileSignalsReportingEnabledDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         enterprise_signals::features::kProfileSignalsReportingEnabled)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"projects-panel", flag_descriptions::kProjectsPanelName,
     flag_descriptions::kProjectsPanelDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(tab_groups::kProjectsPanel,
                                    kProjectsPanelVariations,
                                    "ProjectsPanel")},
    {"sync-ai-threads", flag_descriptions::kSyncAIThreadsName,
     flag_descriptions::kSyncAIThreadsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(syncer::kSyncAIThread)},
    {"sync-gemini-threads", flag_descriptions::kSyncGeminiThreadsName,
     flag_descriptions::kSyncGeminiThreadsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(syncer::kSyncGeminiThread)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
        // BUILDFLAG(IS_CHROMEOS)


#if BUILDFLAG(ENABLE_DEVICE_BOUND_SESSIONS)
    {"use-unexportable-key-service-in-browser-process",
     flag_descriptions::kUseUnexportableKeyServiceInBrowserProcessName,
     flag_descriptions::kUseUnexportableKeyServiceInBrowserProcessDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(
         network::features::kUseUnexportableKeyServiceInBrowserProcess)},
#endif


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"profile-picker-text-variations",
     flag_descriptions::kProfilePickerTextVariationsName,
     flag_descriptions::kProfilePickerTextVariationsDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_WITH_PARAMS_VALUE_TYPE(switches::kProfilePickerTextVariations,
                                    kProfilePickerTextVariations,
                                    "ProfilePickerTextVariations")},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"lens-overlay-optimization-filter",
     flag_descriptions::kLensOverlayOptimizationFilterName,
     flag_descriptions::kLensOverlayOptimizationFilterDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayOptimizationFilter)},
#endif

    {"web-app-migrate-preinstalled-chat",
     flag_descriptions::kWebAppMigratePreinstalledChatName,
     flag_descriptions::kWebAppMigratePreinstalledChatDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebAppMigratePreinstalledChat)},

    {"web-app-install-dialog", flag_descriptions::kWebAppInstallDialogName,
     flag_descriptions::kWebAppInstallDialogDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebAppInstallDialog)},

    {"connection-allowlists", flag_descriptions::kConnectionAllowlistsName,
     flag_descriptions::kConnectionAllowlistsDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kConnectionAllowlists)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"read-anything-with-readability-enabled",
     flag_descriptions::kReadAnythingWithReadabilityName,
     flag_descriptions::kReadAnythingWithReadabilityDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingWithReadability)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"read-anything-omnibox-chip",
     flag_descriptions::kReadAnythingOmniboxChipName,
     flag_descriptions::kReadAnythingOmniboxChipDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingOmniboxChip)},
#endif

#if BUILDFLAG(IS_MAC)
    {"unexportable-key-deletion",
     flag_descriptions::kUnexportableKeyDeletionName,
     flag_descriptions::kUnexportableKeyDeletionDescription, kOsMac,
     FEATURE_VALUE_TYPE(unexportable_keys::kUnexportableKeyDeletion)},
#endif


    {"autofill-disable-bnpl-country-check-for-testing",
     flag_descriptions::kAutofillDisableBnplCountryCheckForTestingName,
     flag_descriptions::kAutofillDisableBnplCountryCheckForTestingDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillDisableBnplCountryCheckForTesting)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"infobar-prioritization", flag_descriptions::kInfobarPrioritizationName,
     flag_descriptions::kInfobarPrioritizationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(infobars::features::kInfobarPrioritization)},
#endif

    {"infobar-refresh", flag_descriptions::kInfobarRefreshName,
     flag_descriptions::kInfobarRefreshDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kInfobarRefresh)},


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"read-anything-immersive-reading-mode",
     flag_descriptions::kReadAnythingImmersiveReadingModeName,
     flag_descriptions::kReadAnythingImmersiveReadingModeDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(features::kImmersiveReadAnything)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"lens-overlay-non-blocking-privacy-notice",
     flag_descriptions::kLensOverlayNonBlockingPrivacyNoticeName,
     flag_descriptions::kLensOverlayNonBlockingPrivacyNoticeDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayNonBlockingPrivacyNotice)},
#endif

    {"migrate-syncing-user-to-signed-in",
     flag_descriptions::kMigrateSyncingUserToSignedInName,
     flag_descriptions::kMigrateSyncingUserToSignedInDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kMigrateSyncingUserToSignedIn)},

    {"undo-migration-of-syncing-user-to-signed-in",
     flag_descriptions::kUndoMigrationOfSyncingUserToSignedInName,
     flag_descriptions::kUndoMigrationOfSyncingUserToSignedInDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kUndoMigrationOfSyncingUserToSignedIn)},


    {"dom-storage-sqlite", flag_descriptions::kDomStorageSqliteName,
     flag_descriptions::kDomStorageSqliteDescription, kOsAll,
     FEATURE_VALUE_TYPE(storage::kDomStorageSqlite)},

    {"idb-sqlite-backing-store", flag_descriptions::kIdbSqliteBackingStoreName,
     flag_descriptions::kIdbSqliteBackingStoreDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kIdbSqliteBackingStore)},


    // On other platforms, this requires --enable-features=ElasticOverscroll to
    // have an effect.
    {"overscroll-effect-on-non-root-scrollers",
     flag_descriptions::kOverscrollEffectOnNonRootScrollersName,
     flag_descriptions::kOverscrollEffectOnNonRootScrollersDescription,
     kOsMac | kOsAndroid | kOsWin,
     FEATURE_VALUE_TYPE(features::kOverscrollEffectOnNonRootScrollers)},


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"enable-your-saved-info-settings-page",
     flag_descriptions::kYourSavedInfoSettingsPageName,
     flag_descriptions::kYourSavedInfoSettingsPageDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kYourSavedInfoSettingsPage)},
#endif

    {"cws-promotion-banner-flag",
     flag_descriptions::kEnableShouldShowPromotionName,
     flag_descriptions::kEnableShouldShowPromotionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kEnableShouldShowPromotion)},




#if BUILDFLAG(IS_MAC)
    {"mac-enable-okta-sso", flag_descriptions::kEnableOktaSSOName,
     flag_descriptions::kEnableOktaSSODescription, kOsMac,
     FEATURE_VALUE_TYPE(enterprise_auth::kOktaSSO)},
#endif

    {"autofill-enable-wallet-branding",
     flag_descriptions::kAutofillEnableWalletBrandingName,
     flag_descriptions::kAutofillEnableWalletBrandingDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableWalletBranding)},




    {"private-metrics-enable-puma",
     flag_descriptions::kPrivateMetricsEnablePumaName,
     flag_descriptions::kPrivateMetricsEnablePumaDescription, kOsAll,
     FEATURE_VALUE_TYPE(metrics::private_metrics::kPrivateMetricsPuma)},

    {"private-metrics-enable-puma-rc",
     flag_descriptions::kPrivateMetricsEnablePumaRcName,
     flag_descriptions::kPrivateMetricsEnablePumaRcDescription, kOsAll,
     FEATURE_VALUE_TYPE(metrics::private_metrics::kPrivateMetricsPumaRc)},

    {"autofill-ai-based-amount-extraction-ignore-seen-terms-for-testing",
     flag_descriptions::
         kAutofillAiBasedAmountExtractionIgnoreSeenTermsForTestingName,
     flag_descriptions::
         kAutofillAiBasedAmountExtractionIgnoreSeenTermsForTestingDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::
             kAutofillAiBasedAmountExtractionIgnoreSeenTermsForTesting)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"updater-ui", flag_descriptions::kUpdaterUIName,
     flag_descriptions::kUpdaterUIDescription, kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(features::kUpdaterUI)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"read-anything-line-focus", flag_descriptions::kReadAnythingLineFocusName,
     flag_descriptions::kReadAnythingLineFocusDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingLineFocus)},
#endif

    {"verify-mtcs", flag_descriptions::kVerifyMTCsName,
     flag_descriptions::kVerifyMTCsDescription, kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(net::features::kVerifyMTCs)},


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"password-upload-ui-update",
     flag_descriptions::kPasswordUploadUiUpdateName,
     flag_descriptions::kPasswordUploadUiUpdateDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(switches::kPasswordUploadUiUpdate)},
#endif





#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"saas-usage-reporting", flag_descriptions::kSaasUsageReportingName,
     flag_descriptions::kSaasUsageReportingDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(enterprise_reporting::kSaasUsageReporting)},
#endif

    {"autofill-enable-pay-now-pay-later-tabs",
     flag_descriptions::kAutofillEnablePayNowPayLaterTabsName,
     flag_descriptions::kAutofillEnablePayNowPayLaterTabsDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnablePayNowPayLaterTabs)},

    {"web-authentication-ambient-signin",
     flag_descriptions::kWebAuthnAmbientSigninName,
     flag_descriptions::kWebAuthnAmbientSigninDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(device::kWebAuthnAmbientSignin)},

    {"devtools-protocol-monitor",
     flag_descriptions::kDevToolsProtocolMonitorName,
     flag_descriptions::kDevToolsProtocolMonitorDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDevToolsProtocolMonitor)},

    {"devtools-webmcp-support", flag_descriptions::kDevToolsWebMCPSupportName,
     flag_descriptions::kDevToolsWebMCPSupportDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kDevToolsWebMCPSupport)},


    {"bundled-security-settings-secure-dns-v2",
     flag_descriptions::kBundledSecuritySettingsSecureDnsV2Name,
     flag_descriptions::kBundledSecuritySettingsSecureDnsV2Description,
     kOsDesktop,
     FEATURE_VALUE_TYPE(safe_browsing::kBundledSecuritySettingsSecureDnsV2)},

    {"launch-queue-stop-sending-on-reload",
     flag_descriptions::kWebAppLaunchQueueStopSendingOnReloadName,
     flag_descriptions::kWebAppLaunchQueueStopSendingOnReloadDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(webapps::features::kLaunchQueueStopSendingOnReload)},

    {"credential-management-unified-ui",
     flag_descriptions::kCredentialManagementUnifiedUiName,
     flag_descriptions::kCredentialManagementUnifiedUiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         password_manager::features::kCredentialManagementUnifiedUi)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
    {"search-engine-explicit-choice-dialog",
     flag_descriptions::kSearchEngineExplicitChoiceDialogName,
     flag_descriptions::kSearchEngineExplicitChoiceDialogDescription,
     kOsWin | kOsMac,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         extensions_features::kSearchEngineExplicitChoiceDialog,
         kSearchEngineExplicitChoiceDialogVariations,
         "SearchEngineExplicitChoiceDialogVariations")},

    {"search-engine-unconditional-dialog",
     flag_descriptions::kSearchEngineUnconditionalDialogName,
     flag_descriptions::kSearchEngineUnconditionalDialogDescription,
     kOsWin | kOsMac,
     FEATURE_VALUE_TYPE(extensions_features::kSearchEngineUnconditionalDialog)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

    {"enable-webmcp-testing", flag_descriptions::kWebMCPTestingName,
     flag_descriptions::kWebMCPTestingDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kWebMCPTesting)},


    {"autofill-enable-new-amex-network-art",
     flag_descriptions::kAutofillEnableNewAmexNetworkArtName,
     flag_descriptions::kAutofillEnableNewAmexNetworkArtDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableNewAmexNetworkArt)},


    {"chrome-finds-internals", flag_descriptions::kChromeFindsInternalsName,
     flag_descriptions::kChromeFindsInternalsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kChromeFindsInternals)},

    {"devtools-enable-durable-messages",
     flag_descriptions::kDevToolsEnableDurableMessagesName,
     flag_descriptions::kDevToolsEnableDurableMessagesDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDevToolsEnableDurableMessages)},

    {"unthrottle-async-touch-moves",
     flag_descriptions::kUnthrottleAsyncTouchMovesName,
     flag_descriptions::kUnthrottleAsyncTouchMovesDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kUnthrottleAsyncTouchMoves,
                                    kUnthrottleAsyncTouchMovesVariations,
                                    "UnthrottleAsyncTouchMoves")},

    {"horizontal-tab-strip-combo-button",
     flag_descriptions::kHorizontalTabStripComboButtonName,
     flag_descriptions::kHorizontalTabStripComboButtonDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(tabs::kHorizontalTabStripComboButton)},



#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    {"signin-promo-on-avatar-pill",
     flag_descriptions::kSigninPromoOnAvatarPillName,
     flag_descriptions::kSigninPromoOnAvatarPillDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(switches::kSigninPromoOnAvatarPill,
                                    kSigninPromoOnAvatarPillVariation,
                                    "SigninPromoOnAvatarPillVariation")},
#endif
    // Add new entries above this line.
    // NOTE: Adding a new flag requires adding a corresponding entry to enum
    // "LoginCustomFlags" in tools/metrics/histograms/enums.xml. See "Flag
    // Histograms" in tools/metrics/histograms/README.md (run the
    // AboutFlagsHistogramTest unit test to verify this process).
};

class FlagsStateSingleton : public flags_ui::FlagsState::Delegate {
 public:
  FlagsStateSingleton()
      : flags_state_(
            std::make_unique<flags_ui::FlagsState>(kFeatureEntries, this)) {}
  FlagsStateSingleton(const FlagsStateSingleton&) = delete;
  FlagsStateSingleton& operator=(const FlagsStateSingleton&) = delete;
  ~FlagsStateSingleton() override = default;

  static FlagsStateSingleton* GetInstance() {
    return base::Singleton<FlagsStateSingleton>::get();
  }

  static flags_ui::FlagsState* GetFlagsState() {
    return GetInstance()->flags_state_.get();
  }

  void RebuildState(const std::vector<flags_ui::FeatureEntry>& entries) {
    flags_state_ = std::make_unique<flags_ui::FlagsState>(entries, this);
  }

  void RestoreDefaultState() {
    flags_state_ =
        std::make_unique<flags_ui::FlagsState>(kFeatureEntries, this);
  }

 private:
  // flags_ui::FlagsState::Delegate:
  bool ShouldExcludeFlag(const flags_ui::FlagsStorage* storage,
                         const FeatureEntry& entry) override {
    return flags::IsFlagExpired(storage, entry.internal_name);
  }

  std::unique_ptr<flags_ui::FlagsState> flags_state_;
};

bool ShouldSkipNonDeprecatedFeatureEntry(const FeatureEntry& entry) {
  return ~entry.supported_platforms & kDeprecated;
}

}  // namespace


// ash-chrome uses different storage flag storage logic from other desktop
// platforms.
void GetStorage(Profile* profile, GetStorageCallback callback) {
  std::move(callback).Run(std::make_unique<flags_ui::PrefServiceFlagsStorage>(
                              g_browser_process->local_state()),
                          flags_ui::kOwnerAccessToFlags);
}

bool ShouldSkipConditionalFeatureEntry(const flags_ui::FlagsStorage* storage,
                                       const FeatureEntry& entry) {
#if BUILDFLAG(ENABLE_EXTENSIONS)
  version_info::Channel chrome_channel = chrome::GetChannel();
  // Only show extension AI data flag in non-stable channels.
  if (std::string_view(kExtensionAiDataInternalName) == entry.internal_name) {
    return chrome_channel != version_info::Channel::BETA &&
           chrome_channel != version_info::Channel::DEV &&
           chrome_channel != version_info::Channel::CANARY &&
           chrome_channel != version_info::Channel::UNKNOWN;
  }
#endif


  // Only show Webium flag for Canary channel and developer builds.
  if (std::string_view(kWebiumFlag) == entry.internal_name) {
    return chrome::GetChannel() != version_info::Channel::CANARY &&
           version_info::IsOfficialBuild();
  }

  if (flags::IsFlagExpired(storage, entry.internal_name)) {
    return true;
  }

  return false;
}

void ConvertFlagsToSwitches(flags_ui::FlagsStorage* flags_storage,
                            base::CommandLine* command_line,
                            flags_ui::SentinelsMode sentinels) {
  if (command_line->HasSwitch(switches::kNoExperiments)) {
    return;
  }

  FlagsStateSingleton::GetFlagsState()->ConvertFlagsToSwitches(
      flags_storage, command_line, sentinels, switches::kEnableFeatures,
      switches::kDisableFeatures);
}

std::vector<std::string> RegisterAllFeatureVariationParameters(
    flags_ui::FlagsStorage* flags_storage,
    base::FeatureList* feature_list) {
  return FlagsStateSingleton::GetFlagsState()
      ->RegisterAllFeatureVariationParameters(flags_storage, feature_list);
}

void GetFlagFeatureEntries(flags_ui::FlagsStorage* flags_storage,
                           flags_ui::FlagAccess access,
                           base::ListValue& supported_entries,
                           base::ListValue& unsupported_entries) {
  FlagsStateSingleton::GetFlagsState()->GetFlagFeatureEntries(
      flags_storage, access, supported_entries, unsupported_entries,
      base::BindRepeating(&ShouldSkipConditionalFeatureEntry,
                          // Unretained: this callback doesn't outlive this
                          // stack frame.
                          base::Unretained(flags_storage)));
}

void GetFlagFeatureEntriesForDeprecatedPage(
    flags_ui::FlagsStorage* flags_storage,
    flags_ui::FlagAccess access,
    base::ListValue& supported_entries,
    base::ListValue& unsupported_entries) {
  FlagsStateSingleton::GetFlagsState()->GetFlagFeatureEntries(
      flags_storage, access, supported_entries, unsupported_entries,
      base::BindRepeating(&ShouldSkipNonDeprecatedFeatureEntry));
}

flags_ui::FlagsState* GetCurrentFlagsState() {
  return FlagsStateSingleton::GetFlagsState();
}

bool IsRestartNeededToCommitChanges() {
  return FlagsStateSingleton::GetFlagsState()->IsRestartNeededToCommitChanges();
}

void SetFeatureEntryEnabled(flags_ui::FlagsStorage* flags_storage,
                            const std::string& internal_name,
                            bool enable) {
  FlagsStateSingleton::GetFlagsState()->SetFeatureEntryEnabled(
      flags_storage, internal_name, enable);
}

void SetOriginListFlag(const std::string& internal_name,
                       const std::string& value,
                       flags_ui::FlagsStorage* flags_storage) {
  FlagsStateSingleton::GetFlagsState()->SetOriginListFlag(internal_name, value,
                                                          flags_storage);
}

void SetStringFlag(const std::string& internal_name,
                   const std::string& value,
                   flags_ui::FlagsStorage* flags_storage) {
  FlagsStateSingleton::GetFlagsState()->SetStringFlag(internal_name, value,
                                                      flags_storage);
}

void RemoveFlagsSwitches(base::CommandLine::SwitchMap* switch_list) {
  FlagsStateSingleton::GetFlagsState()->RemoveFlagsSwitches(switch_list);
}

void ResetAllFlags(flags_ui::FlagsStorage* flags_storage) {
  FlagsStateSingleton::GetFlagsState()->ResetAllFlags(flags_storage);
}

void RecordUMAStatistics(flags_ui::FlagsStorage* flags_storage,
                         const std::string& histogram_name) {
  std::set<std::string> switches;
  std::set<std::string> features;
  std::set<std::string> variation_ids;
  FlagsStateSingleton::GetFlagsState()->GetSwitchesAndFeaturesFromFlags(
      flags_storage, &switches, &features, &variation_ids);
  // Don't report variation IDs since we don't have an UMA histogram for them.
  flags_ui::ReportAboutFlagsHistogram(histogram_name, switches, features);
}

namespace testing {

std::vector<FeatureEntry>* GetEntriesForTesting() {
  static base::NoDestructor<std::vector<FeatureEntry>> entries;
  return entries.get();
}

void SetFeatureEntries(const std::vector<FeatureEntry>& entries) {
  auto* entries_for_testing = GetEntriesForTesting();  // IN-TEST
  CHECK(entries_for_testing->empty());
  entries_for_testing->insert(entries_for_testing->end(), entries.begin(),
                              entries.end());
  FlagsStateSingleton::GetInstance()->RebuildState(*entries_for_testing);
}

ScopedFeatureEntries::ScopedFeatureEntries(
    const std::vector<flags_ui::FeatureEntry>& entries) {
  SetFeatureEntries(entries);
}

ScopedFeatureEntries::~ScopedFeatureEntries() {
  GetEntriesForTesting()->clear();  // IN-TEST
  // Restore the flag state to the production flags.
  FlagsStateSingleton::GetInstance()->RestoreDefaultState();
}

base::span<const FeatureEntry> GetFeatureEntries() {
  if (const auto* entries_for_testing = GetEntriesForTesting();
      !entries_for_testing->empty()) {
    return *entries_for_testing;
  }
  return kFeatureEntries;
}

}  // namespace testing

}  // namespace about_flags
