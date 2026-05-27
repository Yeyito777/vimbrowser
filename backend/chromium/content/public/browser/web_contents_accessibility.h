// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_WEB_CONTENTS_ACCESSIBILITY_H_
#define CONTENT_PUBLIC_BROWSER_WEB_CONTENTS_ACCESSIBILITY_H_

#include "content/common/content_export.h"

namespace ui {
class AXMode;
}

namespace content {

class WebContents;

// Applies an exact accessibility mode to `web_contents` and broadcasts the
// change to its renderers. This preserves the existing WebContentsImpl behavior
// while keeping embedders that only need to toggle accessibility out of the
// heavy internal WebContentsImpl header.
CONTENT_EXPORT void SetWebContentsAccessibilityMode(WebContents* web_contents,
                                                    const ui::AXMode& mode);

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_WEB_CONTENTS_ACCESSIBILITY_H_
