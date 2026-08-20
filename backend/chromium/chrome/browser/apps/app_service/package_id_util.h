// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_APP_SERVICE_PACKAGE_ID_UTIL_H_
#define CHROME_BROWSER_APPS_APP_SERVICE_PACKAGE_ID_UTIL_H_

#include <optional>
#include <string>

#include "build/build_config.h"

class Profile;

namespace apps {

class AppUpdate;
class PackageId;

}  // namespace apps

namespace apps_util {


std::optional<std::string> GetAppWithPackageId(
    Profile* profile,
    const apps::PackageId& package_id);

}  // namespace apps_util

#endif  // CHROME_BROWSER_APPS_APP_SERVICE_PACKAGE_ID_UTIL_H_
