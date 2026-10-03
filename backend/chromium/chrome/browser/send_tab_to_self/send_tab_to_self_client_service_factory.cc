// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/send_tab_to_self/send_tab_to_self_client_service_factory.h"

#include <string>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/send_tab_to_self/send_tab_to_self_client_service.h"
#include "chrome/browser/sync/send_tab_to_self_sync_service_factory.h"
#include "components/send_tab_to_self/send_tab_to_self_model.h"
#include "components/send_tab_to_self/send_tab_to_self_sync_service.h"


#include "chrome/browser/ui/send_tab_to_self/send_tab_to_self_toolbar_icon_controller.h"

namespace send_tab_to_self {
// static
SendTabToSelfClientService* SendTabToSelfClientServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<send_tab_to_self::SendTabToSelfClientService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
SendTabToSelfClientServiceFactory*
SendTabToSelfClientServiceFactory::GetInstance() {
  static base::NoDestructor<SendTabToSelfClientServiceFactory> instance;
  return instance.get();
}

SendTabToSelfClientServiceFactory::SendTabToSelfClientServiceFactory()
    : ProfileKeyedServiceFactory(
          "SendTabToSelfClientService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              // TODO(crbug.com/40257657): Check if this service is needed in
              // Guest mode.
              .WithGuest(ProfileSelection::kOriginalOnly)
              // TODO(crbug.com/41488885): Check if this service is needed for
              // Ash Internals.
              .WithAshInternals(ProfileSelection::kOriginalOnly)
              .Build()) {
  DependsOn(NotificationDisplayServiceFactory::GetInstance());
  DependsOn(SendTabToSelfSyncServiceFactory::GetInstance());
}

SendTabToSelfClientServiceFactory::~SendTabToSelfClientServiceFactory() =
    default;

// BrowserContextKeyedServiceFactory implementation.
std::unique_ptr<KeyedService>
SendTabToSelfClientServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  SendTabToSelfSyncService* sync_service =
      SendTabToSelfSyncServiceFactory::GetForProfile(profile);


  SendTabToSelfModel* model = sync_service->GetSendTabToSelfModel();
  return std::make_unique<SendTabToSelfClientService>(
      std::make_unique<SendTabToSelfToolbarIconController>(profile)
          ,
      model);
}

bool SendTabToSelfClientServiceFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;
}

bool SendTabToSelfClientServiceFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace send_tab_to_self
