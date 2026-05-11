#include "browser_client.h"

#include <iostream>
#include <string>

#include "browser_window.h"
#include "include/cef_app.h"

namespace vimbrowser {

BrowserClient::BrowserClient(BrowserWindow* owner) : owner_(owner) {}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  devtools_registration_ = browser->GetHost()->AddDevToolsMessageObserver(this);
  if (owner_) {
    owner_->OnClientBrowserCreated(this);
  }
  std::cout << "vimbrowser: browser ready; CDP available on remote debugging port" << std::endl;
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  devtools_registration_ = nullptr;
  browser_ = nullptr;
  CefQuitMessageLoop();
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                ErrorCode error_code,
                                const CefString& error_text,
                                const CefString& failed_url) {
  if (!frame->IsMain()) {
    return;
  }

  std::cerr << "vimbrowser: load failed: " << failed_url.ToString() << " "
            << error_text.ToString() << std::endl;
}

bool BrowserClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                  const CefKeyEvent& event,
                                  CefEventHandle os_event,
                                  bool* is_keyboard_shortcut) {
  if (owner_ && owner_->HandleBrowserKeyEvent(event)) {
    return true;
  }

  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }

  const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
  const bool shift = event.modifiers & EVENTFLAG_SHIFT_DOWN;

  // First tiny native command surface. Ctrl+Shift+I opens DevTools just like
  // Chromium; this proves the CEF/CDP path is alive before we build the vim UI.
  if (ctrl && shift && event.windows_key_code == 'I') {
    ShowDevTools();
    return true;
  }

  return false;
}

bool BrowserClient::OnDevToolsMessage(CefRefPtr<CefBrowser> browser,
                                      const void* message,
                                      size_t message_size) {
  // Hook point for native CDP features. Keep this quiet by default; returning
  // false lets CEF continue normal handling.
  return false;
}

void BrowserClient::ShowDevTools() {
  if (!browser_) {
    return;
  }

  CefWindowInfo window_info;
  CefBrowserSettings settings;
  browser_->GetHost()->ShowDevTools(window_info, this, settings, CefPoint());
}

}  // namespace vimbrowser
