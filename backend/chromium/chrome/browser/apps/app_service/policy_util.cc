// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_service/policy_util.h"

#include <algorithm>
#include <functional>
#include <string_view>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/strings/string_util.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/crx_file/id_util.h"
#include "components/services/app_service/public/cpp/app_update.h"
#include "components/services/app_service/public/cpp/types_util.h"
#include "components/web_package/signed_web_bundles/signed_web_bundle_id.h"


namespace apps_util {

namespace {


}  // namespace

bool IsIsolatedWebAppPolicyId(std::string_view policy_id) {
  return web_package::SignedWebBundleId::Create(policy_id).has_value();
}


std::string TransformRawPolicyId(const std::string& raw_policy_id) {
  if (const GURL raw_policy_id_gurl{raw_policy_id};
      raw_policy_id_gurl.is_valid()) {
    return raw_policy_id_gurl.spec();
  }

  return raw_policy_id;
}

std::vector<std::string> GetAppIdsFromPolicyId(Profile* profile,
                                               const std::string& policy_id) {
  if (!apps::AppServiceProxyFactory::IsAppServiceAvailableForProfile(profile)) {
    return {};
  }
  std::vector<std::string> app_ids;
  apps::AppServiceProxyFactory::GetForProfile(profile)
      ->AppRegistryCache()
      .ForEachApp([&policy_id, &app_ids](const apps::AppUpdate& update) {
        if (IsInstalled(update.Readiness()) &&
            std::ranges::contains(update.PolicyIds(), policy_id)) {
          app_ids.push_back(update.AppId());
        }
      });
  return app_ids;
}

std::optional<std::vector<std::string>> GetPolicyIdsFromAppId(
    Profile* profile,
    const std::string& app_id) {
  if (!apps::AppServiceProxyFactory::IsAppServiceAvailableForProfile(profile)) {
    return std::nullopt;
  }
  std::optional<std::vector<std::string>> policy_ids;
  apps::AppServiceProxyFactory::GetForProfile(profile)
      ->AppRegistryCache()
      .ForOneApp(app_id, [&policy_ids](const apps::AppUpdate& update) {
        policy_ids = update.PolicyIds();
      });
  return policy_ids;
}

}  // namespace apps_util
