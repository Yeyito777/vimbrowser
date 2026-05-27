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
#include "content/public/browser/web_contents.h"

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
