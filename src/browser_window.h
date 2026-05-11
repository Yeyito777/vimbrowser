#pragma once

#include <string>
#include <vector>

#include "browser_client.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "tab.h"

namespace vimbrowser {

class BrowserWindow final : public CefWindowDelegate,
                            public CefBrowserViewDelegate,
                            public CefTextfieldDelegate,
                            public CefButtonDelegate {
 public:
  explicit BrowserWindow(std::string initial_url);

  void Create();
  void OnClientBrowserCreated(BrowserClient* client);
  bool HandleBrowserKeyEvent(const CefKeyEvent& event);

  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override;
  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override {}
  bool CanClose(CefRefPtr<CefWindow> window) override;
  bool OnKeyEvent(CefRefPtr<CefWindow> window, const CefKeyEvent& event) override;
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
  CefSize GetMaximumSize(CefRefPtr<CefView> view) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override;

  bool OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                  const CefKeyEvent& event) override;
  void OnButtonPressed(CefRefPtr<CefButton> button) override;

 private:
  enum class Mode {
    kNormal,
    kCommandOpenCurrent,
    kCommandOpenNext,
  };

  void BuildChrome();
  void AddTab(std::string url, bool activate);
  void ActivateTab(size_t index);
  void ActivateRelative(int delta);
  void BeginCommand(Mode mode);
  void CommitCommand();
  void CancelCommand();
  void Layout();
  void RefreshSidebar();
  bool HandleNormalModeKey(const CefKeyEvent& event);
  Tab* ActiveTab();

  std::string initial_url_;
  Mode mode_ = Mode::kNormal;
  std::vector<Tab> tabs_;
  size_t active_index_ = 0;

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> root_panel_;
  CefRefPtr<CefPanel> main_panel_;
  CefRefPtr<CefPanel> sidebar_panel_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<CefTextfield> command_field_;

  IMPLEMENT_REFCOUNTING(BrowserWindow);
  DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace vimbrowser
