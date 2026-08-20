// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/browser_management/browser_management_status_provider.h"

#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/profiles/profile.h"
#include "components/enterprise/browser/controller/browser_dm_token_storage.h"
#include "components/policy/core/common/policy_namespace.h"
#include "components/policy/core/common/policy_service.h"


namespace {

bool IsProfileManaged(Profile* profile) {
  return profile && profile->GetProfilePolicyConnector() &&
         profile->GetProfilePolicyConnector()->IsManaged();
}

}  // namespace

BrowserCloudManagementStatusProvider::BrowserCloudManagementStatusProvider() =
    default;

BrowserCloudManagementStatusProvider::~BrowserCloudManagementStatusProvider() =
    default;

EnterpriseManagementAuthority
BrowserCloudManagementStatusProvider::FetchAuthority() {
  // A machine level user cloud policy manager is only created if the browser is
  // managed by CBCM.
  if (g_browser_process->browser_policy_connector()
          ->machine_level_user_cloud_policy_manager() != nullptr) {
    return EnterpriseManagementAuthority::CLOUD_DOMAIN;
  }
  return EnterpriseManagementAuthority::NONE;
}

LocalBrowserManagementStatusProvider::LocalBrowserManagementStatusProvider() =
    default;

LocalBrowserManagementStatusProvider::~LocalBrowserManagementStatusProvider() =
    default;

EnterpriseManagementAuthority
LocalBrowserManagementStatusProvider::FetchAuthority() {
// BrowserPolicyConnector::HasMachineLevelPolicies is not supported on Chrome
// OS.
  return g_browser_process && g_browser_process->browser_policy_connector() &&
                 g_browser_process->browser_policy_connector()
                     ->HasMachineLevelPolicies()
             ? EnterpriseManagementAuthority::COMPUTER_LOCAL
             : EnterpriseManagementAuthority::NONE;
}

LocalDomainBrowserManagementStatusProvider::
    LocalDomainBrowserManagementStatusProvider() = default;

LocalDomainBrowserManagementStatusProvider::
    ~LocalDomainBrowserManagementStatusProvider() = default;

EnterpriseManagementAuthority
LocalDomainBrowserManagementStatusProvider::FetchAuthority() {
  auto result = EnterpriseManagementAuthority::NONE;
// BrowserPolicyConnector::HasMachineLevelPolicies is not supported on Chrome
// OS.
  if (g_browser_process->browser_policy_connector()
          ->HasMachineLevelPolicies()) {
    result = EnterpriseManagementAuthority::COMPUTER_LOCAL;
  }
  return result;
}

ProfileCloudManagementStatusProvider::ProfileCloudManagementStatusProvider(
    Profile* profile)
    : profile_(profile) {}

ProfileCloudManagementStatusProvider::~ProfileCloudManagementStatusProvider() =
    default;

EnterpriseManagementAuthority
ProfileCloudManagementStatusProvider::FetchAuthority() {
  if (IsProfileManaged(profile_))
    return EnterpriseManagementAuthority::CLOUD;
  return EnterpriseManagementAuthority::NONE;
}

LocalTestPolicyUserManagementProvider::LocalTestPolicyUserManagementProvider(
    Profile* profile)
    : profile_(profile) {}

LocalTestPolicyUserManagementProvider::
    ~LocalTestPolicyUserManagementProvider() = default;

EnterpriseManagementAuthority
LocalTestPolicyUserManagementProvider::FetchAuthority() {
  if (!profile_->GetProfilePolicyConnector()
           ->IsUsingLocalTestPolicyProvider()) {
    return EnterpriseManagementAuthority::NONE;
  }
  for (const auto& [_, entry] :
       profile_->GetProfilePolicyConnector()->policy_service()->GetPolicies(
           policy::PolicyNamespace(policy::POLICY_DOMAIN_CHROME,
                                   std::string()))) {
    if (entry.scope == policy::POLICY_SCOPE_USER &&
        entry.source == policy::POLICY_SOURCE_CLOUD) {
      return EnterpriseManagementAuthority::CLOUD;
    }
  }
  return EnterpriseManagementAuthority::NONE;
}

LocalTestPolicyBrowserManagementProvider::
    LocalTestPolicyBrowserManagementProvider(Profile* profile)
    : profile_(profile) {}

LocalTestPolicyBrowserManagementProvider::
    ~LocalTestPolicyBrowserManagementProvider() = default;

EnterpriseManagementAuthority
LocalTestPolicyBrowserManagementProvider::FetchAuthority() {
  if (!profile_->GetProfilePolicyConnector()
           ->IsUsingLocalTestPolicyProvider()) {
    return EnterpriseManagementAuthority::NONE;
  }
  for (const auto& [_, entry] :
       profile_->GetProfilePolicyConnector()->policy_service()->GetPolicies(
           policy::PolicyNamespace(policy::POLICY_DOMAIN_CHROME,
                                   std::string()))) {
    if (entry.scope == policy::POLICY_SCOPE_MACHINE &&
        entry.source == policy::POLICY_SOURCE_CLOUD) {
      return EnterpriseManagementAuthority::CLOUD_DOMAIN;
    }
    if (entry.scope == policy::POLICY_SCOPE_MACHINE &&
        entry.source == policy::POLICY_SOURCE_PLATFORM) {
      return EnterpriseManagementAuthority::DOMAIN_LOCAL;
    }
  }
  return EnterpriseManagementAuthority::NONE;
}
