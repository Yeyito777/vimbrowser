// Copyright 2026 The vimbrowser Authors. All rights reserved.

#ifndef CEF_LIBCEF_BROWSER_BROWSER_PLATFORM_DELEGATE_PRINTING_PREFS_H_
#define CEF_LIBCEF_BROWSER_BROWSER_PLATFORM_DELEGATE_PRINTING_PREFS_H_

namespace content {
class WebContents;
}

namespace cef {

bool IsPrintPreviewDisabledForWebContents(content::WebContents* web_contents);

}  // namespace cef

#endif  // CEF_LIBCEF_BROWSER_BROWSER_PLATFORM_DELEGATE_PRINTING_PREFS_H_
