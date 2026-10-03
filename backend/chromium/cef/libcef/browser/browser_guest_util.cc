// Copyright (c) 2015 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "cef/libcef/browser/browser_guest_util.h"

#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/browser/web_contents/web_contents_impl.h"

namespace {

content::WebContents* GetOwnerForBrowserPluginGuest(
    const content::WebContents* guest) {
  auto* guest_impl = static_cast<const content::WebContentsImpl*>(guest);
  content::BrowserPluginGuest* plugin_guest =
      guest_impl->GetBrowserPluginGuest();
  if (plugin_guest) {
    return plugin_guest->owner_web_contents();
  }
  return nullptr;
}

}  // namespace

content::WebContents* GetOwnerForGuestContents(
    const content::WebContents* guest) {
  // Maybe it's a guest view. This occurs while loading the PDF viewer.
  if (auto* owner = GetOwnerForBrowserPluginGuest(guest)) {
    return owner;
  }

  return nullptr;
}

bool IsBrowserPluginGuest(const content::WebContents* web_contents) {
  return !!GetOwnerForBrowserPluginGuest(web_contents);
}

bool IsPrintPreviewDialog(const content::WebContents* web_contents) {
  return false;
}
