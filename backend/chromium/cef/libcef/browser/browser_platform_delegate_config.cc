// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/browser_platform_delegate.h"

#include "base/command_line.h"
#include "cef/libcef/browser/browser_context.h"
#include "cef/libcef/common/cef_switches.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/gfx/geometry/size.h"

void CefBrowserPlatformDelegate::SetAccessibilityState(
    cef_state_t accessibility_state) {
  // Do nothing if state is set to default. It'll be disabled by default and
  // controlled by the command-line flags "force-renderer-accessibility" and
  // "disable-renderer-accessibility".
  if (accessibility_state == STATE_DEFAULT) {
    return;
  }

  content::WebContentsImpl* web_contents_impl =
      static_cast<content::WebContentsImpl*>(web_contents_);

  if (!web_contents_impl) {
    return;
  }

  ui::AXMode accMode;
  // In windowless mode set accessibility to TreeOnly mode. Else native
  // accessibility APIs, specific to each platform, are also created.
  if (accessibility_state == STATE_ENABLED) {
    accMode = IsWindowless() ? ui::kAXModeWebContentsOnly : ui::kAXModeComplete;
  }
  web_contents_impl->SetAccessibilityMode(accMode);
}

gfx::Size CefBrowserPlatformDelegate::GetMaximumDialogSize() {
  if (!web_contents_) {
    return gfx::Size();
  }

  // The dialog should try to fit within the overlay for the web contents.
  // Note that, for things like print preview, this is just a suggested maximum.
  return web_contents_->GetContainerBounds().size();
}

bool CefBrowserPlatformDelegate::IsPrintPreviewSupported() const {
  if (IsWindowless()) {
    // Not supported with windowless rendering.
    return false;
  }

  if (web_contents_) {
    auto cef_browser_context = CefBrowserContext::FromBrowserContext(
        web_contents_->GetBrowserContext());
    if (cef_browser_context->AsProfile()->GetPrefs()->GetBoolean(
            prefs::kPrintPreviewDisabled)) {
      // Disabled on the Profile.
      return false;
    }
  }

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDisablePrintPreview)) {
    // Disabled explicitly via the command-line.
    return false;
  }

  const bool default_disabled = IsAlloyStyle();
  if (default_disabled &&
      !command_line->HasSwitch(switches::kEnablePrintPreview)) {
    // Default disabled and not enabled explicitly via the command-line.
    return false;
  }

  return true;
}

bool CefBrowserPlatformDelegate::IsMovePictureInPictureEnabled() const {
  return false;
}

bool CefBrowserPlatformDelegate::AllowPictureInPictureWithoutUserActivation()
    const {
  return false;
}

cef::BrowserConfig CefBrowserPlatformDelegate::GetBrowserConfig() const {
  return {IsWindowless(), IsPrintPreviewSupported(),
          IsMovePictureInPictureEnabled(),
          AllowPictureInPictureWithoutUserActivation()};
}
