// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.h"

#include <memory>
#include <utility>

#include "base/feature_list.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/accessibility/accessibility_labels_service_factory.h"
#include "chrome/browser/accessibility/page_colors_controller_factory.h"
#include "chrome/browser/accessibility_annotator/accessibility_annotation_service_factory.h"
#include "chrome/browser/accessibility_annotator/accessibility_annotator_backend_factory.h"
#include "chrome/browser/accessibility_annotator/accessibility_query_service_factory.h"
#include "chrome/browser/actor/actor_keyed_service_factory.h"
#include "chrome/browser/affiliations/affiliation_service_factory.h"
#include "chrome/browser/ai/ai_data_keyed_service_factory.h"
#include "chrome/browser/autocomplete/aim_eligibility_service_factory.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/autocomplete/document_suggestions_service_factory.h"
#include "chrome/browser/autocomplete/in_memory_url_index_factory.h"
#include "chrome/browser/autocomplete/provider_state_service_factory.h"
#include "chrome/browser/autocomplete/shortcuts_backend_factory.h"
#include "chrome/browser/autofill/account_setting_service_factory.h"
#include "chrome/browser/autofill/autocomplete_history_manager_factory.h"
#include "chrome/browser/autofill/autofill_accessibility_annotator_data_adapter_factory.h"
#include "chrome/browser/autofill/autofill_ai_model_cache_factory.h"
#include "chrome/browser/autofill/autofill_ai_model_executor_factory.h"
#include "chrome/browser/autofill/autofill_entity_data_manager_factory.h"
#include "chrome/browser/autofill/autofill_image_fetcher_factory.h"
#include "chrome/browser/autofill/autofill_offer_manager_factory.h"
#include "chrome/browser/autofill/autofill_optimization_guide_decider_factory.h"
#include "chrome/browser/autofill/iban_manager_factory.h"
#include "chrome/browser/autofill/merchant_promo_code_manager_factory.h"
#include "chrome/browser/autofill/ml_log_router_factory.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/autofill/valuables_data_manager_factory.h"
#include "chrome/browser/autofill/wallet_pass_access_manager_factory.h"
#include "chrome/browser/background/background_contents_service_factory.h"
#include "chrome/browser/background_fetch/background_fetch_delegate_factory.h"
#include "chrome/browser/background_sync/background_sync_controller_factory.h"
#include "chrome/browser/bitmap_fetcher/bitmap_fetcher_service_factory.h"
#include "chrome/browser/bluetooth/bluetooth_chooser_context_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/breadcrumbs/breadcrumb_manager_keyed_service_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browsing_data/browsing_data_history_observer_service.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_lifetime_manager_factory.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_delegate_factory.h"
#include "chrome/browser/browsing_topics/browsing_topics_service_factory.h"
#include "chrome/browser/btm/btm_browser_signin_detector_factory.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/client_hints/client_hints_factory.h"
#include "chrome/browser/collaboration/collaboration_service_factory.h"
#include "chrome/browser/collaboration/comments/comments_service_factory.h"
#include "chrome/browser/collaboration/messaging/messaging_backend_service_factory.h"
#include "chrome/browser/commerce/shopping_service_factory.h"
#include "chrome/browser/consent_auditor/consent_auditor_factory.h"
#include "chrome/browser/content_index/content_index_provider_factory.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/contextual_cueing/contextual_cueing_service_factory.h"
#include "chrome/browser/contextual_search/contextual_search_service_factory.h"
#include "chrome/browser/contextual_tasks/contextual_tasks_service_factory.h"
#include "chrome/browser/contextual_tasks/contextual_tasks_ui_service_factory.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry_factory.h"
#include "chrome/browser/data_sharing/data_sharing_service_factory.h"
#include "chrome/browser/data_sharing/personal_collaboration_data/personal_collaboration_data_service_factory.h"
#include "chrome/browser/device_reauth/chrome_device_authenticator_factory.h"
#include "chrome/browser/digital_credentials/digital_credentials_keyed_service.h"
#include "chrome/browser/dom_distiller/dom_distiller_service_factory.h"
#include "chrome/browser/domain_reliability/service_factory.h"
#include "chrome/browser/download/background_download_service_factory.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/engagement/site_engagement_service_factory.h"
#include "chrome/browser/enterprise/browser_management/management_service_factory.h"
#include "chrome/browser/enterprise/connectors/connectors_service.h"
#include "chrome/browser/enterprise/connectors/reporting/browser_crash_event_router.h"
#include "chrome/browser/enterprise/connectors/reporting/reporting_event_router_factory.h"
#include "chrome/browser/enterprise/data_protection/data_protection_url_lookup_service_factory.h"
#include "chrome/browser/enterprise/identifiers/profile_id_service_factory.h"
#include "chrome/browser/enterprise/remote_commands/user_remote_commands_service_factory.h"
#include "chrome/browser/enterprise/reporting/cloud_profile_reporting_service_factory.h"
#include "chrome/browser/enterprise/reporting/legacy_tech/legacy_tech_service.h"
#include "chrome/browser/enterprise/signals/user_permission_service_factory.h"
#include "chrome/browser/enterprise/signin/profile_management_disclaimer_service_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/favicon/history_ui_favicon_request_handler_factory.h"
#include "chrome/browser/feature_engagement/tracker_factory.h"
#include "chrome/browser/file_system_access/file_system_access_permission_context_factory.h"
#include "chrome/browser/finds/finds_service_factory.h"
#include "chrome/browser/first_party_sets/first_party_sets_policy_service_factory.h"
#include "chrome/browser/font_pref_change_notifier_factory.h"
#include "chrome/browser/glic/public/glic_enabling.h"
#include "chrome/browser/glic/public/glic_keyed_service_factory.h"
#include "chrome/browser/heavy_ad_intervention/heavy_ad_service_factory.h"
#include "chrome/browser/hid/hid_policy_allowed_devices_factory.h"
#include "chrome/browser/history/domain_diversity_reporter_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/top_sites_factory.h"
#include "chrome/browser/history_clusters/history_clusters_service_factory.h"
#include "chrome/browser/history_embeddings/history_embeddings_service_factory.h"
#include "chrome/browser/k_anonymity_service/k_anonymity_service_factory.h"
#include "chrome/browser/language/accept_languages_service_factory.h"
#include "chrome/browser/language/language_model_manager_factory.h"
#include "chrome/browser/language/url_language_histogram_factory.h"
#include "chrome/browser/loader/from_gws_navigation_and_keep_alive_request_tracker_factory.h"
#include "chrome/browser/login_detection/login_detection_keyed_service_factory.h"
#include "chrome/browser/lookalikes/lookalike_url_service_factory.h"
#include "chrome/browser/manta/manta_service_factory.h"
#include "chrome/browser/media/media_engagement_service.h"
#include "chrome/browser/media/media_engagement_service_factory.h"
#include "chrome/browser/media/router/chrome_media_router_factory.h"
#include "chrome/browser/media/router/presentation/chrome_local_presentation_manager_factory.h"
#include "chrome/browser/media/webrtc/media_device_salt_service_factory.h"
#include "chrome/browser/media/webrtc/webrtc_event_log_manager_keyed_service_factory.h"
#include "chrome/browser/metrics/profile_metrics_service_factory.h"
#include "chrome/browser/metrics/variations/google_groups_manager_factory.h"
#include "chrome/browser/multistep_filter/core/multistep_filter_service_factory.h"
#include "chrome/browser/navigation_predictor/navigation_predictor_keyed_service_factory.h"
#include "chrome/browser/navigation_predictor/preloading_model_keyed_service_factory.h"
#include "chrome/browser/navigation_predictor/search_engine_preconnector.h"
#include "chrome/browser/navigation_predictor/search_engine_preconnector_keyed_service_factory.h"
#include "chrome/browser/net/dns_probe_service_factory.h"
#include "chrome/browser/net/profile_network_context_service_factory.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/platform_notification_service_factory.h"
#include "chrome/browser/omnibox/autocomplete_controller_emitter_factory.h"
#include "chrome/browser/optimization_guide/model_validator_keyed_service_factory.h"
#include "chrome/browser/optimization_guide/optimization_guide_keyed_service_factory.h"
#include "chrome/browser/origin_trials/origin_trials_factory.h"
#include "chrome/browser/page_content_annotations/page_content_annotations_service_factory.h"
#include "chrome/browser/page_content_annotations/page_content_extraction_service_factory.h"
#include "chrome/browser/page_content_annotations/page_content_screenshot_service_factory.h"
#include "chrome/browser/page_image_service/image_service_factory.h"
#include "chrome/browser/page_info/about_this_site_service_factory.h"
#include "chrome/browser/page_info/merchant_trust_service_factory.h"
#include "chrome/browser/page_load_metrics/observers/https_engagement_metrics/https_engagement_service_factory.h"
#include "chrome/browser/passage_embeddings/passage_embedder_model_observer_factory.h"
#include "chrome/browser/password_manager/account_password_store_factory.h"
#include "chrome/browser/password_manager/factories/field_info_manager_factory.h"
#include "chrome/browser/password_manager/factories/password_reuse_manager_factory.h"
#include "chrome/browser/password_manager/password_change_service_factory.h"
#include "chrome/browser/password_manager/password_manager_settings_service_factory.h"
#include "chrome/browser/password_manager/profile_password_store_factory.h"
#include "chrome/browser/payments/browser_binding/browser_bound_key_deleter_service_factory.h"
#include "chrome/browser/performance_manager/persistence/site_data/site_data_cache_facade_factory.h"
#include "chrome/browser/permissions/one_time_permissions_tracker_factory.h"
#include "chrome/browser/permissions/origin_keyed_permission_action_service_factory.h"
#include "chrome/browser/permissions/permission_actions_history_factory.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker_factory.h"
#include "chrome/browser/permissions/prediction_service/prediction_model_handler_provider_factory.h"
#include "chrome/browser/permissions/prediction_service/prediction_service_factory.h"
#include "chrome/browser/persisted_state_db/session_proto_db_factory.h"
#include "chrome/browser/plugins/plugin_prefs_factory.h"
#include "chrome/browser/plus_addresses/plus_address_service_factory.h"
#include "chrome/browser/plus_addresses/plus_address_setting_service_factory.h"
#include "chrome/browser/policy/chrome_policy_blocklist_service_factory.h"
#include "chrome/browser/policy/cloud/user_cloud_policy_invalidator_factory.h"
#include "chrome/browser/policy/cloud/user_fm_registration_token_uploader_factory.h"
#include "chrome/browser/policy/developer_tools_policy_checker_factory.h"
#include "chrome/browser/predictors/autocomplete_action_predictor_factory.h"
#include "chrome/browser/predictors/loading_predictor_factory.h"
#include "chrome/browser/predictors/predictor_database_factory.h"
#include "chrome/browser/prefs/pref_metrics_service.h"
#include "chrome/browser/preloading/autocomplete_dictionary_preload_service_factory.h"
#include "chrome/browser/preloading/prefetch/no_state_prefetch/no_state_prefetch_link_manager_factory.h"
#include "chrome/browser/preloading/prefetch/no_state_prefetch/no_state_prefetch_manager_factory.h"
#include "chrome/browser/preloading/prefetch/search_prefetch/search_prefetch_service_factory.h"
#include "chrome/browser/preloading/prerender/search_prewarm_progress_service_factory.h"
#include "chrome/browser/preloading/search_preload/search_preload_service_factory.h"
#include "chrome/browser/privacy/privacy_metrics_service_factory.h"
#include "chrome/browser/privacy_sandbox/notice/notice_service_factory.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_service_factory.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_settings_factory.h"
#include "chrome/browser/profile_resetter/triggered_profile_resetter_factory.h"
#include "chrome/browser/profiles/batch_upload/batch_upload_service_factory.h"
#include "chrome/browser/profiles/renderer_updater_factory.h"
#include "chrome/browser/push_messaging/push_messaging_service_factory.h"
#include "chrome/browser/reading_list/reading_list_model_factory.h"
#include "chrome/browser/reduce_accept_language/reduce_accept_language_factory.h"
#include "chrome/browser/regional_capabilities/regional_capabilities_service_factory.h"
#include "chrome/browser/safe_browsing/verdict_cache_manager_factory.h"
#include "chrome/browser/search/background/ntp_background_service_factory.h"
#include "chrome/browser/search/background/ntp_custom_background_service_factory.h"
#include "chrome/browser/search_engine_choice/search_engine_choice_service_factory.h"
#include "chrome/browser/search_engines/template_url_fetcher_factory.h"
#include "chrome/browser/search_engines/template_url_prepopulate_data_resolver_factory.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/search_provider_logos/logo_service_factory.h"
#include "chrome/browser/segmentation_platform/segmentation_platform_service_factory.h"
#include "chrome/browser/send_tab_to_self/send_tab_to_self_client_service_factory.h"
#include "chrome/browser/serial/serial_chooser_context_factory.h"
#include "chrome/browser/sessions/session_data_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/sharing/sharing_service_factory.h"
#include "chrome/browser/sharing_hub/sharing_hub_service_factory.h"
#include "chrome/browser/signin/about_signin_internals_factory.h"
#include "chrome/browser/signin/account_consistency_mode_manager_factory.h"
#include "chrome/browser/signin/account_investigator_factory.h"
#include "chrome/browser/signin/account_reconcilor_factory.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_metrics_service_factory.h"
#include "chrome/browser/signin/signin_policy_service_factory.h"
#include "chrome/browser/signin/signin_profile_attributes_updater_factory.h"
#include "chrome/browser/ssl/https_first_mode_settings_tracker.h"
#include "chrome/browser/ssl/sct_reporting_service_factory.h"
#include "chrome/browser/ssl/stateful_ssl_host_state_delegate_factory.h"
#include "chrome/browser/storage_access_api/storage_access_api_service_factory.h"
#include "chrome/browser/subresource_filter/subresource_filter_profile_context_factory.h"
#include "chrome/browser/subscription_eligibility/subscription_eligibility_service_factory.h"
#include "chrome/browser/supervised_user/child_accounts/child_account_service_factory.h"
#include "chrome/browser/supervised_user/supervised_user_metrics_service_factory.h"
#include "chrome/browser/supervised_user/supervised_user_service_factory.h"
#include "chrome/browser/supervised_user/supervised_user_url_filtering_service_factory.h"
#include "chrome/browser/sync/account_bookmark_sync_service_factory.h"
#include "chrome/browser/sync/data_type_store_service_factory.h"
#include "chrome/browser/sync/local_or_syncable_bookmark_sync_service_factory.h"
#include "chrome/browser/sync/prefs/cross_device_pref_tracker/cross_device_pref_tracker_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/sync/user_event_service_factory.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/autofill/autofill_client_provider_factory.h"
#include "chrome/browser/ui/find_bar/find_bar_state_factory.h"
#include "chrome/browser/ui/hats/hats_service_factory.h"
#include "chrome/browser/ui/prefs/prefs_tab_helper.h"
#include "chrome/browser/ui/safety_hub/menu_notification_service_factory.h"
#include "chrome/browser/ui/safety_hub/revoked_permissions_os_notification_display_manager_factory.h"
#include "chrome/browser/ui/safety_hub/revoked_permissions_service_factory.h"
#include "chrome/browser/ui/signin/dice_migration_service_factory.h"
#include "chrome/browser/ui/tabs/pinned_tab_service_factory.h"
#include "chrome/browser/ui/toolbar/pinned_toolbar/pinned_toolbar_actions_model_factory.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model_factory.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/webui/chrome_finds_internals/chrome_finds_agent_factory.h"
#include "chrome/browser/ui/webui/ntp/ntp_resource_cache_factory.h"
#include "chrome/browser/ui/webui/signin/history_sync_optin_service_factory.h"
#include "chrome/browser/ui/webui/signin/login_ui_service_factory.h"
#include "chrome/browser/undo/bookmark_undo_service_factory.h"
#include "chrome/browser/unified_consent/unified_consent_service_factory.h"
#include "chrome/browser/usb/usb_chooser_context_factory.h"
#include "chrome/browser/visited_url_ranking/group_suggestions_service_factory.h"
#include "chrome/browser/visited_url_ranking/visited_url_ranking_service_factory.h"
#include "chrome/browser/web_applications/isolated_web_apps/iwa_permissions_policy_cache.h"
#include "chrome/browser/webauthn/enclave_manager_factory.h"
#include "chrome/browser/webauthn/immediate_request_rate_limiter_factory.h"
#include "chrome/browser/webdata_services/web_data_service_factory.h"
#include "chrome/browser/webid/federated_identity_api_permission_context_factory.h"
#include "chrome/browser/webid/federated_identity_auto_reauthn_permission_context_factory.h"
#include "chrome/browser/webid/federated_identity_permission_context_factory.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/chrome_features.h"
#include "components/autofill/content/browser/autofill_log_router_factory.h"
#include "components/breadcrumbs/core/breadcrumbs_status.h"
#include "components/captive_portal/core/buildflags.h"
#include "components/commerce/core/proto/commerce_subscription_db_content.pb.h"
#include "components/commerce/core/proto/persisted_state_db_content.pb.h"
#include "components/enterprise/buildflags/buildflags.h"
#include "components/enterprise/content/clipboard_restriction_service.h"
#include "components/offline_pages/buildflags/buildflags.h"
#include "components/optimization_guide/core/optimization_guide_util.h"
#include "components/optimization_guide/machine_learning_tflite_buildflags.h"
#include "components/password_manager/content/browser/password_manager_log_router_factory.h"
#include "components/password_manager/content/browser/password_requirements_service_factory.h"
#include "components/password_manager/core/browser/password_manager_blocklist_policy.h"
#include "components/payments/content/has_enrolled_instrument_query_factory.h"
#include "components/permissions/features.h"
#include "components/policy/content/safe_search_service.h"
#include "components/policy/core/browser/url_list/policy_blocklist_service.h"
#include "components/safe_browsing/buildflags.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/site_engagement/content/site_engagement_service.h"
#include "components/spellcheck/spellcheck_buildflags.h"
#include "components/sync/base/features.h"
#include "content/public/common/buildflags.h"
#include "crypto/crypto_buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "media/base/media_switches.h"
#include "pdf/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "services/network/public/cpp/features.h"
#include "third_party/blink/public/common/features_generated.h"
#include "ui/base/device_form_factor.h"

// Per-platform #include blocks, in alphabetical order.

#include "chrome/browser/accessibility/ax_main_node_annotator_controller_factory.h"
#include "chrome/browser/accessibility/live_caption/live_caption_controller_factory.h"
#include "chrome/browser/accessibility/phrase_segmentation/dependency_parser_model_loader_factory.h"
#include "chrome/browser/accessibility/tree_fixing/ax_tree_fixing_services_router_factory.h"
#include "chrome/browser/accessibility_annotator/content_annotator/content_annotator_service_factory.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/badging/badge_manager_factory.h"
#include "chrome/browser/contextual_tasks/contextual_tasks_context_service_factory.h"
#include "chrome/browser/device_api/managed_configuration_api_factory.h"
#include "chrome/browser/download/offline_item_model_manager_factory.h"
#include "chrome/browser/feedback/feedback_uploader_factory_chrome.h"
#include "chrome/browser/hid/hid_chooser_context_factory.h"
#include "chrome/browser/hid/hid_connection_tracker_factory.h"
#include "chrome/browser/media/router/discovery/access_code/access_code_cast_sink_service_factory.h"
#include "chrome/browser/metrics/desktop_session_duration/desktop_profile_session_durations_service_factory.h"
#include "chrome/browser/new_tab_page/chrome_colors/chrome_colors_factory.h"
#include "chrome/browser/new_tab_page/microsoft_auth/microsoft_auth_service_factory.h"
#include "chrome/browser/new_tab_page/modules/file_suggestion/drive_service_factory.h"
#include "chrome/browser/new_tab_page/one_google_bar/one_google_bar_service_factory.h"
#include "chrome/browser/new_tab_page/promos/promo_service_factory.h"
#include "chrome/browser/password_manager/factories/bulk_leak_check_service_factory.h"
#include "chrome/browser/password_manager/factories/password_counter_factory.h"
#include "chrome/browser/payments/payment_request_display_manager_factory.h"
#include "chrome/browser/picture_in_picture/hats/auto_picture_in_picture_hats_service_factory.h"
#include "chrome/browser/prefs/persistent_renderer_prefs_manager_factory.h"
#include "chrome/browser/private_ai/private_ai_service_factory.h"
#include "chrome/browser/profile_resetter/reset_report_uploader_factory.h"
#include "chrome/browser/record_replay/recording_data_manager_factory.h"
#include "chrome/browser/screen_ai/screen_ai_service_router_factory.h"
#include "chrome/browser/search/instant_service_factory.h"
#include "chrome/browser/search_engine_choice/search_engine_choice_dialog_service_factory.h"
#include "chrome/browser/speech/speech_recognition_client_browser_interface_factory.h"
#include "chrome/browser/speech/speech_recognition_service_factory.h"
#include "chrome/browser/storage/storage_notification_service_factory.h"
#include "chrome/browser/ui/browser_manager_service_factory.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/global_media_controls/media_notification_service_factory.h"
#include "chrome/browser/ui/lens/lens_keyed_service_factory.h"
#include "chrome/browser/ui/media_router/media_router_ui_service_factory.h"
#include "chrome/browser/ui/performance_controls/performance_controls_hats_service_factory.h"
#include "chrome/browser/ui/read_anything/read_anything_service_factory.h"
#include "chrome/browser/ui/safety_hub/password_status_check_service_factory.h"
#include "chrome/browser/ui/safety_hub/safety_hub_hats_service_factory.h"
#include "chrome/browser/ui/tabs/organization/tab_organization_service_factory.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button_state_manager.h"
#include "chrome/browser/ui/waap/waap_ui_metrics_service_factory.h"
#include "chrome/browser/ui/webui/theme_colors_source_manager_factory.h"
#include "chrome/browser/usb/usb_connection_tracker_factory.h"
#include "chrome/browser/user_education/user_education_service_factory.h"
#include "chrome/browser/web_applications/isolated_web_apps/window_management/isolated_web_apps_window_open_permission_service_factory.h"
#include "components/commerce/core/proto/cart_db_content.pb.h"
#include "components/commerce/core/proto/coupon_db_content.pb.h"
#include "components/commerce/core/proto/discounts_db_content.pb.h"  // nogncheck
#include "components/optimization_guide/core/model_execution/model_execution_features.h"
#include "ui/accessibility/accessibility_features.h"

#include "chrome/browser/download/bubble/download_bubble_update_service_factory.h"
#include "chrome/browser/profiles/profile_statistics_factory.h"
#include "chrome/browser/ui/startup/first_run_service.h"
#include "chrome/browser/ui/webui/signin/turn_sync_on_helper.h"


#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
#include "chrome/browser/enterprise/client_certificates/certificate_provisioning_service_factory.h"
#include "chrome/browser/enterprise/client_certificates/certificate_store_factory.h"
#include "chrome/browser/enterprise/idle/idle_service_factory.h"
#include "chrome/browser/enterprise/signals/signals_aggregator_factory.h"
#endif

#include "chrome/browser/policy/cloud/user_policy_signin_service_factory.h"
#include "chrome/browser/profiles/gaia_info_update_service_factory.h"

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
#include "chrome/browser/enterprise/connectors/device_trust/device_trust_connector_service_factory.h"
#include "chrome/browser/enterprise/connectors/device_trust/device_trust_service_factory.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/collaboration_messaging_observer_factory.h"
#endif

#include "chrome/browser/password_manager/startup_passwords_import_service_factory.h"  // nogncheck (Desktop only)
#include "chrome/browser/webauthn/passkey_unlock_manager_factory.h"
#include "device/fido/public/features.h"
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
#include "chrome/browser/policy/messaging_layer/util/manual_test_heartbeat_event_factory.h"
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "chrome/browser/browser_switcher/browser_switcher_service_factory.h"
#include "chrome/browser/enterprise/reporting/saas_usage/saas_usage_reporting_controller_factory.h"
#include "chrome/browser/enterprise/signin/enterprise_signin_service_factory.h"
#include "chrome/browser/enterprise/signin/oidc_authentication_signin_interceptor_factory.h"
#include "chrome/browser/enterprise/signin/profile_token_web_signin_interceptor_factory.h"
#include "chrome/browser/enterprise/signin/user_policy_oidc_signin_service_factory.h"
#include "chrome/browser/policy/cloud/profile_token_policy_web_signin_service_factory.h"
#include "chrome/browser/search_integrity/search_integrity_factory.h"  // nogncheck
#include "chrome/browser/signin/accounts_policy_manager_factory.h"

#if BUILDFLAG(ENTERPRISE_LOCAL_CONTENT_ANALYSIS)
#include "chrome/browser/enterprise/connectors/analysis/local_binary_upload_service_factory.h"
#endif
#endif


// Feature-specific #includes, in alphabetical order.

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
#include "chrome/browser/autocomplete/autocomplete_scoring_model_service_factory.h"
#include "chrome/browser/autocomplete/on_device_tail_model_service_factory.h"
#include "chrome/browser/autofill/autofill_field_classification_model_service_factory.h"
#include "chrome/browser/password_manager/password_field_classification_model_handler_factory.h"
#endif

#if BUILDFLAG(CHROME_ROOT_STORE_CERT_MANAGEMENT_UI)
#include "chrome/browser/net/server_certificate_database_service_factory.h"
#endif

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
#include "chrome/browser/signin/bound_session_credentials/bound_session_cookie_refresh_service_factory.h"
#include "chrome/browser/signin/bound_session_credentials/unexportable_key_profile_garbage_collection_service_factory.h"

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
#include "chrome/browser/signin/bound_session_credentials/dice_bound_session_cookie_service_factory.h"
#endif
#endif

#if BUILDFLAG(ENABLE_CAPTIVE_PORTAL_DETECTION)
#include "chrome/browser/captive_portal/captive_portal_service_factory.h"
#endif

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
#include "chrome/browser/signin/dice_response_handler_factory.h"
#include "chrome/browser/signin/dice_web_signin_interceptor_factory.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "apps/browser_context_keyed_service_factories.h"
#include "chrome/browser/apps/platform_apps/browser_context_keyed_service_factories.h"
#include "chrome/browser/sync_file_system/sync_file_system_service_factory.h"
#include "chrome/browser/ui/web_applications/web_app_metrics_factory.h"
#include "chrome/browser/web_applications/web_app_provider_factory.h"
#include "components/webapps/isolated_web_apps/reading/response_reader_registry_factory.h"  // nogncheck
#include "components/webapps/isolated_web_apps/url_loading/url_loader_factory.h"  // nogncheck

#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
#include "chrome/browser/extensions/keyed_services/browser_context_keyed_service_factories.h"
#include "chrome/browser/omnibox/omnibox_input_watcher_factory.h"
#include "chrome/browser/omnibox/omnibox_suggestions_watcher_factory.h"
#include "chrome/browser/policy/cloud/extension_install_policy_service_factory.h"
#include "chrome/browser/speech/extension_api/tts_extension_api.h"
#include "extensions/browser/browser_context_keyed_service_factories.h"
#include "extensions/browser/extensions_browser_client.h"
#endif

#include "chrome/browser/ui/tabs/glic_actor_task_icon_manager_factory.h"


#if BUILDFLAG(ENABLE_PLUGINS)
#include "chrome/browser/plugins/plugin_info_host_impl.h"
#endif

#if BUILDFLAG(ENABLE_SESSION_SERVICE)
#include "chrome/browser/sessions/app_session_service_factory.h"
#include "chrome/browser/sessions/exit_type_service_factory.h"
#include "chrome/browser/sessions/session_service_factory.h"
#endif


#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
#include "chrome/browser/enterprise/connectors/reporting/extension_install_event_router.h"
#include "chrome/browser/enterprise/connectors/reporting/extension_telemetry_event_router_factory.h"
#endif

#if BUILDFLAG(ENTERPRISE_TELOMERE_REPORTING)
#include "chrome/browser/enterprise/connectors/reporting/telomere_event_router.h"
#endif

#if BUILDFLAG(ENTERPRISE_DATA_CONTROLS)
#include "chrome/browser/enterprise/data_controls/chrome_rules_service.h"
#endif

#if BUILDFLAG(FULL_SAFE_BROWSING)
#include "chrome/browser/safe_browsing/advanced_protection_status_manager_factory.h"
#include "chrome/browser/safe_browsing/cloud_content_scanning/cloud_binary_upload_service_factory.h"
#include "chrome/browser/safe_browsing/extension_telemetry/extension_telemetry_service_factory.h"
#include "chrome/browser/safe_browsing/hash_realtime_service_factory.h"
#endif

#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
#include "chrome/browser/safe_browsing/chrome_enterprise_url_lookup_service_factory.h"
#include "chrome/browser/safe_browsing/chrome_password_protection_service_factory.h"
#include "chrome/browser/safe_browsing/chrome_ping_manager_factory.h"
#include "chrome/browser/safe_browsing/client_side_detection_intelligent_scan_delegate_factory.h"
#include "chrome/browser/safe_browsing/client_side_detection_service_factory.h"
#include "chrome/browser/safe_browsing/gemini_antiscam_protection/gemini_antiscam_protection_service_factory.h"
#include "chrome/browser/safe_browsing/safe_browsing_metrics_collector_factory.h"
#include "chrome/browser/safe_browsing/safe_browsing_navigation_observer_manager_factory.h"
#include "chrome/browser/safe_browsing/tailored_security/tailored_security_service_factory.h"
#include "chrome/browser/safe_browsing/url_lookup_service_factory.h"
#endif

#if defined(TOOLKIT_VIEWS)
#include "chrome/browser/bookmarks/bookmark_expanded_state_tracker_factory.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_factory.h"
#endif

#if BUILDFLAG(USE_NSS_CERTS)
#include "chrome/browser/net/nss_service_factory.h"
#endif

void AddProfilesExtraParts(ChromeBrowserMainParts* main_parts) {
  main_parts->AddParts(std::make_unique<ChromeBrowserMainExtraPartsProfiles>());
}

ChromeBrowserMainExtraPartsProfiles::ChromeBrowserMainExtraPartsProfiles() =
    default;

ChromeBrowserMainExtraPartsProfiles::~ChromeBrowserMainExtraPartsProfiles() =
    default;

// This method gets the instance of each ServiceFactory. We do this so that
// each ServiceFactory initializes itself and registers its dependencies with
// the global PreferenceDependencyManager. We need to have a complete
// dependency graph when we create a profile so we can dispatch the profile
// creation message to the services that want to create their services at
// profile creation time.
//
// TODO(erg): This needs to be something else. I don't think putting every
// FooServiceFactory here will scale or is desirable long term.
//
// TODO(crbug.com/40256109): Check how to simplify the approach of registering
// every factory in this function.
//
// static
void ChromeBrowserMainExtraPartsProfiles::
    EnsureBrowserContextKeyedServiceFactoriesBuilt() {
  // ---------------------------------------------------------------------------
  // Redirect to those lists for factories that are part of those modules.
  // Module specific registration functions:

#if BUILDFLAG(ENABLE_EXTENSIONS)
  apps::EnsureBrowserContextKeyedServiceFactoriesBuilt();
  chrome_apps::EnsureBrowserContextKeyedServiceFactoriesBuilt();
  chrome_extensions::EnsureBrowserContextKeyedServiceFactoriesBuilt();
  extensions::EnsureBrowserContextKeyedServiceFactoriesBuilt();
#endif

  // ---------------------------------------------------------------------------
  // Common factory registrations (Please keep this list ordered without taking
  // into consideration buildflags, repeating buildflags is ok):
  AboutSigninInternalsFactory::GetInstance();
  AboutThisSiteServiceFactory::GetInstance();
  AcceptLanguagesServiceFactory::GetInstance();
  AccessibilityAnnotatorBackendFactory::GetInstance();
  AccessibilityAnnotationServiceFactory::GetInstance();
  AccessibilityLabelsServiceFactory::GetInstance();
  accessibility_annotator::AccessibilityQueryServiceFactory::GetInstance();
  accessibility_annotator::ContentAnnotatorServiceFactory::GetInstance();
  AccountBookmarkSyncServiceFactory::GetInstance();
  AccountConsistencyModeManagerFactory::GetInstance();
  AccountInvestigatorFactory::GetInstance();
  AccountPasswordStoreFactory::GetInstance();
  AccountReconcilorFactory::GetInstance();
  autofill::AccountSettingServiceFactory::GetInstance();
  autofill::AutofillAccessibilityAnnotatorDataAdapterFactory::GetInstance();
  AutoPictureInPictureHatsServiceFactory::GetInstance();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  AccountsPolicyManagerFactory::GetInstance();
  search_integrity::SearchIntegrityFactory::GetInstance();
#endif
  actor::ActorKeyedServiceFactory::GetInstance();
  AffiliationServiceFactory::GetInstance();
  chrome_finds_internals::ChromeFindsAgentFactory::GetInstance();
  AiDataKeyedServiceFactory::GetInstance();
  AimEligibilityServiceFactory::GetInstance();
  apps::AppServiceProxyFactory::GetInstance();
#if BUILDFLAG(ENABLE_SESSION_SERVICE)
  AppSessionServiceFactory::GetInstance();
#endif
  AutocompleteClassifierFactory::GetInstance();
  AutocompleteControllerEmitterFactory::GetInstance();
  AutocompleteDictionaryPreloadServiceFactory::GetInstance();
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  AutocompleteScoringModelServiceFactory::GetInstance();
#endif
  autofill::AutocompleteHistoryManagerFactory::GetInstance();
  autofill::AutofillAiModelCacheFactory::GetInstance();
  autofill::AutofillAiModelExecutorFactory::GetInstance();
  autofill::AutofillClientProviderFactory::GetInstance();
  autofill::AutofillEntityDataManagerFactory::GetInstance();
  autofill::AutofillImageFetcherFactory::GetInstance();
  autofill::AutofillLogRouterFactory::GetInstance();
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  autofill::AutofillFieldClassificationModelServiceFactory::GetInstance();
#endif
  autofill::AutofillOfferManagerFactory::GetInstance();
  autofill::AutofillOptimizationGuideDeciderFactory::GetInstance();
  autofill::IbanManagerFactory::GetInstance();
  autofill::MerchantPromoCodeManagerFactory::GetInstance();
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  autofill::MlLogRouterFactory::GetInstance();
#endif
  autofill::PersonalDataManagerFactory::GetInstance();
  autofill::ValuablesDataManagerFactory::GetInstance();
  autofill::WalletPassAccessManagerFactory::GetInstance();
#if BUILDFLAG(ENABLE_BACKGROUND_CONTENTS)
  BackgroundContentsServiceFactory::GetInstance();
#endif
  BackgroundDownloadServiceFactory::GetInstance();
  BackgroundFetchDelegateFactory::GetInstance();
  BackgroundSyncControllerFactory::GetInstance();
  badging::BadgeManagerFactory::GetInstance();
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  BatchUploadServiceFactory::GetInstance();
#endif
  BitmapFetcherServiceFactory::GetInstance();
  BluetoothChooserContextFactory::GetInstance();
#if defined(TOOLKIT_VIEWS)
  BookmarkExpandedStateTrackerFactory::GetInstance();
  BookmarkMergedSurfaceServiceFactory::GetInstance();
#endif
  BookmarkModelFactory::GetInstance();
  BookmarkUndoServiceFactory::GetInstance();
  if (breadcrumbs::IsEnabled(
          g_browser_process ? g_browser_process->local_state() : nullptr)) {
    BreadcrumbManagerKeyedServiceFactory::GetInstance();
  }
#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
  BoundSessionCookieRefreshServiceFactory::GetInstance();
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  DiceBoundSessionCookieServiceFactory::GetInstance();
#endif
#endif
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  browser_switcher::BrowserSwitcherServiceFactory::GetInstance();
#endif
  browser_sync::UserEventServiceFactory::GetInstance();
  BrowserManagerServiceFactory::GetInstance();
  browsing_topics::BrowsingTopicsServiceFactory::GetInstance();
  BrowsingDataHistoryObserverService::Factory::GetInstance();
  BulkLeakCheckServiceFactory::GetInstance();
  captions::LiveCaptionControllerFactory::GetInstance();
#if BUILDFLAG(ENABLE_CAPTIVE_PORTAL_DETECTION)
  CaptivePortalServiceFactory::GetInstance();
#endif
  ChildAccountServiceFactory::GetInstance();
  chrome_browser_net::DnsProbeServiceFactory::GetInstance();
  chrome_colors::ChromeColorsFactory::GetInstance();
  ChromeBrowsingDataLifetimeManagerFactory::GetInstance();
  ChromeBrowsingDataRemoverDelegateFactory::GetInstance();
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
  ChromeDeviceAuthenticatorFactory::GetInstance();
#endif
  ChromePolicyBlocklistServiceFactory::GetInstance();
  ChromeSigninClientFactory::GetInstance();
#if BUILDFLAG(ENTERPRISE_CLIENT_CERTIFICATES)
  client_certificates::CertificateProvisioningServiceFactory::GetInstance();
  client_certificates::CertificateStoreFactory::GetInstance();
#endif
  ClientHintsFactory::GetInstance();
  ClipboardRestrictionServiceFactory::GetInstance();
  collaboration::CollaborationServiceFactory::GetInstance();
  collaboration::comments::CommentsServiceFactory::GetInstance();
  collaboration::messaging::MessagingBackendServiceFactory::GetInstance();
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
  tab_groups::CollaborationMessagingObserverFactory::GetInstance();
#endif
  commerce::ShoppingServiceFactory::GetInstance();
  ConsentAuditorFactory::GetInstance();
  contextual_tasks::ContextualTasksContextServiceFactory::GetInstance();
  contextual_tasks::ContextualTasksServiceFactory::GetInstance();
  contextual_tasks::ContextualTasksUiServiceFactory::GetInstance();
  ContentIndexProviderFactory::GetInstance();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  contextual_cueing::ContextualCueingServiceFactory::GetInstance();
#endif
  ContextualSearchServiceFactory::GetInstance();
  CookieSettingsFactory::GetInstance();
  CrossDevicePrefTrackerFactory::GetInstance();
  DataTypeStoreServiceFactory::GetInstance();
#if BUILDFLAG(ENTERPRISE_DATA_CONTROLS)
  data_controls::ChromeRulesServiceFactory::GetInstance();
#endif
  data_sharing::DataSharingServiceFactory::GetInstance();
  data_sharing::personal_collaboration_data::
      PersonalCollaborationDataServiceFactory::GetInstance();
  DependencyParserModelLoaderFactory::GetInstance();
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  DiceMigrationServiceFactory::GetInstance();
  DiceResponseHandlerFactory::GetInstance();
  DiceWebSigninInterceptorFactory::GetInstance();
#endif
  BtmBrowserSigninDetectorFactory::GetInstance();
  policy::DeveloperToolsPolicyCheckerFactory::GetInstance();
  digital_credentials::DigitalCredentialsKeyedServiceFactory::GetInstance();
  DocumentSuggestionsServiceFactory::GetInstance();
  dom_distiller::DomDistillerServiceFactory::GetInstance();
  DomainDiversityReporterFactory::GetInstance();
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS)
  DownloadBubbleUpdateServiceFactory::GetInstance();
#endif
  DownloadCoreServiceFactory::GetInstance();
  DriveServiceFactory::GetInstance();
  EnclaveManagerFactory::GetInstance();
  enterprise::ProfileIdServiceFactory::GetInstance();
  enterprise_commands::UserRemoteCommandsServiceFactory::GetInstance();
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
  enterprise_connectors::ExtensionInstallEventRouterFactory::GetInstance();
  enterprise_connectors::ExtensionTelemetryEventRouterFactory::GetInstance();
#endif
#if BUILDFLAG(ENTERPRISE_TELOMERE_REPORTING)
  if (base::FeatureList::IsEnabled(enterprise_connectors::kTelomereReporting)) {
    enterprise_connectors::TelomereEventRouterFactory::GetInstance();
  }
#endif
  enterprise_connectors::BrowserCrashEventRouterFactory::GetInstance();
  enterprise_connectors::ConnectorsServiceFactory::GetInstance();
  enterprise_connectors::ReportingEventRouterFactory::GetInstance();
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
  enterprise_connectors::DeviceTrustConnectorServiceFactory::GetInstance();
  enterprise_connectors::DeviceTrustServiceFactory::GetInstance();
#endif
#if (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)) && \
    BUILDFLAG(ENTERPRISE_LOCAL_CONTENT_ANALYSIS) &&                    \
    BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  enterprise_connectors::LocalBinaryUploadServiceFactory::GetInstance();
#endif
#if BUILDFLAG(ENTERPRISE_WATERMARK)
  enterprise_data_protection::DataProtectionUrlLookupServiceFactory::
      GetInstance();
#endif
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
  enterprise_idle::IdleServiceFactory::GetInstance();
  enterprise_signals::SignalsAggregatorFactory::GetInstance();
#endif
  enterprise_reporting::CloudProfileReportingServiceFactory::GetInstance();
  enterprise_reporting::LegacyTechServiceFactory::GetInstance();
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  enterprise_reporting::SaasUsageReportingControllerFactory::GetInstance();
#endif
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN) || BUILDFLAG(IS_ANDROID)
  enterprise_signals::UserPermissionServiceFactory::GetInstance();
#endif
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  enterprise_signin::EnterpriseSigninServiceFactory::GetInstance();
#endif
#if BUILDFLAG(ENABLE_SESSION_SERVICE)
  ExitTypeServiceFactory::GetInstance();
#endif
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
  // The TTS API is an outlier. It lives in chrome/browser/speech and is built
  // into //chrome/browser. It's better for Extensions dependencies if its
  // factory is built here, rather than with other Extension APIs.
  extensions::TtsAPI::GetFactoryInstance();
#endif
#if BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_CHROMEOS_DEVICE)
  // A ChromeOS build for a dev linux machine.
  // Makes manual testing possible.
  FakeSmartCardDeviceServiceFactory::GetInstance();
#endif
  FaviconServiceFactory::GetInstance();
  feature_engagement::TrackerFactory::GetInstance();
  FederatedIdentityApiPermissionContextFactory::GetInstance();
  FederatedIdentityAutoReauthnPermissionContextFactory::GetInstance();
  FederatedIdentityPermissionContextFactory::GetInstance();
  feedback::FeedbackUploaderFactoryChrome::GetInstance();
  FieldInfoManagerFactory::GetInstance();
  FileSystemAccessPermissionContextFactory::GetInstance();
  FindBarStateFactory::GetInstance();
  finds::FindsServiceFactory::GetInstance();
  first_party_sets::FirstPartySetsPolicyServiceFactory::GetInstance();
#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
  FirstRunServiceFactory::GetInstance();
#endif
  FontPrefChangeNotifierFactory::GetInstance();
  FromGWSNavigationAndKeepAliveRequestTrackerFactory::GetInstance();
  GAIAInfoUpdateServiceFactory::GetInstance();
  glic::GlicKeyedServiceFactory::GetInstance();
  tabs::GlicActorTaskIconManagerFactory::GetInstance();
  GlobalErrorServiceFactory::GetInstance();
  GoogleGroupsManagerFactory::GetInstance();
  HatsServiceFactory::GetInstance();
  HeavyAdServiceFactory::GetInstance();
  HidChooserContextFactory::GetInstance();
  HidConnectionTrackerFactory::GetInstance();
  HidPolicyAllowedDevicesFactory::GetInstance();
  HistoryClustersServiceFactory::EnsureFactoryBuilt();
  HistoryEmbeddingsServiceFactory::GetInstance();
  HistoryServiceFactory::GetInstance();
  HistoryUiFaviconRequestHandlerFactory::GetInstance();
  HostContentSettingsMapFactory::GetInstance();
  HttpsEngagementServiceFactory::GetInstance();
  HttpsFirstModeServiceFactory::GetInstance();
  IdentityManagerFactory::EnsureFactoryAndDependeeFactoriesBuilt();
  ImmediateRequestRateLimiterFactory::GetInstance();
  InMemoryURLIndexFactory::GetInstance();
  visited_url_ranking::VisitedURLRankingServiceFactory::GetInstance();
  InstantServiceFactory::GetInstance();
  KAnonymityServiceFactory::GetInstance();
  LanguageModelManagerFactory::GetInstance();
  private_ai::PrivateAiServiceFactory::GetInstance();
  LensKeyedServiceFactory::GetInstance();
  LocalOrSyncableBookmarkSyncServiceFactory::GetInstance();
  login_detection::LoginDetectionKeyedServiceFactory::GetInstance();
  LoginUIServiceFactory::GetInstance();
  LogoServiceFactory::GetInstance();
  LookalikeUrlServiceFactory::GetInstance();
  ManagedConfigurationAPIFactory::GetInstance();
  manta::MantaServiceFactory::GetInstance();
  MediaDeviceSaltServiceFactory::GetInstance();
  media_router::AccessCodeCastSinkServiceFactory::GetInstance();
  media_router::ChromeLocalPresentationManagerFactory::GetInstance();
  media_router::ChromeMediaRouterFactory::GetInstance();
  media_router::MediaRouterUIServiceFactory::GetInstance();
  if (MediaEngagementService::IsEnabled()) {
    MediaEngagementServiceFactory::GetInstance();
  }
  MediaNotificationServiceFactory::GetInstance();
  MerchantTrustServiceFactory::GetInstance();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  metrics::DesktopProfileSessionDurationsServiceFactory::GetInstance();
#endif
  ProfileMetricsServiceFactory::GetInstance();
  MicrosoftAuthServiceFactory::GetInstance();
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  multistep_filter::MultistepFilterServiceFactory::GetInstance();
#endif
  web_app::IsolatedWebAppsWindowOpenPermissionServiceFactory::GetInstance();
  web_app::IwaPermissionsPolicyCacheFactory::GetInstance();
  NavigationPredictorKeyedServiceFactory::GetInstance();
#if BUILDFLAG(CHROME_ROOT_STORE_CERT_MANAGEMENT_UI)
  net::ServerCertificateDatabaseServiceFactory::GetInstance();
#endif
  PreloadingModelKeyedServiceFactory::GetInstance();
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  HistorySyncOptinServiceFactory::GetInstance();
  ProfileManagementDisclaimerServiceFactory::GetInstance();
#endif
  NotificationDisplayServiceFactory::GetInstance();
#if BUILDFLAG(USE_NSS_CERTS)
  NssServiceFactory::GetInstance();
#endif
  NtpBackgroundServiceFactory::GetInstance();
  NtpCustomBackgroundServiceFactory::GetInstance();
  NTPResourceCacheFactory::GetInstance();
#if BUILDFLAG(IS_ANDROID) && BUILDFLAG(ENABLE_OFFLINE_PAGES)
  offline_pages::OfflinePageAutoFetcherServiceFactory::GetInstance();
  offline_pages::RequestCoordinatorFactory::GetInstance();
#endif
  OfflineItemModelManagerFactory::GetInstance();
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
  OmniboxInputWatcherFactory::GetInstance();
  OmniboxSuggestionsWatcherFactory::GetInstance();
#endif
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  OnDeviceTailModelServiceFactory::GetInstance();
#endif
  OneGoogleBarServiceFactory::GetInstance();
  OneTimePermissionsTrackerFactory::GetInstance();
  if (optimization_guide::ShouldStartModelValidator()) {
    optimization_guide::ModelValidatorKeyedServiceFactory::GetInstance();
  }
  OptimizationGuideKeyedServiceFactory::GetInstance();
  OriginKeyedPermissionActionServiceFactory::GetInstance();
  OriginTrialsFactory::GetInstance();
  PageContentAnnotationsServiceFactory::GetInstance();
  page_content_annotations::PageContentExtractionServiceFactory::GetInstance();
  page_content_annotations::PageContentScreenshotServiceFactory::GetInstance();
  page_image_service::ImageServiceFactory::EnsureFactoryBuilt();
  PageColorsControllerFactory::GetInstance();
  passage_embeddings::PassageEmbedderModelObserverFactory::GetInstance();
  if (base::FeatureList::IsEnabled(device::kPasskeyUnlockManager)) {
    webauthn::PasskeyUnlockManagerFactory::GetInstance();
  }
  password_manager::PasswordManagerLogRouterFactory::GetInstance();
  password_manager::PasswordRequirementsServiceFactory::GetInstance();
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  PasswordFieldClassificationModelHandlerFactory::GetInstance();
#endif
  PasswordChangeServiceFactory::GetInstance();
  PasswordCounterFactory::GetInstance();
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  PasswordManagerBlocklistPolicyFactory::GetInstance();
#endif
  PasswordManagerSettingsServiceFactory::GetInstance();
  PasswordReuseManagerFactory::GetInstance();
  PasswordStatusCheckServiceFactory::GetInstance();
  payments::BrowserBoundKeyDeleterServiceFactory::GetInstance();
  payments::HasEnrolledInstrumentQueryFactory::GetInstance();
  payments::PaymentRequestDisplayManagerFactory::GetInstance();
  if (ui::GetDeviceFormFactor() == ui::DEVICE_FORM_FACTOR_DESKTOP) {
    performance_manager::SiteDataCacheFacadeFactory::GetInstance();
  }
  PerformanceControlsHatsServiceFactory::GetInstance();
  PermissionActionsHistoryFactory::GetInstance();
  PermissionDecisionAutoBlockerFactory::GetInstance();
  PersistentRendererPrefsManagerFactory::GetInstance();
  PinnedTabServiceFactory::GetInstance();
  PinnedToolbarActionsModelFactory::GetInstance();
  PlatformNotificationServiceFactory::GetInstance();
#if BUILDFLAG(ENABLE_PLUGINS)
  PluginInfoHostImpl::EnsureFactoryBuilt();
  PluginPrefsFactory::GetInstance();
#endif
  PlusAddressServiceFactory::GetInstance();
  PlusAddressSettingServiceFactory::GetInstance();
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
  policy::ExtensionInstallPolicyServiceFactory::GetInstance();
#endif  // BUILDFLAG(ENABLE_EXTENSIONS_CORE)
  policy::ManagementServiceFactory::GetInstance();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  policy::ProfileTokenPolicyWebSigninServiceFactory::GetInstance();
#endif
  policy::UserCloudPolicyInvalidatorFactory::GetInstance();
  policy::UserFmRegistrationTokenUploaderFactory::GetInstance();
  policy::UserPolicySigninServiceFactory::GetInstance();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  policy::UserPolicyOidcSigninServiceFactory::GetInstance();
#endif
  PredictionModelHandlerProviderFactory::GetInstance();
  PredictionServiceFactory::GetInstance();
  predictors::AutocompleteActionPredictorFactory::GetInstance();
  predictors::LoadingPredictorFactory::GetInstance();
  predictors::PredictorDatabaseFactory::GetInstance();
  PrefMetricsService::Factory::GetInstance();
  PrefsTabHelper::GetServiceInstance();
  prerender::NoStatePrefetchLinkManagerFactory::GetInstance();
  prerender::NoStatePrefetchManagerFactory::GetInstance();
  PrivacyMetricsServiceFactory::GetInstance();
  PrivacySandboxNoticeServiceFactory::GetInstance();
  PrivacySandboxServiceFactory::GetInstance();
  PrivacySandboxSettingsFactory::GetInstance();
  ProfileNetworkContextServiceFactory::GetInstance();
  ProfilePasswordStoreFactory::GetInstance();
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS)
  ProfileStatisticsFactory::GetInstance();
#endif
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  ProfileTokenWebSigninInterceptorFactory::GetInstance();
  OidcAuthenticationSigninInterceptorFactory::GetInstance();
#endif
  PromoServiceFactory::GetInstance();
  ProtocolHandlerRegistryFactory::GetInstance();
  ProviderStateServiceFactory::GetInstance();
  PushMessagingServiceFactory::GetInstance();
  ReadAnythingServiceFactory::GetInstance();
  ReadingListModelFactory::GetInstance();
  record_replay::RecordingDataManagerFactory::GetInstance();
  ReduceAcceptLanguageFactory::GetInstance();
  RendererUpdaterFactory::GetInstance();
  regional_capabilities::RegionalCapabilitiesServiceFactory::GetInstance();
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
  reporting::ManualTestHeartbeatEventFactory::GetInstance();
#endif
  RevokedPermissionsOSNotificationDisplayManagerFactory::GetInstance();
  ResetReportUploaderFactory::GetInstance();
#if BUILDFLAG(FULL_SAFE_BROWSING)
  safe_browsing::AdvancedProtectionStatusManagerFactory::GetInstance();
#endif
#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  safe_browsing::ChromeEnterpriseRealTimeUrlLookupServiceFactory::GetInstance();
#endif
#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  safe_browsing::ChromePasswordProtectionServiceFactory::GetInstance();
  safe_browsing::ChromePingManagerFactory::GetInstance();
  safe_browsing::ClientSideDetectionServiceFactory::GetInstance();
  safe_browsing::ClientSideDetectionIntelligentScanDelegateFactory::
      GetInstance();
  safe_browsing::GeminiAntiscamProtectionServiceFactory::GetInstance();
#endif
#if BUILDFLAG(FULL_SAFE_BROWSING)
  safe_browsing::CloudBinaryUploadServiceFactory::GetInstance();
  safe_browsing::ExtensionTelemetryServiceFactory::GetInstance();
  safe_browsing::HashRealTimeServiceFactory::GetInstance();
#endif
#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  safe_browsing::RealTimeUrlLookupServiceFactory::GetInstance();
  safe_browsing::SafeBrowsingMetricsCollectorFactory::GetInstance();
  safe_browsing::SafeBrowsingNavigationObserverManagerFactory::GetInstance();
  safe_browsing::TailoredSecurityServiceFactory::GetInstance();
#endif
  safe_browsing::VerdictCacheManagerFactory::GetInstance();
  SafeSearchFactory::GetInstance();
  SafetyHubMenuNotificationServiceFactory::GetInstance();
  SafetyHubHatsServiceFactory::GetInstance();
  if (features::IsMainNodeAnnotationsEnabled()) {
    screen_ai::AXMainNodeAnnotatorControllerFactory::GetInstance();
  }
  screen_ai::ScreenAIServiceRouterFactory::EnsureFactoryBuilt();
  SCTReportingServiceFactory::GetInstance();
  search_engines::SearchEngineChoiceServiceFactory::GetInstance();
  SearchEngineChoiceDialogServiceFactory::GetInstance();
  SearchPrefetchServiceFactory::GetInstance();
  SearchPrewarmProgressServiceFactory::GetInstance();
  if (SearchEnginePreconnector::ShouldBeEnabledAsKeyedService()) {
    SearchEnginePreconnectorKeyedServiceFactory::GetInstance();
  }
  SearchPreloadServiceFactory::GetInstance();
  segmentation_platform::SegmentationPlatformServiceFactory::GetInstance();
  send_tab_to_self::SendTabToSelfClientServiceFactory::GetInstance();
  SerialChooserContextFactory::GetInstance();
#if BUILDFLAG(ENABLE_SESSION_SERVICE)
  SessionDataServiceFactory::GetInstance();
#endif
  SessionProtoDBFactory<cart_db::ChromeCartContentProto>::GetInstance();
  SessionProtoDBFactory<commerce_subscription_db::
                            CommerceSubscriptionContentProto>::GetInstance();
  SessionProtoDBFactory<coupon_db::CouponContentProto>::GetInstance();
  SessionProtoDBFactory<discounts_db::DiscountsContentProto>::GetInstance();
  SessionProtoDBFactory<
      discount_infos_db::DiscountInfosContentProto>::GetInstance();
  SessionProtoDBFactory<
      persisted_state_db::PersistedStateContentProto>::GetInstance();
#if BUILDFLAG(ENABLE_SESSION_SERVICE)
  SessionServiceFactory::GetInstance();
#endif
  sharing_hub::SharingHubServiceFactory::GetInstance();
  SharingServiceFactory::GetInstance();
  ShortcutsBackendFactory::GetInstance();
  SigninDetectionServiceFactoryEnsureFactoryBuilt();
  SigninMetricsServiceFactory::GetInstance();
  SigninPolicyServiceFactory::GetInstance();
  SigninProfileAttributesUpdaterFactory::GetInstance();
  if (site_engagement::SiteEngagementService::IsEnabled()) {
    site_engagement::SiteEngagementServiceFactory::GetInstance();
  }
  SpeechRecognitionClientBrowserInterfaceFactory::EnsureFactoryBuilt();
  SpeechRecognitionServiceFactory::EnsureFactoryBuilt();
#if BUILDFLAG(ENABLE_SPELLCHECK)
  SpellcheckServiceFactory::GetInstance();
#endif
  StartupPasswordsImportServiceFactory::GetInstance();
  StatefulSSLHostStateDelegateFactory::GetInstance();
  StorageAccessAPIServiceFactory::GetInstance();
  StorageNotificationServiceFactory::GetInstance();
  SubresourceFilterProfileContextFactory::GetInstance();
  subscription_eligibility::SubscriptionEligibilityServiceFactory::
      GetInstance();
  SupervisedUserMetricsServiceFactory::GetInstance();
  SupervisedUserServiceFactory::GetInstance();
  supervised_user::SupervisedUserUrlFilteringServiceFactory::GetInstance();
#if BUILDFLAG(ENABLE_EXTENSIONS)
  sync_file_system::SyncFileSystemServiceFactory::GetInstance();
#endif
  SyncServiceFactory::GetInstance();
  tab_groups::TabGroupSyncServiceFactory::GetInstance();
  TabOrganizationServiceFactory::GetInstance();
  TabRestoreServiceFactory::GetInstance();
  TemplateURLFetcherFactory::GetInstance();
  TemplateURLPrepopulateData::ResolverFactory::GetInstance();
  TemplateURLServiceFactory::GetInstance();
  ThemeColorsSourceManagerFactory::GetInstance();
  ThemeServiceFactory::GetInstance();
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
  ToolbarActionsModelFactory::GetInstance();
#endif
  TopSitesFactory::GetInstance();
  tree_fixing::AXTreeFixingServicesRouterFactory::GetInstance();
  TriggeredProfileResetterFactory::GetInstance();
#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
  TurnSyncOnHelper::EnsureFactoryBuilt();
#endif
#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
  unexportable_keys::UnexportableKeyProfileGarbageCollectionServiceFactory::
      GetInstance();
#endif
  UnifiedConsentServiceFactory::GetInstance();
  RevokedPermissionsServiceFactory::GetInstance();
  UrlLanguageHistogramFactory::GetInstance();
  UsbChooserContextFactory::GetInstance();
  UsbConnectionTrackerFactory::GetInstance();
  UserEducationServiceFactory::GetInstance();
  visited_url_ranking::GroupSuggestionsServiceFactory::GetInstance();
  WaapUIMetricsServiceFactory::GetInstance();
#if BUILDFLAG(ENABLE_EXTENSIONS)
  web_app::IsolatedWebAppReaderRegistryFactory::GetInstance();
  web_app::IsolatedWebAppURLLoaderFactory::EnsureAssociatedFactoryBuilt();
  web_app::WebAppMetricsFactory::GetInstance();
  web_app::WebAppProviderFactory::GetInstance();
#endif
  WebDataServiceFactory::GetInstance();
  webrtc_event_logging::WebRtcEventLogManagerKeyedServiceFactory::GetInstance();
}

void ChromeBrowserMainExtraPartsProfiles::PreProfileInit() {
  EnsureBrowserContextKeyedServiceFactoriesBuilt();
}
