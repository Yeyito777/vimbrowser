// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/browser_platform_delegate.h"

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
