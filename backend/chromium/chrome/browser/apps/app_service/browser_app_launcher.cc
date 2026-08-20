// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_service/browser_app_launcher.h"

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/extensions/app_launch_params.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_util.h"
#include "extensions/common/extension.h"
#include "url/gurl.h"


namespace {

void OnLaunchCompleteReportRestoreMetrics(
    base::OnceCallback<void(content::WebContents*)> callback,
    Profile* profile,
    int restore_id,
    apps::AppLaunchParams params_for_restore,
    base::WeakPtr<Browser> browser,
    base::WeakPtr<content::WebContents> web_contents,
    apps::LaunchContainer launch_container) {
  std::move(callback).Run(web_contents.get());
}

void LaunchAppWithParamsImpl(
    apps::AppLaunchParams params,
    Profile* profile,
    base::OnceCallback<void(content::WebContents*)> on_complete) {
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile)->GetInstalledExtension(
          params.app_id);

  apps::AppLaunchParams params_for_restore(
      params.app_id, params.container, params.disposition, params.launch_source,
      params.display_id, params.launch_files, params.intent);
  int restore_id = params.restore_id;
  std::string app_id = params.app_id;

  if (!extension) {
    web_app::WebAppProvider* provider =
        web_app::WebAppProvider::GetForLocalAppsUnchecked(profile);
    provider->scheduler().LaunchAppWithCustomParams(
        std::move(params),
        base::BindOnce(OnLaunchCompleteReportRestoreMetrics,
                       std::move(on_complete), profile, restore_id,
                       std::move(params_for_restore)));
    return;
  }

  std::move(on_complete).Run(::OpenApplication(profile, std::move(params)));
}
}  // namespace

namespace apps {

BrowserAppLauncher::BrowserAppLauncher(Profile* profile) : profile_(profile) {}

BrowserAppLauncher::~BrowserAppLauncher() = default;

content::WebContents* BrowserAppLauncher::LaunchAppWithParamsForTesting(
    AppLaunchParams params) {
  // For some ChromeOS tests (and specifically ones that use SpeechMonitor),
  // they use a base::RunLoop already to wait for accessibility tasks to
  // complete. Because that makes this base::RunLoop nested,
  // `kNestableTasksAllowed` is required to allow the posted launch command to
  // execute, as it is not a system task.
  base::RunLoop launch_waiter(base::RunLoop::Type::kNestableTasksAllowed);
  content::WebContents* web_contents_holder;
  LaunchAppWithParamsImpl(
      std::move(params), profile_,
      base::BindOnce(
          [](base::OnceClosure done, content::WebContents** output,
             content::WebContents* contents) {
            *output = contents;
            std::move(done).Run();
          },
          launch_waiter.QuitClosure(), &web_contents_holder));
  launch_waiter.Run();
  return web_contents_holder;
}

}  // namespace apps
