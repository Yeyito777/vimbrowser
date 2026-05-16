#include "browser_client.h"

#include <cmath>
#include <iostream>
#include <string>

#include "include/base/cef_callback.h"
#include "browser_window.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

constexpr const char* kFpsTracingCategories =
    "devtools.timeline,disabled-by-default-devtools.timeline.frame,cc,benchmark";

bool IsFrameTraceEvent(CefRefPtr<CefDictionaryValue> event) {
  if (!event) {
    return false;
  }
  // Chromium's tracing backend emits one DrawFrame event per presented page
  // frame for the target. This is backend/compositor telemetry, not page JS.
  return event->GetString("name").ToString() == "DrawFrame";
}

}  // namespace

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
  SetFpsTrackingEnabled(false);
  devtools_registration_ = nullptr;
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
  if (fps_tracking_enabled_ && message && message_size > 0) {
    CefRefPtr<CefValue> value = CefParseJSON(message, message_size, JSON_PARSER_RFC);
    CefRefPtr<CefDictionaryValue> root = value ? value->GetDictionary() : nullptr;
    if (root && root->GetString("method").ToString() == "Tracing.dataCollected") {
      CefRefPtr<CefDictionaryValue> params = root->GetDictionary("params");
      CefRefPtr<CefListValue> events = params ? params->GetList("value") : nullptr;
      if (events) {
        for (size_t i = 0; i < events->GetSize(); ++i) {
          if (IsFrameTraceEvent(events->GetDictionary(i))) {
            ++fps_frame_count_;
          }
        }
      }

      const auto now = std::chrono::steady_clock::now();
      const double elapsed =
          std::chrono::duration<double>(now - fps_sample_start_).count();
      if (elapsed >= 1.0) {
        current_fps_ = std::round(static_cast<double>(fps_frame_count_) / elapsed);
        if (owner_) {
          owner_->OnClientFpsUpdated(this);
        }
      }
    } else if (root && root->GetString("method").ToString() == "Tracing.tracingComplete") {
      fps_tracing_active_ = false;
      fps_tracing_finishing_ = false;
      if (fps_tracking_enabled_) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed =
            std::chrono::duration<double>(now - fps_sample_start_).count();
        if (elapsed > 0.0) {
          current_fps_ = std::round(static_cast<double>(fps_frame_count_) / elapsed);
          if (owner_) {
            owner_->OnClientFpsUpdated(this);
          }
        }
        StartFpsTraceSample();
      }
    }
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

void BrowserClient::SetFpsTrackingEnabled(bool enabled) {
  if (fps_tracking_enabled_ == enabled) {
    return;
  }

  fps_tracking_enabled_ = enabled;
  fps_frame_count_ = 0;
  current_fps_ = 0.0;
  fps_sample_start_ = std::chrono::steady_clock::now();

  if (!browser_ || !browser_->GetHost()) {
    return;
  }

  if (enabled) {
    StartFpsTraceSample();
  } else {
    if (fps_tracing_active_ && !fps_tracing_finishing_) {
      fps_tracing_finishing_ = true;
      browser_->GetHost()->ExecuteDevToolsMethod(0, "Tracing.end", nullptr);
    }
  }
}

void BrowserClient::StartFpsTraceSample() {
  if (!fps_tracking_enabled_ || fps_tracing_active_ || !browser_ ||
      !browser_->GetHost()) {
    return;
  }

  fps_frame_count_ = 0;
  fps_sample_start_ = std::chrono::steady_clock::now();
  fps_tracing_active_ = true;
  fps_tracing_finishing_ = false;

  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("categories", kFpsTracingCategories);
  params->SetString("transferMode", "ReportEvents");
  browser_->GetHost()->ExecuteDevToolsMethod(0, "Tracing.start", params);

  CefRefPtr<BrowserClient> self = this;
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(&BrowserClient::FinishFpsTraceSample, self),
                     1000);
}

void BrowserClient::FinishFpsTraceSample() {
  if (!fps_tracking_enabled_ || !fps_tracing_active_ || fps_tracing_finishing_ ||
      !browser_ || !browser_->GetHost()) {
    return;
  }
  fps_tracing_finishing_ = true;
  browser_->GetHost()->ExecuteDevToolsMethod(0, "Tracing.end", nullptr);
}

}  // namespace vimbrowser
