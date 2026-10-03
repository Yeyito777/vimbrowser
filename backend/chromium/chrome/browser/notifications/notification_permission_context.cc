// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_permission_context.h"

#include <utility>

#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "components/content_settings/browser/page_specific_content_settings.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/permission_result.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom.h"
#include "third_party/blink/public/mojom/permissions/permission_status.mojom.h"
#include "url/gurl.h"

// static
void NotificationPermissionContext::UpdatePermission(
    content::BrowserContext* browser_context,
    const GURL& origin,
    ContentSetting setting) {
  HostContentSettingsMapFactory::GetForProfile(browser_context)
      ->SetContentSettingDefaultScope(origin, GURL(),
                                     ContentSettingsType::NOTIFICATIONS,
                                     CONTENT_SETTING_BLOCK);
}

NotificationPermissionContext::NotificationPermissionContext(
    content::BrowserContext* browser_context)
    : ContentSettingPermissionContextBase(
          browser_context,
          ContentSettingsType::NOTIFICATIONS,
          network::mojom::PermissionsPolicyFeature::kNotFound) {}

NotificationPermissionContext::~NotificationPermissionContext() = default;

ContentSetting NotificationPermissionContext::GetContentSettingStatusInternal(
    content::RenderFrameHost* render_frame_host,
    const GURL& requesting_origin,
    const GURL& embedding_origin) const {
  content_settings::PageSpecificContentSettings::NotificationsAccessed(
      render_frame_host, /*blocked=*/true);
  return CONTENT_SETTING_BLOCK;
}

void NotificationPermissionContext::DecidePermission(
    std::unique_ptr<permissions::PermissionRequestData> request_data,
    permissions::BrowserPermissionCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::move(callback).Run(content::PermissionResult(
      blink::mojom::PermissionStatus::DENIED,
      content::PermissionStatusSource::UNSPECIFIED));
}

void NotificationPermissionContext::UpdateTabContext(
    const permissions::PermissionRequestData& request_data,
    bool allowed) {
  auto* content_settings =
      content_settings::PageSpecificContentSettings::GetForFrame(
          request_data.id.global_render_frame_host_id());
  if (content_settings) {
    content_settings->OnContentBlocked(ContentSettingsType::NOTIFICATIONS);
  }
}
