// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/system_notification_helper.h"

#include "chrome/browser/notifications/notification_display_service.h"

namespace {

SystemNotificationHelper* g_instance = nullptr;

}  // namespace

SystemNotificationHelper* SystemNotificationHelper::GetInstance() {
  DCHECK(g_instance);
  return g_instance;
}

SystemNotificationHelper::SystemNotificationHelper() {
  DCHECK(!g_instance);
  g_instance = this;
}

SystemNotificationHelper::~SystemNotificationHelper() {
  DCHECK_EQ(g_instance, this);
  g_instance = nullptr;

  if (system_service_)
    system_service_->Shutdown();
}

void SystemNotificationHelper::Display(
    const message_center::Notification& notification) {}

void SystemNotificationHelper::Close(const std::string& notification_id) {}

void SystemNotificationHelper::SetSystemServiceForTesting(
    std::unique_ptr<NotificationDisplayService> service) {
  system_service_ = std::move(service);
}
