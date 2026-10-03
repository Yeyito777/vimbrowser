// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_service/package_id_util.h"

#include <optional>
#include <string>

#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/services/app_service/public/cpp/app_registry_cache.h"
#include "components/services/app_service/public/cpp/app_types.h"
#include "components/services/app_service/public/cpp/app_update.h"
#include "components/services/app_service/public/cpp/package_id.h"
#include "components/services/app_service/public/cpp/types_util.h"


namespace apps_util {


std::optional<std::string> GetAppWithPackageId(
    Profile* profile,
    const apps::PackageId& package_id) {
  apps::AppServiceProxy* proxy =
      apps::AppServiceProxyFactory::GetForProfile(profile);
  if (!proxy) {
    return std::nullopt;
  }

  std::optional<std::string> app_id;
  proxy->AppRegistryCache().ForEachApp(
      [&app_id, package_id](const apps::AppUpdate& update) {
        if (!app_id.has_value() && IsInstalled(update.Readiness()) &&
            update.InstallerPackageId() == package_id) {
          app_id = update.AppId();
        }
      });
  return app_id;
}

}  // namespace apps_util
