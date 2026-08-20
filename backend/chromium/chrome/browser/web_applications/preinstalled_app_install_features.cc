// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/preinstalled_app_install_features.h"

#include <string>
#include <string_view>

#include "base/auto_reset.h"
#include "base/feature_list.h"
#include "base/memory/raw_ref.h"
#include "build/build_config.h"


namespace web_app {

namespace {

constexpr const std::string_view kShippedPreinstalledAppInstallFeatures[] = {
    // Enables installing the PWA version of the chrome os calculator instead of
    // the deprecated chrome app.
    "DefaultCalculatorWebApp",

    // Enables migration of default installed GSuite apps over to their
    // replacement web apps.
    "MigrateDefaultChromeAppToWebAppsGSuite",

    // Enables migration of default installed non-GSuite apps over to their
    // replacement web apps.
    "MigrateDefaultChromeAppToWebAppsNonGSuite",

    // Enables installing the Messages app on unmanaged devices.
    "MessagesPreinstall",

    // Enables installing the Cursive device on managed stylus-enabled devices.
    "CursiveManagedStylusPreinstall",
};

bool g_always_enabled_for_testing = false;

// A hard coded list of features available for externally installed apps to
// gate their installation on via their config file settings. See |kFeatureName|
// in preinstalled_web_app_utils.h.
//
// After a feature flag has been shipped and should be cleaned up, move it into
// kShippedPreinstalledAppInstallFeatures to ensure any external installation
// configs that reference it continue to see it as enabled.
constexpr const raw_ref<const base::Feature> kPreinstalledAppInstallFeatures[] =
    {
};

}  // namespace


bool IsPreinstalledWorkspaceStandaloneTabbed(Profile& profile) {
  return false;
}

bool IsPreinstalledAppInstallFeatureEnabled(std::string_view feature_name) {
  if (g_always_enabled_for_testing) {
    return true;
  }

  for (std::string_view feature : kShippedPreinstalledAppInstallFeatures) {
    if (feature == feature_name) {
      return true;
    }
  }

  for (const raw_ref<const base::Feature> feature :
       kPreinstalledAppInstallFeatures) {
    if (feature->name == feature_name) {
      return base::FeatureList::IsEnabled(*feature);
    }
  }

  return false;
}

base::AutoReset<bool>
SetPreinstalledAppInstallFeatureAlwaysEnabledForTesting() {
  return base::AutoReset<bool>(&g_always_enabled_for_testing, true);
}

}  // namespace web_app
