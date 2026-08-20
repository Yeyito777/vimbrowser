// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/lifetime/application_lifetime.h"

#include "base/command_line.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/chrome_switches.h"
#include "ui/aura/client/capture_client.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/views/widget/widget.h"


#if BUILDFLAG(ENABLE_CHROME_NOTIFICATIONS)
#include "chrome/browser/notifications/notification_ui_manager.h"
#endif

namespace chrome {

void HandleAppExitingForPlatform() {
  // Close all non browser windows now. Those includes notifications
  // and windows created by Ash (launcher, background, etc).


#if BUILDFLAG(ENABLE_CHROME_NOTIFICATIONS)
  // This clears existing notifications from the message center and their
  // associated ScopedKeepAlives. Chrome OS doesn't use ScopedKeepAlives for
  // notifications.
  if (auto* notification_ui_manager =
          g_browser_process->notification_ui_manager();
      notification_ui_manager) {
    notification_ui_manager->StartShutdown();
  }
#endif

  views::Widget::CloseAllWidgets();

}

}  // namespace chrome
