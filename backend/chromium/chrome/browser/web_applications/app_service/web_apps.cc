// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/app_service/web_apps.h"

#include <utility>

#include "chrome/common/web_app_id_constants.h"
#include "base/feature_list.h"
#include "base/memory/weak_ptr.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/browser/apps/app_service/app_icon/app_icon_factory.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_app_constants.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "components/services/app_service/public/cpp/app_launch_params.h"
#include "components/services/app_service/public/cpp/icon_effects.h"
#include "components/services/app_service/public/cpp/intent_util.h"


using apps::IconEffects;

namespace web_app {

WebApps::WebApps(apps::AppServiceProxy* proxy)
    : apps::AppPublisher(proxy),
      profile_(proxy->profile()),
      provider_(WebAppProvider::GetForLocalAppsUnchecked(profile_)),
      publisher_helper_(profile_, provider_, this) {
  Initialize();
}

WebApps::~WebApps() = default;

void WebApps::Shutdown() {
  if (provider_) {
    publisher_helper().Shutdown();
  }
}

const WebApp* WebApps::GetWebApp(const webapps::AppId& app_id) const {
  DCHECK(provider_);
  return provider_->registrar_unsafe().GetAppById(app_id);
}

void WebApps::Initialize() {
  DCHECK(profile_);

  // In some tests, WebAppPublisherHelper could be created during the shutdown
  // stage as the web app publisher is created async by AppServiceProxy. So
  // provider_ could be null in some tests.
  if (!AreWebAppsEnabled(profile_) || !provider_) {
    return;
  }

  provider_->on_registry_ready().Post(
      FROM_HERE,
      base::BindOnce(&WebApps::InitWebApps, weak_ptr_factory_.GetWeakPtr()));
}

void WebApps::LoadIcon(const std::string& app_id,
                       const apps::IconKey& icon_key,
                       apps::IconType icon_type,
                       int32_t size_hint_in_dip,
                       bool allow_placeholder_icon,
                       apps::LoadIconCallback callback) {
  publisher_helper().LoadIcon(app_id, icon_type, size_hint_in_dip,
                              static_cast<IconEffects>(icon_key.icon_effects),
                              std::move(callback));
}


void WebApps::Launch(const std::string& app_id,
                     int32_t event_flags,
                     apps::LaunchSource launch_source,
                     apps::WindowInfoPtr window_info) {
  publisher_helper().Launch(app_id, event_flags, launch_source,
                            std::move(window_info), base::DoNothing());
}

void WebApps::LaunchAppWithFiles(const std::string& app_id,
                                 int32_t event_flags,
                                 apps::LaunchSource launch_source,
                                 std::vector<base::FilePath> file_paths) {
  publisher_helper().LaunchAppWithFiles(app_id, event_flags, launch_source,
                                        std::move(file_paths));
}

void WebApps::LaunchAppWithIntent(const std::string& app_id,
                                  int32_t event_flags,
                                  apps::IntentPtr intent,
                                  apps::LaunchSource launch_source,
                                  apps::WindowInfoPtr window_info,
                                  apps::LaunchCallback callback) {
  publisher_helper().LaunchAppWithIntent(app_id, event_flags, std::move(intent),
                                         launch_source, std::move(window_info),
                                         std::move(callback));
}

void WebApps::LaunchAppWithParams(apps::AppLaunchParams&& params,
                                  apps::LaunchCallback callback) {
  publisher_helper().LaunchAppWithParams(
      std::move(params),
      base::BindOnce(
          [](apps::LaunchCallback callback,
             content::WebContents* web_contents) {
            apps::LaunchResult::State result =
                web_contents ? apps::LaunchResult::State::kSuccess
                             : apps::LaunchResult::State::kFailed;
            std::move(callback).Run(apps::LaunchResult(result));
          },
          std::move(callback)));
}

void WebApps::SetPermission(const std::string& app_id,
                            apps::PermissionPtr permission) {
  publisher_helper().SetPermission(app_id, std::move(permission));
}

void WebApps::Uninstall(const std::string& app_id,
                        apps::UninstallSource uninstall_source,
                        bool clear_site_data,
                        bool report_abuse) {
  const WebApp* web_app = GetWebApp(app_id);
  if (!web_app) {
    return;
  }

  publisher_helper().UninstallWebApp(web_app, uninstall_source, clear_site_data,
                                     report_abuse);
}


void WebApps::UpdateAppSize(const std::string& app_id) {
  publisher_helper().UpdateAppSize(app_id);
}

void WebApps::SetWindowMode(const std::string& app_id,
                            apps::WindowMode window_mode) {
  publisher_helper().SetWindowMode(app_id, window_mode);
}

void WebApps::OpenNativeSettings(const std::string& app_id) {
  publisher_helper().OpenNativeSettings(app_id);
}

void WebApps::PublishWebApps(std::vector<apps::AppPtr> apps) {
  if (!is_ready_) {
    return;
  }

  if (apps.empty()) {
    return;
  }

  apps::AppPublisher::Publish(std::move(apps), apps::AppType::kWeb,
                              /*should_notify_initialized=*/false);

}

void WebApps::PublishWebApp(apps::AppPtr app) {
  if (!is_ready_) {
    return;
  }


  apps::AppPublisher::Publish(std::move(app));

}

void WebApps::ModifyWebAppCapabilityAccess(
    const std::string& app_id,
    std::optional<bool> accessing_camera,
    std::optional<bool> accessing_microphone) {
  apps::AppPublisher::ModifyCapabilityAccess(
      app_id, std::move(accessing_camera), std::move(accessing_microphone));
}

std::vector<apps::AppPtr> WebApps::CreateWebApps() {
  DCHECK(provider_);

  std::vector<apps::AppPtr> apps;
  for (const WebApp& web_app : provider_->registrar_unsafe().GetApps()) {
    apps.push_back(publisher_helper().CreateWebApp(&web_app));
  }
  return apps;
}

void WebApps::InitWebApps() {
  TRACE_EVENT0("ui", "WebApps::InitWebApps");
  is_ready_ = true;

  RegisterPublisher(apps::AppType::kWeb);

  std::vector<apps::AppPtr> apps = CreateWebApps();

  apps::AppPublisher::Publish(std::move(apps), apps::AppType::kWeb,
                              /*should_notify_initialized=*/true);
}


}  // namespace web_app
