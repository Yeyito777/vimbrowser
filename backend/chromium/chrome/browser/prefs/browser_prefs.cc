// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/browser_prefs.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/values_util.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "cef/libcef/features/features.h"
#include "chrome/browser/about_flags.h"
#include "chrome/browser/accessibility/accessibility_labels_service.h"
#include "chrome/browser/accessibility/invert_bubble_prefs.h"
#include "chrome/browser/accessibility/page_colors_controller.h"
#include "chrome/browser/accessibility/prefers_default_scrollbar_styles_prefs.h"
#include "chrome/browser/browser_process_impl.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/component_updater/component_updater_prefs.h"
#include "chrome/browser/contextual_cueing/contextual_cueing_prefs.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/engagement/important_sites_util.h"
#include "chrome/browser/enterprise/reporting/prefs.h"
#include "chrome/browser/enterprise/util/managed_browser_utils.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/glic/glic_pref_names.h"
#include "chrome/browser/gpu/gpu_mode_manager.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/login_detection/login_detection_prefs.h"
#include "chrome/browser/media/media_engagement_service.h"
#include "chrome/browser/media/media_storage_id_salt.h"
#include "chrome/browser/media/prefs/capture_device_ranking.h"
#include "chrome/browser/media/webrtc/capture_policy_utils.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/webrtc/permission_bubble_media_access_handler.h"
#include "chrome/browser/memory/enterprise_memory_limit_pref_observer.h"
#include "chrome/browser/metrics/chrome_metrics_service_client.h"
#include "chrome/browser/metrics/tab_stats/tab_stats_tracker.h"
#include "chrome/browser/net/net_error_tab_helper.h"
#include "chrome/browser/net/profile_network_context_service.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/notifications/platform_notification_service_impl.h"
#include "chrome/browser/platform_experience/prefs.h"
#include "chrome/browser/prefs/chrome_pref_service_factory.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/preloading/prefetch/prefetch_service/prefetch_origin_decider.h"
#include "chrome/browser/preloading/prefetch/search_prefetch/search_prefetch_service.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/privacy_sandbox/notice/notice_storage.h"
#include "chrome/browser/profiles/chrome_version_service.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_impl.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/push_messaging/push_messaging_app_identifier.h"
#include "chrome/browser/push_messaging/push_messaging_service_impl.h"
#include "chrome/browser/push_messaging/push_messaging_unsubscribed_entry.h"
#include "chrome/browser/rlz/chrome_rlz_tracker_delegate.h"
#include "chrome/browser/search/background/ntp_custom_background_service.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/serial/serial_policy_allowed_ports.h"
#include "chrome/browser/sharing_hub/sharing_hub_features.h"
#include "chrome/browser/signin/chrome_signin_client.h"
#include "chrome/browser/signin/signin_promo_util.h"
#include "chrome/browser/ssl/ssl_config_service_manager.h"
#include "chrome/browser/subscription_eligibility/subscription_eligibility_prefs.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/tracing/chrome_tracing_delegate.h"
#include "chrome/browser/ui/browser_ui_prefs.h"
#include "chrome/browser/ui/network_profile_bubble.h"
#include "chrome/browser/ui/performance_controls/performance_controls_metrics.h"
#include "chrome/browser/ui/prefs/prefs_tab_helper.h"
#include "chrome/browser/ui/safety_hub/safety_hub_prefs.h"
#include "chrome/browser/ui/search_engines/keyword_editor_controller.h"
#include "chrome/browser/ui/tabs/projects/projects_prefs.h"
#include "chrome/browser/ui/tabs/tab_strip_prefs.h"
#include "chrome/browser/ui/toolbar/chrome_labs/chrome_labs_prefs.h"
#include "chrome/browser/ui/toolbar/chrome_location_bar_model_delegate.h"
#include "chrome/browser/ui/toolbar/toolbar_pref_names.h"
#include "chrome/browser/ui/webui/accessibility/accessibility_ui.h"
#include "chrome/browser/ui/webui/bookmarks/bookmark_prefs.h"
#include "chrome/browser/ui/webui/flags/flags_ui.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "chrome/browser/ui/webui/policy/policy_ui.h"
#include "chrome/browser/webauthn/webauthn_pref_names.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/secure_origin_allowlist.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/blocked_content/safe_browsing_triggered_popup_blocker.h"
#include "components/breadcrumbs/core/breadcrumbs_status.h"
#include "components/browsing_data/core/pref_names.h"
#include "components/certificate_transparency/pref_names.h"
#include "components/collaboration/public/pref_names.h"
#include "components/commerce/core/prefs.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/contextual_search/contextual_search_service.h"
#include "components/custom_handlers/protocol_handler_registry.h"
#include "components/dom_distiller/core/distilled_page_prefs.h"
#include "components/domain_reliability/domain_reliability_prefs.h"
#include "components/embedder_support/origin_trials/origin_trial_prefs.h"
#include "components/enterprise/browser/identifiers/identifiers_prefs.h"
#include "components/enterprise/browser/promotion/promotion_prefs.h"
#include "components/enterprise/buildflags/buildflags.h"
#include "components/enterprise/connectors/core/connectors_prefs.h"
#include "components/feature_engagement/public/pref_names.h"
#include "components/history_clusters/core/history_clusters_prefs.h"
#include "components/image_fetcher/core/cache/image_cache.h"
#include "components/invalidation/impl/per_user_topic_subscription_manager.h"
#include "components/language/content/browser/geo_language_provider.h"
#include "components/language/content/browser/ulp_language_code_locator/ulp_language_code_locator.h"
#include "components/language/core/browser/language_prefs.h"
#include "components/lens/buildflags.h"
#include "components/lookalikes/core/lookalike_url_util.h"
#include "components/media_device_salt/media_device_id_salt.h"
#include "components/metrics/demographics/user_demographics.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/network_time/network_time_tracker.h"
#include "components/ntp_tiles/custom_links_manager_impl.h"
#include "components/ntp_tiles/enterprise/enterprise_shortcuts_manager_impl.h"
#include "components/ntp_tiles/most_visited_sites.h"
#include "components/ntp_tiles/pref_names.h"
#include "components/ntp_tiles/tile_type.h"
#include "components/offline_pages/buildflags/buildflags.h"
#include "components/omnibox/browser/aim_eligibility_service.h"
#include "components/omnibox/browser/document_provider.h"
#include "components/omnibox/browser/omnibox_prefs.h"
#include "components/omnibox/browser/zero_suggest_provider.h"
#include "components/optimization_guide/core/model_execution/model_execution_prefs.h"
#include "components/optimization_guide/core/optimization_guide_prefs.h"
#include "components/page_info/core/merchant_trust_service.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/payments/core/payment_prefs.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/permissions/permission_hats_trigger_helper.h"
#include "components/permissions/pref_names.h"
#include "components/plus_addresses/core/common/plus_address_prefs.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/browser/url_list/url_blocklist_manager.h"
#include "components/policy/core/common/local_test_policy_provider.h"
#include "components/policy/core/common/management/management_service.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/policy/core/common/policy_statistics_collector.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/privacy_sandbox/privacy_sandbox_prefs.h"
#include "components/proxy_config/pref_proxy_config_tracker_impl.h"
#include "components/regional_capabilities/regional_capabilities_prefs.h"
#include "components/safe_browsing/buildflags.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/safety_check/safety_check_prefs.h"
#include "components/saved_tab_groups/public/pref_names.h"
#include "components/search_engines/search_engine_choice/search_engine_choice_service.h"
#include "components/search_engines/template_url_prepopulate_data.h"
#include "components/security_interstitials/content/insecure_form_blocking_page.h"
#include "components/security_interstitials/content/stateful_ssl_host_state_delegate.h"
#include "components/segmentation_platform/embedder/default_model/device_switcher_result_dispatcher.h"
#include "components/segmentation_platform/public/segmentation_platform_service.h"
#include "components/sessions/core/session_id_generator.h"
#include "components/sharing_message/sharing_sync_preference.h"
#include "components/signin/core/browser/active_primary_accounts_metrics_recorder.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/base/signin_prefs.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/site_engagement/content/site_engagement_service.h"
#include "components/subresource_filter/content/browser/ruleset_service.h"
#include "components/subresource_filter/core/common/constants.h"
#include "components/supervised_user/core/browser/supervised_user_preferences.h"
#include "components/supervised_user/core/common/pref_names.h"
#include "components/sync/base/pref_names.h"
#include "components/sync/service/device_statistics_scheduler.h"
#include "components/sync/service/glue/sync_transport_data_prefs.h"
#include "components/sync/service/sync_prefs.h"
#include "components/sync_device_info/device_info_prefs.h"
#include "components/sync_preferences/cross_device_pref_tracker/prefs/cross_device_pref_registry.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "components/sync_sessions/session_sync_prefs.h"
#include "components/tpcd/metadata/browser/prefs.h"
#include "components/tracing/common/pref_names.h"
#include "components/update_client/update_client.h"
#include "components/variations/service/variations_service.h"
#include "components/visited_url_ranking/internal/url_grouping/group_suggestions_service_impl.h"
#include "components/wallet/core/common/wallet_prefs.h"
#include "components/webui/chrome_urls/pref_names.h"
#include "components/webui/flags/pref_service_flags_storage.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "net/http/http_server_properties_manager.h"
#include "pdf/buildflags.h"
#include "rlz/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_BACKGROUND_MODE)
#include "chrome/browser/background/extensions/background_mode_manager.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/commands/command_service.h"
#include "chrome/browser/extensions/extension_url_overrides.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/ui/webui/extensions/extensions_ui_prefs.h"
#include "extensions/browser/api/runtime/runtime_api.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/browser/pref_names.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS_CORE)

#if BUILDFLAG(ENABLE_CEF)
#include "cef/libcef/browser/prefs/browser_prefs.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/accessibility/animation_policy_prefs.h"
#include "chrome/browser/extensions/preinstalled_apps.h"
#include "chrome/browser/ui/extensions/extension_settings_overridden_dialog.h"
#include "chrome/browser/ui/extensions/settings_api_bubble_helpers.h"
#include "extensions/browser/api/audio/audio_api.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

#if BUILDFLAG(ENABLE_PDF)
#include "chrome/browser/pdf/pdf_pref_names.h"
#endif  // BUILDFLAG(ENABLE_PDF)

#include "chrome/browser/media/unified_autoplay_config.h"

#include "chrome/browser/actor/ui/actor_ui_state_manager_prefs.h"
#include "chrome/browser/gcm/gcm_product_util.h"
#include "chrome/browser/hid/hid_policy_allowed_devices.h"
#include "chrome/browser/intranet_redirect_detector.h"
#include "chrome/browser/media/router/discovery/access_code/access_code_cast_feature.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/nearby_sharing/common/nearby_share_prefs.h"
#include "chrome/browser/new_tab_page/modules/file_suggestion/drive_service.h"
#include "chrome/browser/new_tab_page/modules/file_suggestion/microsoft_files_page_handler.h"
#include "chrome/browser/new_tab_page/modules/safe_browsing/safe_browsing_handler.h"
#include "chrome/browser/new_tab_page/modules/v2/authentication/microsoft_auth_page_handler.h"
#include "chrome/browser/new_tab_page/modules/v2/calendar/google_calendar_page_handler.h"
#include "chrome/browser/new_tab_page/modules/v2/calendar/outlook_calendar_page_handler.h"
#include "chrome/browser/new_tab_page/modules/v2/most_relevant_tab_resumption/most_relevant_tab_resumption_page_handler.h"
#include "chrome/browser/new_tab_page/modules/v2/tab_groups/tab_groups_page_handler.h"
#include "chrome/browser/new_tab_page/promos/promo_service.h"
#include "chrome/browser/screen_ai/pref_names.h"
#include "chrome/browser/search_engine_choice/search_engine_choice_dialog_service.h"
#include "chrome/browser/signin/signin_promo.h"
#include "chrome/browser/task_manager/task_manager_interface.h"
#include "chrome/browser/themes/theme_syncable_service.h"
#include "chrome/browser/ui/commerce/commerce_ui_tab_helper.h"
#include "chrome/browser/ui/hats/hats_service_desktop.h"
#include "chrome/browser/ui/read_anything/read_anything_prefs.h"
#include "chrome/browser/ui/send_tab_to_self/send_tab_to_self_bubble.h"
#include "chrome/browser/ui/side_panel/side_panel_prefs.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/tabs/organization/prefs.h"
#include "chrome/browser/ui/tabs/pinned_tab_codec.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_pref_names.h"
#include "chrome/browser/ui/tabs/tab_strip_prefs.h"
#include "chrome/browser/ui/webui/certificate_manager/certificate_manager_handler.h"
#include "chrome/browser/ui/webui/cr_components/theme_color_picker/theme_color_picker_handler.h"
#include "chrome/browser/ui/webui/history/foreign_session_handler.h"
#include "chrome/browser/ui/webui/management/management_ui.h"
#include "chrome/browser/ui/webui/new_tab_footer/new_tab_footer_ui.h"
#include "chrome/browser/ui/webui/new_tab_page/new_tab_page_handler.h"
#include "chrome/browser/ui/webui/new_tab_page/new_tab_page_ui.h"
#include "chrome/browser/ui/webui/new_tab_page/ntp_pref_names.h"
#include "chrome/browser/ui/webui/settings/settings_ui.h"
#include "chrome/browser/ui/webui/tab_search/tab_search_prefs.h"
#include "chrome/browser/upgrade_detector/upgrade_detector.h"
#include "chrome/browser/user_education/browser_user_education_storage_service.h"
#include "chrome/browser/webauthn/chrome_authenticator_request_delegate.h"
#include "components/headless/policy/headless_mode_prefs.h"
#include "components/lens/lens_overlay_permission_utils.h"
#include "components/live_caption/live_caption_controller.h"

#if BUILDFLAG(ENABLE_DEVTOOLS_FRONTEND)
#include "chrome/browser/devtools/devtools_window.h"
#endif  // BUILDFLAG(ENABLE_DEVTOOLS_FRONTEND)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "chrome/browser/ui/webui/whats_new/whats_new_ui.h"
#endif


#if BUILDFLAG(IS_MAC)
#include "chrome/browser/media/webrtc/system_media_capture_permissions_stats_mac.h"
#include "chrome/browser/ui/cocoa/apps/quit_with_apps_controller_mac.h"
#include "chrome/browser/ui/cocoa/confirm_quit.h"
#include "chrome/browser/web_applications/os_integration/mac/app_shim_registry.h"
#endif


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
#include "chrome/browser/enterprise/platform_auth/platform_auth_policy_observer.h"
#include "components/os_crypt/sync/os_crypt.h"  // nogncheck
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
#include "components/device_signals/core/browser/pref_names.h"  // nogncheck due to crbug.com/1125897
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "chrome/browser/browser_switcher/browser_switcher_prefs.h"
#include "chrome/browser/enterprise/signin/enterprise_signin_prefs.h"
#endif

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
#include "chrome/browser/ui/startup/first_run_service.h"
#endif

#if BUILDFLAG(ENABLE_DOWNGRADE_PROCESSING)
#include "chrome/browser/downgrade/downgrade_prefs.h"
#endif

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/device_identity/device_oauth2_token_store_desktop.h"
#include "chrome/browser/ui/startup/default_browser_prompt/default_browser_prompt.h"
#endif

#if defined(TOOLKIT_VIEWS)
#include "chrome/browser/ui/browser_view_prefs.h"
#include "chrome/browser/ui/side_search/side_search_prefs.h"
#endif

#if BUILDFLAG(ENABLE_SESSION_SERVICE)
#include "chrome/browser/sessions/session_data_service.h"
#include "chrome/browser/sessions/session_service_log.h"
#endif

#if BUILDFLAG(IS_LINUX)
#include "ui/color/system_theme.h"
#endif


#if BUILDFLAG(ENTERPRISE_DATA_CONTROLS)
#include "components/enterprise/data_controls/core/browser/prefs.h"
#endif

#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
#include "components/safe_browsing/content/common/file_type_policies_prefs.h"
#endif

namespace {

// Please keep the list of deprecated prefs in chronological order. i.e. Add to
// the bottom of the list, not here at the top.

// Deprecated 01/2025.
inline constexpr char kCompactModeEnabled[] = "compact_mode";

// Deprecated 01/2025.
inline constexpr char kSafeBrowsingAutomaticDeepScanningIPHSeen[] =
    "safebrowsing.automatic_deep_scanning_iph_seen";
inline constexpr char kSafeBrowsingAutomaticDeepScanPerformed[] =
    "safe_browsing.automatic_deep_scan_performed";


// Deprecated 02/2025.
inline constexpr char kUserAgentClientHintsGREASEUpdateEnabled[] =
    "policy.user_agent_client_hints_grease_update_enabled";

// Deprecated 02/2025.
inline constexpr char kDefaultSearchProviderKeywordsUseExtendedList[] =
    "default_search_provider.keywords_use_extended_list";



// Deprecated 03/2025.
inline constexpr char kPasswordChangeFlowNoticeAgreement[] =
    "password_manager.password_change_flow_notice_agreement";





// Deprecated 03/2025.
inline constexpr char kRecurrentSSLInterstitial[] =
    "profile.ssl_recurrent_interstitial";

// Deprecated 04/2025.
inline constexpr char kDefaultSearchProviderChoiceScreenShuffleMilestone[] =
    "default_search_provider.choice_screen_shuffle_milestone";
inline char kPerformanceInterventionNotificationAcceptHistoryDeprecated[] =
    "performance_tuning.intervention_notification.accept_history";

// Deprecated 04/2025.
inline constexpr char kAddedBookmarkSincePowerBookmarksLaunch[] =
    "bookmarks.added_since_power_bookmarks_launch";
inline constexpr char kGlicRolloutEligibility[] = "glic.rollout_eligibility";

// Deprecated 04/2025.
inline constexpr char kManagedAccessToGetAllScreensMediaAllowedForUrls[] =
    "profile.managed_access_to_get_all_screens_media_allowed_for_urls";


// Deprecated 04/2025.
inline constexpr char kSuggestionGroupVisibility[] =
    "omnibox.suggestionGroupVisibility";


// Deprecated 05/2025.
inline constexpr char kSyncCacheGuid[] = "sync.cache_guid";
inline constexpr char kSyncBirthday[] = "sync.birthday";
inline constexpr char kSyncBagOfChips[] = "sync.bag_of_chips";
inline constexpr char kSyncLastSyncedTime[] = "sync.last_synced_time";
inline constexpr char kSyncLastPollTime[] = "sync.last_poll_time";
inline constexpr char kSyncPollInterval[] = "sync.short_poll_interval";
inline constexpr char kHasSeenWelcomePage[] = "browser.has_seen_welcome_page";
inline constexpr char kSharingVapidKey[] = "sharing.vapid_key";


// Deprecated 05/2025.
inline constexpr char kPrivacySandboxFakeNoticePromptShownTimeSync[] =
    "privacy_sandbox.fake_notice.prompt_shown_time_sync";
inline constexpr char kPrivacySandboxFakeNoticePromptShownTime[] =
    "privacy_sandbox.fake_notice.prompt_shown_time";
inline constexpr char kPrivacySandboxFakeNoticeFirstSignInTime[] =
    "privacy_sandbox.fake_notice.first_sign_in_time";
inline constexpr char kPrivacySandboxFakeNoticeFirstSignOutTime[] =
    "privacy_sandbox.fake_notice.first_sign_out_time";

// Deprecated 06/2025.
inline constexpr char kStorageGarbageCollect[] =
    "extensions.storage.garbagecollect";
inline constexpr char kVariationsLimitedEntropySyntheticTrialSeed[] =
    "variations_limited_entropy_synthetic_trial_seed";
inline constexpr char kVariationsLimitedEntropySyntheticTrialSeedV2[] =
    "variations_limited_entropy_synthetic_trial_seed_v2";
inline constexpr char kGaiaCookiePeriodicReportTimeDeprecated[] =
    "gaia_cookie.periodic_report_time";


// Deprecated 06/2025.
inline constexpr char kLastUsedPairingFromSyncPublicKey[] =
    "webauthn.last_used_pairing_from_sync_public_key";
inline constexpr char kWebAuthnCablePairingsPrefName[] =
    "webauthn.cablev2_pairings";
inline constexpr char kSyncedDefaultSearchProviderGUID[] =
    "default_search_provider.synced_guid";


// Deprecated 07/2025.
inline constexpr char kFirstSyncCompletedInFullSyncMode[] =
    "sync.first_full_sync_completed";
inline constexpr char kGoogleServicesSecondLastSyncingGaiaId[] =
    "google.services.second_last_gaia_id";


// Deprecated 07/2025
constexpr char kOptGuideModelFetcherLastFetchAttempt[] =
    "optimization_guide.predictionmodelfetcher.last_fetch_attempt";
constexpr char kOptGuideModelFetcherLastFetchSuccess[] =
    "optimization_guide.predictionmodelfetcher.last_fetch_success";

// Deprecated 07/2025
inline constexpr char kSodaScheduledDeletionTime[] =
    "accessibility.captions.soda_scheduled_deletion_time";


// Deprecated 07/2025.
inline constexpr char kSyncPromoIdentityPillShownCount[] =
    "ChromeSigninSyncPromoIdentityPillShownCount";
inline constexpr char kSyncPromoIdentityPillUsedCount[] =
    "ChromeSigninSyncPromoIdentityPillUsedCount";

// Deprecated 08/2025.
inline constexpr char kInvalidationClientIDCache[] =
    "invalidation.per_sender_client_id_cache";
inline constexpr char kInvalidationTopicsToHandler[] =
    "invalidation.per_sender_topics_to_handler";


// Deprecated 08/2025.
constexpr char kObsoleteAutofillableCredentialsProfileStoreLoginDatabase[] =
    "password_manager.autofillable_credentials_profile_store_login_database";
constexpr char kObsoleteAutofillableCredentialsAccountStoreLoginDatabase[] =
    "password_manager.autofillable_credentials_account_store_login_database";



// Deprecated 09/2025.
constexpr char kGaiaCookieLastListAccountsData[] =
    "gaia_cookie.last_list_accounts_data";

// Deprecated 09/2025.
constexpr char kLensOverlayEduActionChipShownCount[] =
    "lens.edu_action_chip.shown_count";

constexpr char kRendererCodeIntegrityEnabledNeedsDeletion[] =
    "renderer_code_integrity_enabled";

// Deprecated 10/2025.
constexpr char kSessionRestoreTurnOffFromRestartInfoBarTimesShown[] =
    "browser.session_restore_turn_off_from_restart_infobar_times_shown";

constexpr char kSessionRestoreTurnOffFromSessionInfoBarTimesShown[] =
    "browser.session_restore_turn_off_from_session_infobar_times_shown";

constexpr char kSessionRestorePrefChanged[] = "session.restore_pref_changed";

constexpr char kLegacySyncSessionsGUID[] = "sync.session_sync_guid";

const char kRefreshHeuristicBreakageException[] =
    "fingerprinting_protection_filter.refresh_heuristic_breakage_exception_"
    "sites";

const char kFpfRulesetContent[] =
    "fingerprinting_protection_filter.ruleset_version.content";

const char kFpfRulesetFormat[] =
    "fingerprinting_protection_filter.ruleset_version.format";

const char kFpfRulesetChecksum[] =
    "fingerprinting_protection_filter.ruleset_version.checksum";

// Deprecated 12/2025.
const char kPrivacyBudgetGeneration[] = "privacy_budget.generation";
const char kPrivacyBudgetSeenSurfaces[] = "privacy_budget.seen";
const char kPrivacyBudgetSelectedOffsets[] = "privacy_budget.selected";
const char kPrivacyBudgetSelectedBlock[] = "privacy_budget.block_offset";
const char kPrivacyBudgetMetaExperimentActivationSalt[] =
    "privacy_budget.meta_experiment_activation_salt";

// Preference key for Enterprise policy UserAgentReduction which is distinct
// from blink::features::kReduceUserAgentMinorVersion.
constexpr char kReduceUserAgentMinorVersion[] = "user_agent_reduction";

// Deprecated 12/2025.
constexpr char kAutofillStatesDataDir[] = "autofill.states_data_dir";
constexpr char kMerchantTrustUiLastInteractionTime[] =
    "merchant_trust.ui.last_interaction_time";
constexpr char kMerchantTrustPageInfoLastOpenTime[] =
    "merchant_trust.page_info.last_open_time";

// Deprecated 12/2025.
constexpr char kCloudPrintProxyEnabled[] = "cloud_print.enabled";
constexpr char kCloudPrintEmail[] = "cloud_print.email";

// Deprecated 12/2025.
constexpr char kTrackingProtectionEligibleSince[] =
    "tracking_protection.tracking_protection_eligible_since";
constexpr char kTrackingProtectionOnboardedSince[] =
    "tracking_protection.tracking_protection_onboarded_since";
constexpr char kTrackingProtectionNoticeLastShown[] =
    "tracking_protection.tracking_protection_notice_last_shown";
constexpr char kTrackingProtectionOnboardingAckedSince[] =
    "tracking_protection.tracking_protection_onboarding_acked_since";
constexpr char kTrackingProtectionOnboardingAcked[] =
    "tracking_protection.tracking_protection_onboarding_acked";
constexpr char kTrackingProtectionOnboardingAckAction[] =
    "tracking_protection.tracking_protection_onboarding_ack_action";
constexpr char kTrackingProtectionSilentEligibleSince[] =
    "tracking_protection.tracking_protection_silent_eligible_since";
constexpr char kTrackingProtectionSilentOnboardedSince[] =
    "tracking_protection.tracking_protection_silent_onboarded_since";
constexpr char kAllowAll3pcToggleEnabled[] =
    "tracking_protection.allow_all_3pc_toggle_enabled";
constexpr char kTrackingProtectionLevel[] =
    "tracking_protection.tracking_protection_level";
constexpr char kIpProtectionEnabled[] =
    "tracking_protection.ip_protection_enabled";
constexpr char kIpProtectionInitializedByDogfood[] =
    "tracking_protection.ip_protection_initialized_by_dogfood";
constexpr char kUserBypass3pcExceptionsMigrated[] =
    "tracking_protection.user_bypass_3pc_exceptions_migrated";
constexpr char kTrackingProtectionSilentOnboardingStatus[] =
    "tracking_protection.tracking_protection_silent_onboarding_status";
constexpr char kFingerprintingProtectionEnabled[] =
    "tracking_protection.fingerprinting_protection_enabled";
constexpr char kTrackingProtectionOnboardingStatus[] =
    "tracking_protection.tracking_protection_onboarding_status";
constexpr char kTPCDExperimentClientState[] = "tpcd_experiment.client_state";
constexpr char kTPCDExperimentClientStateVersion[] =
    "tpcd_experiment.client_state_version";
constexpr char kTPCDExperimentProfileState[] = "tpcd_experiment.profile_state";



// Deprecated 01/2026.
constexpr char kCookieClearOnExitMigrationNoticeComplete[] =
    "signin.cookie_clear_on_exit_migration_notice_complete";

// Deprecated 02/2026.
// Note that these were replaced by local state prefs of the same names and
// functions: those should not be removed. Only the profile prefs registered
// here are deprecated.
constexpr char kGlicGuestUrlPresetAutopush[] = "glic.guest_url_preset_autopush";
constexpr char kGlicGuestUrlPresetPreprod[] = "glic.guest_url_preset_preprod";
constexpr char kGlicGuestUrlPresetProd[] = "glic.guest_url_preset_prod";

// Deprecated 02/2026.
constexpr char kProfilesDeletedOld[] = "profiles.profiles_deleted";

// Deprecated 02/2026.
inline constexpr char kExplicitBrowserSigninWithoutFeatureEnabled[] =
    "signin.explicit_browser_signin";

// Deprecated 02/2026.
constexpr char kDiceMigrationDialogShownCount[] =
    "signin.dice_migration.dialog_shown_count";
constexpr char kDiceMigrationDialogLastShownTime[] =
    "signin.dice_migration.dialog_last_shown_time";
constexpr char kDiceMigrationBackup[] = "signin.dice_migration.backup";
constexpr char kDiceMigrationRestoredFromBackup[] =
    "signin.dice_migration.restored_from_backup";

// Deprecated 02/2026.
inline constexpr char kTabSearchOpened[] = "tab_search.opened";

// Deprecated 02/2026.
constexpr char kTabOrganizationFeature[] = "tab_organization.feature";

// Deprecated 03/2026.
constexpr char kTabDeclutterUsageCount[] = "tab_declutter.usage_count";

// Register local state used only for migration (clearing or moving to a new
// key).
void RegisterLocalStatePrefsForMigration(PrefRegistrySimple* registry) {
  // Deprecated 02/2025.
  registry->RegisterBooleanPref(kUserAgentClientHintsGREASEUpdateEnabled, true);




  // Deprecated 04/2025.
  registry->RegisterListPref(
      kPerformanceInterventionNotificationAcceptHistoryDeprecated);


  // Deprecated 06/2025.
  registry->RegisterUint64Pref(kVariationsLimitedEntropySyntheticTrialSeed, 0);
  registry->RegisterUint64Pref(kVariationsLimitedEntropySyntheticTrialSeedV2,
                               0);


  // Deprecated 08/2025.
  registry->RegisterDictionaryPref(kInvalidationClientIDCache);
  registry->RegisterDictionaryPref(kInvalidationTopicsToHandler);


  // Deprecated 09/2025.
  registry->RegisterBooleanPref(kRendererCodeIntegrityEnabledNeedsDeletion,
                                false);

  // Deprecated 11/2025.
  registry->RegisterStringPref(kFpfRulesetContent, std::string());
  registry->RegisterIntegerPref(kFpfRulesetFormat, 0);
  registry->RegisterUint64Pref(kFpfRulesetChecksum, 0);

  // Deprecated 12/2025.
  registry->RegisterIntegerPref(kPrivacyBudgetGeneration, 0);
  registry->RegisterStringPref(kPrivacyBudgetSeenSurfaces, std::string());
  registry->RegisterStringPref(kPrivacyBudgetSelectedOffsets, std::string());
  registry->RegisterIntegerPref(kPrivacyBudgetSelectedBlock, -1);
  registry->RegisterDoublePref(kPrivacyBudgetMetaExperimentActivationSalt, 0);

  // Deprecated 12/2025.
  registry->RegisterStringPref(kAutofillStatesDataDir, std::string());

  // Deprecated 12/2025.
  registry->RegisterBooleanPref(kFingerprintingProtectionEnabled, false);
  registry->RegisterBooleanPref(kIpProtectionEnabled, false);
  registry->RegisterBooleanPref(kAllowAll3pcToggleEnabled, false);
  registry->RegisterBooleanPref(kUserBypass3pcExceptionsMigrated, false);
  registry->RegisterIntegerPref(kTrackingProtectionLevel, 0);
  registry->RegisterTimePref(kTrackingProtectionSilentOnboardedSince,
                             base::Time());
  registry->RegisterTimePref(kTrackingProtectionSilentEligibleSince,
                             base::Time());
  registry->RegisterTimePref(kTrackingProtectionEligibleSince, base::Time());
  registry->RegisterTimePref(kTrackingProtectionOnboardedSince, base::Time());
  registry->RegisterTimePref(kTrackingProtectionNoticeLastShown, base::Time());
  registry->RegisterTimePref(kTrackingProtectionOnboardingAckedSince,
                             base::Time());
  registry->RegisterBooleanPref(kTrackingProtectionOnboardingAcked, false);
  registry->RegisterIntegerPref(kTrackingProtectionOnboardingAckAction, 0);
  registry->RegisterBooleanPref(kIpProtectionInitializedByDogfood, false);
  registry->RegisterIntegerPref(kTrackingProtectionSilentOnboardingStatus, 0);
  registry->RegisterIntegerPref(kTrackingProtectionOnboardingStatus, 0);
  registry->RegisterIntegerPref(kTPCDExperimentClientState, 0);
  registry->RegisterIntegerPref(kTPCDExperimentClientStateVersion, 0);
  registry->RegisterIntegerPref(kTPCDExperimentProfileState, 0);


  // Deprecated 02/2026.
  registry->RegisterListPref(kProfilesDeletedOld);
}

// Register prefs used only for migration (clearing or moving to a new key).
void RegisterProfilePrefsForMigration(
    user_prefs::PrefRegistrySyncable* registry) {
  // Deprecated 01/2025.
  registry->RegisterBooleanPref(kCompactModeEnabled, false);

  // Deprecated 01/2025.
  registry->RegisterBooleanPref(kSafeBrowsingAutomaticDeepScanningIPHSeen,
                                false);
  registry->RegisterBooleanPref(kSafeBrowsingAutomaticDeepScanPerformed, false);


  // Deprecated 02/2025.
  registry->RegisterBooleanPref(kDefaultSearchProviderKeywordsUseExtendedList,
                                false);




  // Deprecated 03/2025.
  registry->RegisterBooleanPref(kPasswordChangeFlowNoticeAgreement, false);


  // Deprecated 03/2025
  registry->RegisterDictionaryPref(kRecurrentSSLInterstitial);

  // Deprecated 04/2025.
  registry->RegisterIntegerPref(
      kDefaultSearchProviderChoiceScreenShuffleMilestone, 0);

  // Deprecated 04/2025.
  registry->RegisterBooleanPref(kAddedBookmarkSincePowerBookmarksLaunch, false);

  // Deprecated 04/2025.
  registry->RegisterIntegerPref(kGlicRolloutEligibility, 0);

  // Deprecated 04/2025.
  registry->RegisterListPref(kManagedAccessToGetAllScreensMediaAllowedForUrls);


  // Deprecated 04/2025.
  registry->RegisterDictionaryPref(kSuggestionGroupVisibility);

  // Deprecated 05/2025.
  registry->RegisterTimePref(kPrivacySandboxFakeNoticePromptShownTimeSync,
                             base::Time());
  registry->RegisterTimePref(kPrivacySandboxFakeNoticePromptShownTime,
                             base::Time());
  registry->RegisterTimePref(kPrivacySandboxFakeNoticeFirstSignInTime,
                             base::Time());
  registry->RegisterTimePref(kPrivacySandboxFakeNoticeFirstSignOutTime,
                             base::Time());

  // Deprecated 05/2025.

  // Deprecated 05/2025.
  registry->RegisterStringPref(kSyncCacheGuid, std::string());
  registry->RegisterStringPref(kSyncBirthday, std::string());
  registry->RegisterStringPref(kSyncBagOfChips, std::string());
  registry->RegisterTimePref(kSyncLastSyncedTime, base::Time());
  registry->RegisterTimePref(kSyncLastPollTime, base::Time());
  registry->RegisterTimeDeltaPref(kSyncPollInterval, base::TimeDelta());
  registry->RegisterDictionaryPref(kSharingVapidKey);
  registry->RegisterBooleanPref(kHasSeenWelcomePage, false);

  // Deprecated 06/2025.
  registry->RegisterBooleanPref(kStorageGarbageCollect, false);
  registry->RegisterDoublePref(kGaiaCookiePeriodicReportTimeDeprecated, 0);
  registry->RegisterListPref(kWebAuthnCablePairingsPrefName);
  registry->RegisterStringPref(kLastUsedPairingFromSyncPublicKey, "");
  registry->RegisterStringPref(kSyncedDefaultSearchProviderGUID, std::string());


  // Deprecated 07/2025.
  registry->RegisterBooleanPref(kFirstSyncCompletedInFullSyncMode, false);
  registry->RegisterStringPref(kGoogleServicesSecondLastSyncingGaiaId,
                               std::string());


  // Deprecated 07/2025
  registry->RegisterInt64Pref(kOptGuideModelFetcherLastFetchAttempt, 0);
  registry->RegisterInt64Pref(kOptGuideModelFetcherLastFetchSuccess, 0);

  // Deprecated 07/2025
  registry->RegisterTimePref(kSodaScheduledDeletionTime, base::Time());


  registry->RegisterIntegerPref(kSyncPromoIdentityPillShownCount, 0);
  registry->RegisterIntegerPref(kSyncPromoIdentityPillUsedCount, 0);

  // Deprecated 08/2025.
  registry->RegisterDictionaryPref(kInvalidationClientIDCache);
  registry->RegisterDictionaryPref(kInvalidationTopicsToHandler);


  // Deprecated 08/2025.
  registry->RegisterBooleanPref(
      kObsoleteAutofillableCredentialsProfileStoreLoginDatabase, false);
  registry->RegisterBooleanPref(
      kObsoleteAutofillableCredentialsAccountStoreLoginDatabase, false);

  // Deprecated 08/2025.
  registry->RegisterBooleanPref(ntp_prefs::kNtpUseMostVisitedTiles, false);



  // Deprecated 09/2025.
  registry->RegisterStringPref(kGaiaCookieLastListAccountsData, std::string());

  // Deprecated 09/2025.
  registry->RegisterIntegerPref(kLensOverlayEduActionChipShownCount, 0);

  // Deprecated 10/2025.
  registry->RegisterIntegerPref(
      kSessionRestoreTurnOffFromRestartInfoBarTimesShown, 0);

  // Deprecated 10/2025.
  registry->RegisterIntegerPref(
      kSessionRestoreTurnOffFromSessionInfoBarTimesShown, 0);

  // Deprecated 10/2025.
  registry->RegisterBooleanPref(kSessionRestorePrefChanged, false);

  // Deprecated 10/2025.
  registry->RegisterIntegerPref(ntp_prefs::kNtpShortcutsType, 0);

  // Deprecated 10/2025.
  registry->RegisterStringPref(kLegacySyncSessionsGUID, std::string());

  // Deprecated 11/2025.
  registry->RegisterDictionaryPref(kRefreshHeuristicBreakageException);

  // Deprecated 12/2025.
  registry->RegisterBooleanPref(kReduceUserAgentMinorVersion, false);
  registry->RegisterTimePref(kMerchantTrustUiLastInteractionTime, base::Time());
  registry->RegisterTimePref(kMerchantTrustPageInfoLastOpenTime, base::Time());

  // Deprecated 12/2025.
  registry->RegisterBooleanPref(kCloudPrintProxyEnabled, true);
  registry->RegisterStringPref(kCloudPrintEmail, std::string());


  // Deprecated 01/2026.
  registry->RegisterBooleanPref(kCookieClearOnExitMigrationNoticeComplete,
                                false);

  // Deprecated 02/2026.
  registry->RegisterStringPref(kGlicGuestUrlPresetAutopush, std::string());
  registry->RegisterStringPref(kGlicGuestUrlPresetPreprod, std::string());
  registry->RegisterStringPref(kGlicGuestUrlPresetProd, std::string());

  // Deprecated 02/2026.
  registry->RegisterBooleanPref(kExplicitBrowserSigninWithoutFeatureEnabled,
                                false);

  // Deprecated 02/2026.
  registry->RegisterIntegerPref(kDiceMigrationDialogShownCount, 0);
  registry->RegisterTimePref(kDiceMigrationDialogLastShownTime, base::Time());
  registry->RegisterDictionaryPref(kDiceMigrationBackup);
  registry->RegisterBooleanPref(kDiceMigrationRestoredFromBackup, false);

  // Deprecated 02/2026.
  registry->RegisterBooleanPref(kTabSearchOpened, false);

  // Deprecated 02/2026.
  registry->RegisterIntegerPref(kTabOrganizationFeature, 0);

  // Deprecated 03/2026.
  registry->RegisterIntegerPref(kTabDeclutterUsageCount, 0);
}

}  // namespace

std::string GetCountry() {
  if (!g_browser_process || !g_browser_process->variations_service()) {
    // This should only happen in tests. Ideally this would be guarded by
    // CHECK_IS_TEST, but that is not set on Android, so no specific guard.
    return std::string();
  }
  return std::string(
      g_browser_process->variations_service()->GetStoredPermanentCountry());
}

void RegisterLocalState(PrefRegistrySimple* registry) {
  // Call outs to individual subsystems that register Local State (browser-wide)
  // prefs en masse. See RegisterProfilePrefs for per-profile prefs. Please
  // keep this list alphabetized.
  autofill::prefs::RegisterLocalStatePrefs(registry);
  breadcrumbs::RegisterPrefs(registry);
  browser_shutdown::RegisterPrefs(registry);
  BrowserProcessImpl::RegisterPrefs(registry);
  ChromeContentBrowserClient::RegisterLocalStatePrefs(registry);
  chrome_labs_prefs::RegisterLocalStatePrefs(registry);
  chrome_urls::RegisterPrefs(registry);
  ChromeMetricsServiceClient::RegisterPrefs(registry);
  ChromeSigninClient::RegisterLocalStatePrefs(registry);
  enterprise_connectors::RegisterLocalStatePrefs(registry);
  enterprise_util::RegisterLocalStatePrefs(registry);
  component_updater::RegisterPrefs(registry);
  domain_reliability::RegisterPrefs(registry);
  embedder_support::OriginTrialPrefs::RegisterPrefs(registry);
  enterprise_reporting::RegisterLocalStatePrefs(registry);
  ExternalProtocolHandler::RegisterPrefs(registry);
  flags_ui::PrefServiceFlagsStorage::RegisterPrefs(registry);
  GpuModeManager::RegisterPrefs(registry);
  signin::IdentityManager::RegisterLocalStatePrefs(registry);
  invalidation::PerUserTopicSubscriptionManager::RegisterPrefs(registry);
  language::GeoLanguageProvider::RegisterLocalStatePrefs(registry);
  language::UlpLanguageCodeLocator::RegisterLocalStatePrefs(registry);
  memory::EnterpriseMemoryLimitPrefObserver::RegisterPrefs(registry);
  metrics::RegisterDemographicsLocalStatePrefs(registry);
  metrics::TabStatsTracker::RegisterPrefs(registry);
  network_time::NetworkTimeTracker::RegisterPrefs(registry);
  omnibox::RegisterLocalStatePrefs(registry);
  optimization_guide::prefs::RegisterLocalStatePrefs(registry);
  optimization_guide::model_execution::prefs::RegisterLocalStatePrefs(registry);
  password_manager::PasswordManager::RegisterLocalPrefs(registry);
  policy::BrowserPolicyConnector::RegisterPrefs(registry);
  policy::LocalTestPolicyProvider::RegisterLocalStatePrefs(registry);
  policy::ManagementService::RegisterLocalStatePrefs(registry);
  policy::PolicyStatisticsCollector::RegisterPrefs(registry);
  PrefProxyConfigTrackerImpl::RegisterPrefs(registry);
  ProfileAttributesEntry::RegisterLocalStatePrefs(registry);
  ProfileAttributesStorage::RegisterPrefs(registry);
  ProfileNetworkContextService::RegisterLocalStatePrefs(registry);
  profiles::RegisterPrefs(registry);
  feature_engagement::RegisterLocalStatePrefs(registry);
  RegisterScreenshotPrefs(registry);
  safe_browsing::RegisterLocalStatePrefs(registry);
  search_engines::SearchEngineChoiceService::RegisterLocalStatePrefs(registry);
  secure_origin_allowlist::RegisterPrefs(registry);
  segmentation_platform::SegmentationPlatformService::RegisterLocalStatePrefs(
      registry);
  SerialPolicyAllowedPorts::RegisterPrefs(registry);
  HidPolicyAllowedDevices::RegisterLocalStatePrefs(registry);
  sessions::SessionIdGenerator::RegisterPrefs(registry);
  signin::ActivePrimaryAccountsMetricsRecorder::RegisterLocalStatePrefs(
      registry);
  SSLConfigServiceManager::RegisterPrefs(registry);
  subresource_filter::IndexedRulesetVersion::RegisterPrefs(
      registry, subresource_filter::kSafeBrowsingRulesetConfig.filter_tag);
  SystemNetworkContextManager::RegisterPrefs(registry);
  tpcd::metadata::RegisterLocalStatePrefs(registry);
  tracing::RegisterPrefs(registry);
  update_client::RegisterPrefs(registry);
  variations::VariationsService::RegisterPrefs(registry);

  // Individual preferences. If you have multiple preferences that should
  // clearly be grouped together, please group them together into a helper
  // function called above. Please keep this list alphabetized.
  registry->RegisterBooleanPref(
      policy::policy_prefs::kIntensiveWakeUpThrottlingEnabled, false);
#if BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)
  registry->RegisterBooleanPref(prefs::kFeatureNotificationsEnabled, true);
#endif  // BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)

  // Below this point is for platform-specific and compile-time conditional
  // calls. Please follow the helper-function-first-then-direct-calls pattern
  // established above, and keep things alphabetized.

#if BUILDFLAG(ENABLE_BACKGROUND_MODE)
  BackgroundModeManager::RegisterPrefs(registry);
#endif

  gcm::RegisterPrefs(registry);
  headless::RegisterPrefs(registry);
  IntranetRedirectDetector::RegisterPrefs(registry);
  media_router::RegisterLocalStatePrefs(registry);
  performance_manager::user_tuning::prefs::RegisterLocalStatePrefs(registry);
  PerformanceInterventionMetricsReporter::RegisterLocalStatePrefs(registry);
  RegisterBrowserPrefs(registry);
  speech::SodaInstaller::RegisterLocalStatePrefs(registry);
  StartupBrowserCreator::RegisterLocalStatePrefs(registry);
  task_manager::TaskManagerInterface::RegisterPrefs(registry);
  UpgradeDetector::RegisterPrefs(registry);
  registry->RegisterIntegerPref(prefs::kLastWhatsNewVersion, 0);

#if BUILDFLAG(ENABLE_EXTENSIONS)
  extensions::ExtensionPrefs::RegisterBrowserPrefs(registry);
#endif


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  WhatsNewUI::RegisterLocalStatePrefs(registry);
#endif

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  FirstRunService::RegisterLocalStatePrefs(registry);
#endif


#if BUILDFLAG(IS_MAC)
  confirm_quit::RegisterLocalState(registry);
  QuitWithAppsController::RegisterPrefs(registry);
  system_media_permissions::RegisterSystemMediaPermissionStatesPrefs(registry);
  AppShimRegistry::Get()->RegisterLocalPrefs(registry);

  // The default value is not signicant as this preference is only consulted if
  // it is explicitly set by an enterprise policy.
  registry->RegisterBooleanPref(prefs::kWebAppsUseAdHocCodeSigningForAppShims,
                                false);
#endif


#if BUILDFLAG(ENABLE_DOWNGRADE_PROCESSING)
  downgrade::RegisterPrefs(registry);
#endif

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS)
  RegisterDefaultBrowserPromptPrefs(registry);
  DeviceOAuth2TokenStoreDesktop::RegisterPrefs(registry);
#endif

  screen_ai::RegisterLocalStatePrefs(registry);

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
  PlatformAuthPolicyObserver::RegisterPrefs(registry);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

  // Platform-specific and compile-time conditional individual preferences.
  // If you have multiple preferences that should clearly be grouped together,
  // please group them together into a helper function called above. Please
  // keep this list alphabetized.

#if BUILDFLAG(ENABLE_OOP_PRINTING)
  registry->RegisterBooleanPref(prefs::kOopPrintDriversAllowedByPolicy, true);
#endif

#if BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)
  // TODO(b/328668317): Default pref should be set to true once this is
  // launched.
  registry->RegisterBooleanPref(prefs::kOsUpdateHandlerEnabled, false);
  platform_experience::prefs::RegisterPrefs(*registry);
#endif  // BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)

#if BUILDFLAG(ENABLE_PDF)
  registry->RegisterBooleanPref(prefs::kPdfViewerOutOfProcessIframeEnabled,
                                true);
#endif  // BUILDFLAG(ENABLE_PDF)

#if BUILDFLAG(ENABLE_PDF_SAVE_TO_DRIVE)
  registry->RegisterStringPref(
      prefs::kRestrictPdfSaveToGoogleDriveAccountsToPattern, "");
#endif  // BUILDFLAG(ENABLE_PDF_SAVE_TO_DRIVE)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_ANDROID)
  registry->RegisterBooleanPref(prefs::kChromeForTestingAllowed, true);
#endif

  registry->RegisterBooleanPref(prefs::kQRCodeGeneratorEnabled, true);

  registry->RegisterIntegerPref(prefs::kChromeDataRegionSetting, 0);

  glic::prefs::RegisterLocalStatePrefs(registry);

  registry->RegisterIntegerPref(prefs::kToastAlertLevel, 0);

  registry->RegisterStringPref(prefs::kNonMilestoneUpdateToastVersion, "");
  registry->RegisterBooleanPref(prefs::kSilentPrintingEnabled, false);

  registry->RegisterListPref(
      prefs::kManagedLocalNetworkAccessIpAddressSpaceOverrides);
  registry->RegisterBooleanPref(
      policy::policy_prefs::kLocalNetworkAccessPermissionsPolicyDefaultEnabled,
      false);

  // This is intentionally last.
  RegisterLocalStatePrefsForMigration(registry);

#if BUILDFLAG(ENABLE_CEF)
  // Always call this last.
  browser_prefs::RegisterLocalStatePrefs(registry);
#endif
}

// Register prefs applicable to all profiles.
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry,
                          const std::string& locale) {
  TRACE_EVENT0("browser", "chrome::RegisterProfilePrefs");
  // User prefs. Please keep this list alphabetized.
  AccessibilityLabelsService::RegisterProfilePrefs(registry);
  AccessibilityUIMessageHandler::RegisterProfilePrefs(registry);
  AimEligibilityService::RegisterProfilePrefs(registry);
  autofill::prefs::RegisterProfilePrefs(registry);
  browsing_data::prefs::RegisterBrowserUserPrefs(registry);
  capture_policy::RegisterProfilePrefs(registry);
  certificate_transparency::prefs::RegisterPrefs(registry);
  ChromeContentBrowserClient::RegisterProfilePrefs(registry);
  chrome_labs_prefs::RegisterProfilePrefs(registry);
  ChromeLocationBarModelDelegate::RegisterProfilePrefs(registry);
  content_settings::CookieSettings::RegisterProfilePrefs(registry);
  ChromeVersionService::RegisterProfilePrefs(registry);
  chrome_browser_net::NetErrorTabHelper::RegisterProfilePrefs(registry);
  chrome_prefs::RegisterProfilePrefs(registry);
  collaboration::prefs::RegisterProfilePrefs(registry);
  contextual_cueing::prefs::RegisterProfilePrefs(registry);
  commerce::RegisterProfilePrefs(registry);
  contextual_search::ContextualSearchService::RegisterProfilePrefs(registry);
  registry->RegisterIntegerPref(prefs::kContextualTasksNextPanelOpenCount, 0);
  cross_device::RegisterProfilePrefs(registry);
  enterprise::RegisterIdentifiersProfilePrefs(registry);
  enterprise_connectors::RegisterProfilePrefs(registry);
  enterprise_promotion::RegisterProfilePrefs(registry);
  enterprise_reporting::RegisterProfilePrefs(registry);
  dom_distiller::DistilledPagePrefs::RegisterProfilePrefs(registry);
  DownloadPrefs::RegisterProfilePrefs(registry);
  glic::prefs::RegisterProfilePrefs(registry);
  permissions::PermissionHatsTriggerHelper::RegisterProfilePrefs(registry);
  history_clusters::prefs::RegisterProfilePrefs(registry);
  HostContentSettingsMap::RegisterProfilePrefs(registry);
  image_fetcher::ImageCache::RegisterProfilePrefs(registry);
  site_engagement::ImportantSitesUtil::RegisterProfilePrefs(registry);
  IncognitoModePrefs::RegisterProfilePrefs(registry);
  invalidation::PerUserTopicSubscriptionManager::RegisterProfilePrefs(registry);
  language::LanguagePrefs::RegisterProfilePrefs(registry);
  login_detection::prefs::RegisterProfilePrefs(registry);
  lookalikes::RegisterProfilePrefs(registry);
  media_prefs::RegisterUserPrefs(registry);
  MediaCaptureDevicesDispatcher::RegisterProfilePrefs(registry);
  media_device_salt::MediaDeviceIDSalt::RegisterProfilePrefs(registry);
  MediaEngagementService::RegisterProfilePrefs(registry);
  MediaStorageIdSalt::RegisterProfilePrefs(registry);
  metrics::RegisterDemographicsProfilePrefs(registry);
  ntp_tiles::CustomLinksManagerImpl::RegisterProfilePrefs(registry);
  ntp_tiles::MostVisitedSites::RegisterProfilePrefs(registry);
  optimization_guide::prefs::RegisterProfilePrefs(registry);
  optimization_guide::model_execution::prefs::RegisterProfilePrefs(registry);
  PageColorsController::RegisterProfilePrefs(registry);
  password_manager::PasswordManager::RegisterProfilePrefs(registry);
  payments::RegisterProfilePrefs(registry);
  performance_manager::user_tuning::prefs::RegisterProfilePrefs(registry);
  permissions::RegisterProfilePrefs(registry);
  PermissionBubbleMediaAccessHandler::RegisterProfilePrefs(registry);
  PlatformNotificationServiceImpl::RegisterProfilePrefs(registry);
  plus_addresses::prefs::RegisterProfilePrefs(registry);
  policy::URLBlocklistManager::RegisterProfilePrefs(registry);
  PolicyUI::RegisterProfilePrefs(registry);
  PrefProxyConfigTrackerImpl::RegisterProfilePrefs(registry);
  prefetch::RegisterPredictionOptionsProfilePrefs(registry);
  PrefetchOriginDecider::RegisterPrefs(registry);
  PrefsTabHelper::RegisterProfilePrefs(registry, locale);
  privacy_sandbox::RegisterProfilePrefs(registry);
  privacy_sandbox::PrivacySandboxNoticeStorage::RegisterProfilePrefs(registry);
  Profile::RegisterProfilePrefs(registry);
  ProfileImpl::RegisterProfilePrefs(registry);
  ProfileNetworkContextService::RegisterProfilePrefs(registry);
  custom_handlers::ProtocolHandlerRegistry::RegisterProfilePrefs(registry);
  PushMessagingAppIdentifier::RegisterProfilePrefs(registry);
  PushMessagingUnsubscribedEntry::RegisterProfilePrefs(registry);
  regional_capabilities::prefs::RegisterProfilePrefs(registry);
  RegisterBrowserUserPrefs(registry);
  RegisterGeminiSettingsPrefs(registry);
  RegisterPrefersDefaultScrollbarStylesPrefs(registry);
  RegisterSafetyHubProfilePrefs(registry);
#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  safe_browsing::file_type::RegisterProfilePrefs(registry);
#endif
  safe_browsing::RegisterProfilePrefs(registry);
  safety_check::prefs::RegisterProfilePrefs(registry);
  SearchPrefetchService::RegisterProfilePrefs(registry);
  blocked_content::SafeBrowsingTriggeredPopupBlocker::RegisterProfilePrefs(
      registry);
  security_interstitials::InsecureFormBlockingPage::RegisterProfilePrefs(
      registry);
  segmentation_platform::SegmentationPlatformService::RegisterProfilePrefs(
      registry);
  segmentation_platform::DeviceSwitcherResultDispatcher::RegisterProfilePrefs(
      registry);
  SessionStartupPref::RegisterProfilePrefs(registry);
  SharingSyncPreference::RegisterProfilePrefs(registry);
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  signin::AvatarButtonPromoManager::RegisterProfilePrefs(registry);
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
  SigninPrefs::RegisterProfilePrefs(registry);
  site_engagement::SiteEngagementService::RegisterProfilePrefs(registry);
  subscription_eligibility::prefs::RegisterProfilePrefs(registry);
  supervised_user::RegisterProfilePrefs(registry);
  sync_sessions::SessionSyncPrefs::RegisterProfilePrefs(registry);
  syncer::DeviceInfoPrefs::RegisterProfilePrefs(registry);
  syncer::DeviceStatisticsScheduler::RegisterProfilePrefs(registry);
  syncer::SyncPrefs::RegisterProfilePrefs(registry);
  syncer::SyncTransportDataPrefs::RegisterProfilePrefs(registry);
  TemplateURLPrepopulateData::RegisterProfilePrefs(registry);
  tab_groups::prefs::RegisterProfilePrefs(registry);
  visited_url_ranking::GroupSuggestionsServiceImpl::RegisterProfilePrefs(
      registry);
  wallet::prefs::RegisterProfilePrefs(registry);
  omnibox::RegisterProfilePrefs(registry);
  ZeroSuggestProvider::RegisterProfilePrefs(registry);
  NtpCustomBackgroundService::RegisterProfilePrefs(registry);

#if BUILDFLAG(ENABLE_SESSION_SERVICE)
  RegisterSessionServiceLogProfilePrefs(registry);
  SessionDataService::RegisterProfilePrefs(registry);
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
  extensions::ActivityLog::RegisterProfilePrefs(registry);
  extensions::PermissionsManager::RegisterProfilePrefs(registry);
  extensions::ExtensionPrefs::RegisterProfilePrefs(registry);
  extensions::RuntimeAPI::RegisterPrefs(registry);
  extensions::CommandService::RegisterProfilePrefs(registry);
  extensions::util::RegisterProfilePrefs(registry);
  extensions_ui_prefs::RegisterProfilePrefs(registry);
  ExtensionUrlOverrides::RegisterProfilePrefs(registry);
  update_client::RegisterProfilePrefs(registry);
#endif  // BUILDFLAG(ENABLE_EXTENSIONS_CORE)

#if BUILDFLAG(ENABLE_EXTENSIONS)
  RegisterAnimationPolicyPrefs(registry);
  extensions::AudioAPI::RegisterUserPrefs(registry);
  // TODO(devlin): This would be more inline with the other calls here if it
  // were nested in either a class or separate namespace with a simple
  // Register[Profile]Prefs() name.
  extensions::RegisterSettingsOverriddenUiPrefs(registry);
  ExtensionSettingsOverriddenDialog::RegisterProfilePrefs(registry);
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

#if BUILDFLAG(ENABLE_PDF)
  registry->RegisterListPref(prefs::kPdfLocalFileAccessAllowedForDomains,
                             base::ListValue());
  registry->RegisterBooleanPref(prefs::kPdfUseSkiaRendererEnabled, true);
#endif  // BUILDFLAG(ENABLE_PDF)

#if BUILDFLAG(ENABLE_RLZ)
  ChromeRLZTrackerDelegate::RegisterProfilePrefs(registry);
#endif

  UnifiedAutoplayConfig::RegisterProfilePrefs(registry);

  bookmarks_webui::RegisterProfilePrefs(registry);
  browser_sync::ForeignSessionHandler::RegisterProfilePrefs(registry);
  BrowserUserEducationStorageService::RegisterProfilePrefs(registry);
  captions::LiveCaptionController::RegisterProfilePrefs(registry);
  ChromeAuthenticatorRequestDelegate::RegisterProfilePrefs(registry);
  commerce::CommerceUiTabHelper::RegisterProfilePrefs(registry);
  DriveService::RegisterProfilePrefs(registry);
  first_run::RegisterProfilePrefs(registry);
  gcm::RegisterProfilePrefs(registry);
  GoogleCalendarPageHandler::RegisterProfilePrefs(registry);
  HatsServiceDesktop::RegisterProfilePrefs(registry);
  lens::prefs::RegisterProfilePrefs(registry);
  ManagementUI::RegisterProfilePrefs(registry);
  media_router::RegisterAccessCodeProfilePrefs(registry);
  media_router::RegisterProfilePrefs(registry);
  MicrosoftAuthPageHandler::RegisterProfilePrefs(registry);
  MicrosoftFilesPageHandler::RegisterProfilePrefs(registry);
  NewTabFooterUI::RegisterProfilePrefs(registry);
  NewTabPageHandler::RegisterProfilePrefs(registry);
  NewTabPageUI::RegisterProfilePrefs(registry);
  ntp::SafeBrowsingHandler::RegisterProfilePrefs(registry);
  OutlookCalendarPageHandler::RegisterProfilePrefs(registry);
  PinnedTabCodec::RegisterProfilePrefs(registry);
  PromoService::RegisterProfilePrefs(registry);
  RegisterReadAnythingProfilePrefs(registry);
  settings::SettingsUI::RegisterProfilePrefs(registry);
  send_tab_to_self::RegisterProfilePrefs(registry);
  signin::RegisterProfilePrefs(registry);
  StartupBrowserCreator::RegisterProfilePrefs(registry);
  MostRelevantTabResumptionPageHandler::RegisterProfilePrefs(registry);
  TabGroupsPageHandler::RegisterProfilePrefs(registry);
  tab_groups::saved_tab_groups::prefs::RegisterProfilePrefs(registry);
  tab_organization_prefs::RegisterProfilePrefs(registry);
  tab_search_prefs::RegisterProfilePrefs(registry);
  ThemeColorPickerHandler::RegisterProfilePrefs(registry);
  ThemeService::RegisterProfilePrefs(registry);
  toolbar::RegisterProfilePrefs(registry);

#if BUILDFLAG(ENABLE_DEVTOOLS_FRONTEND)
  DevToolsWindow::RegisterProfilePrefs(registry);
#endif  // BUILDFLAG(ENABLE_DEVTOOLS_FRONTEND)



#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  device_signals::RegisterProfilePrefs(registry);
  ntp_tiles::EnterpriseShortcutsManagerImpl::RegisterProfilePrefs(registry);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  browser_switcher::BrowserSwitcherPrefs::RegisterProfilePrefs(registry);
  enterprise_signin::RegisterProfilePrefs(registry);
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS) && !BUILDFLAG(IS_CHROMEOS)
  preinstalled_apps::RegisterProfilePrefs(registry);
#endif

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS)
  sharing_hub::RegisterProfilePrefs(registry);
#endif

#if defined(TOOLKIT_VIEWS)
  accessibility_prefs::RegisterInvertBubbleUserPrefs(registry);
  side_search_prefs::RegisterProfilePrefs(registry);
  RegisterBrowserViewProfilePrefs(registry);
#endif

#if BUILDFLAG(ENABLE_LENS_DESKTOP)
  registry->RegisterBooleanPref(
      prefs::kLensRegionSearchEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(prefs::kLensDesktopNTPSearchEnabled, true);
#endif

  registry->RegisterBooleanPref(prefs::kDisableScreenshots, false);
  registry->RegisterListPref(
      webauthn::pref_names::kRemoteDesktopAllowedOrigins);

  registry->RegisterBooleanPref(
      webauthn::pref_names::kRemoteProxiedRequestsAllowed, false);

  registry->RegisterIntegerPref(
      webauthn::pref_names::kEnclaveFailedPINAttemptsCount, 0);

  side_panel_prefs::RegisterProfilePrefs(registry);

  tabs::RegisterProfilePrefs(registry);

  CertificateManagerPageHandler::RegisterProfilePrefs(registry);

  actor::ui::RegisterProfilePrefs(registry);

  projects::RegisterProfilePrefs(registry);

  registry->RegisterBooleanPref(webauthn::pref_names::kAllowWithBrokenCerts,
                                false);

  registry->RegisterBooleanPref(prefs::kPrivacyGuideViewed, false);

  RegisterProfilePrefsForMigration(registry);

  registry->RegisterIntegerPref(prefs::kMemorySaverChipExpandedCount, 0);
  registry->RegisterTimePref(prefs::kLastMemorySaverChipExpandedTimestamp,
                             base::Time());
  registry->RegisterBooleanPref(
      prefs::kAccessibilityMainNodeAnnotationsEnabled, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);

  registry->RegisterBooleanPref(
      prefs::kManagedLocalNetworkAccessRestrictionsTemporaryOptOut, false);


#if BUILDFLAG(ENTERPRISE_DATA_CONTROLS)
  data_controls::RegisterProfilePrefs(registry);
#endif  // BUILDFLAG(ENTERPRISE_DATA_CONTROLS)


#if BUILDFLAG(ENABLE_COMPOSE)
  registry->RegisterBooleanPref(prefs::kPrefHasCompletedComposeFRE, false);
  registry->RegisterBooleanPref(prefs::kEnableProactiveNudge, true);
  registry->RegisterDictionaryPref(prefs::kProactiveNudgeDisabledSitesWithTime);
#endif

  registry->RegisterIntegerPref(prefs::kChromeDataRegionSetting, 0);

  registry->RegisterIntegerPref(prefs::kLensOverlayStartCount, 0);

  registry->RegisterBooleanPref(prefs::kViewSourceLineWrappingEnabled, false);

  // TODO(crbug.com/442891187): Move these to appropriate manager files when
  // the policies logic is implemented.
  registry->RegisterListPref(policy::policy_prefs::kIncognitoModeUrlBlocklist);
  registry->RegisterListPref(policy::policy_prefs::kIncognitoModeUrlAllowlist);

  registry->RegisterBooleanPref(
      ntp_tiles::prefs::kTabResumptionHomeModuleEnabled, true);

  registry->RegisterBooleanPref(ntp_tiles::prefs::kMagicStackHomeModuleEnabled,
                                true);

  registry->RegisterBooleanPref(ntp_tiles::prefs::kTipsHomeModuleEnabled, true);


  registry->RegisterBooleanPref(prefs::kStaticStorageQuotaEnabled, false);
}

void RegisterUserProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  RegisterUserProfilePrefs(registry, g_browser_process->GetApplicationLocale());
}

void RegisterUserProfilePrefs(user_prefs::PrefRegistrySyncable* registry,
                              const std::string& locale) {
  RegisterProfilePrefs(registry, locale);

#if BUILDFLAG(ENABLE_CEF)
  browser_prefs::RegisterProfilePrefs(registry);
#endif

}

void RegisterScreenshotPrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kDisableScreenshots, false);
}

void RegisterGeminiSettingsPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(prefs::kGeminiSettings, 0);
}


// This method should be periodically pruned of year+ old migrations.
// See chrome/browser/prefs/README.md for details.
void MigrateObsoleteLocalStatePrefs(PrefService* local_state) {
  // IMPORTANT NOTE: This code is *not* run on iOS Chrome. If a pref is migrated
  // or cleared here, and that pref is also used in iOS Chrome, it may also need
  // to be migrated or cleared specifically for iOS as well. This could be by
  // doing the migration in feature code that's called by all platforms instead
  // of here, or by calling migration code in the appropriate place for iOS
  // specifically, e.g. ios/chrome/browser/shared/model/prefs/browser_prefs.mm.

  // BEGIN_MIGRATE_OBSOLETE_LOCAL_STATE_PREFS
  // Please don't delete the preceding line. It is used by PRESUBMIT.py.

  // Added 02/2025.
  local_state->ClearPref(kUserAgentClientHintsGREASEUpdateEnabled);

  // Added 02/2025
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS)
  local_state->ClearPref(prefs::kDefaultBrowserPromptRefreshStudyGroup);
#endif

  // Added 02/2025

  // Added 03/2025.

  // Added 03/2025.

  local_state->ClearPref(
      kPerformanceInterventionNotificationAcceptHistoryDeprecated);


  // Added 06/2025.
  local_state->ClearPref(kVariationsLimitedEntropySyntheticTrialSeed);
  local_state->ClearPref(kVariationsLimitedEntropySyntheticTrialSeedV2);


  // Added 08/2025.
  local_state->ClearPref(kInvalidationClientIDCache);
  local_state->ClearPref(kInvalidationTopicsToHandler);


  // Added 09/2025
  local_state->ClearPref(kRendererCodeIntegrityEnabledNeedsDeletion);

  // Added 10/2025
  local_state->ClearPref(prefs::kDefaultBrowserFirstShownTime);

  // Added 11/2025
  local_state->ClearPref(kFpfRulesetContent);
  local_state->ClearPref(kFpfRulesetFormat);
  local_state->ClearPref(kFpfRulesetChecksum);

  // Added 12/2025
  local_state->ClearPref(kPrivacyBudgetGeneration);
  local_state->ClearPref(kPrivacyBudgetSeenSurfaces);
  local_state->ClearPref(kPrivacyBudgetSelectedOffsets);
  local_state->ClearPref(kPrivacyBudgetSelectedBlock);
  local_state->ClearPref(kPrivacyBudgetMetaExperimentActivationSalt);

  // Added 12/2025
  local_state->ClearPref(kAutofillStatesDataDir);

  // Added 12/2025
  local_state->ClearPref(kFingerprintingProtectionEnabled);
  local_state->ClearPref(kIpProtectionEnabled);
  local_state->ClearPref(kAllowAll3pcToggleEnabled);
  local_state->ClearPref(kUserBypass3pcExceptionsMigrated);
  local_state->ClearPref(kTrackingProtectionLevel);
  local_state->ClearPref(kTrackingProtectionSilentOnboardedSince);
  local_state->ClearPref(kTrackingProtectionSilentEligibleSince);
  local_state->ClearPref(kTrackingProtectionEligibleSince);
  local_state->ClearPref(kTrackingProtectionOnboardedSince);
  local_state->ClearPref(kTrackingProtectionNoticeLastShown);
  local_state->ClearPref(kTrackingProtectionOnboardingAckedSince);
  local_state->ClearPref(kTrackingProtectionOnboardingAcked);
  local_state->ClearPref(kTrackingProtectionOnboardingAckAction);
  local_state->ClearPref(kIpProtectionInitializedByDogfood);
  local_state->ClearPref(kTrackingProtectionSilentOnboardingStatus);
  local_state->ClearPref(kTrackingProtectionOnboardingStatus);
  local_state->ClearPref(kTPCDExperimentClientState);
  local_state->ClearPref(kTPCDExperimentClientStateVersion);
  local_state->ClearPref(kTPCDExperimentProfileState);


  // Added 02/2026.
  if (local_state->HasPrefPath(kProfilesDeletedOld)) {
    const base::ListValue& old_list = local_state->GetList(kProfilesDeletedOld);
    if (!old_list.empty()) {
      ScopedListPrefUpdate update(local_state, prefs::kProfilesDeleted);
      for (const auto& value : old_list) {
        std::optional<base::FilePath> path = base::ValueToFilePath(value);
        if (path) {
          base::FilePath basename = path->BaseName();
          // Avoid the edge case where the base name is the root, e.g `/` on
          // linux.
          if (!basename.IsAbsolute()) {
            update->Append(base::FilePathToValue(basename));
          }
        }
      }
    }
    local_state->ClearPref(kProfilesDeletedOld);
  }

  // Please don't delete the following line. It is used by PRESUBMIT.py.
  // END_MIGRATE_OBSOLETE_LOCAL_STATE_PREFS

  // IMPORTANT NOTE: This code is *not* run on iOS Chrome. If a pref is migrated
  // or cleared here, and that pref is also used in iOS Chrome, it may also need
  // to be migrated or cleared specifically for iOS as well. This could be by
  // doing the migration in feature code that's called by all platforms instead
  // of here, or by calling migration code in the appropriate place for iOS
  // specifically, e.g. ios/chrome/browser/shared/model/prefs/browser_prefs.mm.
}

// This method should be periodically pruned of year+ old migrations.
// See chrome/browser/prefs/README.md for details.
void MigrateObsoleteProfilePrefs(PrefService* profile_prefs,
                                 const base::FilePath& profile_path) {
  // IMPORTANT NOTE: This code is *not* run on iOS Chrome. If a pref is migrated
  // or cleared here, and that pref is also used in iOS Chrome, it may also need
  // to be migrated or cleared specifically for iOS as well. This could be by
  // doing the migration in feature code that's called by all platforms instead
  // of here, or by calling migration code in the appropriate place for iOS
  // specifically, e.g. ios/chrome/browser/shared/model/prefs/browser_prefs.mm.

  // BEGIN_MIGRATE_OBSOLETE_PROFILE_PREFS
  // Please don't delete the preceding line. It is used by PRESUBMIT.py.

  // Added 08/2024, but DO NOT REMOVE after the usual year.
  // TODO(crbug.com/356148174): Remove once kMoveThemePrefsToSpecifics has been
  // enabled for an year.
  MigrateSyncingThemePrefsToNonSyncingIfNeeded(profile_prefs);

  // Added 10/2025.
  profile_prefs->ClearPref(kSessionRestoreTurnOffFromRestartInfoBarTimesShown);
  profile_prefs->ClearPref(kSessionRestoreTurnOffFromSessionInfoBarTimesShown);
  profile_prefs->ClearPref(kSessionRestorePrefChanged);

  // Added 05/2025.
  profile_prefs->ClearPref(kPrivacySandboxFakeNoticePromptShownTimeSync);
  profile_prefs->ClearPref(kPrivacySandboxFakeNoticePromptShownTime);
  profile_prefs->ClearPref(kPrivacySandboxFakeNoticeFirstSignInTime);
  profile_prefs->ClearPref(kPrivacySandboxFakeNoticeFirstSignOutTime);

  privacy_sandbox::PrivacySandboxNoticeStorage::UpdateNoticeSchemaV2(
      profile_prefs);

  // Check MigrateDeprecatedAutofillPrefs() to see if this is safe to remove.
  autofill::prefs::MigrateDeprecatedAutofillPrefs(profile_prefs);

  // TODO(326079444): After experiment is over, update the deprecated date and
  // allow this to be cleaned up.
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS)
  MigrateDefaultBrowserLastDeclinedPref(profile_prefs);
#endif

  // Added 01/2025.
  profile_prefs->ClearPref(kCompactModeEnabled);

  // Added 01/2025.
  profile_prefs->ClearPref(kSafeBrowsingAutomaticDeepScanPerformed);
  profile_prefs->ClearPref(kSafeBrowsingAutomaticDeepScanningIPHSeen);


  // Added 02/2025.
  profile_prefs->ClearPref(kDefaultSearchProviderKeywordsUseExtendedList);




  // Added 03/2025.
  profile_prefs->ClearPref(kPasswordChangeFlowNoticeAgreement);

  // Added 03/2025.
  profile_prefs->ClearPref(prefs::kChildAccountStatusKnown);


  // Added 03/2025.
  profile_prefs->ClearPref(kRecurrentSSLInterstitial);

  // Added 04/2025.
  profile_prefs->ClearPref(kDefaultSearchProviderChoiceScreenShuffleMilestone);

  // Added 04/2025.
  profile_prefs->ClearPref(kAddedBookmarkSincePowerBookmarksLaunch);

  // Added 04/2025.
  profile_prefs->ClearPref(kGlicRolloutEligibility);

  // Added 04/2025
  profile_prefs->ClearPref(kManagedAccessToGetAllScreensMediaAllowedForUrls);


  // Added 04/2025.
  profile_prefs->ClearPref(kSuggestionGroupVisibility);


  // Added 05/2025.
  profile_prefs->ClearPref(kSyncCacheGuid);
  profile_prefs->ClearPref(kSyncBirthday);
  profile_prefs->ClearPref(kSyncBagOfChips);
  profile_prefs->ClearPref(kSyncLastSyncedTime);
  profile_prefs->ClearPref(kSyncLastPollTime);
  profile_prefs->ClearPref(kSyncPollInterval);
  profile_prefs->ClearPref(kSharingVapidKey);
  profile_prefs->ClearPref(kHasSeenWelcomePage);

  // Added 06/2025.
  profile_prefs->ClearPref(kStorageGarbageCollect);
  profile_prefs->ClearPref(kGaiaCookiePeriodicReportTimeDeprecated);
  profile_prefs->ClearPref(kWebAuthnCablePairingsPrefName);
  profile_prefs->ClearPref(kLastUsedPairingFromSyncPublicKey);
  profile_prefs->ClearPref(kSyncedDefaultSearchProviderGUID);


  // Added 07/2025.
  profile_prefs->ClearPref(kFirstSyncCompletedInFullSyncMode);
  profile_prefs->ClearPref(kGoogleServicesSecondLastSyncingGaiaId);


  // Added 07/2025
  profile_prefs->ClearPref(kOptGuideModelFetcherLastFetchAttempt);
  profile_prefs->ClearPref(kOptGuideModelFetcherLastFetchSuccess);


  profile_prefs->ClearPref(kSyncPromoIdentityPillShownCount);
  profile_prefs->ClearPref(kSyncPromoIdentityPillUsedCount);

  // Added 08/2025.
  profile_prefs->ClearPref(kInvalidationClientIDCache);
  profile_prefs->ClearPref(kInvalidationTopicsToHandler);


  // Deprecated 08/2025.
  profile_prefs->ClearPref(
      kObsoleteAutofillableCredentialsProfileStoreLoginDatabase);
  profile_prefs->ClearPref(
      kObsoleteAutofillableCredentialsAccountStoreLoginDatabase);

  // Added 08/2025.
  NewTabPageUI::MigrateDeprecatedUseMostVisitedTilesPref(profile_prefs);



  // Added 09/2025.
  PageColorsController::MigrateObsoleteProfilePrefs(profile_prefs);
  profile_prefs->ClearPref(kGaiaCookieLastListAccountsData);

  // Added 09/2025.
  profile_prefs->ClearPref(kLensOverlayEduActionChipShownCount);

  SigninPrefs(*profile_prefs).MigrateObsoleteSigninPrefs();

  // Added 10/2025
  NewTabPageUI::MigrateDeprecatedShortcutsTypePref(profile_prefs);

  // Added 10/2025.
  profile_prefs->ClearPref(kLegacySyncSessionsGUID);

  // Added 11/2025.
  profile_prefs->ClearPref(kRefreshHeuristicBreakageException);

  // Added 12/2025.
  profile_prefs->ClearPref(kReduceUserAgentMinorVersion);
  profile_prefs->ClearPref(kMerchantTrustUiLastInteractionTime);
  profile_prefs->ClearPref(kMerchantTrustPageInfoLastOpenTime);

  // Added 12/2025.
  profile_prefs->ClearPref(kCloudPrintProxyEnabled);
  profile_prefs->ClearPref(kCloudPrintEmail);


  // Added 01/2026.
  profile_prefs->ClearPref(kCookieClearOnExitMigrationNoticeComplete);

  // Added 02/2026.
  profile_prefs->ClearPref(kGlicGuestUrlPresetAutopush);
  profile_prefs->ClearPref(kGlicGuestUrlPresetPreprod);
  profile_prefs->ClearPref(kGlicGuestUrlPresetProd);

  // Added 02/2026.
  profile_prefs->ClearPref(kExplicitBrowserSigninWithoutFeatureEnabled);

  // Added 02/2026.
  profile_prefs->ClearPref(kTabSearchOpened);

  // Added 03/2026.
  profile_prefs->ClearPref(kTabDeclutterUsageCount);

  // Added 02/2026.
  tabs::MigrateTabSearchPref(profile_prefs);

  // Please don't delete the following line. It is used by PRESUBMIT.py.
  // END_MIGRATE_OBSOLETE_PROFILE_PREFS

  // IMPORTANT NOTE: This code is *not* run on iOS Chrome. If a pref is migrated
  // or cleared here, and that pref is also used in iOS Chrome, it may also need
  // to be migrated or cleared specifically for iOS as well. This could be by
  // doing the migration in feature code that's called by all platforms instead
  // of here, or by calling migration code in the appropriate place for iOS
  // specifically, e.g. ios/chrome/browser/shared/model/prefs/browser_prefs.mm.
}
