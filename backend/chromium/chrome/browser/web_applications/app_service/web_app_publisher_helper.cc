// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/app_service/web_app_publisher_helper.h"

#include <stddef.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/checked_iterators.h"
#include "base/containers/extend.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/containers/flat_tree.h"
#include "base/containers/map_util.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/functional/concurrent_callbacks.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/notreached.h"
#include "base/one_shot_event.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/to_string.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "chrome/browser/apps/app_service/app_icon/app_icon_factory.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/apps/app_service/intent_util.h"
#include "chrome/browser/apps/app_service/launch_utils.h"
#include "chrome/browser/apps/app_service/policy_util.h"
#include "chrome/browser/apps/app_service/publishers/app_publisher.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/app_service/publisher_helper.h"
#include "chrome/browser/web_applications/commands/compute_app_size_command.h"
#include "chrome/browser/web_applications/commands/computed_app_size.h"
#include "chrome/browser/web_applications/locks/app_lock.h"
#include "chrome/browser/web_applications/mojom/user_display_mode.mojom-shared.h"
#include "chrome/browser/web_applications/os_integration/os_integration_manager.h"
#include "chrome/browser/web_applications/os_integration/web_app_file_handler_manager.h"
#include "chrome/browser/web_applications/policy/web_app_policy_manager.h"
#include "chrome/browser/web_applications/proto/web_app.pb.h"
#include "chrome/browser/web_applications/scope_extension_info.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_chromeos_data.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_app_constants.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_management_type.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_tab_helper.h"
#include "chrome/browser/web_applications/web_app_ui_manager.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_features.h"
#include "components/content_settings/core/browser/content_settings_type_set.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/services/app_service/public/cpp/app_launch_params.h"
#include "components/services/app_service/public/cpp/app_launch_util.h"
#include "components/services/app_service/public/cpp/file_handler.h"
#include "components/services/app_service/public/cpp/icon_effects.h"
#include "components/services/app_service/public/cpp/intent_filter.h"
#include "components/services/app_service/public/cpp/intent_filter_util.h"
#include "components/services/app_service/public/cpp/intent_util.h"
#include "components/services/app_service/public/cpp/run_on_os_login_types.h"
#include "components/services/app_service/public/cpp/share_target.h"
#include "components/webapps/browser/installable/installable_metrics.h"
#include "content/public/browser/clear_site_data_utils.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "net/cookies/cookie_partition_key.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom-shared.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/types/display_constants.h"
#include "ui/gfx/native_ui_types.h"
#include "url/gurl.h"
#include "url/origin.h"


using apps::IconEffects;

namespace content {
class BrowserContext;
}

namespace ui {
enum ResourceScaleFactor : int;
}

namespace web_app {

namespace {

// Only supporting important permissions for now.
const ContentSettingsType kSupportedPermissionTypes[] = {
    ContentSettingsType::MEDIASTREAM_MIC,
    ContentSettingsType::MEDIASTREAM_CAMERA,
    ContentSettingsType::GEOLOCATION,
    ContentSettingsType::NOTIFICATIONS,
};

// Mime Type for plain text.
const char kTextPlain[] = "text/plain";

bool GetContentSettingsType(apps::PermissionType permission_type,
                            ContentSettingsType& content_setting_type) {
  switch (permission_type) {
    case apps::PermissionType::kCamera:
      content_setting_type = ContentSettingsType::MEDIASTREAM_CAMERA;
      return true;
    case apps::PermissionType::kLocation:
      content_setting_type = ContentSettingsType::GEOLOCATION;
      return true;
    case apps::PermissionType::kMicrophone:
      content_setting_type = ContentSettingsType::MEDIASTREAM_MIC;
      return true;
    case apps::PermissionType::kNotifications:
      content_setting_type = ContentSettingsType::NOTIFICATIONS;
      return true;
    case apps::PermissionType::kUnknown:
    case apps::PermissionType::kContacts:
    case apps::PermissionType::kStorage:
    case apps::PermissionType::kPrinting:
    case apps::PermissionType::kFileHandling:
      return false;
  }
}

apps::PermissionType GetPermissionType(
    ContentSettingsType content_setting_type) {
  switch (content_setting_type) {
    case ContentSettingsType::MEDIASTREAM_CAMERA:
      return apps::PermissionType::kCamera;
    case ContentSettingsType::GEOLOCATION:
      return apps::PermissionType::kLocation;
    case ContentSettingsType::MEDIASTREAM_MIC:
      return apps::PermissionType::kMicrophone;
    case ContentSettingsType::NOTIFICATIONS:
      return apps::PermissionType::kNotifications;
    default:
      return apps::PermissionType::kUnknown;
  }
}

apps::InstallReason GetHighestPriorityInstallReason(const WebApp* web_app) {
  // TODO(crbug.com/40755721): Migrate apps with chromeos_data.oem_installed set
  // to the new WebAppManagement::Type::kOem install type.
  if (web_app->chromeos_data().has_value()) {
    auto& chromeos_data = web_app->chromeos_data().value();
    if (chromeos_data.oem_installed) {
      DCHECK(!web_app->IsSystemApp());
      return apps::InstallReason::kOem;
    }
  }

  // We do not make a distinction in `apps::InstallReason` between IWA sources
  // and non-IWA sources. For example, we map both `WebAppManagement::kPolicy`
  // and `WebAppManagement::kIwaPolicy` to `apps::InstallReason::kPolicy`. This
  // is only possible because there is only a one-way conversion from
  // `WebAppManagement::Type` to `apps::InstallReason`. Should we ever make them
  // convertible in the other direction, we'd need to add IWA-specific sources
  // to `apps::InstallReason` first.
  switch (web_app->GetHighestPrioritySource()) {
    case WebAppManagement::kSystem:
    case WebAppManagement::kIwaShimlessRma:
      return apps::InstallReason::kSystem;
    case WebAppManagement::kKiosk:
      return apps::InstallReason::kKiosk;
    case WebAppManagement::kPolicy:
    case WebAppManagement::kIwaPolicy:
      return apps::InstallReason::kPolicy;
    case WebAppManagement::kOem:
      return apps::InstallReason::kOem;
    case WebAppManagement::kSubApp:
      return apps::InstallReason::kSubApp;
    case WebAppManagement::kWebAppStore:
    case WebAppManagement::kOneDriveIntegration:
    case WebAppManagement::kIwaUserInstalled:
    case WebAppManagement::kUserInstalled:
      return apps::InstallReason::kUser;
    case WebAppManagement::kSync:
      return apps::InstallReason::kSync;
    case WebAppManagement::kDefault:
    case WebAppManagement::kApsDefault:
      return apps::InstallReason::kDefault;
  }
}

apps::InstallSource GetInstallSource(
    std::optional<webapps::WebappInstallSource> source) {
  if (!source) {
    return apps::InstallSource::kUnknown;
  }

  switch (*source) {
    case webapps::WebappInstallSource::MENU_BROWSER_TAB:
    case webapps::WebappInstallSource::MENU_CUSTOM_TAB:
    case webapps::WebappInstallSource::AUTOMATIC_PROMPT_BROWSER_TAB:
    case webapps::WebappInstallSource::AUTOMATIC_PROMPT_CUSTOM_TAB:
    case webapps::WebappInstallSource::API_BROWSER_TAB:
    case webapps::WebappInstallSource::API_CUSTOM_TAB:
    case webapps::WebappInstallSource::DEVTOOLS:
    case webapps::WebappInstallSource::MANAGEMENT_API:
    case webapps::WebappInstallSource::IWA_DEV_UI:
    case webapps::WebappInstallSource::IWA_DEV_COMMAND_LINE:
    case webapps::WebappInstallSource::IWA_GRAPHICAL_INSTALLER:
    case webapps::WebappInstallSource::IWA_EXTERNAL_POLICY:
    case webapps::WebappInstallSource::IWA_SHIMLESS_RMA:
    case webapps::WebappInstallSource::AMBIENT_BADGE_BROWSER_TAB:
    case webapps::WebappInstallSource::AMBIENT_BADGE_CUSTOM_TAB:
    case webapps::WebappInstallSource::RICH_INSTALL_UI_WEBLAYER:
    case webapps::WebappInstallSource::EXTERNAL_POLICY:
    case webapps::WebappInstallSource::ML_PROMOTION:
    case webapps::WebappInstallSource::OMNIBOX_INSTALL_ICON:
    case webapps::WebappInstallSource::MENU_CREATE_SHORTCUT:
    case webapps::WebappInstallSource::SUB_APP:
    case webapps::WebappInstallSource::CHROME_SERVICE:
    case webapps::WebappInstallSource::KIOSK:
    case webapps::WebappInstallSource::MICROSOFT_365_SETUP:
    case webapps::WebappInstallSource::PROFILE_MENU:
    case webapps::WebappInstallSource::ALMANAC_INSTALL_APP_URI:
    case webapps::WebappInstallSource::OOBE_APP_RECOMMENDATIONS:
    case webapps::WebappInstallSource::WEB_INSTALL:
    case webapps::WebappInstallSource::CHROMEOS_HELP_APP:
    case webapps::WebappInstallSource::MIGRATION:
      return apps::InstallSource::kBrowser;
    case webapps::WebappInstallSource::ARC:
      return apps::InstallSource::kPlayStore;
    case webapps::WebappInstallSource::INTERNAL_DEFAULT:
    case webapps::WebappInstallSource::EXTERNAL_DEFAULT:
    case webapps::WebappInstallSource::EXTERNAL_LOCK_SCREEN:
    case webapps::WebappInstallSource::SYSTEM_DEFAULT:
    case webapps::WebappInstallSource::PRELOADED_OEM:
    case webapps::WebappInstallSource::PRELOADED_DEFAULT:
      return apps::InstallSource::kSystem;
    case webapps::WebappInstallSource::SYNC:
    case webapps::WebappInstallSource::WEBAPK_RESTORE:
      return apps::InstallSource::kSync;
  }
}

apps::Readiness ConvertWebappUninstallSourceToReadiness(
    webapps::WebappUninstallSource source) {
  switch (source) {
    case webapps::WebappUninstallSource::kUnknown:
    case webapps::WebappUninstallSource::kAppMenu:
    case webapps::WebappUninstallSource::kAppsPage:
    case webapps::WebappUninstallSource::kOsSettings:
    case webapps::WebappUninstallSource::kSync:
    case webapps::WebappUninstallSource::kAppManagement:
    case webapps::WebappUninstallSource::kAppList:
    case webapps::WebappUninstallSource::kShelf:
    case webapps::WebappUninstallSource::kPlaceholderReplacement:
    case webapps::WebappUninstallSource::kArc:
    case webapps::WebappUninstallSource::kSubApp:
    case webapps::WebappUninstallSource::kStartupCleanup:
    case webapps::WebappUninstallSource::kParentUninstall:
    case webapps::WebappUninstallSource::kTestCleanup:
    case webapps::WebappUninstallSource::kDevtools:
    case webapps::WebappUninstallSource::kAppMigration:
      return apps::Readiness::kUninstalledByUser;
    case webapps::WebappUninstallSource::kUninstallAndReplaceMigration:
    case webapps::WebappUninstallSource::kInternalPreinstalled:
    case webapps::WebappUninstallSource::kExternalPreinstalled:
    case webapps::WebappUninstallSource::kExternalPolicy:
    case webapps::WebappUninstallSource::kSystemPreinstalled:
    case webapps::WebappUninstallSource::kExternalLockScreen:
    case webapps::WebappUninstallSource::kInstallUrlDeduping:
    case webapps::WebappUninstallSource::kHealthcareUserInstallCleanup:
    case webapps::WebappUninstallSource::kIwaEnterprisePolicy:
    case webapps::WebappUninstallSource::kIwaBlocklisted:
      return apps::Readiness::kUninstalledByNonUser;
  }
}

bool IsNoteTakingWebApp(const WebApp& web_app) {
  return web_app.note_taking_new_note_url().is_valid();
}

bool IsLockScreenCapable(const WebApp& web_app) {
  if (!base::FeatureList::IsEnabled(features::kWebLockScreenApi)) {
    return false;
  }
  return web_app.lock_screen_start_url().is_valid();
}

apps::IntentFilterPtr CreateMimeTypeShareFilter(
    const std::vector<std::string>& mime_types) {
  DCHECK(!mime_types.empty());
  auto intent_filter = std::make_unique<apps::IntentFilter>();

  std::vector<apps::ConditionValuePtr> action_condition_values;
  action_condition_values.push_back(std::make_unique<apps::ConditionValue>(
      apps_util::kIntentActionSend, apps::PatternMatchType::kLiteral));
  auto action_condition = std::make_unique<apps::Condition>(
      apps::ConditionType::kAction, std::move(action_condition_values));
  intent_filter->conditions.push_back(std::move(action_condition));

  std::vector<apps::ConditionValuePtr> condition_values;
  for (auto& mime_type : mime_types) {
    condition_values.push_back(std::make_unique<apps::ConditionValue>(
        mime_type, apps::PatternMatchType::kMimeType));
  }
  auto mime_condition = std::make_unique<apps::Condition>(
      apps::ConditionType::kMimeType, std::move(condition_values));
  intent_filter->conditions.push_back(std::move(mime_condition));

  return intent_filter;
}

apps::IntentFilterPtr CreateIntentFilterFromOrigin(
    const url::Origin& origin,
    const GURL& extended_scope,
    bool add_subdomain_wildcard) {
  CHECK(!origin.opaque());

  auto intent_filter = std::make_unique<apps::IntentFilter>();

  intent_filter->AddSingleValueCondition(apps::ConditionType::kAction,
                                         apps_util::kIntentActionView,
                                         apps::PatternMatchType::kLiteral);

  intent_filter->AddSingleValueCondition(apps::ConditionType::kScheme,
                                         origin.scheme(),
                                         apps::PatternMatchType::kLiteral);

  std::string authority = apps_util::AuthorityView::Encode(origin);
  if (add_subdomain_wildcard) {
    DCHECK(!base::StartsWith(authority, "."));
    authority = '.' + authority;
  }
  intent_filter->AddSingleValueCondition(
      apps::ConditionType::kAuthority, authority,
      add_subdomain_wildcard ? apps::PatternMatchType::kSuffix
                             : apps::PatternMatchType::kLiteral);

  intent_filter->AddSingleValueCondition(apps::ConditionType::kPath,
                                         extended_scope.GetPath(),
                                         apps::PatternMatchType::kPrefix);

  return intent_filter;
}

apps::IntentFilters CreateIntentFiltersFromScopeExtensionInfo(
    const web_app::ScopeExtensionInfo& scope_extension_info) {
  apps::IntentFilters filters;
  filters.push_back(CreateIntentFilterFromOrigin(
      scope_extension_info.origin, scope_extension_info.scope,
      /*add_subdomain_wildcard=*/false));
  if (scope_extension_info.has_origin_wildcard) {
    // In addition to matching the exact same origin, the wildcard should match
    // subdomains.
    filters.push_back(CreateIntentFilterFromOrigin(
        scope_extension_info.origin, scope_extension_info.scope,
        /*add_subdomain_wildcard=*/true));
  }
  return filters;
}

apps::IntentFilters CreateIntentFiltersFromProtocolHandlers(
    const std::vector<custom_handlers::ProtocolHandler>& protocol_handlers) {
  apps::IntentFilters filters;
  for (const auto& handler : protocol_handlers) {
    auto intent_filter = std::make_unique<apps::IntentFilter>();
    intent_filter->AddSingleValueCondition(apps::ConditionType::kAction,
                                           apps_util::kIntentActionView,
                                           apps::PatternMatchType::kLiteral);
    intent_filter->AddSingleValueCondition(apps::ConditionType::kScheme,
                                           handler.protocol(),
                                           apps::PatternMatchType::kLiteral);
    filters.push_back(std::move(intent_filter));
  }
  return filters;
}

apps::IntentFilters CreateShareIntentFiltersFromShareTarget(
    const apps::ShareTarget& share_target) {
  apps::IntentFilters filters;

  if (!share_target.params.text.empty()) {
    // The share target accepts navigator.share() calls with text.
    filters.push_back(CreateMimeTypeShareFilter({kTextPlain}));
  }

  std::vector<std::string> content_types;
  for (const auto& files_entry : share_target.params.files) {
    for (const auto& file_type : files_entry.accept) {
      // Skip any file_type that is not a MIME type.
      if (file_type.empty() || file_type[0] == '.' ||
          std::ranges::count(file_type, '/') != 1) {
        continue;
      }

      content_types.push_back(file_type);
    }
  }

  if (!content_types.empty()) {
    const std::vector<std::string> intent_actions(
        {apps_util::kIntentActionSend, apps_util::kIntentActionSendMultiple});
    filters.push_back(
        apps_util::CreateFileFilter(intent_actions, content_types, {}));
  }

  return filters;
}

apps::IntentFilters CreateIntentFiltersFromFileHandlers(
    const apps::FileHandlers& file_handlers) {
  apps::IntentFilters filters;
  for (const apps::FileHandler& handler : file_handlers) {
    std::vector<std::string> mime_types;
    std::vector<std::string> file_extensions;
    std::string action_url = handler.action.spec();
    // TODO(petermarshall): Use GetFileExtensionsFromFileHandlers /
    // GetMimeTypesFromFileHandlers?
    for (const apps::FileHandler::AcceptEntry& accept_entry : handler.accept) {
      mime_types.push_back(accept_entry.mime_type);
      for (const std::string& extension : accept_entry.file_extensions) {
        file_extensions.push_back(extension);
      }
    }
    filters.push_back(
        apps_util::CreateFileFilter({apps_util::kIntentActionView}, mime_types,
                                    file_extensions, action_url));
  }

  return filters;
}


}  // namespace

void UninstallImpl(WebAppProvider* provider,
                   const std::string& app_id,
                   apps::UninstallSource uninstall_source,
                   gfx::NativeWindow parent_window) {
  if (!provider) {
    return;
  }

  if (provider->registrar_unsafe().CanUserUninstallWebApp(app_id)) {
    webapps::WebappUninstallSource webapp_uninstall_source =
        ConvertUninstallSourceToWebAppUninstallSource(uninstall_source);
    provider->ui_manager().PresentUserUninstallDialog(
        app_id, webapp_uninstall_source, parent_window, base::DoNothing());
  }
}

WebAppPublisherHelper::Delegate::Delegate() = default;

WebAppPublisherHelper::Delegate::~Delegate() = default;


WebAppPublisherHelper::WebAppPublisherHelper(Profile* profile,
                                             WebAppProvider* provider,
                                             Delegate* delegate)
    : profile_(profile), provider_(provider), delegate_(delegate) {
  DCHECK(profile_);
  DCHECK(delegate_);
  Init();
}

WebAppPublisherHelper::~WebAppPublisherHelper() = default;

// static
bool WebAppPublisherHelper::IsSupportedWebAppPermissionType(
    ContentSettingsType permission_type) {
  return std::ranges::contains(kSupportedPermissionTypes, permission_type);
}

void WebAppPublisherHelper::Shutdown() {
  registrar_observation_.Reset();
  content_settings_observation_.Reset();
  is_shutting_down_ = true;
}

void WebAppPublisherHelper::SetWebAppShowInFields(const WebApp* web_app,
                                                  apps::App& app) {
  if (web_app->chromeos_data().has_value()) {
    auto& chromeos_data = web_app->chromeos_data().value();
    bool should_show_app = true;
    // TODO(b/201422755): Remove Web app specific hiding for demo mode once icon
    // load fixed.
    app.show_in_launcher = chromeos_data.show_in_launcher && should_show_app;
    app.show_in_shelf = app.show_in_search =
        chromeos_data.show_in_search_and_shelf && should_show_app;
    app.show_in_management = chromeos_data.show_in_management;
    app.handles_intents =
        chromeos_data.handles_file_open_intents ? true : app.show_in_launcher;
    return;
  }

  // Show the app everywhere by default.
  app.show_in_launcher = true;
  app.show_in_shelf = true;
  app.show_in_search = true;
  app.show_in_management = true;
  app.handles_intents = true;
}

apps::Permissions WebAppPublisherHelper::CreatePermissions(
    const WebApp* web_app) {
  apps::Permissions permissions;

  const GURL& url = web_app->start_url();
  auto* host_content_settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile());
  DCHECK(host_content_settings_map);

  for (ContentSettingsType type : kSupportedPermissionTypes) {
    ContentSetting setting =
        host_content_settings_map->GetContentSetting(url, url, type);

    // Map ContentSettingsType to an apps::TriState value
    apps::TriState setting_val;
    switch (setting) {
      case CONTENT_SETTING_ALLOW:
        setting_val = apps::TriState::kAllow;
        break;
      case CONTENT_SETTING_ASK:
        setting_val = apps::TriState::kAsk;
        break;
      case CONTENT_SETTING_BLOCK:
        setting_val = apps::TriState::kBlock;
        break;
      default:
        setting_val = apps::TriState::kAsk;
    }

    content_settings::SettingInfo setting_info;
    host_content_settings_map->GetWebsiteSetting(url, url, type, &setting_info);

    permissions.push_back(std::make_unique<apps::Permission>(
        GetPermissionType(type), setting_val,
        /*is_managed=*/setting_info.source ==
            content_settings::SettingSource::kPolicy));
  }

  // File handling permission.
  permissions.push_back(std::make_unique<apps::Permission>(
      apps::PermissionType::kFileHandling,
      !registrar().IsAppFileHandlerPermissionBlocked(web_app->app_id()),
      /*is_managed=*/false));

  return permissions;
}

// static
apps::IntentFilters WebAppPublisherHelper::CreateIntentFiltersForWebApp(
    const WebAppProvider& provider,
    const web_app::WebApp& app) {
  apps::IntentFilters filters;

  GURL app_scope = provider.registrar_unsafe().GetAppScope(app.app_id());
  if (!app_scope.is_empty()) {
    filters.push_back(apps_util::MakeIntentFilterForUrlScope(app_scope));
  }

  for (const ScopeExtensionInfo& scope_extension_info :
       app.validated_scope_extensions()) {
    base::Extend(filters, CreateIntentFiltersFromScopeExtensionInfo(
                              scope_extension_info));
  }


  if (app.share_target()) {
    base::Extend(filters,
                 CreateShareIntentFiltersFromShareTarget(*app.share_target()));
  }

  // TODO(crbug.com/458291386): Launch protocol handlers for all PWAs (and not
  // just IWAs) on ChromeOS -- this requires some additional UX work.
  if (provider.registrar_unsafe().AppMatches(
          app.app_id(),
          WebAppFilter::IsIsolatedApp() | WebAppFilter::IsIsolatedSubApp())) {
    // Includes all protocol handlers except for the ones that the user has
    // explicitly disallowed.
    const std::vector<custom_handlers::ProtocolHandler> protocol_handlers =
        provider.os_integration_manager().GetAppProtocolHandlers(app.app_id());
    base::Extend(filters,
                 CreateIntentFiltersFromProtocolHandlers(protocol_handlers));
  }

  const apps::FileHandlers* enabled_file_handlers =
      provider.os_integration_manager().GetEnabledFileHandlers(app.app_id());
  if (enabled_file_handlers) {
    base::Extend(filters,
                 CreateIntentFiltersFromFileHandlers(*enabled_file_handlers));
  }


  return filters;
}

apps::AppPtr WebAppPublisherHelper::CreateWebApp(const WebApp* web_app) {
  DCHECK(!IsShuttingDown());

  apps::Readiness readiness;

  switch (web_app->install_state()) {
    case proto::InstallState::INSTALLED_WITH_OS_INTEGRATION:
    case proto::InstallState::INSTALLED_WITHOUT_OS_INTEGRATION:
      readiness =
          (web_app->is_uninstalling() ? apps::Readiness::kUninstalledByUser
                                      : apps::Readiness::kReady);
      break;
    case proto::InstallState::SUGGESTED_FROM_ANOTHER_DEVICE:
    case proto::InstallState::SUGGESTED_FROM_MIGRATION:
      readiness = apps::Readiness::kDisabledByUser;
  }


  auto app = apps::AppPublisher::MakeApp(
      apps::AppType::kWeb, web_app->app_id(), readiness,
      provider_->registrar_unsafe().GetAppShortName(web_app->app_id()),
      GetHighestPriorityInstallReason(web_app),
      GetInstallSource(provider_->registrar_unsafe().GetLatestAppInstallSource(
          web_app->app_id())));

  app->description =
      provider_->registrar_unsafe().GetAppDescription(web_app->app_id());
  if (provider_->registrar_unsafe().AppMatches(web_app->app_id(),
                                               WebAppFilter::IsIsolatedApp())) {
    // Show the version of Isolated Web App in ChromeOS Settings
    app->version = web_app->isolation_data()->version().GetString();
  }

  app->additional_search_terms = web_app->additional_search_terms();

  // Web App's publisher_id the start url.
  app->publisher_id = web_app->start_url().spec();
  app->installer_package_id = GetPackageId(*web_app);

  app->icon_key = apps::IconKey(GetIconEffects(web_app));

  app->last_launch_time = web_app->last_launch_time();
  app->install_time = web_app->first_install_time();

  // For system web apps and shimless RMA IWAs (only), the install source is
  // `kSystem`.
  DCHECK_EQ(web_app->IsSystemApp() || web_app->IsIwaShimlessRmaApp(),
            app->install_reason == apps::InstallReason::kSystem)
      << base::ToString(app->install_reason);

  app->policy_ids = WebAppPolicyManager::GetPolicyIds(profile(), *web_app);

  app->permissions = CreatePermissions(web_app);

  // Isolated web apps can only be opened in window.
  app->allow_window_mode_selection = !provider_->registrar_unsafe().AppMatches(
      web_app->app_id(),
      WebAppFilter::IsIsolatedApp() | WebAppFilter::IsIsolatedSubApp());

  SetWebAppShowInFields(web_app, *app);

  app->has_badge = false;

  app->allow_uninstall = web_app->CanUserUninstallWebApp();

  app->paused = false;

  // Add the intent filters for PWAs.
  app->intent_filters = CreateIntentFiltersForWebApp(*provider_, *web_app);

  // These filters are used by the settings page to display would-be-handled
  // extensions even when the feature is not enabled for the app, whereas
  // `GetEnabledFileHandlers` above only returns the ones that currently are
  // enabled.
  const apps::FileHandlers* all_file_handlers =
      registrar().GetAppFileHandlers(web_app->app_id());
  if (all_file_handlers && !all_file_handlers->empty()) {
    std::set<std::string> extensions_set =
        apps::GetFileExtensionsFromFileHandlers(*all_file_handlers);
    app->intent_filters->push_back(apps_util::CreateFileFilter(
        {apps_util::kIntentActionPotentialFileHandler},
        /*mime_types=*/{},
        /*file_extensions=*/
        {extensions_set.begin(), extensions_set.end()}));
  }

  if (IsNoteTakingWebApp(*web_app)) {
    app->intent_filters->push_back(apps_util::CreateNoteTakingFilter());
  }

  if (IsLockScreenCapable(*web_app)) {
    app->intent_filters->push_back(apps_util::CreateLockScreenFilter());
  }


  app->window_mode = ConvertDisplayModeToWindowMode(
      registrar().GetAppEffectiveDisplayMode(web_app->app_id()));

  const auto login_mode = registrar().GetAppRunOnOsLoginMode(web_app->app_id());
  app->run_on_os_login = apps::RunOnOsLogin(
      ConvertOsLoginMode(login_mode.value), !login_mode.user_controllable);

  app->allow_close = !registrar().IsPreventCloseEnabled(web_app->app_id());

  for (const auto& shortcut : web_app->shortcuts_menu_item_infos()) {
    const std::string name = base::UTF16ToUTF8(shortcut.name);
    std::string shortcut_id = GenerateShortcutId();
    StoreShortcutId(shortcut_id, shortcut);
  }

  return app;
}

apps::AppPtr WebAppPublisherHelper::ConvertUninstalledWebApp(
    const webapps::AppId& app_id,
    webapps::WebappUninstallSource uninstall_source) {
  auto app = std::make_unique<apps::App>(apps::AppType::kWeb, app_id);
  app->readiness = ConvertWebappUninstallSourceToReadiness(uninstall_source);

  return app;
}

apps::AppPtr WebAppPublisherHelper::ConvertLaunchedWebApp(
    const WebApp* web_app) {
  auto app =
      std::make_unique<apps::App>(apps::AppType::kWeb, web_app->app_id());
  app->last_launch_time = web_app->last_launch_time();
  return app;
}

void WebAppPublisherHelper::UninstallWebApp(
    const WebApp* web_app,
    apps::UninstallSource uninstall_source,
    bool clear_site_data,
    bool report_abuse) {
  if (IsShuttingDown()) {
    return;
  }

  auto origin = url::Origin::Create(web_app->start_url());

  DCHECK(provider_);
  DCHECK(
      provider_->registrar_unsafe().CanUserUninstallWebApp(web_app->app_id()));
  webapps::WebappUninstallSource webapp_uninstall_source =
      ConvertUninstallSourceToWebAppUninstallSource(uninstall_source);
  provider_->scheduler().RemoveUserUninstallableManagements(
      web_app->app_id(), webapp_uninstall_source, base::DoNothing());
  web_app = nullptr;

  if (!clear_site_data) {
    return;
  }

  // Off the record profiles cannot be 'kept alive'.
  std::unique_ptr<ScopedProfileKeepAlive> profile_keep_alive =
      profile_->IsOffTheRecord()
          ? nullptr
          : std::make_unique<ScopedProfileKeepAlive>(
                profile_, ProfileKeepAliveOrigin::kWebAppUninstall);
  // Ensure profile is kept alive until ClearSiteData is done.
  auto callback = base::DoNothingWithBoundArgs(std::move(profile_keep_alive));
  content::ClearSiteData(
      profile()->GetWeakPtr(),
      /*storage_partition_config=*/std::nullopt, origin,
      content::ClearSiteDataTypeSet::All(),
      /*storage_buckets_to_remove=*/{}, /*avoid_closing_connections=*/false,
      /*cookie_partition_key=*/std::nullopt,
      /*storage_key=*/std::nullopt,
      /*partitioned_state_allowed_only=*/false, std::move(callback));
}

void WebAppPublisherHelper::SetIconEffect(const std::string& app_id) {
  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    return;
  }

  auto app = std::make_unique<apps::App>(apps::AppType::kWeb, app_id);
  app->icon_key = apps::IconKey(GetIconEffects(web_app));
  delegate_->PublishWebApp(std::move(app));
}


void WebAppPublisherHelper::LoadIcon(const std::string& app_id,
                                     apps::IconType icon_type,
                                     int32_t size_hint_in_dip,
                                     apps::IconEffects icon_effects,
                                     apps::LoadIconCallback callback) {
  DCHECK(provider_);
  if (IsShuttingDown()) {
    return;
  }

  LoadIconFromWebApp(profile_, icon_type, size_hint_in_dip, app_id,
                     icon_effects, std::move(callback));
}

void WebAppPublisherHelper::Launch(
    const std::string& app_id,
    int32_t event_flags,
    apps::LaunchSource launch_source,
    apps::WindowInfoPtr window_info,
    base::OnceCallback<void(content::WebContents*)> on_complete) {
  if (IsShuttingDown()) {
    std::move(on_complete).Run(nullptr);
    return;
  }

  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    std::move(on_complete).Run(nullptr);
    return;
  }

  DisplayMode display_mode = registrar().GetAppEffectiveDisplayMode(app_id);

  apps::AppLaunchParams params = apps::CreateAppIdLaunchParamsWithEventFlags(
      web_app->app_id(), event_flags, launch_source,
      window_info ? window_info->display_id : display::kInvalidDisplayId,
      /*fallback_container=*/
      ConvertDisplayModeToAppLaunchContainer(display_mode));

  // The app will be launched for the currently active profile.
  LaunchAppWithParams(std::move(params), std::move(on_complete));
}

void WebAppPublisherHelper::LaunchAppWithFiles(
    const std::string& app_id,
    int32_t event_flags,
    apps::LaunchSource launch_source,
    std::vector<base::FilePath> file_paths) {
  if (IsShuttingDown()) {
    return;
  }

  DisplayMode display_mode = registrar().GetAppEffectiveDisplayMode(app_id);
  apps::AppLaunchParams params = apps::CreateAppIdLaunchParamsWithEventFlags(
      app_id, event_flags, launch_source, display::kInvalidDisplayId,
      /*fallback_container=*/
      ConvertDisplayModeToAppLaunchContainer(display_mode));
  params.launch_files = std::move(file_paths);
  LaunchAppWithFilesCheckingUserPermission(app_id, std::move(params),
                                           base::DoNothing());
}

void WebAppPublisherHelper::LaunchAppWithIntent(
    const std::string& app_id,
    int32_t event_flags,
    apps::IntentPtr intent,
    apps::LaunchSource launch_source,
    apps::WindowInfoPtr window_info,
    apps::LaunchCallback callback) {
  CHECK(intent);

  if (IsShuttingDown()) {
    std::move(callback).Run(apps::LaunchResult(apps::State::kFailed));
    return;
  }


  LaunchAppWithIntentImpl(
      app_id, event_flags, std::move(intent), launch_source,
      window_info ? window_info->display_id : display::kInvalidDisplayId,
      base::BindOnce(
          [](apps::LaunchCallback callback, apps::LaunchSource launch_source,
             std::vector<content::WebContents*> web_contentses) {
            std::move(callback).Run(
                apps::ConvertBoolToLaunchResult(!web_contentses.empty()));
          },
          std::move(callback), launch_source));
}

void WebAppPublisherHelper::LaunchAppWithParams(
    apps::AppLaunchParams params,
    base::OnceCallback<void(content::WebContents*)> on_complete) {
  if (IsShuttingDown()) {
    std::move(on_complete).Run(nullptr);
    return;
  }

  if (params.protocol_handler_launch_url) {
    LaunchAppFromProtocolCheckingUserPermission(std::move(params),
                                                std::move(on_complete));
    return;
  }

  apps::AppLaunchParams params_for_restore(
      params.app_id, params.container, params.disposition, params.override_url,
      params.launch_source, params.display_id, params.launch_files,
      params.intent);

  bool is_system_web_app = false;
  std::optional<GURL> override_url = std::nullopt;


  provider_->scheduler().LaunchAppWithCustomParams(
      std::move(params),
      base::BindOnce(&WebAppPublisherHelper::OnLaunchCompleted,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(params_for_restore), is_system_web_app,
                     override_url, std::move(on_complete)));
}

void WebAppPublisherHelper::SetPermission(const std::string& app_id,
                                          apps::PermissionPtr permission) {
  if (IsShuttingDown()) {
    return;
  }

  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    return;
  }

  if (permission->permission_type == apps::PermissionType::kFileHandling) {
    if (std::holds_alternative<bool>(permission->value)) {
      provider_->scheduler().PersistFileHandlersUserChoice(
          app_id, std::get<bool>(permission->value), base::DoNothing());
    }
    return;
  }

  auto* host_content_settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile_);
  DCHECK(host_content_settings_map);

  const GURL url = web_app->start_url();

  ContentSettingsType permission_type;

  if (!GetContentSettingsType(permission->permission_type, permission_type)) {
    return;
  }

  DCHECK(std::holds_alternative<apps::TriState>(permission->value));
  ContentSetting permission_value = CONTENT_SETTING_DEFAULT;
  switch (std::get<apps::TriState>(permission->value)) {
    case apps::TriState::kAllow:
      permission_value = CONTENT_SETTING_ALLOW;
      break;
    case apps::TriState::kAsk:
      permission_value = CONTENT_SETTING_ASK;
      break;
    case apps::TriState::kBlock:
      permission_value = CONTENT_SETTING_BLOCK;
      break;
    default:  // Return if value is invalid.
      return;
  }

  host_content_settings_map->SetContentSettingDefaultScope(
      url, url, permission_type, permission_value);
}

void WebAppPublisherHelper::OpenNativeSettings(const std::string& app_id) {
  if (IsShuttingDown()) {
    return;
  }

  provider_->ui_manager().ShowWebAppSettings(app_id);
}

apps::WindowMode WebAppPublisherHelper::GetWindowMode(
    const std::string& app_id) {
  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    return apps::WindowMode::kUnknown;
  }

  auto display_mode = registrar().GetAppEffectiveDisplayMode(web_app->app_id());
  return ConvertDisplayModeToWindowMode(display_mode);
}

void WebAppPublisherHelper::UpdateAppSize(const std::string& app_id) {
  const auto* web_app = GetWebApp(app_id);
  if (!web_app) {
    return;
  }

  provider_->scheduler().ComputeAppSize(
      app_id, base::BindOnce(&WebAppPublisherHelper::OnGetWebAppSize,
                             weak_ptr_factory_.GetWeakPtr(), app_id));
}

void WebAppPublisherHelper::SetWindowMode(const std::string& app_id,
                                          apps::WindowMode window_mode) {
  auto user_display_mode = mojom::UserDisplayMode::kStandalone;
  switch (window_mode) {
    case apps::WindowMode::kBrowser:
      user_display_mode = mojom::UserDisplayMode::kBrowser;
      break;
    case apps::WindowMode::kUnknown:
    case apps::WindowMode::kWindow:
      user_display_mode = mojom::UserDisplayMode::kStandalone;
      break;
    case apps::WindowMode::kTabbedWindow:
      user_display_mode = mojom::UserDisplayMode::kTabbed;
      break;
  }
  provider_->scheduler().SetUserDisplayMode(app_id, user_display_mode,
                                            base::DoNothing());
}

apps::WindowMode WebAppPublisherHelper::ConvertDisplayModeToWindowMode(
    blink::mojom::DisplayMode display_mode) {
  switch (display_mode) {
    case blink::mojom::DisplayMode::kUndefined:
      return apps::WindowMode::kUnknown;
    case blink::mojom::DisplayMode::kBrowser:
      return apps::WindowMode::kBrowser;
    case blink::mojom::DisplayMode::kTabbed:
      if (base::FeatureList::IsEnabled(blink::features::kDesktopPWAsTabStrip) &&
          base::FeatureList::IsEnabled(
              features::kDesktopPWAsTabStripSettings)) {
        return apps::WindowMode::kTabbedWindow;
      } else {
        [[fallthrough]];
      }
    case blink::mojom::DisplayMode::kMinimalUi:
    case blink::mojom::DisplayMode::kStandalone:
    case blink::mojom::DisplayMode::kFullscreen:
    case blink::mojom::DisplayMode::kWindowControlsOverlay:
    case blink::mojom::DisplayMode::kUnframed:
    case blink::mojom::DisplayMode::kPictureInPicture:
      return apps::WindowMode::kWindow;
  }
}

void WebAppPublisherHelper::PublishWindowModeUpdate(
    const std::string& app_id,
    blink::mojom::DisplayMode display_mode) {
  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    return;
  }

  auto app = std::make_unique<apps::App>(apps::AppType::kWeb, app_id);
  app->window_mode = ConvertDisplayModeToWindowMode(display_mode);
  delegate_->PublishWebApp(std::move(app));
}

void WebAppPublisherHelper::PublishRunOnOsLoginModeUpdate(
    const std::string& app_id,
    RunOnOsLoginMode run_on_os_login_mode) {
  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    return;
  }

  auto app = std::make_unique<apps::App>(apps::AppType::kWeb, app_id);
  const auto login_mode = registrar().GetAppRunOnOsLoginMode(app_id);
  app->run_on_os_login = apps::RunOnOsLogin(
      ConvertOsLoginMode(run_on_os_login_mode), !login_mode.user_controllable);
  delegate_->PublishWebApp(std::move(app));
}

std::string WebAppPublisherHelper::GenerateShortcutId() {
  return base::NumberToString(shortcut_id_generator_.GenerateNextId().value());
}

void WebAppPublisherHelper::StoreShortcutId(
    const std::string& shortcut_id,
    const WebAppShortcutsMenuItemInfo& menu_item_info) {
  shortcut_id_map_.emplace(shortcut_id, std::move(menu_item_info));
}

void WebAppPublisherHelper::ExecuteContextMenuCommand(
    const std::string& app_id,
    const std::string& shortcut_id,
    int64_t display_id,
    base::OnceCallback<void(content::WebContents*)> on_complete) {
  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    std::move(on_complete).Run(nullptr);
    return;
  }

  DisplayMode display_mode = registrar().GetAppEffectiveDisplayMode(app_id);

  apps::AppLaunchParams params(
      app_id, ConvertDisplayModeToAppLaunchContainer(display_mode),
      WindowOpenDisposition::CURRENT_TAB, apps::LaunchSource::kFromMenu,
      display_id);

  auto menu_item = shortcut_id_map_.find(shortcut_id);
  if (menu_item != shortcut_id_map_.end()) {
    params.override_url = menu_item->second.url;
  }

  LaunchAppWithParams(std::move(params), std::move(on_complete));
}

WebAppRegistrar& WebAppPublisherHelper::registrar() const {
  return provider_->registrar_unsafe();
}

WebAppInstallManager& WebAppPublisherHelper::install_manager() const {
  return provider_->install_manager();
}

bool WebAppPublisherHelper::IsShuttingDown() const {
  return is_shutting_down_;
}

void WebAppPublisherHelper::OnWebAppProtocolSettingsChanged(
    const webapps::AppId& app_id) {
  const WebApp* web_app = GetWebApp(app_id);
  if (web_app) {
    delegate_->PublishWebApp(CreateWebApp(web_app));
  }
}

void WebAppPublisherHelper::OnWebAppFileHandlerApprovalStateChanged(
    const webapps::AppId& app_id) {
  const WebApp* web_app = GetWebApp(app_id);
  if (web_app) {
    delegate_->PublishWebApp(CreateWebApp(web_app));
  }
}

void WebAppPublisherHelper::OnWebAppInstalled(const webapps::AppId& app_id) {
  const WebApp* web_app = GetWebApp(app_id);
  if (web_app) {
    auto app = CreateWebApp(web_app);
    // If the installation was a force reinstallation on top of an existing app,
    // the raw icon might have changed. Notify App Service to invalidate the
    // icon disk cache.
    app->icon_key->update_version = true;


    delegate_->PublishWebApp(std::move(app));

    // Todo(b:372661290): Extract custom link preference handling into a new
    // post web app install hook.
  }
}

void WebAppPublisherHelper::OnWebAppInstalledWithOsHooks(
    const webapps::AppId& app_id) {
  const WebApp* web_app = GetWebApp(app_id);
  if (web_app) {
    delegate_->PublishWebApp(CreateWebApp(web_app));
  }
}

void WebAppPublisherHelper::OnWebAppManifestUpdated(
    const webapps::AppId& app_id) {
  const WebApp* web_app = GetWebApp(app_id);
  if (web_app) {
    auto app = CreateWebApp(web_app);


    // The manifest updated might cause the app raw icon updated. So set
    // a new `raw_icon_data_version`, to remove the icon files saved in the
    // AppService icon directory, to get the new raw icon files of the web app
    // for AppService.
    app->icon_key->update_version = true;
    delegate_->PublishWebApp(std::move(app));

  }
}

void WebAppPublisherHelper::OnWebAppUninstalled(
    const webapps::AppId& app_id,
    webapps::WebappUninstallSource uninstall_source) {

  delegate_->PublishWebApp(ConvertUninstalledWebApp(app_id, uninstall_source));
}

void WebAppPublisherHelper::OnWebAppInstallManagerDestroyed() {
  install_manager_observation_.Reset();
}

void WebAppPublisherHelper::OnAppRegistrarDestroyed() {
  registrar_observation_.Reset();
}

void WebAppPublisherHelper::OnWebAppLastLaunchTimeChanged(
    const std::string& app_id,
    const base::Time& last_launch_time) {
  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    return;
  }

  delegate_->PublishWebApp(ConvertLaunchedWebApp(web_app));
}

void WebAppPublisherHelper::OnWebAppUserDisplayModeChanged(
    const webapps::AppId& app_id,
    mojom::UserDisplayMode user_display_mode) {
  // If the app that changed display mode is not registered in app service, it
  // is because this was considered as a shortcut and now considered as an app
  // due to display mode change, in this case we should publish the full app.
  if (apps::AppServiceProxyFactory::GetForProfile(profile_)
          ->AppRegistryCache()
          .IsAppInstalled(app_id)) {
    PublishWindowModeUpdate(app_id,
                            registrar().GetAppEffectiveDisplayMode(app_id));
  } else {
    const WebApp* web_app = GetWebApp(app_id);
    if (web_app) {
      delegate_->PublishWebApp(CreateWebApp(web_app));
    }
  }
}

void WebAppPublisherHelper::OnWebAppRunOnOsLoginModeChanged(
    const webapps::AppId& app_id,
    RunOnOsLoginMode run_on_os_login_mode) {
  PublishRunOnOsLoginModeUpdate(app_id, run_on_os_login_mode);
}



void WebAppPublisherHelper::OnContentSettingChanged(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsTypeSet content_type_set) {
  DCHECK(!IsShuttingDown());
  // If content_type is not one of the supported permissions, do nothing.
  if (!content_type_set.ContainsAllTypes() &&
      !IsSupportedWebAppPermissionType(content_type_set.GetType())) {
    return;
  }

  for (const WebApp& web_app : registrar().GetApps()) {
    if (primary_pattern.Matches(web_app.start_url())) {
      auto app =
          std::make_unique<apps::App>(apps::AppType::kWeb, web_app.app_id());
      app->permissions = CreatePermissions(&web_app);
      delegate_->PublishWebApp(std::move(app));
    }
  }
}

void WebAppPublisherHelper::OnWebAppSettingsPolicyChanged() {
  DCHECK(!IsShuttingDown());

  for (const WebApp& web_app : registrar().GetApps()) {
    delegate_->PublishWebApp(CreateWebApp(&web_app));
  }
}

void WebAppPublisherHelper::Init() {
  // Allow for web app migration tests.
  // In some tests, WebAppPublisherHelper could be created during the shutdown
  // stage as the web app publisher is created async by AppServiceProxy. So
  // provider_ could be null in some tests.
  if (!AreWebAppsEnabled(profile_) || !provider_) {
    return;
  }

  provider_->on_registry_ready().Post(
      FROM_HERE, base::BindOnce(&WebAppPublisherHelper::ObserveWebAppSubsystems,
                                weak_ptr_factory_.GetWeakPtr()));

  content_settings_observation_.Observe(
      HostContentSettingsMapFactory::GetForProfile(profile_));


}

void WebAppPublisherHelper::ObserveWebAppSubsystems() {
  install_manager_observation_.Observe(&install_manager());
  registrar_observation_.Observe(&registrar());
}

IconEffects WebAppPublisherHelper::GetIconEffects(const WebApp* web_app) {
  IconEffects icon_effects = IconEffects::kRoundCorners;
  if (web_app->install_state() ==
      proto::InstallState::SUGGESTED_FROM_ANOTHER_DEVICE) {
    icon_effects |= IconEffects::kBlocked;
  }

  icon_effects |= web_app->is_generated_icon() ? IconEffects::kCrOsStandardMask
                                               : IconEffects::kCrOsStandardIcon;


  bool is_disabled = false;
  if (web_app->chromeos_data().has_value()) {
    is_disabled = web_app->chromeos_data()->is_disabled;
  }
  if (is_disabled) {
    icon_effects |= IconEffects::kBlocked;
  }

  return icon_effects;
}

const WebApp* WebAppPublisherHelper::GetWebApp(
    const webapps::AppId& app_id) const {
  return registrar().GetAppById(app_id);
}

void WebAppPublisherHelper::LaunchAppWithIntentImpl(
    const std::string& app_id,
    int32_t event_flags,
    apps::IntentPtr intent,
    apps::LaunchSource launch_source,
    int64_t display_id,
    base::OnceCallback<void(std::vector<content::WebContents*>)> callback) {
  bool is_file_handling_launch =
      intent && !intent->files.empty() && !intent->IsShareIntent();
  auto params = apps::CreateAppLaunchParamsForIntent(
      app_id, event_flags, launch_source, display_id,
      ConvertDisplayModeToAppLaunchContainer(
          registrar().GetAppEffectiveDisplayMode(app_id)),
      std::move(intent), profile_);
  if (is_file_handling_launch) {
    LaunchAppWithFilesCheckingUserPermission(app_id, std::move(params),
                                             std::move(callback));
    return;
  }

  LaunchAppWithParams(
      std::move(params),
      base::BindOnce(
          [](base::OnceCallback<void(std::vector<content::WebContents*>)>
                 callback,
             content::WebContents* contents) {
            // These calls are piped through LaunchWebAppCommand and can end
            // early during an Abort due to various reasons (like
            // FirstRunService not completed), in which case there will be no
            // web contents.
            if (contents) {
              std::move(callback).Run({contents});
            } else {
              std::move(callback).Run({});
            }
          },
          std::move(callback)));
}

apps::PackageId WebAppPublisherHelper::GetPackageId(
    const WebApp& web_app) const {
  return apps::PackageId(apps::PackageType::kWeb, web_app.manifest_id().spec());
}


void WebAppPublisherHelper::LaunchAppWithFilesCheckingUserPermission(
    const std::string& app_id,
    apps::AppLaunchParams params,
    base::OnceCallback<void(std::vector<content::WebContents*>)> callback) {
  std::vector<base::FilePath> file_paths = params.launch_files;
  auto launch_callback =
      base::BindOnce(&WebAppPublisherHelper::OnFileHandlerDialogCompleted,
                     weak_ptr_factory_.GetWeakPtr(), app_id, std::move(params),
                     std::move(callback));

  if (std::ranges::all_of(file_paths, [this, &app_id](auto file) {
        std::optional<std::string> file_extension_string;
        file_extension_string = file.Extension();
        return provider_->registrar_unsafe().GetAppFileHandlerApprovalState(
                   app_id, file_extension_string) == ApiApprovalState::kAllowed;
      })) {
    return std::move(launch_callback)
        .Run(/*allowed=*/true, /*remember_user_choice=*/false);
  }

  CHECK_EQ(
      provider_->registrar_unsafe().GetAppFileHandlerUserApprovalState(app_id),
      ApiApprovalState::kRequiresPrompt);

  return provider_->ui_manager().ShowWebAppFileLaunchDialog(
      file_paths, app_id, std::move(launch_callback));
}

void WebAppPublisherHelper::LaunchAppFromProtocolCheckingUserPermission(
    apps::AppLaunchParams params,
    base::OnceCallback<void(content::WebContents*)> callback) {
  CHECK(params.protocol_handler_launch_url);
  std::string app_id = params.app_id;
  GURL protocol_url = *params.protocol_handler_launch_url;

  WebAppRegistrar& registrar = provider_->registrar_unsafe();
  const std::string scheme = protocol_url.GetScheme();
  if (!registrar.IsRegisteredLaunchProtocol(app_id, scheme) ||
      registrar.IsDisallowedLaunchProtocol(app_id, scheme)) {
    std::move(callback).Run(nullptr);
    return;
  }

  if (registrar.IsAllowedLaunchProtocol(params.app_id, scheme)) {
    OnProtocolHandlerDialogCompleted(std::move(params), std::move(callback),
                                     /*allowed=*/true,
                                     /*remember_user_choice=*/false);
    return;
  }


  provider_->ui_manager().ShowWebAppProtocolLaunchDialog(
      protocol_url, app_id,
      base::BindOnce(&WebAppPublisherHelper::OnProtocolHandlerDialogCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(params),
                     std::move(callback)));
}

void WebAppPublisherHelper::OnFileHandlerDialogCompleted(
    std::string app_id,
    apps::AppLaunchParams params,
    base::OnceCallback<void(std::vector<content::WebContents*>)> callback,
    bool allowed,
    bool remember_user_choice) {
  if (remember_user_choice) {
    provider_->scheduler().PersistFileHandlersUserChoice(app_id, allowed,
                                                         base::DoNothing());
  }

  if (!allowed) {
    std::move(callback).Run({});
    return;
  }

  // System web apps behave differently than when launching a normal PWA with
  // the File Handling API. Per the web spec, PWAs require that the extension
  // matches what's specified in the manifest. System apps rely on MIME type
  // sniffing to work even when the extensions don't match. For this reason,
  // `GetMatchingFileHandlerUrls` and therefore multilaunch won't work for
  // system apps.
  const WebApp* web_app = GetWebApp(params.app_id);
  bool can_multilaunch = !(web_app && web_app->IsSystemApp());
  base::ConcurrentCallbacks<content::WebContents*> concurrent;

  if (can_multilaunch) {
    WebAppFileHandlerManager::LaunchInfos file_launch_infos =
        provider_->os_integration_manager()
            .file_handler_manager()
            .GetMatchingFileHandlerUrls(app_id, params.launch_files);
    for (const auto& [url, files] : file_launch_infos) {
      apps::AppLaunchParams params_for_file_launch(
          app_id, params.container, params.disposition, params.launch_source,
          params.display_id, files, nullptr);
      params_for_file_launch.override_url = url;
      LaunchAppWithParams(std::move(params_for_file_launch),
                          concurrent.CreateCallback());
    }
  } else {
    apps::AppLaunchParams params_for_file_launch(
        app_id, params.container, params.disposition, params.launch_source,
        params.display_id, params.launch_files, params.intent);
    // For system web apps, the URL is calculated by the file browser and passed
    // in the intent.
    // TODO(crbug.com/40203246): remove this check. It's only here to support
    // tests that haven't been updated.
    if (params.intent) {
      params_for_file_launch.override_url = GURL(*params.intent->activity_name);
    }
    LaunchAppWithParams(std::move(params_for_file_launch),
                        concurrent.CreateCallback());
  }

  std::move(concurrent).Done(std::move(callback));
}

void WebAppPublisherHelper::OnProtocolHandlerDialogCompleted(
    apps::AppLaunchParams params,
    base::OnceCallback<void(content::WebContents*)> on_complete,
    bool allowed,
    bool remember_user_choice) {
  if (remember_user_choice) {
    ApiApprovalState approval_state =
        allowed ? ApiApprovalState::kAllowed : ApiApprovalState::kDisallowed;
    provider_->scheduler().UpdateProtocolHandlerUserApproval(
        params.app_id, params.protocol_handler_launch_url->GetScheme(),
        approval_state, base::DoNothing());
  }
  if (!allowed) {
    std::move(on_complete).Run(nullptr);
    return;
  }
  provider_->scheduler().LaunchAppWithCustomParams(
      std::move(params),
      base::BindOnce([](base::WeakPtr<Browser>,
                        base::WeakPtr<content::WebContents> web_contents,
                        apps::LaunchContainer) {
        return web_contents.get();
      }).Then(std::move(on_complete)));
}

void WebAppPublisherHelper::OnLaunchCompleted(
    apps::AppLaunchParams params_for_restore,
    bool is_system_web_app,
    std::optional<GURL> override_url,
    base::OnceCallback<void(content::WebContents*)> on_complete,
    base::WeakPtr<Browser> browser,
    base::WeakPtr<content::WebContents> web_contents,
    apps::LaunchContainer container) {

  std::move(on_complete).Run(web_contents.get());
}

void WebAppPublisherHelper::OnGetWebAppSize(
    webapps::AppId app_id,
    std::optional<ComputedAppSizeWithOrigin> size) {
  auto app = std::make_unique<apps::App>(apps::AppType::kWeb, app_id);
  if (!size.has_value()) {
    return;
  }
  app->app_size_in_bytes = size->app_size_in_bytes();
  app->data_size_in_bytes = size->data_size_in_bytes();
  delegate_->PublishWebApp(std::move(app));
}

}  // namespace web_app
