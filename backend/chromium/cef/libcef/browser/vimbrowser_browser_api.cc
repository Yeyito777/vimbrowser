// Copyright 2026 The vimbrowser Authors. All rights reserved.

#include "cef/include/internal/cef_export.h"
#include "cef/libcef/browser/browser_platform_delegate.h"
#include "cef/libcef/browser/browser_platform_delegate_lookup.h"

extern "C" CEF_EXPORT bool vimbrowser_browser_has_fps_sample(int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return false;
  }
  return delegate->HasFpsSample();
}

extern "C" CEF_EXPORT double vimbrowser_get_browser_fps(int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return 0.0;
  }
  return delegate->GetCurrentFps();
}

extern "C" CEF_EXPORT double vimbrowser_get_browser_refresh_rate(
    int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return 0.0;
  }
  return delegate->GetCompositorRefreshRate();
}

extern "C" CEF_EXPORT bool vimbrowser_browser_is_currently_audible(
    int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return false;
  }
  return delegate->IsCurrentlyAudible();
}

extern "C" CEF_EXPORT void vimbrowser_send_browser_command_key_event(
    int browser_id,
    const CefKeyEvent* event) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate || !event) {
    return;
  }
  delegate->SendVimbrowserBrowserCommandKeyEvent(*event);
}
