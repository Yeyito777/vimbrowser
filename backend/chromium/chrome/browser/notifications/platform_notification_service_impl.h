// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_IMPL_H_
#define CHROME_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_IMPL_H_

#include <stdint.h>

#include <string>

#include "base/memory/raw_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/platform_notification_service.h"

class GURL;
class Profile;

namespace blink {
struct NotificationResources;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

// Minimal compatibility service retained for push-messaging bookkeeping.
// Browser contexts do not expose this service to Blink, and every presentation
// operation is a no-op.
class PlatformNotificationServiceImpl
    : public content::PlatformNotificationService,
      public KeyedService {
 public:
  explicit PlatformNotificationServiceImpl(Profile* profile);
  PlatformNotificationServiceImpl(const PlatformNotificationServiceImpl&) =
      delete;
  PlatformNotificationServiceImpl& operator=(
      const PlatformNotificationServiceImpl&) = delete;
  ~PlatformNotificationServiceImpl() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  bool WasClosedProgrammatically(const std::string& notification_id);

  void DisplayNotification(
      const std::string& notification_id,
      const GURL& origin,
      const GURL& document_url,
      const blink::PlatformNotificationData& notification_data,
      const blink::NotificationResources& notification_resources) override;
  void DisplayPersistentNotification(
      const std::string& notification_id,
      const GURL& service_worker_scope,
      const GURL& origin,
      const blink::PlatformNotificationData& notification_data,
      const blink::NotificationResources& notification_resources) override;
  void CloseNotification(const std::string& notification_id) override;
  void ClosePersistentNotification(const std::string& notification_id) override;
  void GetDisplayedNotifications(
      DisplayedNotificationsCallback callback) override;
  void GetDisplayedNotificationsForOrigin(
      const GURL& origin,
      DisplayedNotificationsCallback callback) override;
  void ScheduleTrigger(base::Time timestamp) override;
  base::Time ReadNextTriggerTimestamp() override;
  int64_t ReadNextPersistentNotificationId() override;
  void RecordNotificationUkmEvent(
      const content::NotificationDatabaseData& data) override;

 private:
  void Shutdown() override;

  raw_ptr<Profile> profile_;
};

#endif  // CHROME_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_IMPL_H_
