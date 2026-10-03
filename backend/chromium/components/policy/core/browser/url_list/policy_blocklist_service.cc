// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/browser/url_list/policy_blocklist_service.h"

#include <utility>

#include "components/policy/core/common/policy_pref_names.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"


PolicyBlocklistService::PolicyBlocklistService(
    std::unique_ptr<policy::URLBlocklistManager> url_blocklist_manager,
    PrefService* user_prefs)
    : PolicyBlocklistService(std::move(url_blocklist_manager),
                             nullptr,
                             user_prefs) {}
PolicyBlocklistService::PolicyBlocklistService(
    std::unique_ptr<policy::URLBlocklistManager> url_blocklist_manager,
    std::unique_ptr<policy::URLBlocklistManager>
        incognito_url_blocklist_manager,
    PrefService* user_prefs)
    : url_blocklist_manager_(std::move(url_blocklist_manager)),
      incognito_url_blocklist_manager_(
          std::move(incognito_url_blocklist_manager)),
      user_prefs_(user_prefs) {
  CHECK(user_prefs_);
}

PolicyBlocklistService::~PolicyBlocklistService() = default;

policy::URLBlocklist::URLBlocklistState
PolicyBlocklistService::GetURLBlocklistState(const GURL& url) const {
  return GetURLBlocklistStateWithPolicySource(url).url_blocklist_state;
}

PolicyBlocklistService::PolicyBlocklistState
PolicyBlocklistService::GetURLBlocklistStateWithPolicySource(
    const GURL& url) const {
  if (incognito_url_blocklist_manager_) {
    const auto incognito_state =
        incognito_url_blocklist_manager_->GetURLBlocklistState(url);
    if (incognito_state != policy::URLBlocklist::URL_NEUTRAL_STATE) {
      return {
          .url_blocklist_state = incognito_state,
          .policy_source =
              PolicyBlocklistService::PolicyBlocklistState::INCOGNITO_POLICY};
    }
  }
  return {
      .url_blocklist_state = url_blocklist_manager_->GetURLBlocklistState(url),
      .policy_source =
          PolicyBlocklistService::PolicyBlocklistState::URL_POLICY};
}
