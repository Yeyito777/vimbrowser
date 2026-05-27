// Copyright 2026 The vimbrowser Authors. All rights reserved.

#include "cef/libcef/browser/browser_platform_delegate_lookup.h"

#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/browser_platform_delegate.h"

namespace cef {

CefBrowserPlatformDelegate* GetBrowserPlatformDelegateForBrowserId(
    int browser_id) {
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  return browser ? browser->platform_delegate() : nullptr;
}

}  // namespace cef
