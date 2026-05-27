// Copyright 2026 The vimbrowser Authors. All rights reserved.

#ifndef CEF_LIBCEF_BROWSER_BROWSER_PLATFORM_DELEGATE_LOOKUP_H_
#define CEF_LIBCEF_BROWSER_BROWSER_PLATFORM_DELEGATE_LOOKUP_H_

class CefBrowserPlatformDelegate;

namespace cef {

CefBrowserPlatformDelegate* GetBrowserPlatformDelegateForBrowserId(
    int browser_id);

}  // namespace cef

#endif  // CEF_LIBCEF_BROWSER_BROWSER_PLATFORM_DELEGATE_LOOKUP_H_
