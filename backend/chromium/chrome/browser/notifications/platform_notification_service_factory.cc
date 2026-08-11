// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/platform_notification_service_factory.h"

#include <memory>

#include "chrome/browser/notifications/platform_notification_service_impl.h"
#include "chrome/browser/profiles/profile.h"

// static
PlatformNotificationServiceImpl*
PlatformNotificationServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<PlatformNotificationServiceImpl*>(
      GetInstance()->GetServiceForBrowserContext(profile, /* create= */ true));
}

// static
PlatformNotificationServiceFactory*
PlatformNotificationServiceFactory::GetInstance() {
  static base::NoDestructor<PlatformNotificationServiceFactory> instance;
  return instance.get();
}

PlatformNotificationServiceFactory::PlatformNotificationServiceFactory()
    : ProfileKeyedServiceFactory(
          "PlatformNotificationService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              // TODO(crbug.com/40257657): Check if this service is needed in
              // Guest mode.
              .WithGuest(ProfileSelection::kOwnInstance)
              // TODO(crbug.com/41488885): Check if this service is needed for
              // Ash Internals.
              .WithAshInternals(ProfileSelection::kOwnInstance)
              .Build()) {}

std::unique_ptr<KeyedService>
PlatformNotificationServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<PlatformNotificationServiceImpl>(
      Profile::FromBrowserContext(context));
}
