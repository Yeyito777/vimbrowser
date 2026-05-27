// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/browser_platform_delegate.h"

#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/size.h"


gfx::Size CefBrowserPlatformDelegate::GetMaximumDialogSize() {
  if (!web_contents_) {
    return gfx::Size();
  }

  // The dialog should try to fit within the overlay for the web contents.
  // Note that, for things like print preview, this is just a suggested maximum.
  return web_contents_->GetContainerBounds().size();
}
