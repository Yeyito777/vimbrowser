// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/platform_notification_service_impl.h"

#include <set>
#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

// static
void PlatformNotificationServiceImpl::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(prefs::kNotificationNextPersistentId, 10000);
}

PlatformNotificationServiceImpl::PlatformNotificationServiceImpl(Profile* profile)
    : profile_(profile) {}

PlatformNotificationServiceImpl::~PlatformNotificationServiceImpl() = default;

void PlatformNotificationServiceImpl::Shutdown() {
  profile_ = nullptr;
}

bool PlatformNotificationServiceImpl::WasClosedProgrammatically(
    const std::string& notification_id) {
  return false;
}

void PlatformNotificationServiceImpl::DisplayNotification(
    const std::string& notification_id,
    const GURL& origin,
    const GURL& document_url,
    const blink::PlatformNotificationData& notification_data,
    const blink::NotificationResources& notification_resources) {}

void PlatformNotificationServiceImpl::DisplayPersistentNotification(
    const std::string& notification_id,
    const GURL& service_worker_scope,
    const GURL& origin,
    const blink::PlatformNotificationData& notification_data,
    const blink::NotificationResources& notification_resources) {}

void PlatformNotificationServiceImpl::CloseNotification(
    const std::string& notification_id) {}

void PlatformNotificationServiceImpl::ClosePersistentNotification(
    const std::string& notification_id) {}

void PlatformNotificationServiceImpl::GetDisplayedNotifications(
    DisplayedNotificationsCallback callback) {
  std::move(callback).Run({}, /*supports_synchronization=*/false);
}

void PlatformNotificationServiceImpl::GetDisplayedNotificationsForOrigin(
    const GURL& origin,
    DisplayedNotificationsCallback callback) {
  std::move(callback).Run({}, /*supports_synchronization=*/false);
}

void PlatformNotificationServiceImpl::ScheduleTrigger(base::Time timestamp) {}

base::Time PlatformNotificationServiceImpl::ReadNextTriggerTimestamp() {
  return base::Time::Max();
}

int64_t PlatformNotificationServiceImpl::ReadNextPersistentNotificationId() {
  if (!profile_)
    return 0;

  PrefService* prefs = profile_->GetPrefs();
  const int64_t next_id =
      prefs->GetInteger(prefs::kNotificationNextPersistentId) + 1;
  prefs->SetInteger(prefs::kNotificationNextPersistentId, next_id);
  return next_id;
}

void PlatformNotificationServiceImpl::RecordNotificationUkmEvent(
    const content::NotificationDatabaseData& data) {}
