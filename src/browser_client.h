#pragma once

#include <chrono>

#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

namespace vimbrowser {

class BrowserWindow;

class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefKeyboardHandler,
                            public CefDevToolsMessageObserver {
 public:
  explicit BrowserClient(BrowserWindow* owner = nullptr);

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;
  void OnLoadStart(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   TransitionType transition_type) override;

  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;

  bool OnDevToolsMessage(CefRefPtr<CefBrowser> browser,
                         const void* message,
                         size_t message_size) override;

  CefRefPtr<CefBrowser> browser() const { return browser_; }
  void ShowDevTools();
  void SetFpsTrackingEnabled(bool enabled);
  double current_fps() const { return current_fps_; }
  bool fps_has_sample() const { return fps_has_sample_; }

 private:
  void StartFpsTraceSample();
  void FinishFpsTraceSample();

  BrowserWindow* owner_ = nullptr;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> devtools_registration_;
  bool fps_tracking_enabled_ = false;
  bool fps_tracing_active_ = false;
  bool fps_tracing_finishing_ = false;
  int fps_draw_frame_count_ = 0;
  int fps_begin_frame_count_ = 0;
  int fps_animation_frame_count_ = 0;
  bool fps_has_sample_ = false;
  double current_fps_ = 0.0;
  std::chrono::steady_clock::time_point fps_sample_start_;

  IMPLEMENT_REFCOUNTING(BrowserClient);
  DISALLOW_COPY_AND_ASSIGN(BrowserClient);
};

}  // namespace vimbrowser
