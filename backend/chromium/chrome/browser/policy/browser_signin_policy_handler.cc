// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/browser_signin_policy_handler.h"

#include <memory>

#include "base/command_line.h"
#include "base/syslog_logging.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/common/pref_names.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_value_map.h"
#include "components/signin/public/base/signin_pref_names.h"

namespace policy {

BrowserSigninPolicyHandler::BrowserSigninPolicyHandler(Schema chrome_schema)
    : IntRangePolicyHandler(key::kBrowserSignin,
                            prefs::kForceBrowserSignin,
                            static_cast<int>(BrowserSigninMode::kDisabled),
                            static_cast<int>(BrowserSigninMode::kForced),
                            false /* clamp */) {}

BrowserSigninPolicyHandler::~BrowserSigninPolicyHandler() = default;

void BrowserSigninPolicyHandler::ApplyPolicySettings(const PolicyMap& policies,
                                                     PrefValueMap* prefs) {

  const base::Value* value =
      policies.GetValue(policy_name(), base::Value::Type::INTEGER);
  switch (static_cast<BrowserSigninMode>(value->GetInt())) {
    case BrowserSigninMode::kForced:
      prefs->SetValue(prefs::kForceBrowserSignin, base::Value(true));
      [[fallthrough]];
    case BrowserSigninMode::kEnabled:
      prefs->SetValue(
          prefs::kSigninAllowedOnNextStartup,
          base::Value(true));
      break;
    case BrowserSigninMode::kDisabled:
      prefs->SetValue(
          prefs::kSigninAllowedOnNextStartup,
          base::Value(false));
      break;
  }
}

}  // namespace policy
