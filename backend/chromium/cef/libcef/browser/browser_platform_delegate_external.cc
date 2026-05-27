// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/browser_platform_delegate.h"

#include "base/functional/bind.h"
#include "cef/libcef/browser/thread_util.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/shell_integration.h"
#include "url/gurl.h"

namespace {

void ExecuteExternalProtocol(const GURL& url) {
  CEF_REQUIRE_BLOCKING();

  // Check that an application is associated with the scheme.
  if (shell_integration::GetApplicationNameForScheme(url).empty()) {
    return;
  }

  CEF_POST_TASK(TID_UI, base::BindOnce(&platform_util::OpenExternal, url));
}

}  // namespace

// static
void CefBrowserPlatformDelegate::HandleExternalProtocol(const GURL& url) {
  CEF_POST_USER_VISIBLE_TASK(base::BindOnce(ExecuteExternalProtocol, url));
}
