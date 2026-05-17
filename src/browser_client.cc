#include "browser_client.h"

#include <iostream>
#include <string_view>

#include "browser_window.h"
#include "include/cef_app.h"

extern "C" bool vimbrowser_browser_has_fps_sample(int browser_id);
extern "C" double vimbrowser_get_browser_fps(int browser_id);
extern "C" double vimbrowser_get_browser_refresh_rate(int browser_id);
extern "C" void vimbrowser_send_browser_command_key_event(
    int browser_id,
    const CefKeyEvent* event);

namespace vimbrowser {

BrowserClient::BrowserClient(BrowserWindow* owner) : owner_(owner) {}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (owner_) {
    owner_->OnClientBrowserCreated(this);
  }
  std::cout << "vimbrowser: browser ready" << std::endl;
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  browser_ = nullptr;
}

void BrowserClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                TransitionType transition_type) {
  if (owner_ && frame && frame->IsMain()) {
    owner_->OnClientLoadStart(this, frame->GetURL().ToString());
  }
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

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  int popup_id,
                                  const CefString& target_url,
                                  const CefString& target_frame_name,
                                  WindowOpenDisposition target_disposition,
                                  bool user_gesture,
                                  const CefPopupFeatures& popupFeatures,
                                  CefWindowInfo& windowInfo,
                                  CefRefPtr<CefClient>& client,
                                  CefBrowserSettings& settings,
                                  CefRefPtr<CefDictionaryValue>& extra_info,
                                  bool* no_javascript_access) {
  if (!owner_) {
    return false;
  }

  const bool activate = target_disposition != CEF_WOD_NEW_BACKGROUND_TAB;
  return owner_->OnClientBeforePopup(this, target_url.ToString(), activate);
}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                     cef_log_severity_t level,
                                     const CefString& message,
                                     const CefString& source,
                                     int line) {
  const std::string text = message.ToString();
  if (!source.ToString().empty()) {
    return false;
  }

  constexpr std::string_view kOpenTabPrefix =
      "__vimbrowser_native_hint_open_tab__";
  if (text.rfind(kOpenTabPrefix, 0) == 0) {
    if (owner_) {
      owner_->OnNativeHintOpenTab(this, text.substr(kOpenTabPrefix.size()));
    }
    return true;
  }
  if (text == "__vimbrowser_native_hints_stopped__") {
    if (owner_) {
      owner_->OnNativeHintsStopped(this);
    }
    return true;
  }
  return false;
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

void BrowserClient::ShowDevTools() {
  if (!browser_) {
    return;
  }

  CefWindowInfo window_info;
  CefBrowserSettings settings;
  browser_->GetHost()->ShowDevTools(window_info, this, settings, CefPoint());
}

double BrowserClient::current_fps() const {
  return browser_ ? vimbrowser_get_browser_fps(browser_->GetIdentifier()) : 0.0;
}

bool BrowserClient::fps_has_sample() const {
  return browser_ && vimbrowser_browser_has_fps_sample(browser_->GetIdentifier());
}

double BrowserClient::compositor_refresh_rate() const {
  return browser_ ? vimbrowser_get_browser_refresh_rate(browser_->GetIdentifier())
                  : 0.0;
}

void BrowserClient::SendBrowserCommandKeyEvent(const CefKeyEvent& event) {
  if (browser_) {
    vimbrowser_send_browser_command_key_event(browser_->GetIdentifier(), &event);
  }
}

}  // namespace vimbrowser
