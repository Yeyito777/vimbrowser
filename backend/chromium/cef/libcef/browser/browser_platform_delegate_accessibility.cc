// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/browser_platform_delegate.h"

#include "content/public/browser/web_contents_accessibility.h"
#include "ui/accessibility/ax_mode.h"

void CefBrowserPlatformDelegate::SetAccessibilityState(
    cef_state_t accessibility_state) {
  // Do nothing if state is set to default. It'll be disabled by default and
  // controlled by the command-line flags "force-renderer-accessibility" and
  // "disable-renderer-accessibility".
  if (accessibility_state == STATE_DEFAULT) {
    return;
  }

  if (!web_contents_) {
    return;
  }

  ui::AXMode accMode;
  // In windowless mode set accessibility to TreeOnly mode. Else native
  // accessibility APIs, specific to each platform, are also created.
  if (accessibility_state == STATE_ENABLED) {
    accMode = IsWindowless() ? ui::kAXModeWebContentsOnly : ui::kAXModeComplete;
  }
  content::SetWebContentsAccessibilityMode(web_contents_, accMode);
}
