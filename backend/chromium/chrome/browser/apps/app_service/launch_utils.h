// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_APP_SERVICE_LAUNCH_UTILS_H_
#define CHROME_BROWSER_APPS_APP_SERVICE_LAUNCH_UTILS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "build/build_config.h"
#include "components/services/app_service/public/cpp/app_launch_params.h"
#include "components/services/app_service/public/cpp/app_launch_util.h"
#include "components/services/app_service/public/cpp/app_types.h"
#include "components/services/app_service/public/cpp/intent.h"
#include "extensions/common/constants.h"
#include "ui/base/window_open_disposition.h"


class Browser;
class Profile;

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace content {
class WebContents;
}  // namespace content

namespace apps {

LaunchContainer ConvertWindowModeToAppLaunchContainer(WindowMode window_mode);

// Converts file arguments to an app on |command_line| into base::FilePaths.
std::vector<base::FilePath> GetLaunchFilesFromCommandLine(
    const base::CommandLine& command_line);

// When a command line launch has an unknown app id, we open a browser with only
// the new tab page.
Browser* CreateBrowserWithNewTabPage(Profile* profile);

// Helper to create AppLaunchParams using event flags that allows user to
// override the user-configured container using modifier keys. |display_id| is
// the id of the display from which the app is launched.
AppLaunchParams CreateAppIdLaunchParamsWithEventFlags(
    const std::string& app_id,
    int event_flags,
    LaunchSource source,
    int64_t display_id,
    LaunchContainer fallback_container);

AppLaunchParams CreateAppLaunchParamsForIntent(
    const std::string& app_id,
    int32_t event_flags,
    LaunchSource source,
    int64_t display_id,
    LaunchContainer fallback_container,
    IntentPtr&& intent,
    Profile* profile);

extensions::AppLaunchSource GetAppLaunchSource(LaunchSource launch_source);

// Returns event flag for |disposition|. If |prefer_container|
// is true, |disposition| will be ignored. Otherwise, an event flag based on
// |disposition| will be returned.
int GetEventFlags(WindowOpenDisposition disposition, bool prefer_container);

// Returns the browser's session id for restoration if |web_contents| is valid
// for a system web app, or for a web app not opened in tab. Otherwise, returns
// an invalid session id.
int GetSessionIdForRestoreFromWebContents(
    const content::WebContents* web_contents);


}  // namespace apps

#endif  // CHROME_BROWSER_APPS_APP_SERVICE_LAUNCH_UTILS_H_
