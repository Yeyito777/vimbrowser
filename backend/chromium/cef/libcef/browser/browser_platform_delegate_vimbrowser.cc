// Copyright 2026 The vimbrowser Authors. All rights reserved.

#include "cef/libcef/browser/browser_platform_delegate.h"

#include "content/public/browser/web_contents.h"

bool CefBrowserPlatformDelegate::HasFpsSample() const {
  return false;
}

double CefBrowserPlatformDelegate::GetCurrentFps() const {
  return 0.0;
}

double CefBrowserPlatformDelegate::GetCompositorRefreshRate() const {
  return 0.0;
}

bool CefBrowserPlatformDelegate::IsCurrentlyAudible() const {
  return web_contents_ && web_contents_->IsCurrentlyAudible();
}

void CefBrowserPlatformDelegate::SendVimbrowserBrowserCommandKeyEvent(
    const CefKeyEvent& event) {
  SendKeyEvent(event);
}
