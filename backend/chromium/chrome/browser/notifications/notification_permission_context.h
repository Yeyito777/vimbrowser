// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_PERMISSION_CONTEXT_H_
#define CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_PERMISSION_CONTEXT_H_

#include <memory>

#include "components/content_settings/core/common/content_settings.h"
#include "components/permissions/content_setting_permission_context_base.h"

class GURL;

// Denied compatibility context for the removed Notifications API. Keeping the
// content-setting type registered makes shared permission code deterministic
// without exposing a prompt or a presentation backend.
class NotificationPermissionContext
    : public permissions::ContentSettingPermissionContextBase {
 public:
  static void UpdatePermission(content::BrowserContext* browser_context,
                               const GURL& origin,
                               ContentSetting setting);

  explicit NotificationPermissionContext(
      content::BrowserContext* browser_context);
  ~NotificationPermissionContext() override;

  ContentSetting GetContentSettingStatusInternal(
      content::RenderFrameHost* render_frame_host,
      const GURL& requesting_origin,
      const GURL& embedding_origin) const override;

 private:
  void DecidePermission(
      std::unique_ptr<permissions::PermissionRequestData> request_data,
      permissions::BrowserPermissionCallback callback) override;
  void UpdateTabContext(const permissions::PermissionRequestData& request_data,
                        bool allowed) override;
};

#endif  // CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_PERMISSION_CONTEXT_H_
