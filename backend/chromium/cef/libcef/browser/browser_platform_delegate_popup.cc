// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/browser_platform_delegate.h"

#include "base/check.h"
#include "cef/include/views/cef_window.h"
#include "cef/include/views/cef_window_delegate.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/views/browser_view_impl.h"
#include "cef/libcef/common/task_runner_impl.h"
#include "content/public/browser/web_contents.h"

namespace {

// Vimbrowser has no generic top-level web-popup window. Document
// Picture-in-Picture is the sole retained floating browsing surface.
class PictureInPictureWindowDelegate : public CefWindowDelegate {
 public:
  explicit PictureInPictureWindowDelegate(
      CefRefPtr<CefBrowserView> browser_view)
      : browser_view_(browser_view) {}

  PictureInPictureWindowDelegate(const PictureInPictureWindowDelegate&) =
      delete;
  PictureInPictureWindowDelegate& operator=(
      const PictureInPictureWindowDelegate&) = delete;

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->AddChildView(browser_view_);
    window->Show();
    browser_view_->RequestFocus();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    browser_view_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
    if (browser) {
      return browser->GetHost()->TryCloseBrowser();
    }
    return true;
  }

  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return browser_view_->GetRuntimeStyle();
  }

 private:
  CefRefPtr<CefBrowserView> browser_view_;

  IMPLEMENT_REFCOUNTING(PictureInPictureWindowDelegate);
};

}  // namespace

void CefBrowserPlatformDelegate::PopupWebContentsCreated(
    const CefBrowserSettings& settings,
    CefRefPtr<CefClient> client,
    content::WebContents* new_web_contents,
    CefBrowserPlatformDelegate* new_platform_delegate,
    bool is_devtools) {
  // Default popup handling may not be Views-hosted.
  if (!new_platform_delegate->IsViewsHosted()) {
    return;
  }

  CefRefPtr<CefBrowserViewDelegate> new_delegate;
  CefRefPtr<CefBrowserViewDelegate> opener_delegate;
  cef_runtime_style_t opener_runtime_style = CEF_RUNTIME_STYLE_DEFAULT;

  auto browser_view = GetBrowserView();
  if (browser_view) {
    // When |this| (the popup opener) is Views-hosted use the current delegate.
    opener_delegate =
        static_cast<CefBrowserViewImpl*>(browser_view.get())->delegate();
  }
  if (!opener_delegate) {
    opener_delegate =
        new_platform_delegate->GetDefaultBrowserViewDelegateForPopupOpener();
  }
  if (opener_delegate) {
    new_delegate = opener_delegate->GetDelegateForPopupBrowserView(
        browser_view, settings, client, is_devtools);
  }

  if (browser_view) {
    opener_runtime_style = browser_view->GetRuntimeStyle();
  } else if (opener_delegate) {
    opener_runtime_style = opener_delegate->GetBrowserRuntimeStyle();
  }

  // Create a new BrowserView for the popup.
  CefRefPtr<CefBrowserViewImpl> new_browser_view =
      CefBrowserViewImpl::CreateForPopup(settings, new_delegate, is_devtools,
                                         opener_runtime_style);

  // Associate the PlatformDelegate with the new BrowserView.
  new_platform_delegate->SetBrowserView(new_browser_view);

  // Keep the BrowserView alive until PopupBrowserCreated() is called.
  new_browser_view->AddRef();
}

void CefBrowserPlatformDelegate::PopupBrowserCreated(
    CefBrowserPlatformDelegate* new_platform_delegate,
    CefBrowserHostBase* new_browser,
    bool is_devtools) {
  // Default popup handling may not be Views-hosted.
  if (!new_platform_delegate->IsViewsHosted()) {
    return;
  }

  auto new_browser_view = new_browser->GetBrowserView();
  CHECK(new_browser_view);

  bool popup_handled = false;

  CefRefPtr<CefBrowserViewDelegate> opener_delegate;
  auto browser_view = GetBrowserView();
  if (browser_view) {
    // When |this| (the popup opener) is Views-hosted use the current delegate.
    opener_delegate =
        static_cast<CefBrowserViewImpl*>(browser_view.get())->delegate();
  }
  if (!opener_delegate) {
    opener_delegate =
        new_platform_delegate->GetDefaultBrowserViewDelegateForPopupOpener();
  }
  if (opener_delegate) {
    popup_handled = opener_delegate->OnPopupBrowserViewCreated(
        browser_view, new_browser_view.get(), is_devtools);
  }

  if (!popup_handled) {
    content::WebContents* web_contents = new_browser->GetWebContents();
    if (web_contents && web_contents->GetPictureInPictureOptions()) {
      CefWindow::CreateTopLevelWindow(
          new PictureInPictureWindowDelegate(new_browser_view.get()));
    } else {
      // Ordinary web popups must be claimed by the opener's BrowserView
      // delegate and embedded as tabs. Fail closed instead of silently escaping
      // into an unmanaged native window when a shell-side invariant regresses.
      // The Alloy creation stack still has BrowserView notifications to deliver,
      // so close asynchronously rather than invalidating its weak view here.
      CEF_POST_TASK(
          CEF_UIT,
          base::BindOnce(
              [](CefRefPtr<CefBrowserHostBase> browser) {
                if (browser) {
                  browser->CloseBrowser(true);
                }
              },
              CefRefPtr<CefBrowserHostBase>(new_browser)));
    }
  }

  // Release the reference added in PopupWebContentsCreated().
  new_browser_view->Release();
}

CefRefPtr<CefBrowserViewDelegate>
CefBrowserPlatformDelegate::GetDefaultBrowserViewDelegateForPopupOpener() {
  return nullptr;
}
