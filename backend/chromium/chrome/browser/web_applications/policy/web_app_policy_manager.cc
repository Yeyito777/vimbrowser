// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/policy/web_app_policy_manager.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check_deref.h"
#include "base/containers/extend.h"
#include "base/containers/fixed_flat_map.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/containers/map_util.h"
#include "base/containers/to_vector.h"
#include "base/feature_list.h"
#include "base/features.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/functional/concurrent_closures.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/syslog_logging.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/external_install_options.h"
#include "chrome/browser/web_applications/isolated_web_apps/policy/isolated_web_app_external_install_options.h"
#include "chrome/browser/web_applications/mojom/user_display_mode.mojom.h"
#include "chrome/browser/web_applications/os_integration/os_integration_manager.h"
#include "chrome/browser/web_applications/os_integration/os_integration_sub_manager.h"
#include "chrome/browser/web_applications/policy/pre_redirection_url_observer.h"
#include "chrome/browser/web_applications/policy/web_app_policy_constants.h"
#include "chrome/browser/web_applications/proto/web_app_install_state.pb.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_app_constants.h"
#include "chrome/browser/web_applications/web_app_filter.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_install_utils.h"
#include "chrome/browser/web_applications/web_app_management_type.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_sync_bridge.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "components/crx_file/id_util.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/webapps/browser/install_result_code.h"
#include "components/webapps/common/web_app_id.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_set.h"
#include "third_party/blink/public/common/manifest/manifest.h"
#include "url/url_constants.h"


namespace {

bool IconInfosContainIconURL(const std::vector<apps::IconInfo>& icon_infos,
                             const GURL& url) {
  for (const apps::IconInfo& info : icon_infos) {
    if (info.url.EqualsIgnoringRef(url)) {
      return true;
    }
  }
  return false;
}

bool IsForceUnregistrationPolicyEnabled() {
  return base::FeatureList::IsEnabled(
      web_app::kDesktopPWAsForceUnregisterOSIntegration);
}


std::optional<base::flat_map<std::string_view, std::string_view>>&
GetPreinstalledWebAppsMappingForTesting() {
  static base::NoDestructor<
      std::optional<base::flat_map<std::string_view, std::string_view>>>
      preinstalled_web_apps_mapping_for_testing;
  return *preinstalled_web_apps_mapping_for_testing;
}

}  // namespace

namespace web_app {

BASE_FEATURE(kDesktopPWAsForceUnregisterOSIntegration,
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
);

const char WebAppPolicyManager::kInstallResultHistogramName[];

WebAppPolicyManager::WebAppPolicyManager(Profile* profile)
    : profile_(profile),
      pref_service_(profile_->GetPrefs()),
      effective_web_apps_user_installable_policy_(
          pref_service_->GetBoolean(prefs::kWebAppInstallByUserEnabled)) {}

WebAppPolicyManager::~WebAppPolicyManager() = default;


void WebAppPolicyManager::SetProvider(base::PassKey<WebAppProvider>,
                                      WebAppProvider& provider) {
  provider_ = &provider;
}

void WebAppPolicyManager::Start(
    base::OnceClosure policy_settings_and_force_installs_applied) {
  DCHECK(policy_settings_and_force_installs_applied_.is_null());

  policy_settings_and_force_installs_applied_ =
      std::move(policy_settings_and_force_installs_applied);
  if (base::FeatureList::IsEnabled(
          base::features::kScopedBestEffortExecutionFenceForTaskQueue)) {
    // ScopedBestEffortExecutionFenceForTaskQueue can delay the execution of
    // BEST_EFFORT tasks for a longer period after startup. The policy for
    // force-installed apps must be available quickly so with this feature the
    // registry should be initialized immediately.
    InitChangeRegistrarAndRefreshPolicy();
  } else {
    content::GetUIThreadTaskRunner({base::TaskPriority::BEST_EFFORT})
        ->PostTask(
            FROM_HERE,
            base::BindOnce(
                &WebAppPolicyManager::InitChangeRegistrarAndRefreshPolicy,
                weak_ptr_factory_.GetWeakPtr()));
  }
}

void WebAppPolicyManager::Shutdown() {
  weak_ptr_factory_.InvalidateWeakPtrs();
}

void WebAppPolicyManager::ReinstallPlaceholderAppIfNecessary(
    const GURL& url,
    ExternallyManagedAppManager::OnceInstallCallback on_complete) {
  const base::ListValue& web_apps =
      pref_service_->GetList(prefs::kWebAppInstallForceList);
  const auto& web_apps_list = web_apps;

  const auto it = std::ranges::find(
      web_apps_list, url.spec(), [](const base::Value& entry) {
        return CHECK_DEREF(entry.GetDict().FindString(kUrlKey));
      });

  bool is_placeholder_url =
      provider_->registrar_unsafe()
          .LookupPlaceholderAppId(url, WebAppManagement::kPolicy)
          .has_value();

  if (it == web_apps_list.end() || !is_placeholder_url) {
    std::move(on_complete)
        .Run(url, ExternallyManagedAppManager::InstallResult(
                      webapps::InstallResultCode::kFailedPlaceholderUninstall));
    return;
  }

  std::optional<ExternalInstallOptions> install_options =
      ParseInstallPolicyEntry(it->GetDict());

  // The install_url must have been invalid for install policy parsing to return
  // a `std::nullopt`.
  if (!install_options.has_value()) {
    std::move(on_complete)
        .Run(url, ExternallyManagedAppManager::InstallResult(
                      webapps::InstallResultCode::kInstallURLInvalid));
    return;
  }

  // No need to install a placeholder because there should be one already.
  install_options->placeholder_resolution_behavior =
      PlaceholderResolutionBehavior::kWaitForAppWindowsClosed;

  // If the app is not a placeholder app, ExternallyManagedAppManager will
  // ignore the request.
  provider_->externally_managed_app_manager().InstallNow(
      std::move(*install_options), std::move(on_complete));
}

// static
void WebAppPolicyManager::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(prefs::kWebAppInstallForceList);
  registry->RegisterListPref(prefs::kWebAppSettings);
  registry->RegisterBooleanPref(prefs::kWebAppInstallByUserEnabled, true);
}

// static
bool WebAppPolicyManager::IsChromeAppPolicyId(std::string_view policy_id) {
  return crx_file::id_util::IdIsValid(policy_id);
}

// static
bool WebAppPolicyManager::IsWebAppPolicyId(std::string_view policy_id) {
  return GURL{policy_id}.is_valid();
}

// static
std::optional<std::string_view>
WebAppPolicyManager::GetPolicyIdForPreinstalledWebApp(std::string_view app_id) {
  if (const auto& test_mapping = GetPreinstalledWebAppsMappingForTesting()) {
    for (const auto& [policy_id, mapped_app_id] : *test_mapping) {
      if (mapped_app_id == app_id) {
        return policy_id;
      }
    }
    return {};
  }

  return {};
}

// static
void WebAppPolicyManager::SetPreinstalledWebAppsMappingForTesting(  // IN-TEST
    std::optional<base::flat_map<std::string_view, std::string_view>>
        preinstalled_web_apps_mapping_for_testing) {
  GetPreinstalledWebAppsMappingForTesting() =                // IN-TEST
      std::move(preinstalled_web_apps_mapping_for_testing);  // IN-TEST
}

// static
bool WebAppPolicyManager::IsPreinstalledWebAppPolicyId(
    std::string_view policy_id) {
  if (auto& mapping = GetPreinstalledWebAppsMappingForTesting()) {  // IN-TEST
    return mapping->contains(policy_id);
  }
  return false;
}

// static
bool WebAppPolicyManager::IsIsolatedWebAppPolicyId(std::string_view policy_id) {
  return web_package::SignedWebBundleId::Create(policy_id).has_value();
}

// static
std::vector<std::string> WebAppPolicyManager::GetPolicyIds(
    Profile* profile,
    const WebApp& web_app) {
  const auto& app_id = web_app.app_id();
  WebAppRegistrar& web_app_registrar =
      WebAppProvider::GetForWebApps(profile)->registrar_unsafe();

  if (web_app_registrar.AppMatches(
          app_id, WebAppFilter::PolicyInstalledIsolatedWebApp())) {
    // This is an IWA - and thus, web_bundle_id == policy_id == URL hostname
    return {web_app.start_url().GetHost()};
  }

  std::vector<std::string> policy_ids;

  if (std::optional<std::string_view> preinstalled_web_app_policy_id =
          GetPolicyIdForPreinstalledWebApp(app_id)) {
    policy_ids.emplace_back(*preinstalled_web_app_policy_id);
  }


  for (const auto& [source, external_config] :
       web_app.management_to_external_config_map()) {
    if (!external_config.additional_policy_ids.empty()) {
      base::Extend(policy_ids, external_config.additional_policy_ids);
    }
  }

  if (!web_app_registrar.HasExternalAppWithInstallSource(
          app_id, ExternalInstallSource::kExternalPolicy)) {
    return policy_ids;
  }

  base::flat_map<webapps::AppId, base::flat_set<GURL>> installed_apps =
      web_app_registrar.GetExternallyInstalledApps(
          ExternalInstallSource::kExternalPolicy);
  if (auto* install_urls = base::FindOrNull(installed_apps, app_id)) {
    DCHECK(!install_urls->empty());
    base::Extend(policy_ids, base::ToVector(*install_urls, &GURL::spec));
  }

  return policy_ids;
}

void WebAppPolicyManager::InitChangeRegistrarAndRefreshPolicy() {
  pref_change_registrar_.Init(pref_service_);
  pref_change_registrar_.Add(
      prefs::kWebAppInstallForceList,
      base::BindRepeating(&WebAppPolicyManager::RefreshPolicyInstalledApps,
                          weak_ptr_factory_.GetWeakPtr(),
                          /*allow_close_and_relaunch=*/false));
  pref_change_registrar_.Add(
      prefs::kWebAppSettings,
      base::BindRepeating(&WebAppPolicyManager::RefreshPolicySettings,
                          weak_ptr_factory_.GetWeakPtr()));

  RefreshPolicySettings();
  RefreshPolicyInstalledApps(/*allow_close_and_relaunch=*/false);
  ObserveDisabledSystemFeaturesPolicy();
}

void WebAppPolicyManager::OnDisableListPolicyChanged() {
}

void WebAppPolicyManager::OnSyncPolicySettingsCommandsComplete() {
  provider_->registrar_unsafe().NotifyWebAppSettingsPolicyChanged();
  if (refresh_policy_settings_completed_) {
    std::move(refresh_policy_settings_completed_).Run();
  }
}


bool WebAppPolicyManager::IsWebAppInDisabledList(
    const webapps::AppId& app_id) const {
  return disabled_web_apps_.contains(app_id);
}

void WebAppPolicyManager::RefreshPolicyInstalledApps(
    bool allow_close_and_relaunch) {
  CHECK(!allow_close_and_relaunch);

  if (!web_app::AreWebAppsForceInstallable(profile_)) {
    OnWebAppForceInstallPolicyParsed();
    return;
  }

  // If this is called again while in progress, we will run it again once the
  // |SynchronizeInstalledApps| call is finished.
  if (is_refreshing_) {
    needs_refresh_ = true;
    return;
  }

  is_refreshing_ = true;
  needs_refresh_ = false;

  custom_manifest_values_by_url_.clear();

  const base::ListValue& web_apps =
      pref_service_->GetList(prefs::kWebAppInstallForceList);
  std::vector<ExternalInstallOptions> install_options_list;
  // No need to validate the types or values of the policy members because we
  // are using a SimpleSchemaValidatingPolicyHandler which should validate them
  // for us.
  for (const base::Value& entry : web_apps) {
    std::optional<ExternalInstallOptions> install_options =
        ParseInstallPolicyEntry(entry.GetDict());

    if (!install_options.has_value()) {
      continue;
    }

    install_options->install_placeholder = true;
    // When the policy gets refreshed, we should try to reinstall placeholder
    // apps but only if they are not being used. In the non-placeholder case, we
    // will not reinstall and there is no need to wait for windows being closed.
    // Note: an exception to this rule is described in
    // go/preventclose-waitforwindowsclosed.

    CHECK(install_options->install_url.is_valid());
    install_options->placeholder_resolution_behavior =
        provider_->registrar_unsafe()
                .LookupPlaceholderAppId(install_options->install_url,
                                        WebAppManagement::kPolicy)
                .has_value()
            ? (allow_close_and_relaunch
                   ? PlaceholderResolutionBehavior::kCloseAndRelaunch
                   : PlaceholderResolutionBehavior::kWaitForAppWindowsClosed)
            : PlaceholderResolutionBehavior::kClose;

    std::optional<webapps::AppId> app_id =
        provider_->registrar_unsafe().LookupExternalAppId(
            install_options->install_url);

    if (app_id) {
      // If the override name has changed, reinstall:
      if (install_options->override_name &&
          install_options->override_name.value() !=
              provider_->registrar_unsafe().GetAppShortName(app_id.value())) {
        install_options->force_reinstall = true;
      }

      // If the override icon has changed, reinstall:
      if (install_options->override_icon_url &&
          !IconInfosContainIconURL(
              provider_->registrar_unsafe().GetAppIconInfos(app_id.value()),
              install_options->override_icon_url.value())) {
        install_options->force_reinstall = true;
      }
    }
    install_options_list.push_back(std::move(*install_options));
  }

  provider_->externally_managed_app_manager().SynchronizeInstalledApps(
      std::move(install_options_list), ExternalInstallSource::kExternalPolicy,
      base::BindOnce(&WebAppPolicyManager::OnAppsSynchronized,
                     weak_ptr_factory_.GetWeakPtr()));
}

void WebAppPolicyManager::ParsePolicySettings() {
  // No need to validate the types or values of the policy members because we
  // are using a WebAppSettingsPolicyHandler which should validate them for us.
  const base::ListValue& web_apps_list =
      pref_service_->GetList(prefs::kWebAppSettings);

  settings_by_url_.clear();
  default_settings_ = WebAppPolicyManager::WebAppSetting();

  // Read default policy, if provided.
  const auto it =
      std::ranges::find(web_apps_list, kWildcard, [](const base::Value& entry) {
        return CHECK_DEREF(entry.GetDict().FindString(kManifestId));
      });

  if (it != web_apps_list.end() && it->is_dict()) {
    if (!default_settings_.Parse(it->GetDict(), true)) {
      SYSLOG(WARNING) << "Malformed default web app management setting.";
      default_settings_ = WebAppPolicyManager::WebAppSetting();
    }
  }

  // Read policy for individual web apps
  for (const auto& iter : web_apps_list) {
    const auto& dict = iter.GetDict();
    const std::string* web_app_id_str = dict.FindString(kManifestId);

    if (*web_app_id_str == kWildcard) {
      continue;
    }

    GURL url = GURL(*web_app_id_str);
    if (!url.is_valid()) {
      LOG(WARNING) << "Invalid URL: " << *web_app_id_str;
      continue;
    }

    WebAppPolicyManager::WebAppSetting by_url(default_settings_);
    if (by_url.Parse(dict, /*for_default_settings=*/false)) {
      settings_by_url_[url.spec()] = by_url;
    } else {
      LOG(WARNING) << "Malformed web app settings for " << url;
    }
  }
}

void WebAppPolicyManager::RefreshPolicySettings() {
  ParsePolicySettings();
  ApplyPolicySettings();
}

void WebAppPolicyManager::SynchronizeOsWithPolicyDefinedFileHandlers() {
  provider_->scheduler().SynchronizeOsIntegrationForAllApps(
      WebAppFilter::InstalledInChrome(), base::DoNothing());
}

void WebAppPolicyManager::ApplyPolicySettings() {
  // The number of closures are 2, since we want to wait for 2 things to
  // complete:
  // 1. Applying Run on OS login settings policy.
  // 2. Applying force unregistration settings policy.
  // If for any reason the same app_id is being used for both Run on OS
  // login and force unregistration, it is still safe, since both functions
  // invoke commands, so the Run on OS login will always be scheduled before the
  // force unregistration, and execution will be synchronous.
  base::ConcurrentClosures concurrent;
  ApplyRunOnOsLoginPolicySettings(concurrent.CreateClosure());
  ApplyForceOSUnregistrationPolicySettings(concurrent.CreateClosure());
  std::move(concurrent)
      .Done(base::BindOnce(
          &WebAppPolicyManager::OnSyncPolicySettingsCommandsComplete,
          weak_ptr_factory_.GetWeakPtr()));
}

void WebAppPolicyManager::ApplyRunOnOsLoginPolicySettings(
    base::OnceClosure policy_settings_applied_callback) {
  base::ConcurrentClosures concurrent;
  WebAppProvider* provider = WebAppProvider::GetForLocalAppsUnchecked(profile_);
  for (const webapps::AppId& app_id :
       provider_->registrar_unsafe().GetAppIds()) {
    provider->scheduler().SyncRunOnOsLoginMode(app_id,
                                               concurrent.CreateClosure());
  }
  std::move(concurrent).Done(std::move(policy_settings_applied_callback));
}

void WebAppPolicyManager::ApplyForceOSUnregistrationPolicySettings(
    base::OnceClosure policy_settings_applied_callback) {
  if (!IsForceUnregistrationPolicyEnabled()) {
    std::move(policy_settings_applied_callback).Run();
    return;
  }

  base::ConcurrentClosures concurrent;
  SynchronizeOsOptions options;
  options.force_unregister_os_integration = true;
  for (const auto& [manifest_string, setting] : settings_by_url_) {
    const GURL manifest_id = GURL(manifest_string);
    if (!manifest_id.is_valid()) {
      continue;
    }

    const webapps::AppId& app_id =
        web_app::GenerateAppIdFromManifestId(manifest_id);
    if (provider_->registrar_unsafe().GetInstallState(app_id) !=
        proto::INSTALLED_WITH_OS_INTEGRATION) {
      continue;
    }

    if (setting.force_unregister_os_integration) {
      provider_->scheduler().SynchronizeOsIntegration(
          app_id, concurrent.CreateClosure(), options);
    }
  }

  std::move(concurrent).Done(std::move(policy_settings_applied_callback));
}

std::optional<ExternalInstallOptions>
WebAppPolicyManager::ParseInstallPolicyEntry(const base::DictValue& entry) {
  const std::string* install_url = entry.FindString(kUrlKey);
  // url is a required field and is validated by
  // SimpleSchemaValidatingPolicyHandler. It is guaranteed to exist.
  const GURL install_gurl(CHECK_DEREF(install_url));
  const std::string* default_launch_container =
      entry.FindString(kDefaultLaunchContainerKey);
  const std::optional<bool> create_desktop_shortcut =
      entry.FindBool(kCreateDesktopShortcutKey);
  const std::string* fallback_app_name = entry.FindString(kFallbackAppNameKey);
  const base::ListValue* uninstall_and_replace =
      entry.FindList(kUninstallAndReplaceKey);
  const std::optional<bool> install_as_diy = entry.FindBool(kInstallAsShortcut);

  DCHECK(!default_launch_container ||
         (*default_launch_container == kDefaultLaunchContainerWindowValue) ||
         (*default_launch_container == kDefaultLaunchContainerTabValue));

  if (!install_gurl.is_valid()) {
    LOG(WARNING) << "Policy-installed web app has invalid URL " << *install_url;
    return std::nullopt;
  }

  mojom::UserDisplayMode user_display_mode;
  if (!default_launch_container) {
    user_display_mode = mojom::UserDisplayMode::kBrowser;
  } else if (*default_launch_container == kDefaultLaunchContainerTabValue) {
    user_display_mode = mojom::UserDisplayMode::kBrowser;
  } else {
    user_display_mode = mojom::UserDisplayMode::kStandalone;
  }

  ExternalInstallOptions install_options{
      install_gurl, user_display_mode, ExternalInstallSource::kExternalPolicy};

  // TODO(dmurph): Store expected os integration state in the database so
  // this doesn't re-apply when we already have it done.
  // https://crbug.com/1295044
  install_options.add_to_applications_menu = true;
  install_options.add_to_desktop = create_desktop_shortcut.value_or(false);
  // Pinning apps to the ChromeOS shelf is done through the PinnedLauncherApps
  // policy.
  install_options.add_to_quick_launch_bar = false;

  // Allow administrators to override the name of the placeholder app, as well
  // as the permanent name for Web Apps without a manifest.
  if (fallback_app_name) {
    install_options.fallback_app_name = *fallback_app_name;
  }

  // Used by default Chrome app policy migration to force install web apps and
  // uninstall the old Chrome app equivalents.
  if (uninstall_and_replace) {
    for (const base::Value& item : *uninstall_and_replace) {
      if (item.is_string()) {
        install_options.uninstall_and_replace.push_back(item.GetString());
      }
    }
  }

  // Shortcut apps no longer exist in the web applications system and are
  // treated as DIY apps now.
  install_options.install_as_diy = install_as_diy.value_or(false);

  const std::string* custom_name = entry.FindString(kCustomNameKey);
  if (custom_name) {
    install_options.override_name = *custom_name;
    if (install_gurl.is_valid()) {
      custom_manifest_values_by_url_[install_gurl].SetName(*custom_name);
    }
  }

  const base::DictValue* custom_icon = entry.FindDict(kCustomIconKey);
  if (custom_icon && custom_icon) {
    const std::string* icon_url = custom_icon->FindString(kCustomIconURLKey);
    if (icon_url) {
      GURL icon_gurl = GURL(*icon_url);
      if (icon_gurl.SchemeIs(url::kHttpsScheme)) {
        install_options.override_icon_url = icon_gurl;
        if (install_gurl.is_valid()) {
          custom_manifest_values_by_url_[install_gurl].SetIcon(icon_gurl);
        }
      } else {
        LOG(WARNING) << "Policy-installed web app " << *install_url
                     << " has non-https custom icon URL " << *icon_url
                     << ", ignoring custom icon.";
      }
    }
  }

  return install_options;
}

RunOnOsLoginPolicy WebAppPolicyManager::GetUrlRunOnOsLoginPolicy(
    const webapps::AppId& app_id) const {
  return GetUrlRunOnOsLoginPolicyByManifestId(
      provider_->registrar_unsafe().GetComputedManifestId(app_id).spec());
}

RunOnOsLoginPolicy WebAppPolicyManager::GetUrlRunOnOsLoginPolicyByManifestId(
    const std::string& manifest_id) const {
  auto it = settings_by_url_.find(manifest_id);
  if (it != settings_by_url_.end()) {
    return it->second.run_on_os_login_policy;
  }
  return default_settings_.run_on_os_login_policy;
}

void WebAppPolicyManager::SetOnAppsSynchronizedCompletedCallbackForTesting(
    base::OnceClosure callback) {
  on_apps_synchronized_for_testing_ = std::move(callback);
}

void WebAppPolicyManager::SetRefreshPolicySettingsCompletedCallbackForTesting(
    base::OnceClosure callback) {
  refresh_policy_settings_completed_ = std::move(callback);
}

void WebAppPolicyManager::RefreshPolicySettingsForTesting() {
  RefreshPolicySettings();
}

void WebAppPolicyManager::OverrideManifest(
    const GURL& custom_values_key,
    blink::mojom::ManifestPtr& manifest) const {
  const CustomManifestValues& custom_values = CHECK_DEREF(
      base::FindOrNull(custom_manifest_values_by_url_, custom_values_key));
  if (custom_values.name) {
    manifest->name = custom_values.name.value();
  }
  if (custom_values.icons) {
    manifest->icons = custom_values.icons.value();
  }
}

void WebAppPolicyManager::MaybeOverrideManifest(
    content::RenderFrameHost* frame_host,
    blink::mojom::ManifestPtr& manifest) const {
  // This doesn't override the manifest properly on a non primary page since it
  // checks the url from PreRedirectionURLObserver that works only on a primary
  // page.
  if (!frame_host->IsInPrimaryMainFrame()) {
    return;
  }

  if (!manifest) {
    return;
  }

  // For policy-installed apps there are two ways for getting to the manifest:
  // via the policy install URL, or via the manifest-specified identity
  // of an already installed app. Websites without a manifest will use the
  // policy-installed URL as start_url, so they are covered by the first case.
  // Second case first:
  if (manifest->id.is_valid()) {
    const webapps::AppId& app_id = GenerateAppIdFromManifestId(manifest->id);
    // List of policy-installed apps and their install URLs:
    base::flat_map<webapps::AppId, base::flat_set<GURL>> policy_installed_apps =
        provider_->registrar_unsafe().GetExternallyInstalledApps(
            ExternalInstallSource::kExternalPolicy);
    if (policy_installed_apps.contains(app_id)) {
      DCHECK_GT(policy_installed_apps[app_id].size(), 0UL);
      for (const GURL& policy_install_url : policy_installed_apps[app_id]) {
        if (custom_manifest_values_by_url_.contains(policy_install_url)) {
          OverrideManifest(policy_install_url, manifest);
        }
      }
      return;
    }
  }

  // And now the first case: assume we got here from the policy install URL.
  // We might have been redirected in between, so check where we started
  // the current navigation.
  const webapps::PreRedirectionURLObserver* const pre_redirect =
      webapps::PreRedirectionURLObserver::FromWebContents(
          content::WebContents::FromRenderFrameHost(frame_host));
  if (!pre_redirect) {
    return;
  }
  GURL install_url = pre_redirect->last_url();
  if (custom_manifest_values_by_url_.contains(install_url)) {
    OverrideManifest(install_url, manifest);
  }
}

// TODO(crbug.com/329823863): This method should be placed somewhere else, as it
// is also used for IWAs, which do not use `WebAppPolicyManager`, but
// `IsolatedWebAppPolicyManager`.
bool WebAppPolicyManager::IsPreventCloseEnabled(
    const webapps::AppId& app_id) const {
  return false;
}

bool WebAppPolicyManager::GetEffectiveInstallPolicyValue() {
  return effective_web_apps_user_installable_policy_;
}

void WebAppPolicyManager::RefreshPolicyInstalledAppsForTesting(
    bool allow_close_and_relaunch) {
  RefreshPolicyInstalledApps(allow_close_and_relaunch);
}

void WebAppPolicyManager::OnAppsSynchronized(
    std::map<GURL, ExternallyManagedAppManager::InstallResult> install_results,
    std::map<GURL, webapps::UninstallResultCode> uninstall_results) {
  is_refreshing_ = false;

  if (!install_results.empty()) {
    ApplyPolicySettings();
  }

  if (needs_refresh_) {
    RefreshPolicyInstalledApps();
  }

  for (const auto& url_and_result : install_results) {
    base::UmaHistogramEnumeration(kInstallResultHistogramName,
                                  url_and_result.second.code);
  }

  OnWebAppForceInstallPolicyParsed();
}

bool WebAppPolicyManager::WebAppSetting::Parse(const base::DictValue& dict,
                                               bool for_default_settings) {
  const std::string* run_on_os_login_str = dict.FindString(kRunOnOsLogin);
  if (run_on_os_login_str) {
    if (*run_on_os_login_str == kAllowed) {
      run_on_os_login_policy = RunOnOsLoginPolicy::kAllowed;
    } else if (*run_on_os_login_str == kBlocked) {
      run_on_os_login_policy = RunOnOsLoginPolicy::kBlocked;
    } else if (!for_default_settings && *run_on_os_login_str == kRunWindowed) {
      run_on_os_login_policy = RunOnOsLoginPolicy::kRunWindowed;
    } else {
      SYSLOG(WARNING) << "Malformed web app run on os login preference.";
      return false;
    }
  }

  // The value of "prevent_close" shall only be considered for non-default
  // settings if run-on-os-login is enforced.
  if (!for_default_settings &&
      run_on_os_login_policy == RunOnOsLoginPolicy::kRunWindowed) {
    prevent_close = dict.FindBool(kPreventClose).value_or(false);
  }

  if (IsForceUnregistrationPolicyEnabled()) {
    std::optional<bool> force_unregistration_value =
        dict.FindBool(kForceUnregisterOsIntegration);
    force_unregister_os_integration =
        force_unregistration_value.value_or(false);
  }
  return true;
}

WebAppPolicyManager::CustomManifestValues::CustomManifestValues() = default;
WebAppPolicyManager::CustomManifestValues::CustomManifestValues(
    const WebAppPolicyManager::CustomManifestValues&) = default;
WebAppPolicyManager::CustomManifestValues::~CustomManifestValues() = default;

void WebAppPolicyManager::CustomManifestValues::SetName(
    const std::string& utf8_name) {
  name = base::UTF8ToUTF16(utf8_name);
}

void WebAppPolicyManager::CustomManifestValues::SetIcon(const GURL& icon_gurl) {
  blink::Manifest::ImageResource icon;

  icon.src = GURL(icon_gurl);
  icon.sizes.emplace_back(0, 0);  // Represents size "any".
  icon.purpose.push_back(blink::mojom::ManifestImageResource::Purpose::ANY);

  // Initialize icons to only contain icon, possibly resetting icons:
  icons.emplace(1, icon);
}

void WebAppPolicyManager::ObserveDisabledSystemFeaturesPolicy() {
}

void WebAppPolicyManager::OnDisableModePolicyChanged() {
}

void WebAppPolicyManager::PopulateDisabledWebAppsIdsLists() {
  disabled_web_apps_.clear();

}

void WebAppPolicyManager::OnWebAppForceInstallPolicyParsed() {
  if (on_apps_synchronized_for_testing_) {
    std::move(on_apps_synchronized_for_testing_).Run();
  }

  // Policy settings have already been applied, as that happens synchronously
  // before force-installs are applied.
  if (policy_settings_and_force_installs_applied_) {
    std::move(policy_settings_and_force_installs_applied_).Run();
  }
}

}  // namespace web_app
