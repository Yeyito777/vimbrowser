#pragma once

#include <string>
#include <vector>

#include "browser_client.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "tab.h"

namespace vimbrowser {

class BrowserWindow final : public CefWindowDelegate,
                            public CefBrowserViewDelegate,
                            public CefTextfieldDelegate {
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
  void OnThemeChanged(CefRefPtr<CefView> view) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override;

  bool OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                  const CefKeyEvent& event) override;
  void OnAfterUserAction(CefRefPtr<CefTextfield> textfield) override;

 private:
  enum class Mode {
    kNormal,
    kCommandOpenCurrent,
    kCommandOpenNext,
  };

  enum class FocusArea {
    kTabSidebar,
    kWebView,
    kCommandLine,
  };

  enum class WebsiteMode {
    kWebsiteNormal,
    kNormal,
    kInsert,
    kVisual,
  };

  void BuildChrome();
  void AddTab(std::string url, bool activate);
  void ActivateTab(size_t index);
  void ActivateRelative(int delta);
  void BeginCommand(Mode mode);
  void CommitCommand();
  void CancelCommand();
  bool HandleCommandModeKey(const CefKeyEvent& event);
  void SetCommandText(std::string text);
  void Layout();
  void RefreshSidebar();
  void SetFocusArea(FocusArea area);
  void ToggleSidebar();
  bool HandleGlobalFocusKey(const CefKeyEvent& event);
  bool HandleWebsiteModeKey(const CefKeyEvent& event);
  void RestyleView(CefRefPtr<CefView> view);
  void RestyleCommandText();
  std::string SidebarHtml() const;
  bool HandleNormalModeKey(const CefKeyEvent& event);
  Tab* ActiveTab();

  std::string initial_url_;
  std::string command_text_;
  Mode mode_ = Mode::kNormal;
  FocusArea focus_area_ = FocusArea::kWebView;
  FocusArea previous_focus_area_ = FocusArea::kWebView;
  WebsiteMode website_mode_ = WebsiteMode::kWebsiteNormal;
  bool sidebar_visible_ = true;
  std::vector<Tab> tabs_;
  size_t active_index_ = 0;

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> root_panel_;
  CefRefPtr<CefPanel> main_panel_;
  CefRefPtr<CefPanel> sidebar_panel_;
  CefRefPtr<CefPanel> sidebar_content_panel_;
  CefRefPtr<CefPanel> sidebar_border_panel_;
  CefRefPtr<BrowserClient> sidebar_client_;
  CefRefPtr<CefBrowserView> sidebar_view_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<CefPanel> content_inner_panel_;
  CefRefPtr<CefPanel> command_panel_;
  CefRefPtr<CefPanel> command_content_panel_;
  CefRefPtr<CefPanel> command_separator_panel_;
  CefRefPtr<CefTextfield> command_field_;
  CefRefPtr<CefOverlayController> command_overlay_;

  IMPLEMENT_REFCOUNTING(BrowserWindow);
  DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace vimbrowser
