#pragma once

#include <string>

#include "browser_client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_window.h"

namespace vimbrowser {

class BrowserWindow final : public CefWindowDelegate,
                            public CefBrowserViewDelegate {
 public:
  explicit BrowserWindow(std::string initial_url);

  void Create();

  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  bool CanClose(CefRefPtr<CefWindow> window) override;
  bool OnKeyEvent(CefRefPtr<CefWindow> window, const CefKeyEvent& event) override;
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override;

 private:
  std::string initial_url_;
  CefRefPtr<BrowserClient> client_;
  CefRefPtr<CefBrowserView> browser_view_;
  CefRefPtr<CefWindow> window_;

  IMPLEMENT_REFCOUNTING(BrowserWindow);
  DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace vimbrowser
