// Copyright 2026 The vimbrowser Authors. All rights reserved.

#include "cef/include/internal/cef_export.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/browser_platform_delegate.h"

extern "C" CEF_EXPORT bool vimbrowser_browser_has_fps_sample(int browser_id) {
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  if (!browser || !browser->platform_delegate()) {
    return false;
  }
  return browser->platform_delegate()->HasFpsSample();
}

extern "C" CEF_EXPORT double vimbrowser_get_browser_fps(int browser_id) {
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  if (!browser || !browser->platform_delegate()) {
    return 0.0;
  }
  return browser->platform_delegate()->GetCurrentFps();
}

extern "C" CEF_EXPORT double vimbrowser_get_browser_refresh_rate(
    int browser_id) {
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  if (!browser || !browser->platform_delegate()) {
    return 0.0;
  }
  return browser->platform_delegate()->GetCompositorRefreshRate();
}

extern "C" CEF_EXPORT bool vimbrowser_browser_is_currently_audible(
    int browser_id) {
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  if (!browser || !browser->platform_delegate()) {
    return false;
  }
  return browser->platform_delegate()->IsCurrentlyAudible();
}

extern "C" CEF_EXPORT void vimbrowser_send_browser_command_key_event(
    int browser_id,
    const CefKeyEvent* event) {
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  if (!browser || !browser->platform_delegate() || !event) {
    return;
  }
  browser->platform_delegate()->SendVimbrowserBrowserCommandKeyEvent(*event);
}
