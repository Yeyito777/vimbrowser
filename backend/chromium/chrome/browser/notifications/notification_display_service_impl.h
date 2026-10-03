// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_DISPLAY_SERVICE_IMPL_H_
#define CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_DISPLAY_SERVICE_IMPL_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/observer_list.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/common/notifications/notification_operation.h"

class GURL;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}

// Compatibility implementation for Chrome features that still share the
// notification display interface. Vimbrowser has no notification presentation
// backend, so display/close operations are deliberately ignored.
class NotificationDisplayServiceImpl : public NotificationDisplayService {
 public:
  explicit NotificationDisplayServiceImpl(Profile* profile);
  NotificationDisplayServiceImpl(const NotificationDisplayServiceImpl&) =
      delete;
  NotificationDisplayServiceImpl& operator=(
      const NotificationDisplayServiceImpl&) = delete;
  ~NotificationDisplayServiceImpl() override;

  static NotificationDisplayServiceImpl* GetForProfile(Profile* profile);
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  virtual void ProcessNotificationOperation(
      NotificationOperation operation,
      NotificationHandler::Type notification_type,
      const GURL& origin,
      const std::string& notification_id,
      const std::optional<int>& action_index,
      const std::optional<std::u16string>& reply,
      const std::optional<bool>& by_user,
      const std::optional<bool>& is_suspicious,
      base::OnceClosure on_completed_cb);

  void Shutdown() override;
  void Display(NotificationHandler::Type notification_type,
               const message_center::Notification& notification,
               std::unique_ptr<NotificationCommon::Metadata> metadata) override;
  void Close(NotificationHandler::Type notification_type,
             const std::string& notification_id) override;
  void GetDisplayed(DisplayedNotificationsCallback callback) override;
  void GetDisplayedForOrigin(const GURL& origin,
                             DisplayedNotificationsCallback callback) override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

  static void ProfileLoadedCallback(
      NotificationOperation operation,
      NotificationHandler::Type notification_type,
      const GURL& origin,
      const std::string& notification_id,
      const std::optional<int>& action_index,
      const std::optional<std::u16string>& reply,
      const std::optional<bool>& by_user,
      const std::optional<bool>& is_suspicious,
      base::OnceClosure on_completed_cb,
      Profile* profile);

 private:
  base::ObserverList<Observer> observers_;
};

#endif  // CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_DISPLAY_SERVICE_IMPL_H_
