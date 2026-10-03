// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/system_private/system_private_api.h"

#include <array>
#include <memory>
#include <utility>

#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/event_router_forwarder.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/system_private.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/prefs/pref_service.h"
#include "google_apis/google_api_keys.h"

#include "chrome/browser/upgrade_detector/upgrade_detector.h"

namespace {

// Maps policy::policy_prefs::kIncognitoModeAvailability values (0 = enabled,
// ...) to strings exposed to extensions.
constexpr auto kIncognitoModeAvailabilityStrings = std::to_array<const char*>({
    "enabled",
    "disabled",
    "forced",
});

// Property keys.
const char kDownloadProgressKey[] = "downloadProgress";
const char kStateKey[] = "state";

// System update states.
const char kNotAvailableState[] = "NotAvailable";
const char kNeedRestartState[] = "NeedRestart";


}  // namespace

namespace extensions {

namespace system_private = api::system_private;

ExtensionFunction::ResponseAction
SystemPrivateGetIncognitoModeAvailabilityFunction::Run() {
  PrefService* prefs =
      Profile::FromBrowserContext(browser_context())->GetPrefs();
  int value =
      prefs->GetInteger(policy::policy_prefs::kIncognitoModeAvailability);
  EXTENSION_FUNCTION_VALIDATE(
      value >= 0 &&
      value < static_cast<int>(std::size(kIncognitoModeAvailabilityStrings)));
  return RespondNow(WithArguments(kIncognitoModeAvailabilityStrings[value]));
}

ExtensionFunction::ResponseAction SystemPrivateGetUpdateStatusFunction::Run() {
  std::string state;
  double download_progress = 0;
  if (UpgradeDetector::GetInstance()->notify_upgrade()) {
    state = kNeedRestartState;
    download_progress = 1;
  } else {
    state = kNotAvailableState;
  }

  base::DictValue dict;
  dict.Set(kStateKey, state);
  dict.Set(kDownloadProgressKey, download_progress);
  return RespondNow(WithArguments(std::move(dict)));
}

ExtensionFunction::ResponseAction SystemPrivateGetApiKeyFunction::Run() {
  return RespondNow(WithArguments(google_apis::GetAPIKey()));
}

}  // namespace extensions
