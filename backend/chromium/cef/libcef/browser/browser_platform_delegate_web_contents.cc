// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/browser_platform_delegate.h"

#include "base/check.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"

content::WebContents* CefBrowserPlatformDelegate::CreateWebContents(
    CefBrowserCreateParams& create_params,
    bool& own_web_contents) {
  DCHECK(false);
  return nullptr;
}

void CefBrowserPlatformDelegate::CreateViewForWebContents(
    raw_ptr<content::WebContentsView>* view,
    raw_ptr<content::RenderViewHostDelegateView>* delegate_view) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::WebContentsCreated(
    content::WebContents* web_contents,
    bool owned) {
  // We should not have a browser at this point.
  DCHECK(!browser_);

  DCHECK(!web_contents_);
  web_contents_ = web_contents;

  // Enable support for draggable regions.
  // TODO(cef): This has performance consequences so consider making it
  // configurable (e.g. only enabled for frameless windows). See issue #3636.
  web_contents->SetSupportsDraggableRegions(true);
}

void CefBrowserPlatformDelegate::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::WebContentsDestroyed(
    content::WebContents* web_contents) {
  DCHECK(web_contents_ && web_contents_ == web_contents);
  web_contents_ = nullptr;
}

void CefBrowserPlatformDelegate::RenderViewCreated(
    content::RenderViewHost* render_view_host) {
  // Indicate that the view has an external parent (namely us). This setting is
  // required for proper focus handling on Windows and Linux.
  if (HasExternalParent() && render_view_host->GetWidget()->GetView()) {
    render_view_host->GetWidget()->GetView()->SetHasExternalParent(true);
  }
}

void CefBrowserPlatformDelegate::RenderViewReady() {}
