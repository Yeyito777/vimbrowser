// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/signals/user_permission_service_factory.h"

#include <memory>

#include "base/no_destructor.h"
#include "build/build_config.h"
#include "chrome/browser/enterprise/browser_management/management_service_factory.h"
#include "chrome/browser/enterprise/signals/user_delegate_impl.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "components/device_signals/core/browser/user_delegate.h"
#include "components/device_signals/core/browser/user_permission_service.h"
#include "components/device_signals/core/browser/user_permission_service_impl.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/policy/core/common/management/management_service.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "content/public/browser/browser_context.h"


#include "chrome/browser/enterprise/connectors/device_trust/device_trust_connector_service.h"
#include "chrome/browser/enterprise/connectors/device_trust/device_trust_connector_service_factory.h"

namespace enterprise_signals {

// static
UserPermissionServiceFactory* UserPermissionServiceFactory::GetInstance() {
  static base::NoDestructor<UserPermissionServiceFactory> instance;
  return instance.get();
}

// static
device_signals::UserPermissionService*
UserPermissionServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<device_signals::UserPermissionService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

UserPermissionServiceFactory::UserPermissionServiceFactory()
    : ProfileKeyedServiceFactory(
          "UserPermissionService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              // TODO(crbug.com/41488885): Check if this service is needed for
              // Ash Internals.
              .WithAshInternals(ProfileSelection::kOwnInstance)
              .Build()) {
  DependsOn(IdentityManagerFactory::GetInstance());
  DependsOn(policy::ManagementServiceFactory::GetInstance());
  DependsOn(
      enterprise_connectors::DeviceTrustConnectorServiceFactory::GetInstance());
}

UserPermissionServiceFactory::~UserPermissionServiceFactory() = default;

std::unique_ptr<KeyedService>
UserPermissionServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  auto* profile = Profile::FromBrowserContext(context);

  device_signals::UserDelegate::SignalsDependencyDelegate*
      signals_dependency_delegate = nullptr;
  signals_dependency_delegate =
      enterprise_connectors::DeviceTrustConnectorServiceFactory::GetForProfile(
          profile);

  if (!signals_dependency_delegate) {
    // Unsupported configuration (e.g. CrOS login Profile supported, but not
    // incognito).
    return nullptr;
  }

  auto* management_service =
      policy::ManagementServiceFactory::GetForProfile(profile);

  auto* identity_manager = IdentityManagerFactory::GetForProfile(profile);

  auto user_delegate = std::make_unique<UserDelegateImpl>(
      profile, identity_manager, signals_dependency_delegate);

  auto user_permission_service =
      std::make_unique<device_signals::UserPermissionServiceImpl>(
          management_service, std::move(user_delegate), profile->GetPrefs());

  return user_permission_service;
}

}  // namespace enterprise_signals
