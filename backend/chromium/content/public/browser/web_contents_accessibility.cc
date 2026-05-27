// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/web_contents_accessibility.h"

#include "content/browser/web_contents/web_contents_impl.h"
#include "ui/accessibility/ax_mode.h"

namespace content {

void SetWebContentsAccessibilityMode(WebContents* web_contents,
                                     const ui::AXMode& mode) {
  if (!web_contents) {
    return;
  }

  static_cast<WebContentsImpl*>(web_contents)->SetAccessibilityMode(mode);
}

}  // namespace content
