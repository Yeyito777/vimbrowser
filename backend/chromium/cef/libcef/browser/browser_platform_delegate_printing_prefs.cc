// Copyright 2026 The vimbrowser Authors. All rights reserved.

#include "cef/libcef/browser/browser_platform_delegate_printing_prefs.h"

#include "cef/libcef/browser/browser_context.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"

namespace cef {

bool IsPrintPreviewDisabledForWebContents(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }

  auto cef_browser_context =
      CefBrowserContext::FromBrowserContext(web_contents->GetBrowserContext());
  return cef_browser_context->AsProfile()->GetPrefs()->GetBoolean(
      prefs::kPrintPreviewDisabled);
}

}  // namespace cef
