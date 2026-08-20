// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/web_applications/web_app_ui_manager_impl.h"

#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/one_shot_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/types/pass_key.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/intent_picker_tab_helper.h"
#include "chrome/browser/ui/user_education/browser_user_education_interface.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/views/web_apps/web_app_blocked_migration_infobar_delegate.h"
#include "chrome/browser/ui/web_applications/web_app_dialog_utils.h"
#include "chrome/browser/ui/web_applications/web_app_dialogs.h"
#include "chrome/browser/ui/web_applications/web_app_launch_utils.h"
#include "chrome/browser/ui/web_applications/web_app_metrics.h"
#include "chrome/browser/ui/web_applications/web_app_run_on_os_login_notification.h"
#include "chrome/browser/user_education/user_education_service.h"
#include "chrome/browser/user_education/user_education_service_factory.h"
#include "chrome/browser/web_applications/web_app_callback_app_identity.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_app_icon_manager.h"
#include "chrome/browser/web_applications/web_app_pref_guardrails.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_tab_helper.h"
#include "chrome/browser/web_applications/web_app_ui_manager.h"
#include "chrome/browser/web_applications/web_app_uninstall_dialog_user_options.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/services/app_service/public/cpp/app_launch_params.h"
#include "components/user_education/common/feature_promo/feature_promo_controller.h"
#include "components/user_education/common/feature_promo/feature_promo_result.h"
#include "components/user_education/common/user_education_data.h"
#include "components/webapps/browser/installable/installable_metrics.h"
#include "components/webapps/browser/installable/ml_install_operation_tracker.h"
#include "components/webapps/browser/uninstall_result_code.h"
#include "components/webapps/common/web_app_id.h"
#include "content/public/browser/clear_site_data_utils.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/app_sorting.h"
#include "extensions/browser/extension_system.h"
#include "third_party/blink/public/mojom/manifest/manifest.mojom-shared.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/native_window_tracker/native_window_tracker.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

#include "chrome/browser/apps/link_capturing/enable_link_capturing_infobar_delegate.h"
#include "chrome/browser/infobars/confirm_infobar_creator.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/infobar.h"

#if !BUILDFLAG(IS_MAC)
#include "ui/aura/window.h"
#endif  // !BUILDFLAG(IS_MAC)



namespace base {
class FilePath;
}

namespace web_app {

class AppLock;

namespace {



}  // namespace

// static
std::unique_ptr<WebAppUiManager> WebAppUiManager::Create(Profile* profile) {
  return std::make_unique<WebAppUiManagerImpl>(profile);
}

// static
void WebAppUiManager::TriggerInstallNotSupportedDialog(
    content::WebContents* web_contents,
    Profile* profile,
    base::OnceClosure callback) {
  NotSupportedReason reason;
  if (profile->IsGuestSession()) {
    reason = NotSupportedReason::kGuestMode;
  } else if (profile->IsOffTheRecord()) {
    reason = NotSupportedReason::kOffTheRecord;
  } else if (!web_app::IsWebAppInstallByUserPolicyEnabled(profile)) {
    reason = NotSupportedReason::kPolicyDisabled;
  } else {
    NOTREACHED();
  }
  ShowInstallNotSupportedDialog(web_contents, profile, reason,
                                std::move(callback));
}

WebAppUiManagerImpl::WebAppUiManagerImpl(Profile* profile)
    : profile_(profile) {}

WebAppUiManagerImpl::~WebAppUiManagerImpl() = default;

void WebAppUiManagerImpl::Start() {
  DCHECK(!started_);
  started_ = true;

  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* browser_window_interface) {
        if (!IsBrowserForInstalledApp(browser_window_interface)) {
          return true;
        }

        ++num_windows_for_apps_map_[GetAppIdForBrowser(
            browser_window_interface)];
        return true;
      });

  extensions::ExtensionSystem::Get(profile_)->ready().Post(
      FROM_HERE, base::BindOnce(&WebAppUiManagerImpl::OnExtensionSystemReady,
                                weak_ptr_factory_.GetWeakPtr()));

  browser_collection_observation_.Observe(
      GlobalBrowserCollection::GetInstance());
}

void WebAppUiManagerImpl::Shutdown() {
  browser_collection_observation_.Reset();
  started_ = false;
}

WebAppUiManagerImpl* WebAppUiManagerImpl::AsImpl() {
  return this;
}

size_t WebAppUiManagerImpl::GetNumWindowsForApp(const webapps::AppId& app_id) {
  DCHECK(started_);

  auto it = num_windows_for_apps_map_.find(app_id);
  if (it == num_windows_for_apps_map_.end()) {
    return 0;
  }

  return it->second;
}

void WebAppUiManagerImpl::CloseAppWindows(const webapps::AppId& app_id) {
  DCHECK(started_);

  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&app_id](BrowserWindowInterface* browser_window_interface) {
        const auto* const app_controller =
            AppBrowserController::From(browser_window_interface);
        if (app_controller && app_controller->app_id() == app_id) {
          browser_window_interface->GetWindow()->Close();
        }
        return true;
      });
}

void WebAppUiManagerImpl::NotifyOnAllAppWindowsClosed(
    const webapps::AppId& app_id,
    base::OnceClosure callback) {
  DCHECK(started_);

  const size_t num_windows_for_app = GetNumWindowsForApp(app_id);
  if (num_windows_for_app == 0) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(callback));
    return;
  }

  windows_closed_requests_map_[app_id].push_back(std::move(callback));
}

void WebAppUiManagerImpl::OnExtensionSystemReady() {
  extensions::ExtensionSystem::Get(profile_)
      ->app_sorting()
      ->InitializePageOrdinalMapFromWebApps();
}

bool WebAppUiManagerImpl::CanAddAppToQuickLaunchBar() const {
  return false;
}

void WebAppUiManagerImpl::AddAppToQuickLaunchBar(const webapps::AppId& app_id) {
  DCHECK(CanAddAppToQuickLaunchBar());
}

bool WebAppUiManagerImpl::IsAppInQuickLaunchBar(
    const webapps::AppId& app_id) const {
  DCHECK(CanAddAppToQuickLaunchBar());
  return false;
}

bool WebAppUiManagerImpl::CanReparentAppTabToWindow(
    const webapps::AppId& app_id,
    bool shortcut_created,
    content::WebContents* web_contents) const {
  CHECK(web_contents);
  const WebAppTabHelper* tab_helper =
      WebAppTabHelper::FromWebContents(web_contents);
  bool is_in_app_window = false;
  if (tab_helper) {
    // The tab helper doesn't exist in unit tests.
    is_in_app_window = tab_helper->is_in_app_window();
  }
  // App-to-app reparenting is not currently supported.
  if (is_in_app_window) {
    return false;
  }
#if BUILDFLAG(IS_MAC)
  // On macOS it is only possible to reparent the window when the shortcut (app
  // shim) was created. See https://crbug.com/915571.
  return shortcut_created;
#else
  return true;
#endif
}

Browser* WebAppUiManagerImpl::ReparentAppTabToWindow(
    content::WebContents* contents,
    const webapps::AppId& app_id,
    bool shortcut_created) {
  DCHECK(CanReparentAppTabToWindow(app_id, shortcut_created, contents));
  // Reparent the tab into an app window immediately.
  BrowserWindowInterface* browser =
      ReparentWebContentsIntoAppBrowser(contents, app_id);
  return browser ? browser->GetBrowserForMigrationOnly() : nullptr;
}

Browser* WebAppUiManagerImpl::ReparentAppTabToWindow(
    content::WebContents* contents,
    const webapps::AppId& app_id,
    base::OnceCallback<void(content::WebContents*)> completion_callback) {
  BrowserWindowInterface* browser = ReparentWebContentsIntoAppBrowser(
      contents, app_id, std::move(completion_callback));
  return browser == nullptr ? nullptr : browser->GetBrowserForMigrationOnly();
}

void WebAppUiManagerImpl::ShowWebAppFileLaunchDialog(
    const std::vector<base::FilePath>& file_paths,
    const webapps::AppId& app_id,
    WebAppLaunchAcceptanceCallback launch_callback) {
  ::web_app::ShowWebAppFileLaunchDialog(file_paths, profile_, app_id,
                                        std::move(launch_callback));
}

void WebAppUiManagerImpl::ShowWebAppProtocolLaunchDialog(
    const GURL& protocol_url,
    const webapps::AppId& app_id,
    WebAppLaunchAcceptanceCallback launch_callback) {
  ::web_app::ShowWebAppProtocolLaunchDialog(protocol_url, profile_, app_id,
                                            std::move(launch_callback));
}

void WebAppUiManagerImpl::ShowWebAppIdentityUpdateDialog(
    const std::string& app_id,
    bool title_change,
    bool icon_change,
    const std::u16string& old_title,
    const std::u16string& new_title,
    const SkBitmap& old_icon,
    const SkBitmap& new_icon,
    content::WebContents* web_contents,
    web_app::AppIdentityDialogCallback callback) {
  ::web_app::ShowWebAppIdentityUpdateDialog(
      app_id, title_change, icon_change, old_title, new_title, old_icon,
      new_icon, web_contents, std::move(callback));
}

void WebAppUiManagerImpl::ShowSubAppsInstallDialog(
    content::WebContents* initiating_web_contents,
    const std::vector<std::unique_ptr<WebAppInstallInfo>>& sub_apps,
    const webapps::AppId& parent_app_id,
    base::OnceCallback<void(bool)> callback) {
  std::string parent_app_name = WebAppProvider::GetForWebApps(profile_)
                                    ->registrar_unsafe()
                                    .GetAppShortName(parent_app_id);
  web_app::ShowSubAppsInstallDialog(initiating_web_contents, sub_apps,
                                    parent_app_name, parent_app_id,
                                    std::move(callback));
}

void WebAppUiManagerImpl::ShowWebAppSettings(const webapps::AppId& app_id) {
  WebAppProvider* provider = WebAppProvider::GetForWebApps(profile_);
  if (!provider) {
    return;
  }

  GURL start_url = provider->registrar_unsafe().GetAppStartUrl(app_id);
  if (!start_url.is_valid()) {
    return;
  }

  chrome::ShowSiteSettings(profile_, start_url);
}

void WebAppUiManagerImpl::LaunchWebApp(apps::AppLaunchParams params,
                                       LaunchWebAppWindowSetting launch_setting,
                                       Profile& profile,
                                       LaunchWebAppDebugValueCallback callback,
                                       WithAppResources& lock) {
  ::web_app::LaunchWebApp(std::move(params), launch_setting, profile, lock,
                          std::move(callback));
}


void WebAppUiManagerImpl::NotifyAppRelaunchState(
    const webapps::AppId& placeholder_app_id,
    const webapps::AppId& final_app_id,
    const std::u16string& final_app_name,
    base::WeakPtr<Profile> profile,
    AppRelaunchState relaunch_state) {
}

content::WebContents* WebAppUiManagerImpl::CreateNewTab() {
  NavigateParams params(profile_, GURL(url::kAboutBlankURL),
                        ui::PAGE_TRANSITION_FROM_API);
  base::WeakPtr<content::NavigationHandle> handle = Navigate(&params);
  if (handle) {
    return handle->GetWebContents();
  }
  return nullptr;
}

bool WebAppUiManagerImpl::IsWebContentsActiveTabInBrowser(
    content::WebContents* web_contents) {
  Browser* browser = chrome::FindBrowserWithTab(web_contents);
  return browser && browser->tab_strip_model() &&
         browser->tab_strip_model()->GetActiveWebContents() == web_contents;
}

void WebAppUiManagerImpl::TriggerInstallDialog(
    content::WebContents* web_contents,
    webapps::WebappInstallSource source,
    InstallCallback callback) {
  web_app::CreateWebAppFromManifest(web_contents, source, std::move(callback));
}

void WebAppUiManagerImpl::TriggerInstallDialogForBackgroundInstall(
    content::WebContents* initiating_web_contents,
    std::unique_ptr<webapps::MlInstallOperationTracker> tracker,
    const GURL& install_url,
    const std::optional<GURL>& manifest_id,
    const GURL& last_committed_url,
    InstallCallback callback) {
  web_app::CreateWebAppForBackgroundInstall(
      initiating_web_contents, std::move(tracker), install_url, manifest_id,
      last_committed_url, std::move(callback));
}

void WebAppUiManagerImpl::TriggerLaunchDialogForBackgroundInstall(
    content::WebContents* initiating_web_contents,
    const webapps::AppId& app_id,
    Profile* profile,
    const std::string& app_name,
    const SkBitmap& icon,
    WebInstallAppLaunchAcceptanceCallback callback) {
  ShowWebInstallAppLaunchDialog(initiating_web_contents, app_id, profile,
                                app_name, icon, std::move(callback));
}

void WebAppUiManagerImpl::PresentUserUninstallDialog(
    const webapps::AppId& app_id,
    webapps::WebappUninstallSource uninstall_source,
    BrowserWindow* parent_window,
    UninstallCompleteCallback callback) {
  PresentUserUninstallDialog(
      app_id, uninstall_source,
      parent_window ? parent_window->GetNativeWindow() : gfx::NativeWindow(),
      std::move(callback), base::DoNothing());
}

void WebAppUiManagerImpl::PresentUserUninstallDialog(
    const webapps::AppId& app_id,
    webapps::WebappUninstallSource uninstall_source,
    gfx::NativeWindow native_window,
    UninstallCompleteCallback callback) {
  PresentUserUninstallDialog(app_id, uninstall_source, native_window,
                             std::move(callback), base::DoNothing());
}

void WebAppUiManagerImpl::PresentUserUninstallDialog(
    const webapps::AppId& app_id,
    webapps::WebappUninstallSource uninstall_source,
    gfx::NativeWindow parent_window,
    UninstallCompleteCallback uninstall_complete_callback,
    UninstallScheduledCallback uninstall_scheduled_callback) {
  std::unique_ptr<ui::NativeWindowTracker> parent_window_tracker;
  if (parent_window) {
    parent_window_tracker = ui::NativeWindowTracker::Create(parent_window);
  }

  if (parent_window && parent_window_tracker->WasNativeWindowDestroyed()) {
    OnUninstallCancelled(std::move(uninstall_complete_callback),
                         std::move(uninstall_scheduled_callback));
    return;
  }

  WebAppProvider* provider = WebAppProvider::GetForWebApps(profile_);
  CHECK(provider);

  provider->icon_manager().ReadTrustedIconsWithFallbackToManifestIcons(
      app_id,
      provider->registrar_unsafe().GetAppTrustedIconSizesFallbackToUntrusted(
          app_id),
      IconPurpose::ANY,
      base::BindOnce(&WebAppUiManagerImpl::OnIconsReadForUninstall,
                     weak_ptr_factory_.GetWeakPtr(), app_id, uninstall_source,
                     parent_window, std::move(parent_window_tracker),
                     std::move(uninstall_complete_callback),
                     std::move(uninstall_scheduled_callback)));
}

void WebAppUiManagerImpl::ShowIntentPicker(
    const GURL& url,
    content::WebContents* web_contents,
    ShowIntentPickerBubbleCallback callback) {
  IntentPickerTabHelper* intent_picker_tab_helper =
      IntentPickerTabHelper::FromWebContents(web_contents);

  if (!intent_picker_tab_helper) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), /*launched=*/false));
    return;
  }
  intent_picker_tab_helper->ShowIntentPickerBubbleOrLaunchApp(
      url, /*always_show =*/true, std::move(callback));
}

void WebAppUiManagerImpl::LaunchOrFocusIsolatedWebAppInstaller(
    const base::FilePath& bundle_path) {
  auto it = active_installers_.find(bundle_path);

  if (it == active_installers_.end()) {
    // If no installer exists for the path, we create a new coordinator
    active_installers_[bundle_path] = ::web_app::LaunchIsolatedWebAppInstaller(
        profile_, bundle_path,
        base::BindOnce(&WebAppUiManagerImpl::OnIsolatedWebAppInstallerClosed,
                       weak_ptr_factory_.GetWeakPtr(), bundle_path));
  } else {
    // If an installer already exists for |path|, we focus the existing
    // installer.
    FocusIsolatedWebAppInstaller(it->second);
  }
}

void WebAppUiManagerImpl::OnIsolatedWebAppInstallerClosed(
    base::FilePath bundle_path) {
  auto it = active_installers_.find(bundle_path);
  CHECK(it != active_installers_.end())
      << "Installer with path " << bundle_path
      << " is being closed, but it is not found in the list of active "
         "installers.";
  active_installers_.erase(it);
}

void WebAppUiManagerImpl::MaybeCreateEnableSupportedLinksInfobar(
    content::WebContents* web_contents,
    const std::string& launch_name) {
  std::unique_ptr<apps::EnableLinkCapturingInfoBarDelegate> delegate =
      apps::EnableLinkCapturingInfoBarDelegate::MaybeCreate(web_contents,
                                                            launch_name);
  if (delegate) {
    infobars::ContentInfoBarManager::FromWebContents(web_contents)
        ->AddInfoBar(CreateConfirmInfoBar(std::move(delegate)));
  }
}

void WebAppUiManagerImpl::MaybeCreateWebAppBlockedMigrationInfoBar(
    content::WebContents* web_contents) {
  WebAppBlockedMigrationInfoBarDelegate::Create(web_contents);
}

void WebAppUiManagerImpl::MaybeRemoveWebAppBlockedMigrationInfoBar(
    content::WebContents* web_contents) {
  WebAppBlockedMigrationInfoBarDelegate::Remove(web_contents);
}

void WebAppUiManagerImpl::MaybeShowIPHPromoForAppsLaunchedViaLinkCapturing(
    Browser* browser,
    Profile* profile,
    const std::string& app_id) {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  WebAppProvider* provider = WebAppProvider::GetForWebApps(profile);
  CHECK(provider);

  if (!provider->registrar_unsafe().CapturesLinksInScope(app_id)) {
    return;
  }

  BrowserWindowInterface* const app_browser =
      browser ? browser : AppBrowserController::FindForWebApp(*profile, app_id);
  if (!app_browser) {
    return;
  }

  if (WebAppPrefGuardrails::GetForNavigationCapturingIph(
          app_browser->GetProfile()->GetPrefs())
          .IsBlockedByGuardrails(app_id)) {
    return;
  }

  web_app::PostCallbackOnBrowserActivation(
      app_browser->GetBrowserForMigrationOnly(), kToolbarAppMenuButtonElementId,
      base::BindOnce(
          &WebAppUiManagerImpl::ShowIPHPromoForAppsLaunchedViaLinkCapturing,
          weak_ptr_factory_.GetWeakPtr(),
          app_browser->GetBrowserForMigrationOnly(), app_id));
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
}

void WebAppUiManagerImpl::OnBrowserCreated(BrowserWindowInterface* browser) {
  DCHECK(started_);
  if (!IsBrowserForInstalledApp(browser)) {
    return;
  }

  ++num_windows_for_apps_map_[GetAppIdForBrowser(browser)];

}

void WebAppUiManagerImpl::OnBrowserClosed(BrowserWindowInterface* browser) {
  DCHECK(started_);
  if (!IsBrowserForInstalledApp(browser)) {
    return;
  }

  const auto& app_id = GetAppIdForBrowser(browser);

  size_t& num_windows_for_app = num_windows_for_apps_map_[app_id];
  DCHECK_GT(num_windows_for_app, 0u);
  --num_windows_for_app;

  if (num_windows_for_app > 0) {
    return;
  }

  auto it = windows_closed_requests_map_.find(app_id);
  if (it == windows_closed_requests_map_.end()) {
    return;
  }

  for (auto& callback : it->second) {
    std::move(callback).Run();
  }

  windows_closed_requests_map_.erase(app_id);
}



bool WebAppUiManagerImpl::IsBrowserForInstalledApp(
    const BrowserWindowInterface* browser) const {
  if (browser->GetProfile() != profile_) {
    return false;
  }

  if (!web_app::AppBrowserController::IsWebApp(browser)) {
    return false;
  }

  return true;
}

webapps::AppId WebAppUiManagerImpl::GetAppIdForBrowser(
    const BrowserWindowInterface* browser) const {
  return web_app::AppBrowserController::From(browser)->app_id();
}

void WebAppUiManagerImpl::OnIconsReadForUninstall(
    const webapps::AppId& app_id,
    webapps::WebappUninstallSource uninstall_source,
    gfx::NativeWindow parent_window,
    std::unique_ptr<ui::NativeWindowTracker> parent_window_tracker,
    UninstallCompleteCallback complete_callback,
    UninstallScheduledCallback uninstall_scheduled_callback,
    IconMetadataFromDisk icon_metadata) {
  if (parent_window && parent_window_tracker->WasNativeWindowDestroyed()) {
    OnUninstallCancelled(std::move(complete_callback),
                         std::move(uninstall_scheduled_callback));
    return;
  }

  ShowWebAppUninstallDialog(
      profile_, app_id, uninstall_source, parent_window,
      std::move(icon_metadata),
      base::BindOnce(&WebAppUiManagerImpl::ScheduleUninstallIfUserRequested,
                     weak_ptr_factory_.GetWeakPtr(), app_id, uninstall_source,
                     std::move(complete_callback),
                     std::move(uninstall_scheduled_callback)));
}

void WebAppUiManagerImpl::ScheduleUninstallIfUserRequested(
    const webapps::AppId& app_id,
    webapps::WebappUninstallSource uninstall_source,
    UninstallCompleteCallback complete_callback,
    UninstallScheduledCallback uninstall_scheduled_callback,
    web_app::UninstallUserOptions uninstall_options) {
  WebAppProvider* provider = WebAppProvider::GetForWebApps(profile_);
  CHECK(provider);

  const bool uninstall_scheduled =
      uninstall_options.user_wants_uninstall &&
      provider->registrar_unsafe().CanUserUninstallWebApp(app_id);
  std::move(uninstall_scheduled_callback).Run(uninstall_scheduled);
  if (!uninstall_scheduled) {
    std::move(complete_callback).Run(webapps::UninstallResultCode::kCancelled);
    return;
  }

  UninstallCompleteCallback final_callback;
  if (uninstall_options.clear_site_data) {
    CHECK(uninstall_options.user_wants_uninstall);
    const GURL app_start_url =
        provider->registrar_unsafe().GetAppStartUrl(app_id);
    final_callback =
        base::BindOnce(&WebAppUiManagerImpl::ClearWebAppSiteDataIfNeeded,
                       weak_ptr_factory_.GetWeakPtr(), app_start_url,
                       std::move(complete_callback));
  } else {
    final_callback = std::move(complete_callback);
  }

  provider->scheduler().RemoveUserUninstallableManagements(
      app_id, uninstall_source, std::move(final_callback));
}

void WebAppUiManagerImpl::OnUninstallCancelled(
    UninstallCompleteCallback complete_callback,
    UninstallScheduledCallback uninstall_scheduled_callback) {
  std::move(uninstall_scheduled_callback).Run(false);
  std::move(complete_callback).Run(webapps::UninstallResultCode::kCancelled);
}

void WebAppUiManagerImpl::ClearWebAppSiteDataIfNeeded(
    const GURL app_start_url,
    UninstallCompleteCallback uninstall_complete_callback,
    webapps::UninstallResultCode uninstall_code) {
  // This callback should be run at the very end of the uninstallation + site
  // data removal process (if any).
  base::OnceClosure final_uninstall_callback =
      base::BindOnce(std::move(uninstall_complete_callback), uninstall_code);

  // Only clear site data if the uninstallation has succeeded, i.e. either the
  // app has been uninstalled completely, or it was previously uninstalled but
  // some data had been left over.
  if (webapps::UninstallSucceeded(uninstall_code)) {
    content::ClearSiteData(profile_->GetWeakPtr(),
                           /*storage_partition_config=*/std::nullopt,
                           url::Origin::Create(app_start_url),
                           content::ClearSiteDataTypeSet::All(),
                           /*storage_buckets_to_remove=*/{},
                           /*avoid_closing_connections=*/false,
                           /*cookie_partition_key=*/std::nullopt,
                           /*storage_key=*/std::nullopt,
                           /*partitioned_state_allowed_only=*/false,
                           std::move(final_uninstall_callback));
  } else {
    std::move(final_uninstall_callback).Run();
  }
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

const base::Feature& GetPromoFeatureEngagementFromBrowser(
    const BrowserWindowInterface* browser) {
  return web_app::AppBrowserController::IsWebApp(browser)
             ? feature_engagement::kIPHDesktopPWAsLinkCapturingLaunch
             : feature_engagement::kIPHDesktopPWAsLinkCapturingLaunchAppInTab;
}

void WebAppUiManagerImpl::ShowIPHPromoForAppsLaunchedViaLinkCapturing(
    Browser* browser,
    const webapps::AppId& app_id,
    bool is_activated) {
  if (!is_activated) {
    return;
  }

  const auto& feature = GetPromoFeatureEngagementFromBrowser(browser);
  user_education::FeaturePromoParams promo_params(feature, app_id);
  promo_params.close_callback =
      base::BindOnce(&WebAppUiManagerImpl::OnIPHPromoResponseForLinkCapturing,
                     weak_ptr_factory_.GetWeakPtr(), browser, app_id);
  promo_params.show_promo_result_callback =
      base::BindOnce([](user_education::FeaturePromoResult result) {
        if (result) {
          base::RecordAction(
              base::UserMetricsAction("LinkCapturingIPHAppBubbleShown"));
        }
      });

  BrowserUserEducationInterface::From(browser)->MaybeShowFeaturePromo(
      std::move(promo_params));

  // This is only needed for IPH bubbles that are anchored to a tab in a
  // browser. App browsers don't require this logic since tab switching and
  // navigating to another page isn't something to worry about in an app
  // window.
  if (&feature ==
      &feature_engagement::kIPHDesktopPWAsLinkCapturingLaunchAppInTab) {
    WebAppTabHelper* const tab_helper = WebAppTabHelper::FromWebContents(
        browser->tab_strip_model()->GetActiveWebContents());
    CHECK(tab_helper);
    tab_helper->SetCallbackToRunOnTabChanges(base::BindOnce(
        &WebAppUiManagerImpl::OnTabChangedDuringIph,
        weak_ptr_factory_.GetWeakPtr(), base::Unretained(browser)));
  }
}

void WebAppUiManagerImpl::OnIPHPromoResponseForLinkCapturing(
    BrowserWindowInterface* browser,
    const webapps::AppId& app_id) {
  if (!browser) {
    return;
  }

  const auto* const feature_promo_controller =
      UserEducationServiceFactory::GetForBrowserContext(browser->GetProfile())
          ->GetFeaturePromoController(base::PassKey<WebAppUiManagerImpl>());
  if (!feature_promo_controller) {
    return;
  }

  user_education::FeaturePromoClosedReason close_reason;
  feature_promo_controller->HasPromoBeenDismissed(
      {GetPromoFeatureEngagementFromBrowser(browser), app_id}, &close_reason);
  switch (close_reason) {
    case user_education::FeaturePromoClosedReason::kAction:
      base::RecordAction(
          base::UserMetricsAction("LinkCapturingIPHAppBubbleAccepted"));
      WebAppPrefGuardrails::GetForNavigationCapturingIph(
          browser->GetProfile()->GetPrefs())
          .RecordAccept(app_id);
      break;
    case user_education::FeaturePromoClosedReason::kDismiss:
    case user_education::FeaturePromoClosedReason::kCancel:
    // This is needed if the promo is cancelled automatically by the
    // `WebAppTabHelper`.
    case user_education::FeaturePromoClosedReason::kFeatureEngaged:
      base::RecordAction(
          base::UserMetricsAction("LinkCapturingIPHAppBubbleNotAccepted"));
      WebAppPrefGuardrails::GetForNavigationCapturingIph(
          browser->GetProfile()->GetPrefs())
          .RecordDismiss(app_id, base::Time::Now());
      break;
    default:
      break;
  }
}

void WebAppUiManagerImpl::OnTabChangedDuringIph(
    BrowserWindowInterface* browser) {
  const auto& feature =
      feature_engagement::kIPHDesktopPWAsLinkCapturingLaunchAppInTab;
  auto* const user_education = BrowserUserEducationInterface::From(browser);
  if (user_education->IsFeaturePromoQueued(feature)) {
    user_education->AbortFeaturePromo(feature);
  } else if (user_education->IsFeaturePromoActive(feature)) {
    user_education->NotifyFeaturePromoFeatureUsed(
        feature, FeaturePromoFeatureUsedAction::kClosePromoIfPresent);
  }
}

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)


}  // namespace web_app
