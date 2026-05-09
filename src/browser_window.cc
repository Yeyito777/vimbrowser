#include "browser_window.h"

#include <utility>

namespace vimbrowser {

BrowserWindow::BrowserWindow(std::string initial_url)
    : initial_url_(std::move(initial_url)), client_(new BrowserClient) {}

void BrowserWindow::Create() {
  CefBrowserSettings browser_settings;
  browser_settings.background_color = CefColorSetARGB(255, 5, 8, 18);

  browser_view_ = CefBrowserView::CreateBrowserView(
      client_, initial_url_, browser_settings, nullptr, nullptr, this);
  browser_view_->SetPreferAccelerators(true);

  CefWindow::CreateTopLevelWindow(this);
}

void BrowserWindow::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window_ = window;
  window_->SetTitle("vimbrowser");
  window_->SetToFillLayout();
  window_->AddChildView(browser_view_);
  window_->CenterWindow(CefSize(1200, 800));
  window_->Show();
  browser_view_->RequestFocus();
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  browser_view_ = nullptr;
  window_ = nullptr;
}

bool BrowserWindow::CanClose(CefRefPtr<CefWindow> window) {
  if (client_ && client_->browser()) {
    client_->browser()->GetHost()->CloseBrowser(false);
  }
  return true;
}

bool BrowserWindow::OnKeyEvent(CefRefPtr<CefWindow> window,
                               const CefKeyEvent& event) {
  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }

  const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
  const bool shift = event.modifiers & EVENTFLAG_SHIFT_DOWN;

  if (ctrl && shift && event.windows_key_code == 'I') {
    client_->ShowDevTools();
    return true;
  }

  return false;
}

CefSize BrowserWindow::GetPreferredSize(CefRefPtr<CefView> view) {
  return CefSize(1200, 800);
}

cef_runtime_style_t BrowserWindow::GetWindowRuntimeStyle() {
  return CEF_RUNTIME_STYLE_ALLOY;
}

cef_runtime_style_t BrowserWindow::GetBrowserRuntimeStyle() {
  return CEF_RUNTIME_STYLE_ALLOY;
}

}  // namespace vimbrowser
