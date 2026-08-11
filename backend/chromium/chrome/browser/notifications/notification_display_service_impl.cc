// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_display_service_impl.h"

#include <set>
#include <utility>

#include "chrome/browser/notifications/notification_display_service_factory.h"

// static
NotificationDisplayServiceImpl* NotificationDisplayServiceImpl::GetForProfile(
    Profile* profile) {
  return static_cast<NotificationDisplayServiceImpl*>(
      NotificationDisplayServiceFactory::GetForProfile(profile));
}

// static
void NotificationDisplayServiceImpl::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {}

NotificationDisplayServiceImpl::NotificationDisplayServiceImpl(Profile* profile) {}

NotificationDisplayServiceImpl::~NotificationDisplayServiceImpl() {
  for (auto& observer : observers_)
    observer.OnNotificationDisplayServiceDestroyed(this);
}

void NotificationDisplayServiceImpl::ProcessNotificationOperation(
    NotificationOperation operation,
    NotificationHandler::Type notification_type,
    const GURL& origin,
    const std::string& notification_id,
    const std::optional<int>& action_index,
    const std::optional<std::u16string>& reply,
    const std::optional<bool>& by_user,
    const std::optional<bool>& is_suspicious,
    base::OnceClosure on_completed_cb) {
  if (on_completed_cb)
    std::move(on_completed_cb).Run();
}

void NotificationDisplayServiceImpl::Shutdown() {}

void NotificationDisplayServiceImpl::Display(
    NotificationHandler::Type notification_type,
    const message_center::Notification& notification,
    std::unique_ptr<NotificationCommon::Metadata> metadata) {}

void NotificationDisplayServiceImpl::Close(
    NotificationHandler::Type notification_type,
    const std::string& notification_id) {}

void NotificationDisplayServiceImpl::GetDisplayed(
    DisplayedNotificationsCallback callback) {
  std::move(callback).Run({}, /*supports_synchronization=*/false);
}

void NotificationDisplayServiceImpl::GetDisplayedForOrigin(
    const GURL& origin,
    DisplayedNotificationsCallback callback) {
  std::move(callback).Run({}, /*supports_synchronization=*/false);
}

void NotificationDisplayServiceImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void NotificationDisplayServiceImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

// static
void NotificationDisplayServiceImpl::ProfileLoadedCallback(
    NotificationOperation operation,
    NotificationHandler::Type notification_type,
    const GURL& origin,
    const std::string& notification_id,
    const std::optional<int>& action_index,
    const std::optional<std::u16string>& reply,
    const std::optional<bool>& by_user,
    const std::optional<bool>& is_suspicious,
    base::OnceClosure on_completed_cb,
    Profile* profile) {
  if (on_completed_cb)
    std::move(on_completed_cb).Run();
}
