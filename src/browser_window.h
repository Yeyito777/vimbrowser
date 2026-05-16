#pragma once

#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "browser_client.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "ipc_server.h"
#include "tab.h"
#include "vim.h"

namespace vimbrowser {

struct CompletionItem {
  std::string name;
  std::string description;
};

class BrowserWindow final : public CefWindowDelegate,
                            public CefBrowserViewDelegate,
                            public CefButtonDelegate,
                            public CefTextfieldDelegate {
 public:
  BrowserWindow(std::vector<std::string> initial_urls,
                size_t active_index,
                bool show_mode_indicator,
                bool show_fps_indicator,
                std::string state_path);

  void Create();
  void OnClientBrowserCreated(BrowserClient* client);
  void OnClientLoadStart(BrowserClient* client, const std::string& url);
  bool HandleBrowserKeyEvent(const CefKeyEvent& event);
  std::string HandleIpcCommand(const std::string& command);

  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override;
  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override {}
  bool CanClose(CefRefPtr<CefWindow> window) override;
  bool OnKeyEvent(CefRefPtr<CefWindow> window, const CefKeyEvent& event) override;
  bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
  CefSize GetMaximumSize(CefRefPtr<CefView> view) override;
  void OnThemeChanged(CefRefPtr<CefView> view) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override;

  bool OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                  const CefKeyEvent& event) override;
  void OnAfterUserAction(CefRefPtr<CefTextfield> textfield) override;
  void OnButtonPressed(CefRefPtr<CefButton> button) override;

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

  struct CommandAutocompleteState {
    bool active = false;
    int selection = -1;
    std::string prefix;
    size_t token_start = 0;
    std::vector<CompletionItem> matches;
  };

  struct SidebarRowViews {
    CefRefPtr<CefTextfield> row;
  };

  void BuildChrome();
  void AddTab(std::string url, bool activate);
  void ActivateTab(size_t index);
  void ActivateRelative(int delta);
  void ActivateFirstTab();
  void ActivateLastTab();
  void MoveActiveTab(int delta);
  void CloneActiveTab();
  void CloseActiveTab();
  void UndoCloseTab();
  std::string ActiveTabUrl() const;
  std::string ActiveTabTitle() const;
  CefRefPtr<CefBrowser> ActiveBrowser() const;
  void BeginCommand(Mode mode);
  void BeginCommandText(std::string text);
  void CommitCommand();
  void CancelCommand();
  bool HandleCommandModeKey(const CefKeyEvent& event);
  void ClearCommandAutocomplete();
  void UpdateCommandAutocomplete();
  bool CycleCommandAutocomplete(int direction);
  void FillCommandAutocomplete(const std::string& name);
  int CommandAutocompleteVisibleRows() const;
  int CommandAutocompleteHeight() const;
  int CommandAutocompleteWidth() const;
  void SetCommandText(std::string text);
  bool SyncCommandTextFromField();
  void UpdateAutocompleteView();
  void Layout();
  void RefreshSidebar();
  void SetFocusArea(FocusArea area);
  void ToggleSidebar();
  bool HandleGlobalFocusKey(const CefKeyEvent& event);
  bool HandleWebsiteModeKey(const CefKeyEvent& event);
  bool HandleWebsiteCommandKey(const CefKeyEvent& event);
  void ResetWebsitePendingKeys();
  void ScrollActivePageBy(int dy);
  void ScrollActivePageToTop();
  void ScrollActivePageToBottom();
  void OpenClipboard(bool new_tab);
  void ZoomActivePage(cef_zoom_command_t command);
  void YankActiveUrl();
  void YankActiveTitle();
  void YankActiveMarkdown();
  void YankActiveDom();
  void RestyleView(CefRefPtr<CefView> view);
  void UpdateCommandView();
  void UpdateModeIndicator();
  void SetShowModeIndicator(bool visible);
  void UpdateFpsIndicator();
  void SetShowFpsIndicator(bool visible);
  void ScheduleFpsIndicatorUpdate();
  void OnFpsIndicatorUpdateTimer();
  std::string IpcStatusJson() const;
  void SaveState() const;
  void RebuildCommandCells();
  void RebuildAutocompleteRows();
  std::string ModeIndicatorText() const;
  cef_color_t ModeIndicatorColor() const;
  bool HandleNormalModeKey(const CefKeyEvent& event);
  Tab* ActiveTab();

  std::vector<std::string> initial_urls_;
  std::string state_path_;
  size_t initial_active_index_ = 0;
  std::string command_text_;
  std::string website_pending_keys_;
  std::vector<std::string> closed_tab_urls_;
  vim::LineEditState command_vim_;
  CommandAutocompleteState command_autocomplete_;
  Mode mode_ = Mode::kNormal;
  FocusArea focus_area_ = FocusArea::kWebView;
  FocusArea previous_focus_area_ = FocusArea::kWebView;
  vim::Mode website_mode_ = vim::Mode::kWebsiteNormal;
  bool suppress_next_char_event_ = false;
  bool sidebar_visible_ = true;
  bool show_mode_indicator_ = true;
  bool show_fps_indicator_ = false;
  bool fps_update_scheduled_ = false;
  bool last_tab_close_placeholder_ = false;
  std::vector<Tab> tabs_;
  std::vector<Tab> closed_tabs_;
  size_t active_index_ = 0;

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> root_panel_;
  CefRefPtr<CefPanel> main_panel_;
  CefRefPtr<CefPanel> sidebar_panel_;
  CefRefPtr<CefPanel> sidebar_content_panel_;
  CefRefPtr<CefPanel> sidebar_border_panel_;
  std::vector<SidebarRowViews> sidebar_rows_;
  CefRefPtr<CefTextfield> sidebar_spacer_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<CefPanel> content_inner_panel_;
  CefRefPtr<CefPanel> command_panel_;
  CefRefPtr<CefPanel> command_content_panel_;
  CefRefPtr<CefPanel> command_separator_panel_;
  CefRefPtr<CefTextfield> command_field_;
  CefRefPtr<CefOverlayController> command_overlay_;
  CefRefPtr<CefOverlayController> command_separator_overlay_;
  CefRefPtr<CefPanel> autocomplete_panel_;
  std::vector<CefRefPtr<CefTextfield>> autocomplete_rows_;
  CefRefPtr<CefOverlayController> autocomplete_overlay_;
  CefRefPtr<CefPanel> mode_indicator_panel_;
  CefRefPtr<CefLabelButton> mode_indicator_label_;
  CefRefPtr<CefOverlayController> mode_indicator_overlay_;
  CefRefPtr<CefPanel> fps_indicator_panel_;
  CefRefPtr<CefLabelButton> fps_indicator_label_;
  CefRefPtr<CefOverlayController> fps_indicator_overlay_;
  std::unique_ptr<IpcServer> ipc_server_;

  IMPLEMENT_REFCOUNTING(BrowserWindow);
  DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace vimbrowser
