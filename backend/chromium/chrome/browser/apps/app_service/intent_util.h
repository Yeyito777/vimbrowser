// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_APP_SERVICE_INTENT_UTIL_H_
#define CHROME_BROWSER_APPS_APP_SERVICE_INTENT_UTIL_H_

#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "components/services/app_service/public/cpp/intent.h"
#include "components/services/app_service/public/cpp/intent_filter.h"


class Profile;

namespace base {
class FilePath;
}  // namespace base

namespace extensions {
class Extension;
}  // namespace extensions

namespace apps_util {

// Creates a file filter.
apps::IntentFilterPtr CreateFileFilter(
    const std::vector<std::string>& intent_actions,
    const std::vector<std::string>& mime_types,
    const std::vector<std::string>& file_extensions,
    const std::string& activity_name = "",
    bool include_directories = false);


// Create intent filters for a Chrome app (extension-based) e.g. for
// file_handlers.
apps::IntentFilters CreateIntentFiltersForChromeApp(
    const extensions::Extension* extension);

// Create intent filters for an Extension (is_extension() == true) e.g. for
// file_browser_handlers.
apps::IntentFilters CreateIntentFiltersForExtension(
    const extensions::Extension* extension);

// Create an intent filter for a note-taking app.
apps::IntentFilterPtr CreateNoteTakingFilter();

// Create an intent filter for an app capable of running on the lock screen.
apps::IntentFilterPtr CreateLockScreenFilter();


}  // namespace apps_util

#endif  // CHROME_BROWSER_APPS_APP_SERVICE_INTENT_UTIL_H_
