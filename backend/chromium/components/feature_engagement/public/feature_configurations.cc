// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feature_engagement/public/feature_configurations.h"

#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "components/feature_engagement/public/configuration.h"
#include "components/feature_engagement/public/event_constants.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/feature_engagement/public/group_constants.h"


namespace {
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
const int k10YearsInDays = 365 * 10;
#endif
}  // namespace

namespace feature_engagement {

FeatureConfig CreateAlwaysTriggerConfig(const base::Feature* feature) {
  // Trim "IPH_" prefix from the feature name to use for trigger and used
  // events.
  const char* prefix = "IPH_";
  std::string stripped_feature_name = feature->name;
  if (base::StartsWith(stripped_feature_name, prefix,
                       base::CompareCase::SENSITIVE)) {
    stripped_feature_name = stripped_feature_name.substr(strlen(prefix));
  }

  // A config that always meets condition to trigger IPH.
  FeatureConfig config;
  config.valid = true;
  config.availability = Comparator(ANY, 0);
  config.session_rate = Comparator(ANY, 0);
  config.trigger = EventConfig(stripped_feature_name + "_trigger",
                               Comparator(ANY, 0), 90, 90);
  config.used =
      EventConfig(stripped_feature_name + "_used", Comparator(ANY, 0), 90, 90);
  return config;
}


std::optional<FeatureConfig> GetClientSideFeatureConfig(
    const base::Feature* feature) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)

  // The IPH bubble for link capturing has a trigger set to ANY so that it
  // always shows up. The per app specific guardrails are independently stored
  // under the web_app_prefs.
  if (kIPHDesktopPWAsLinkCapturingLaunch.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.trigger = EventConfig("desktop_pwa_launch_link_capturing",
                                 Comparator(ANY, 0), 0, 0);
    config.used = EventConfig("desktop_pwa_launch_link_capturing_used",
                              Comparator(ANY, 0), 0, 0);
    return config;
  }

#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  if (kIPHPasswordsManagementBubbleAfterSaveFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.trigger =
        EventConfig("password_saved", Comparator(LESS_THAN, 1), 360, 360);
    config.session_rate = Comparator(ANY, 0);
    config.availability = Comparator(ANY, 0);
    return config;
  }

  if (kIPHPasswordsManagementBubbleDuringSigninFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.trigger =
        EventConfig("signin_flow_detected", Comparator(LESS_THAN, 1), 360, 360);
    config.session_rate = Comparator(ANY, 0);
    config.availability = Comparator(ANY, 0);
    return config;
  }

  if (kIPHProfileSwitchFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    // Show the promo once a year if the profile menu was not opened.
    config.trigger =
        EventConfig("profile_switch_trigger", Comparator(EQUAL, 0), 360, 360);
    config.used =
        EventConfig("profile_menu_shown", Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHReadingListInSidePanelFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    // Show the promo once a year if the side panel was not opened.
    config.trigger =
        EventConfig("side_panel_trigger", Comparator(EQUAL, 0), 360, 360);
    config.used =
        EventConfig("side_panel_shown", Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHReadingModeSidePanelFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    // Show the promo up to 3 times a year.
    config.trigger = EventConfig("iph_reading_mode_side_panel_trigger",
                                 Comparator(LESS_THAN, 3), 360, 360);
    config.used = EventConfig("reading_mode_side_panel_shown",
                              Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHSidePanelGenericPinnableFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    // Show the promo once a year if the side panel was not opened.
    config.trigger = EventConfig("side_panel_pinnable_trigger",
                                 Comparator(EQUAL, 0), 360, 360);
    config.used = EventConfig(feature_engagement::events::kSidePanelPinned,
                              Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHGMCCastStartStopFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger = EventConfig("gmc_start_stop_iph_trigger",
                                 Comparator(EQUAL, 0), 180, 180);
    config.used = EventConfig("media_route_stopped_from_gmc",
                              Comparator(EQUAL, 0), 180, 180);
    return config;
  }

  if (kIPHGMCLocalMediaCastingFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.trigger = EventConfig("gmc_local_media_cast_iph_trigger",
                                 Comparator(EQUAL, 0), 180, 180);
    config.used = EventConfig("media_route_started_from_gmc",
                              Comparator(EQUAL, 0), 180, 180);

    return config;
  }

  if (kIPHDesktopSharedHighlightingFeature.name == feature->name) {
    // A config that allows the shared highlighting desktop IPH to be shown
    // when a user receives a highlight:
    // * Once per 7 days
    // * Up to 5 times but only if unused in the last 7 days.
    // * Used fewer than 2 times

    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.trigger = EventConfig("iph_desktop_shared_highlighting_trigger",
                                 Comparator(LESS_THAN, 5), 360, 360);
    config.used = EventConfig("iph_desktop_shared_highlighting_used",
                              Comparator(LESS_THAN, 2), 360, 360);
    config.event_configs.insert(
        EventConfig("iph_desktop_shared_highlighting_trigger",
                    Comparator(EQUAL, 0), 7, 360));
    return config;
  }

  if (kIPHBatterySaverModeFeature.name == feature->name) {
    // Show promo once a year when the battery saver toolbar icon is visible.
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger = EventConfig("battery_saver_info_triggered",
                                 Comparator(LESS_THAN, 1), 360, 360);
    config.used =
        EventConfig("battery_saver_info_shown", Comparator(EQUAL, 0), 7, 360);
    return config;
  }

  if (kIPHMemorySaverModeFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    // Show the promo max 3 times, once per week.
    config.trigger = EventConfig("high_efficiency_prompt_in_trigger",
                                 Comparator(LESS_THAN, 1), 7, 360);
    // This event is never logged but is included for consistency.
    config.used = EventConfig("high_efficiency_prompt_in_used",
                              Comparator(EQUAL, 0), 360, 360);
    config.event_configs.insert(EventConfig("high_efficiency_prompt_in_trigger",
                                            Comparator(LESS_THAN, 1), 360,
                                            360));
    return config;
  }

  if (kIPHPerformanceInterventionDialogFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    // Show intervention dialog at most 3 times per day and no more than 21
    // times per week.
    config.trigger = EventConfig("performance_intervention_dialog_trigger",
                                 Comparator(LESS_THAN, 3), 1, 360);
    config.event_configs.insert(
        EventConfig("performance_intervention_dialog_trigger",
                    Comparator(LESS_THAN, 21), 7, 360));
    return config;
  }

  if (kIPHPowerBookmarksSidePanelFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger = EventConfig("iph_power_bookmarks_side_panel_trigger",
                                 Comparator(LESS_THAN, 3), 360, 360);
    config.used = EventConfig("power_bookmarks_side_panel_shown",
                              Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHPriceInsightsPageActionIconLabelFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    // Show the label once per day, 3 times max in 28 days.
    config.trigger =
        EventConfig("price_insights_page_action_icon_label_in_trigger",
                    Comparator(ANY, 0), 0, 360);
    config.used = EventConfig("price_insights_page_action_icon_label_used",
                              Comparator(ANY, 0), 0, 360);
    config.event_configs.insert(
        EventConfig("price_insights_page_action_icon_label_in_trigger",
                    Comparator(ANY, 0), 0, 360));
    return config;
  }

  if (kIPHPriceTrackingEmailConsentFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    // Show the IPH up to 3 times per month.
    config.trigger = EventConfig("price_tracking_email_consent_trigger",
                                 Comparator(LESS_THAN, 3), 30, 360);
    return config;
  }

  if (kIPHPriceTrackingInSidePanelFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    // Show the promo once a year if the price tracking IPH was not triggered.
    config.trigger = EventConfig("iph_price_tracking_side_panel_trigger",
                                 Comparator(EQUAL, 0), 360, 360);
    config.used = EventConfig("price_tracking_side_panel_shown",
                              Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHPriceTrackingPageActionIconLabelFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    // Show the promo once per day.
    config.trigger =
        EventConfig("price_tracking_page_action_icon_label_in_trigger",
                    Comparator(LESS_THAN, 1), 1, 360);
    return config;
  }

  if (kIPHShoppingCollectionFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    // Show the IPH 3 times per year.
    config.trigger = EventConfig("shopping_collection_trigger",
                                 Comparator(LESS_THAN, 3), 360, 360);
    return config;
  }

#if BUILDFLAG(ENABLE_EXTENSIONS)
  if (kIPHExtensionsMenuFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(LESS_THAN, 1);

    // Show promo up to three times a year or until the extensions menu is
    // opened while any extension has access to the current site.
    config.trigger = EventConfig("extensions_menu_trigger",
                                 Comparator(LESS_THAN, 3), 360, 360);
    config.used =
        EventConfig("extensions_menu_opened_while_extension_has_access",
                    Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHExtensionsRequestAccessButtonFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(LESS_THAN, 1);

    // Show promo up to three times a year or until the request access button
    // is clicked.
    config.trigger = EventConfig("extensions_request_access_button_trigger",
                                 Comparator(LESS_THAN, 3), 360, 360);
    config.used = EventConfig("extensions_request_access_button_clicked",
                              Comparator(EQUAL, 0), 360, 360);
    return config;
  }
#endif

  if (kIPHCompanionSidePanelFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;

    // Show the promo up to 3 times a year.
    config.trigger = EventConfig("iph_companion_side_panel_trigger",
                                 Comparator(LESS_THAN, 3), 360, 360);
    config.used =
        EventConfig("companion_side_panel_accessed_via_toolbar_button",
                    Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHCompanionSidePanelRegionSearchFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    // Show the promo up to 3 times a year.
    config.trigger =
        EventConfig("iph_companion_side_panel_region_search_trigger",
                    Comparator(LESS_THAN, 3), 360, 360);
    config.used =
        EventConfig("companion_side_panel_region_search_button_clicked",
                    Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHPasswordsWebAppProfileSwitchFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.trigger =
        EventConfig("iph_passwords_web_app_profile_switch_triggered",
                    Comparator(EQUAL, 0), 360, 360);
    config.used = EventConfig("web_app_profile_menu_shown",
                              Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHPasswordManagerShortcutFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger = EventConfig("iph_password_manager_shortcut_triggered",
                                 Comparator(EQUAL, 0), 360, 360);
    config.used = EventConfig("password_manager_shortcut_created",
                              Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHPasswordSharingFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.trigger = EventConfig("password_sharing_iph_triggered",
                                 Comparator(EQUAL, 0), 360, 360);
    config.used = EventConfig("password_share_button_clicked",
                              Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHDiscardRingFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger =
        EventConfig("discard_ring_trigger", Comparator(EQUAL, 0), 360, 360);
    // This event is never logged but is included for consistency.
    config.used =
        EventConfig("discard_ring_used", Comparator(EQUAL, 0), 360, 360);
    return config;
  }

  if (kIPHDownloadEsbPromoFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    // Because this is a custom configuration being used in desktop user ed, use
    // a non-default availability so the configurator doesn't try to write its
    // own.
    config.availability = Comparator(GREATER_THAN_OR_EQUAL, 0);
    config.session_rate = Comparator(ANY, 0);
    // Don't show if user has already seen an IPH this session.
    // Show the promo max once a year if the user hasn't interacted with
    // a dangerous download within the last 21 days.
    config.trigger = EventConfig("download_bubble_esb_iph_trigger",
                                 Comparator(EQUAL, 0), 360, 360);
    config.used = EventConfig("enable_enhanced_protection",
                              Comparator(EQUAL, 0), 21, 360);
    config.event_configs.insert(
        EventConfig("download_bubble_dangerous_download_detected",
                    Comparator(GREATER_THAN_OR_EQUAL, 1), 21, 360));
    return config;
  }
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  if (kEsbDownloadRowPromoFeature.name == feature->name) {
    // A config that allows a promotion row referring users to enable Enhanced
    // Safe Browsing (ESB), to be shown on the Downloads manager page. It
    // can be viewed at most 7 times without interaction across a 90 day period.
    // If the user clicks, then we aritificially increment the viewed event by 4
    // so that the row can be seen at most 2 more times.
    //
    // The trigger management can be found in
    // c/b/ui/webui/downloads/downloads_dom_handler.cc
    FeatureConfig config;
    config.valid = true;
    // Because this is a custom configuration being used in desktop user ed, use
    // a non-default availability so the configurator doesn't try to write its
    // own.
    config.availability = Comparator(GREATER_THAN_OR_EQUAL, 0);
    config.session_rate = Comparator(ANY, 0);

    // This isn't an IPH so we don't suppress other engagement features.
    SessionRateImpact session_rate_impact;
    session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.session_rate_impact = session_rate_impact;

    // This isn't an IPH so we don't want to block or be blocked by any other
    // engagement features.
    config.blocked_by.type = BlockedBy::Type::NONE;
    config.blocking.type = Blocking::Type::NONE;

    config.trigger = EventConfig("dangerous_download_esb_promo_row_trigger",
                                 Comparator(ANY, 0), 360, 360);
    config.used =
        EventConfig("enable_enhanced_protection", Comparator(EQUAL, 0), 21, 90);
    config.event_configs.insert(EventConfig("esb_download_promo_row_viewed",
                                            Comparator(LESS_THAN, 7), 90, 90));
    config.event_configs.insert(EventConfig("esb_download_promo_row_clicked",
                                            Comparator(LESS_THAN, 3), 90, 90));

    return config;
  }
#endif

  if (kIPHBackNavigationMenuFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger = EventConfig("back_navigation_menu_iph_is_triggered",
                                 Comparator(LESS_THAN_OR_EQUAL, 4), 360, 360);
    config.used = EventConfig("back_navigation_menu_is_opened",
                              Comparator(EQUAL, 0), 7, 360);
    config.snooze_params.snooze_interval = 7;
    config.snooze_params.max_limit = 4;
    return config;
  }

  if (kIPHComposeNewBadgeFeature.name == feature->name) {
    // A config that allows the new badge displayed in the Compose feature nudge
    // to be shown at most 4 times in a 10-day window and only while the user
    // has opened the Compose feature less than 3 times.
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.trigger = EventConfig("compose_new_badge_triggered",
                                 Comparator(LESS_THAN, 4), 10, 360);
    config.used =
        EventConfig("compose_activated", Comparator(LESS_THAN, 3), 360, 360);
    return config;
  }

  if (kIPHComposeMSBBSettingsFeature.name == feature->name) {
    // A config that allows a toast to be displayed in the Settings page when
    // opened via the Compose MSBB feature
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.trigger = EventConfig("compose_msbb_settings_feature_trigger",
                                 Comparator(ANY, 0), 90, 90);
    config.used = EventConfig("compose_msbb_settings_feature_used",
                              Comparator(ANY, 0), 90, 90);
    return config;
  }

  if (kIPHTabOrganizationSuccessFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    // Show the IPH once per year.
    config.trigger = EventConfig("iph_tab_organization_success_trigger",
                                 Comparator(EQUAL, 0), 360, 360);
    config.used =
        EventConfig("tab_group_editor_shown", Comparator(EQUAL, 0), 360, 360);
    return config;
  }

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) ||
  // BUILDFLAG(IS_CHROMEOS)

  if (kIPHiOSPasswordPromoDesktopFeature.name == feature->name) {
    // A config for allowing other IPH's to explicitly block the iOS password
    // promo bubble on desktop if needed. Blocked and blocking by default, so
    // won't appear at the same time as other IPH, but without any session rate
    // impact.

    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.blocked_by.type = BlockedBy::Type::ALL;
    config.blocking.type = Blocking::Type::ALL;
    config.used =
        EventConfig("ios_password_promo_bubble_on_desktop_interacted_with",
                    Comparator(ANY, 0), 0, 0);
    config.trigger = EventConfig("ios_password_promo_bubble_on_desktop_shown",
                                 Comparator(ANY, 0), 0, 0);
    return config;
  }

  if (kIPHiOSAddressPromoDesktopFeature.name == feature->name) {
    // A config for allowing other IPH's to explicitly block the iOS address
    // promo bubble on desktop if needed. Blocked and blocking by default, so
    // won't appear at the same time as other IPH, but without any session rate
    // impact.

    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.blocked_by.type = BlockedBy::Type::ALL;
    config.blocking.type = Blocking::Type::ALL;
    config.used =
        EventConfig("ios_address_promo_bubble_on_desktop_interacted_with",
                    Comparator(ANY, 0), 0, 0);
    config.trigger = EventConfig("ios_address_promo_bubble_on_desktop_shown",
                                 Comparator(ANY, 0), 0, 0);
    return config;
  }

  if (kIPHiOSPaymentPromoDesktopFeature.name == feature->name) {
    // A config for allowing other IPH's to explicitly block the iOS payment
    // promo bubble on desktop if needed. Blocked and blocking by default, so
    // won't appear at the same time as other IPH, but without any session rate
    // impact.

    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.blocked_by.type = BlockedBy::Type::ALL;
    config.blocking.type = Blocking::Type::ALL;
    config.used =
        EventConfig("ios_payment_promo_bubble_on_desktop_interacted_with",
                    Comparator(ANY, 0), 0, 0);
    config.trigger = EventConfig("ios_payment_promo_bubble_on_desktop_shown",
                                 Comparator(ANY, 0), 0, 0);
    return config;
  }

  if (kIPHiOSLensPromoDesktopFeature.name == feature->name) {
    // Config for allowing other IPH's to explicitly block the iOS lens
    // promo bubble on desktop if needed. Blocked and blocking by default, so
    // won't appear at the same time as other IPH, but without any session rate
    // impact.

    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.blocked_by.type = BlockedBy::Type::ALL;
    config.blocking.type = Blocking::Type::ALL;
    config.used =
        EventConfig("ios_lens_promo_bubble_on_desktop_interacted_with",
                    Comparator(ANY, 0), 0, 0);
    config.trigger = EventConfig("ios_lens_promo_bubble_on_desktop_shown",
                                 Comparator(ANY, 0), 0, 0);
    return config;
  }

  if (kIPHiOSEnhancedBrowsingDesktopFeature.name == feature->name) {
    // Config for allowing other IPH's to explicitly block the iOS enhanced
    // browsing promo bubble on desktop if needed. Blocked and blocking by
    // default, so won't appear at the same time as other IPH, but without any
    // session rate impact.

    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(ANY, 0);
    config.session_rate_impact.type = SessionRateImpact::Type::NONE;
    config.blocked_by.type = BlockedBy::Type::ALL;
    config.blocking.type = Blocking::Type::ALL;
    config.used = EventConfig(
        "ios_enhanced_browsing_promo_bubble_on_desktop_interacted_with",
        Comparator(ANY, 0), 0, 0);
    config.trigger =
        EventConfig("ios_enhanced_browsing_promo_bubble_on_desktop_shown",
                    Comparator(ANY, 0), 0, 0);
    return config;
  }


#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)

  if (kIPHAutofillCreditCardBenefitFeature.name == feature->name) {
    // The credit card benefit IPH appears up to three times over 10 years and
    // only once per session. Dismissing it stops it from showing again.
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(LESS_THAN, 1);
    config.trigger = EventConfig("autofill_credit_card_benefit_iph_trigger",
                                 Comparator(LESS_THAN, 3), 90, 360);
    config.used =
        EventConfig("autofill_credit_card_benefit_iph_accepted",
                    Comparator(EQUAL, 0), feature_engagement::kMaxStoragePeriod,
                    feature_engagement::kMaxStoragePeriod);
    return config;
  }

  if (kIPHAutofillExternalAccountProfileSuggestionFeature.name ==
      feature->name) {
    // Externally created account profile suggestion IPH is shown:
    // * once for an installation, 10-year window is used as the maximum
    // * if there was no address keyboard accessory IPH in the last 2 weeks
    // * if such a suggestion was not already accepted
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger =
        EventConfig("autofill_external_account_profile_suggestion_iph_trigger",
                    Comparator(EQUAL, 0), feature_engagement::kMaxStoragePeriod,
                    feature_engagement::kMaxStoragePeriod);
    config.used =
        EventConfig("autofill_external_account_profile_suggestion_accepted",
                    Comparator(EQUAL, 0), feature_engagement::kMaxStoragePeriod,
                    feature_engagement::kMaxStoragePeriod);


    return config;
  }

  if (kIPHAutofillVirtualCardSuggestionFeature.name == feature->name) {
    // A config that allows the virtual card credit card suggestion IPH to be
    // shown when:
    // * it has been shown less than three times in last 90 days;
    // * the virtual card suggestion has been selected less than twice in last
    // 90 days.

    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger = EventConfig("autofill_virtual_card_iph_trigger",
                                 Comparator(LESS_THAN, 3), 90, 360);
    config.used = EventConfig("autofill_virtual_card_suggestion_accepted",
                              Comparator(LESS_THAN, 2), 90, 360);


    return config;
  }

  if (kIPHAutofillVirtualCardCVCSuggestionFeature.name == feature->name) {
    // A config that allows the virtual card CVC suggestion IPH to be
    // shown when:
    // * it has been shown less than three times in last 90 days;
    // * the virtual card CVC suggestion has been selected less than twice in
    // last 90 days.

    FeatureConfig config;
    config.valid = true;
    // On desktop, toasts should always be available.
    config.availability = Comparator(ANY, 0);
    config.trigger = EventConfig("autofill_virtual_card_cvc_iph_trigger",
                                 Comparator(LESS_THAN, 3), 90, 360);
    config.used = EventConfig("autofill_virtual_card_cvc_suggestion_accepted",
                              Comparator(LESS_THAN, 2), 90, 360);

    // This promo blocks specific promos in the same session.
    config.session_rate_impact.type = SessionRateImpact::Type::EXPLICIT;
    config.session_rate_impact.affected_features.emplace();
    config.session_rate_impact.affected_features->push_back(
        "IPH_AutofillVirtualCardSuggestion");

    return config;
  }

  if (kIPHCookieControlsFeature.name == feature->name) {
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    // Show promo up to 3 times per year and only if user hasn't interacted with
    // the cookie controls bubble in the last week.
    config.trigger = EventConfig("iph_cookie_controls_triggered",
                                 Comparator(LESS_THAN, 3), 360, 360);
    config.used =
        EventConfig(feature_engagement::events::kCookieControlsBubbleShown,
                    Comparator(EQUAL, 0), 7, 7);
    return config;
  }

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) ||
  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) ||
  // BUILDFLAG(IS_FUCHSIA)



#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  if (kIPHPlusAddressCreateSuggestionFeature.name == feature->name) {
    // A config that allows a user education bubble to be shown for the plus
    // address feature. Will be shown up to 9 times in the 90 day window with
    // the exception of 2 times if the user accepted the suggestion.

    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger =
        EventConfig("plus_address_create_suggestion_feature_trigger",
                    Comparator(LESS_THAN, 9), 90, 360);
    config.used = EventConfig("plus_address_create_suggestion_feature_used",
                              Comparator(LESS_THAN, 2), 90, 360);
    return config;
  }

  if (kIPHAutofillHomeWorkProfileSuggestionFeature.name == feature->name) {
    // Allows an IPH for showing the home and work address suggestion. This will
    // only be shown once.
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger =
        EventConfig("home_work_address_create_suggestion_feature_trigger",
                    Comparator(LESS_THAN, 1), k10YearsInDays, k10YearsInDays);
    config.used =
        EventConfig("home_work_address_create_suggestion_feature_used",
                    Comparator(EQUAL, 0), k10YearsInDays, k10YearsInDays);

    return config;
  }

  if (kIPHAutofillAccountNameEmailSuggestionFeature.name == feature->name) {
    // Allows an IPH for showing the account name and email address suggestion.
    // This will only be shown once.
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(ANY, 0);
    config.session_rate = Comparator(EQUAL, 0);
    config.trigger =
        EventConfig("account_name_email_create_suggestion_feature_trigger",
                    Comparator(LESS_THAN, 1), k10YearsInDays, k10YearsInDays);
    config.used =
        EventConfig("account_name_email_create_suggestion_feature_used",
                    Comparator(EQUAL, 0), k10YearsInDays, k10YearsInDays);

    return config;
  }
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)

  if (kIPHDummyFeature.name == feature->name) {
    // Only used for tests. Various magic tricks are used below to ensure this
    // config is invalid and unusable.
    FeatureConfig config;
    config.valid = true;
    config.availability = Comparator(LESS_THAN, 0);
    config.session_rate = Comparator(LESS_THAN, 0);
    config.trigger = EventConfig("dummy_feature_iph_trigger",
                                 Comparator(LESS_THAN, 0), 1, 1);
    config.used =
        EventConfig("dummy_feature_action", Comparator(LESS_THAN, 0), 1, 1);
    return config;
  }

  return std::nullopt;
}

}  // namespace feature_engagement
