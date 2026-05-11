#include "browser_client.h"

#include <iostream>
#include <string>

#include "browser_window.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"

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

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              int httpStatusCode) {
  if (!frame->IsMain()) {
    return;
  }

  if (owner_) {
    owner_->OnClientLoadEnd(this, frame->GetURL().ToString());
  }
  RequestScrollStatus();
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

  CefRefPtr<BrowserClient> self = this;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce([](CefRefPtr<BrowserClient> client) {
        if (client) {
          client->RequestScrollStatus();
        }
      }, std::move(self)),
      120);

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

void BrowserClient::OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser,
                                           int message_id,
                                           bool success,
                                           const void* result,
                                           size_t result_size) {
  if (!success || message_id == 0 || message_id != pending_scroll_message_id_) {
    return;
  }
  pending_scroll_message_id_ = 0;

  CefRefPtr<CefValue> value = CefParseJSON(result, result_size, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) {
    return;
  }

  CefRefPtr<CefDictionaryValue> dict = value->GetDictionary();
  if (!dict || !dict->HasKey("result")) {
    return;
  }

  CefRefPtr<CefDictionaryValue> result_dict = dict->GetDictionary("result");
  if (!result_dict || !result_dict->HasKey("value")) {
    return;
  }

  if (owner_) {
    owner_->OnClientScrollStatus(this, result_dict->GetString("value").ToString());
  }
}

void BrowserClient::ShowDevTools() {
  if (!browser_) {
    return;
  }

  CefWindowInfo window_info;
  CefBrowserSettings settings;
  browser_->GetHost()->ShowDevTools(window_info, this, settings, CefPoint());
}

void BrowserClient::RequestScrollStatus() {
  if (!browser_) {
    return;
  }

  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString(
      "expression",
      "(()=>{const d=document.documentElement,b=document.body;"
      "const y=window.scrollY||d.scrollTop||(b?b.scrollTop:0)||0;"
      "const h=Math.max(d.scrollHeight,b?b.scrollHeight:0,d.clientHeight,window.innerHeight);"
      "const v=window.innerHeight||d.clientHeight||0;"
      "const max=Math.max(0,h-v);"
      "if(max<=0)return 'All';"
      "if(y<=1)return 'Top';"
      "if(y>=max-2)return 'Bot';"
      "return Math.max(0,Math.min(100,Math.round((y/max)*100)))+'%';})()");
  params->SetBool("returnByValue", true);
  pending_scroll_message_id_ = browser_->GetHost()->ExecuteDevToolsMethod(
      0, "Runtime.evaluate", params);
}

}  // namespace vimbrowser
