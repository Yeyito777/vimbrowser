#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <unordered_map>
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
  std::string insert_text;
};

using IpcReplyCallback = std::function<void(std::string)>;

class BrowserWindow final : public CefWindowDelegate,
                            public CefBrowserViewDelegate,
                            public CefButtonDelegate,
                            public CefTextfieldDelegate {
 public:
  BrowserWindow(std::vector<std::string> initial_urls,
                size_t active_index,
                bool show_mode_indicator,
                bool show_fps_indicator,
                bool shader_enabled,
                std::string state_path);

  void Create();
  void OnClientBrowserCreated(BrowserClient* client);
  void OnClientBeforeClose(BrowserClient* client);
  void OnClientLoadStart(BrowserClient* client, const std::string& url);
  bool OnClientProcessMessage(BrowserClient* client,
                              CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefProcessId source_process,
                              CefRefPtr<CefProcessMessage> message);
  bool OnClientDoClose(BrowserClient* client);
  bool OnClientBeforePopup(BrowserClient* client,
                           CefRefPtr<BrowserClient> popup_client,
                           int popup_id,
                           const std::string& target_url,
                           bool activate);
  void OnClientBeforePopupAborted(BrowserClient* client, int popup_id);
  void OnNativeHintOpenTab(BrowserClient* client, const std::string& url);
  void OnNativeHintsStopped(BrowserClient* client);
  bool HandleBrowserKeyEvent(const CefKeyEvent& event);
  // Canonical vimbrowser IPC command dispatcher. Keep external app automation
  // here and documented in docs/ipc.md.
  std::string HandleIpcCommand(const std::string& command);
  void HandleIpcCommandAsync(const std::string& command, IpcReplyCallback reply);

  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override;
  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override {}
  bool OnPopupBrowserViewCreated(
      CefRefPtr<CefBrowserView> browser_view,
      CefRefPtr<CefBrowserView> popup_browser_view,
      bool is_devtools) override;
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

  enum class CloseFocus {
    kPreviousTab,
    kNextTab,
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

  struct ClosedTab {
    std::string url;
    size_t index = 0;
  };

  struct PendingPopup {
    CefRefPtr<BrowserClient> client;
    int popup_id = 0;
    std::string target_url;
    bool activate = true;
  };

  void BuildChrome();
  void AddTab(std::string url, bool activate);
  void AddTabAfterActive(std::string url, bool activate);
  void InsertTab(std::string url, size_t index, bool activate);
  void InsertPopupTab(CefRefPtr<CefBrowserView> popup_browser_view,
                      CefRefPtr<BrowserClient> popup_client,
                      std::string url,
                      bool activate);
  void ActivateTab(size_t index);
  void ScheduleActiveBrowserSync();
  void ApplyActiveBrowserSelection(uint64_t generation);
  void ScheduleStateSave();
  void SaveStateForGeneration(uint64_t generation);
  void ActivateRelative(int delta);
  void ActivateFirstTab();
  void ActivateLastTab();
  void MoveActiveTab(int delta);
  bool MoveTabToIndex(size_t from, size_t to);
  CefRefPtr<CefBrowser> BrowserForTabId(uint64_t tab_id,
                                        std::string* error,
                                        size_t* index_out = nullptr) const;
  void CloneActiveTab();
  void CloseActiveTab(CloseFocus focus_after_close = CloseFocus::kPreviousTab);
  void CloseTabAtIndex(size_t closing,
                       CloseFocus focus_after_close = CloseFocus::kPreviousTab);
  void CloseTabBackend(Tab& tab);
  void QuitBrowser();
  void UndoCloseTab();
  std::optional<size_t> FindTabIndexById(uint64_t tab_id) const;
  std::string TabsJson() const;
  std::string TabJson(const Tab& tab, size_t index) const;
  uint64_t ActiveTabId() const;
  void CompleteJsIpcRequest(uint64_t request_id, std::string response);
  void HandleHtmlIpcCommand(uint64_t tab_id, bool text, IpcReplyCallback reply);
  void HandleJsIpcCommand(uint64_t tab_id,
                          std::string code,
                          IpcReplyCallback reply);
  void HandleCookiesIpcCommand(uint64_t tab_id, IpcReplyCallback reply);
  void HandleCookieDeleteIpcCommand(uint64_t tab_id,
                                    std::string name,
                                    IpcReplyCallback reply);
  void HandleCookieSetIpcCommand(uint64_t tab_id,
                                 std::string name,
                                 std::string value,
                                 std::string domain,
                                 std::string path,
                                 IpcReplyCallback reply);
  void HandleNetworkReplayIpcCommand(uint64_t tab_id,
                                     uint64_t request_id,
                                     IpcReplyCallback reply);
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
  void AppendOpenHistoryMatches(const std::string& prefix,
                                std::vector<CompletionItem>& matches) const;
  void AppendTabFocusMatches(const std::string& prefix,
                             std::vector<CompletionItem>& matches) const;
  bool CycleCommandAutocomplete(int direction);
  void FillCommandAutocomplete(const CompletionItem& item);
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
  std::optional<bool> HandlePageShortcut(const CefKeyEvent& event,
                                         bool allow_forward_to_page);
  void ResetWebsitePendingKeys();
  bool StartNativeHints(const CefKeyEvent& event);
  void ScrollActivePageBy(int dy);
  void ScrollActivePageToTop();
  void ScrollActivePageToBottom();
  void OpenClipboard(bool new_tab);
  void RecordOpenHistory(const std::string& text);
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
  void SetShaderEnabled(bool enabled);
  void BroadcastShaderState();
  void ScheduleFpsIndicatorUpdate();
  void OnFpsIndicatorUpdateTimer();
  std::string IpcStatusJson() const;
  void SaveState() const;
  void RebuildCommandCells();
  void RebuildAutocompleteRows();
  std::string ModeIndicatorText() const;
  cef_color_t ModeIndicatorColor() const;
  bool HandleNormalModeKey(const CefKeyEvent& event);
  bool AllTabBrowsersClosed() const;
  Tab* ActiveTab();

  std::vector<std::string> initial_urls_;
  std::string state_path_;
  size_t initial_active_index_ = 0;
  std::string command_text_;
  std::vector<std::string> open_history_;
  std::string website_pending_keys_;
  std::vector<ClosedTab> closed_tabs_;
  vim::LineEditState command_vim_;
  CommandAutocompleteState command_autocomplete_;
  std::vector<PendingPopup> pending_popups_;
  Mode mode_ = Mode::kNormal;
  FocusArea focus_area_ = FocusArea::kWebView;
  FocusArea previous_focus_area_ = FocusArea::kWebView;
  vim::Mode website_mode_ = vim::Mode::kWebsiteNormal;
  bool suppress_next_char_event_ = false;
  bool sidebar_visible_ = true;
  bool show_mode_indicator_ = true;
  bool show_fps_indicator_ = false;
  bool shader_enabled_ = true;
  bool fps_update_scheduled_ = false;
  bool native_hints_active_ = false;
  bool last_tab_close_placeholder_ = false;
  bool window_close_pending_ = false;
  bool window_close_allowed_ = false;
  size_t visible_tab_index_ = static_cast<size_t>(-1);
  uint64_t active_browser_sync_generation_ = 0;
  uint64_t state_save_generation_ = 0;
  uint64_t next_tab_id_ = 1;
  uint64_t next_ipc_request_id_ = 1;
  int laid_out_content_width_ = 0;
  int laid_out_content_height_ = 0;
  std::vector<Tab> tabs_;
  std::unordered_map<uint64_t, IpcReplyCallback> pending_js_ipc_;
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
