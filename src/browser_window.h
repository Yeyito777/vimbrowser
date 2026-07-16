#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "browser_client.h"
#include "include/cef_request_context.h"
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

enum class CompletionSource {
  kStatic,
  kOpenHistory,
  kSearchHistory,
};

struct CompletionItem {
  std::string name;
  std::string description;
  std::string insert_text;
  CompletionSource source = CompletionSource::kStatic;
  std::string source_key;
};

using IpcReplyCallback = std::function<void(std::string)>;

class BrowserWindow final : public CefWindowDelegate,
                            public CefBrowserViewDelegate,
                            public CefButtonDelegate,
                            public CefTextfieldDelegate {
 public:
  BrowserWindow(std::vector<std::string> initial_urls,
                std::vector<uint64_t> initial_tab_folder_ids,
                std::vector<uint64_t> initial_tab_sort_orders,
                std::vector<bool> initial_tab_pinned,
                size_t active_index,
                bool show_mode_indicator,
                bool show_fps_indicator,
                bool show_statusline,
                bool shader_enabled,
                std::string state_path,
                std::string dwm_save_argv,
                std::string root_cache_path);

  void Create();
  void OnClientBrowserCreated(BrowserClient* client);
  void OnClientBeforeClose(BrowserClient* client);
  void OnClientLoadStart(BrowserClient* client, const std::string& url);
  void OnClientAddressChange(BrowserClient* client, const std::string& url);
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
  void OnNamedRequestContextInitialized(
      std::string context_name,
      CefRefPtr<CefRequestContext> request_context);
  bool RunNativeContextMenu(
      BrowserClient* client,
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefContextMenuParams> params,
      CefRefPtr<CefRunContextMenuCallback> callback);
  bool OnNativeContextMenuCommand(BrowserClient* client,
                                  CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefContextMenuParams> params,
                                  int command_id,
                                  cef_event_flags_t event_flags);
  void OnNativeContextMenuDismissed(BrowserClient* client);
  bool OnClientMediaAccessRequest(
      BrowserClient* client,
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      const CefString& requesting_origin,
      uint32_t requested_permissions,
      CefRefPtr<CefMediaAccessCallback> callback);
  bool GetRootWindowScreenRectForClient(BrowserClient* client,
                                        CefRect& rect) const;
  void OnNativeHintOpenTab(BrowserClient* client, const std::string& url);
  void OnNativeHintScrollTarget(BrowserClient* client,
                                int x,
                                int y,
                                bool is_page_scroller,
                                bool is_pdf_viewport);
  void OnNativeHintFocusedEditable(BrowserClient* client);
  void OnNativeHintsStopped(BrowserClient* client);
  void OnDevToolsNativeHintScrollTarget(int x,
                                        int y,
                                        bool is_page_scroller,
                                        bool is_pdf_viewport);
  void OnDevToolsNativeHintOpenTab(const std::string& url);
  void OnDevToolsNativeHintFocusedEditable();
  void OnDevToolsNativeHintsStopped();
  bool HandleBrowserKeyEvent(const CefKeyEvent& event);
  void ShowDevToolsForClient(
      BrowserClient* client,
      const CefPoint& inspect_element_at = CefPoint());
  // Canonical vimbrowser IPC command dispatcher. Keep external app automation
  // here and documented in docs/ipc.md.
  std::string HandleIpcCommand(const std::string& command);
  void HandleIpcCommandAsync(const std::string& command, IpcReplyCallback reply);

  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override;
  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override;
  void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser> browser) override;
  CefRefPtr<CefBrowserViewDelegate> GetDelegateForPopupBrowserView(
      CefRefPtr<CefBrowserView> browser_view,
      const CefBrowserSettings& settings,
      CefRefPtr<CefClient> client,
      bool is_devtools) override;
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
  void OnButtonStateChanged(CefRefPtr<CefButton> button) override;

 private:
  enum class Mode {
    kNormal,
    kCommandOpenCurrent,
    kCommandOpenNext,
    kSidebarSearchForward,
    kSidebarSearchBackward,
  };

  enum class FocusArea {
    kTabSidebar,
    kWebView,
    kDevTools,
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
    size_t completion_start = std::string::npos;
    std::vector<CompletionItem> matches;
  };

  enum class SidebarItemType {
    kNone,
    kParent,
    kFolder,
    kTab,
  };

  struct SidebarItemRef {
    SidebarItemType type = SidebarItemType::kNone;
    uint64_t id = 0;

    bool operator==(const SidebarItemRef& other) const {
      return type == other.type && id == other.id;
    }
  };

  enum class SidebarRowKind {
    kFolderHeader,
    kSectionLabel,
    kSeparator,
    kEntry,
  };

  struct SidebarDisplayRow {
    SidebarRowKind kind = SidebarRowKind::kEntry;
    SidebarItemRef item;
    size_t tab_index = static_cast<size_t>(-1);
    std::string text;
    bool selected = false;
    bool active = false;
    bool audible = false;
    uint32_t audible_utf16_offset = 0;
  };

  struct SidebarFolder {
    uint64_t id = 0;
    uint64_t parent_id = 0;
    uint64_t sort_order = 0;
    std::string name;
    bool pinned = false;
  };

  enum class SidebarPromptPurpose {
    kNone,
    kCreateFolder,
    kMoveItems,
    kRenameFolder,
  };

  struct SidebarPromptContext {
    SidebarPromptPurpose purpose = SidebarPromptPurpose::kNone;
    uint64_t folder_id = 0;
    std::vector<SidebarItemRef> items;
  };

  struct SidebarRowViews {
    CefRefPtr<CefTextfield> row;
    SidebarRowKind kind = SidebarRowKind::kEntry;
    SidebarItemRef item;
    size_t tab_index = 0;
    std::string text;
    cef_color_t text_color = 0;
    cef_color_t background_color = 0;
  };

  struct ClosedTab {
    std::string url;
    size_t index = 0;
    uint64_t folder_id = 0;
    uint64_t sidebar_sort_order = 0;
    bool pinned = false;
  };

  struct PendingPopup {
    CefRefPtr<BrowserClient> client;
    int popup_id = 0;
    std::string target_url;
    bool activate = true;
    uint64_t opener_tab_id = 0;
    bool insert_after_opener = false;
    std::string context;
  };

  struct MediaPermissionRequest {
    BrowserClient* client = nullptr;
    std::string origin;
    uint32_t requested_permissions = CEF_MEDIA_PERMISSION_NONE;
    CefRefPtr<CefMediaAccessCallback> callback;
    bool mock = false;
  };

 public:
  struct ContextMenuItem {
    int command_id = 0;
    std::string label;
    std::string detail;
    char key = 0;
    bool enabled = true;
    bool separator = false;
  };

  struct NativeContextMenu {
    BrowserClient* client = nullptr;
    CefRefPtr<CefBrowser> browser;
    CefRefPtr<CefFrame> frame;
    CefRefPtr<CefRunContextMenuCallback> callback;
    int x = 0;
    int y = 0;
    int selected_index = -1;
    cef_context_menu_type_flags_t type_flags = CM_TYPEFLAG_NONE;
    cef_context_menu_media_type_t media_type = CM_MEDIATYPE_NONE;
    cef_context_menu_media_state_flags_t media_state_flags = CM_MEDIAFLAG_NONE;
    cef_context_menu_edit_state_flags_t edit_state_flags = CM_EDITFLAG_NONE;
    bool has_image_contents = false;
    bool editable = false;
    bool spellcheck_enabled = false;
    bool closing = false;
    std::string page_url;
    std::string frame_url;
    std::string link_url;
    std::string unfiltered_link_url;
    std::string source_url;
    std::string title_text;
    std::string selection_text;
    std::string misspelled_word;
    std::vector<std::string> dictionary_suggestions;
    std::vector<ContextMenuItem> items;
  };

 private:
  void BuildChrome();
  void RegisterDwmSaveArgv();
  void AddTab(std::string url,
              bool activate,
              std::string context_name = {});
  void AddTabAfterActive(std::string url, bool activate);
  void InsertTab(std::string url,
                 size_t index,
                 bool activate,
                 bool defer_load = false,
                 uint64_t folder_id = 0,
                 uint64_t sidebar_sort_order = 0,
                 bool pinned = false,
                 std::string context_name = {});
  bool AddContextTab(std::string context_name,
                     std::string url,
                     std::string* error);
  CefRefPtr<CefRequestContext> RequestContextForName(
      const std::string& context_name,
      std::string* error = nullptr);
  bool EnsureTabBrowser(size_t index, bool load_deferred_now);
  void InsertPopupTab(CefRefPtr<CefBrowserView> popup_browser_view,
                      CefRefPtr<BrowserClient> popup_client,
                      std::string url,
                      size_t index,
                      bool activate,
                      uint64_t folder_id,
                      uint64_t sidebar_sort_order,
                      std::string context_name);
  void ActivateTab(size_t index);
  void UpdateClientUrl(BrowserClient* client,
                       const std::string& url,
                       bool force_update);
  void ScheduleActiveBrowserSync();
  void ApplyActiveBrowserSelection(uint64_t generation);
  void ScheduleStateSave();
  void SaveStateForGeneration(uint64_t generation);
  void ActivateRelative(int delta);
  bool ActivateRelativeAudible(int delta);
  void ActivateFirstTab();
  void ActivateLastTab();
  void ScheduleActivePageBlur();
  void BlurPageFocus(CefRefPtr<CefBrowser> browser);
  void ForwardKeyToActivePage(const CefKeyEvent& event);
  void ClearForwardingKeyGuard();
  void ClearForwardingDevToolsKeyGuard();
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
  void CloseTabsInDeletedSidebarFolders(
      const std::unordered_set<uint64_t>& folder_ids);
  void QuitBrowser();
  void UndoCloseTab();
  std::optional<size_t> FindTabIndexById(uint64_t tab_id) const;
  std::string TabsJson() const;
  void AppendTabJson(std::string& out, const Tab& tab, size_t index) const;
  uint64_t ActiveTabId() const;
  void CompleteJsIpcRequest(uint64_t request_id, std::string response);
  void HandleHtmlIpcCommand(uint64_t tab_id, bool text, IpcReplyCallback reply);
  void HandleJsIpcCommand(uint64_t tab_id,
                          std::string code,
                          IpcReplyCallback reply,
                          int timeout_ms = 10000);
  void ReadJsFileForIpc(uint64_t tab_id,
                        std::string path,
                        IpcReplyCallback reply);
  void FinishJsFileForIpc(uint64_t tab_id,
                          std::string code,
                          std::string error,
                          IpcReplyCallback reply);
  void HandleCookiesIpcCommand(uint64_t tab_id,
                               std::string url_override,
                               IpcReplyCallback reply);
  void HandleCookiesForUrlIpcCommand(std::string url, IpcReplyCallback reply);
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
  void HandleScreenshotIpcCommand(uint64_t tab_id, IpcReplyCallback reply);
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
  void AppendSearchHistoryMatches(const std::string& engine,
                                  const std::string& prefix,
                                  std::vector<CompletionItem>& matches) const;
  void AppendTabFocusMatches(const std::string& prefix,
                             std::vector<CompletionItem>& matches) const;
  void AppendFolderDestinationMatches(
      const std::string& prefix,
      const std::vector<SidebarItemRef>& moving_items,
      std::vector<CompletionItem>& matches) const;
  bool CycleCommandAutocomplete(int direction);
  bool DeleteSelectedCommandAutocomplete();
  void FillCommandAutocomplete(const CompletionItem& item);
  int CommandAutocompleteVisibleRows() const;
  int CommandAutocompleteHeight() const;
  int CommandAutocompleteWidth() const;
  void SetCommandText(std::string text);
  bool SyncCommandTextFromField();
  void UpdateAutocompleteView();
  void Layout();
  bool RefreshSidebar();
  void ScheduleSidebarRefresh();
  void RefreshSidebarForGeneration(uint64_t generation);
  void RefreshSidebarRow(size_t index);
  std::vector<SidebarDisplayRow> BuildSidebarDisplayRows() const;
  void EnsureSidebarSelection();
  void RevealTabInSidebar(size_t index);
  void MoveSidebarSelection(int delta);
  void MoveSidebarSelectionToEdge(bool last);
  bool ScrollSidebarByKey(char key);
  size_t SidebarFixedRowCount(
      const std::vector<SidebarDisplayRow>& rows) const;
  size_t SidebarViewportRowCapacity(size_t fixed_rows) const;
  size_t SnapSidebarScrollOffset(
      const std::vector<SidebarDisplayRow>& rows,
      size_t fixed_rows,
      size_t viewport_rows,
      size_t offset,
      int direction) const;
  void EnsureSidebarSelectionVisible(
      const std::vector<SidebarDisplayRow>& rows);
  void ActivateSidebarItem(const SidebarItemRef& item);
  void EnterSidebarFolder(uint64_t folder_id);
  void LeaveSidebarFolder();
  void ToggleSidebarVisualSelection();
  void ToggleSelectedSidebarItemPinned();
  bool SetSidebarItemPinned(const SidebarItemRef& item, bool pinned);
  bool SetTabPinned(uint64_t tab_id, bool pinned);
  bool SetFolderPinned(uint64_t folder_id, bool pinned);
  std::vector<SidebarItemRef> SelectedSidebarItems() const;
  SidebarItemRef SidebarFocusTargetAfterRemovingItems(
      const std::vector<SidebarItemRef>& items) const;
  void BeginCreateFolderPrompt();
  void BeginMoveSidebarItemsPrompt();
  void BeginRenameFolderPrompt();
  bool CommitSidebarFolderCommand(const std::string& command,
                                  const std::string& args);
  uint64_t CreateSidebarFolder(std::string name,
                               uint64_t parent_id,
                               const std::vector<SidebarItemRef>& items = {});
  bool RenameSidebarFolder(uint64_t folder_id, std::string name);
  bool MoveSidebarItems(const std::vector<SidebarItemRef>& items,
                        uint64_t destination_folder_id);
  bool MoveSelectedSidebarItem(int delta);
  bool DeleteSidebarFolder(uint64_t folder_id, bool unwrap);
  void DeleteSelectedSidebarItems();
  void ClearSidebarDeleteConfirmation(uint64_t generation);
  void UnwrapSelectedSidebarFolder();
  const SidebarFolder* FindSidebarFolder(uint64_t folder_id) const;
  SidebarFolder* FindSidebarFolder(uint64_t folder_id);
  bool SidebarFolderExists(uint64_t folder_id) const;
  bool SidebarFolderIsDescendantOf(uint64_t folder_id,
                                   uint64_t ancestor_id) const;
  std::string SidebarFolderPath(uint64_t folder_id) const;
  std::optional<uint64_t> ResolveSidebarFolderDestination(
      const std::string& text,
      const std::vector<SidebarItemRef>& moving_items) const;
  uint64_t NextSidebarSortOrder(uint64_t parent_id) const;
  uint64_t SidebarSortOrderAfterItem(const SidebarItemRef& item);
  uint64_t NewTabFolderId() const;
  bool IsSidebarSearchMode() const;
  std::string ActiveSidebarSearchQuery() const;
  void BeginSidebarSearch(bool forward);
  void UpdateSidebarSearchLive();
  void CommitSidebarSearch();
  bool JumpSidebarSearch(bool forward);
  std::optional<SidebarItemRef> FindSidebarSearchMatch(
      const std::string& query,
      const SidebarItemRef& from,
      bool forward) const;
  void ClearSidebarSearchHighlights();
  void RestoreSidebarSearchOrigin();
  std::string SidebarJson() const;
  std::string FoldersJson() const;
  void RefreshAudibleTabs();
  void SetFocusArea(FocusArea area);
  void FocusRelative(int delta);
  void ToggleSidebar();
  void ToggleDevTools();
  void CloseDevTools();
  bool FocusAreaAvailable(FocusArea area) const;
  bool HandleGlobalFocusKey(const CefKeyEvent& event);
  bool HandleWebsiteModeKey(const CefKeyEvent& event);
  bool HandleWebsiteCommandKey(const CefKeyEvent& event);
  bool HandleDevToolsModeKey(const CefKeyEvent& event);
  std::optional<bool> HandlePageShortcut(const CefKeyEvent& event,
                                         bool allow_forward_to_page);
  void ResetWebsitePendingKeys();
  bool StopPageNativeHintsForClient(BrowserClient* client);
  bool StartNativeHints(const CefKeyEvent& event);
  bool StartDevToolsNativeHints(const CefKeyEvent& event);
  void ScrollActivePageBy(int dy);
  void ScrollDevToolsBy(int dy);
  void CycleDevToolsPanel(int delta);
  void ScrollActivePageToTop();
  void ScrollActivePageToBottom();
  void StartPageSearch(std::string text, bool forward);
  void FindNextPageSearch(bool reverse_direction);
  void ClearPageSearchHighlights();
  void OpenClipboard(bool new_tab);
  void RecordOpenHistory(const std::string& text);
  void RecordSearchHistory(const std::string& engine,
                           const std::string& query);
  void ZoomActivePage(cef_zoom_command_t command);
  void YankActiveUrl();
  void YankActiveTitle();
  void YankActiveMarkdown();
  void YankActiveDom();
  void StartMuseScorePdfDownload();
  void OnMuseScoreMetadata(std::string response);
  void RunMuseScorePdfDownload(std::string title,
                               std::vector<std::string> urls,
                               std::string download_directory);
  void UpdateMuseScorePdfStatus(std::string message);
  void FinishMuseScorePdfDownload(std::string output_path,
                                  std::string error);
  void RestyleView(CefRefPtr<CefView> view);
  void SetStatusOutput(std::string message, int timeout_ms = 3000);
  void ClearStatusOutputForGeneration(uint64_t generation);
  void UpdateStatusBar();
  void UpdateCommandView();
  void StartSidebarMouseWatcher();
  void StopSidebarMouseWatcher();
  void RunSidebarMouseWatcher();
  void UpdateSidebarMouseBounds();
  void HandleSidebarMouseRowClick(size_t row_index);
  void UpdateModeIndicator();
  void SetShowModeIndicator(bool visible);
  void UpdateFpsIndicator();
  void SetShowFpsIndicator(bool visible);
  void SetShowStatusLine(bool visible);
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
  cef_color_t SidebarBorderColor() const;
  cef_color_t StatusBarBackgroundColor() const;
  bool HandleNormalModeKey(const CefKeyEvent& event);
  bool AllTabBrowsersClosed() const;
  Tab* ActiveTab();
  bool PageHasFocusedEditable(const CefKeyEvent& event);
  void ShowNextMediaPermissionRequest();
  void ShowMockMediaPermissionPrompt();
  void UpdateMediaPermissionPrompt();
  void ResolveActiveMediaPermissionRequest(bool allow, bool remember);
  void DismissActiveMediaPermissionRequest();
  void CancelMediaPermissionRequestsForClient(BrowserClient* client);
  void CancelAllMediaPermissionRequests();
  bool HandleMediaPermissionPromptKey(const CefKeyEvent& event);
  void EnsureContextMenuViews();
  void BuildNativeContextMenuItems(NativeContextMenu* menu);
  void RebuildNativeContextMenuRows();
  void LayoutNativeContextMenu(int window_width, int window_height);
  bool HandleNativeContextMenuKey(const CefKeyEvent& event);
  void SelectNativeContextMenuRelative(int delta);
  void UpdateNativeContextMenuSelection();
  void ActivateNativeContextMenuRow(size_t row_index);
  void HoverNativeContextMenuRow(size_t row_index);
  void CompleteNativeContextMenu(int command_id);
  void CancelNativeContextMenu();
  void HideNativeContextMenuViews();
  int NativeContextMenuWidth() const;
  int NativeContextMenuHeight() const;
  void UpdateContextMenuMouseBounds(int x, int y, int width, int height);
  void ClearContextMenuMouseBounds();
  void CopyContextImageToClipboard(CefRefPtr<CefBrowser> browser,
                                   const std::string& image_url);

  std::vector<std::string> initial_urls_;
  std::vector<uint64_t> initial_tab_folder_ids_;
  std::vector<uint64_t> initial_tab_sort_orders_;
  std::vector<bool> initial_tab_pinned_;
  std::string state_path_;
  std::string dwm_save_argv_;
  std::string root_cache_path_;
  size_t initial_active_index_ = 0;
  std::string command_text_;
  std::string status_output_text_;
  std::string page_search_text_;
  std::string sidebar_search_query_;
  SidebarItemRef sidebar_search_saved_item_;
  uint64_t sidebar_search_saved_folder_id_ = 0;
  size_t sidebar_search_saved_scroll_offset_ = 0;
  std::vector<std::string> open_history_;
  std::map<std::string, std::vector<std::string>> search_history_;
  std::unordered_map<std::string, uint32_t> media_permission_grants_;
  std::unordered_map<std::string, uint32_t> media_permission_denials_;
  std::unordered_map<std::string, CefRefPtr<CefRequestContext>>
      request_contexts_;
  std::unordered_set<std::string> initialized_request_contexts_;
  std::string website_pending_keys_;
  std::string sidebar_pending_keys_;
  std::vector<ClosedTab> closed_tabs_;
  vim::LineEditState command_vim_;
  CommandAutocompleteState command_autocomplete_;
  std::vector<PendingPopup> pending_popups_;
  std::vector<MediaPermissionRequest> queued_media_permissions_;
  std::optional<MediaPermissionRequest> active_media_permission_;
  Mode mode_ = Mode::kNormal;
  FocusArea focus_area_ = FocusArea::kWebView;
  FocusArea previous_focus_area_ = FocusArea::kWebView;
  vim::Mode website_mode_ = vim::Mode::kWebsiteNormal;
  vim::Mode devtools_mode_ = vim::Mode::kNormal;
  std::optional<char> suppress_next_website_char_;
  std::optional<char> suppress_next_devtools_char_;
  bool suppress_next_char_event_ = false;
  bool sidebar_visible_ = true;
  bool bulk_tab_update_ = false;
  bool show_mode_indicator_ = true;
  bool show_fps_indicator_ = false;
  bool show_statusline_ = true;
  bool shader_enabled_ = true;
  bool dwm_save_registered_ = false;
  bool forwarding_key_to_page_ = false;
  bool forwarding_key_to_devtools_ = false;
  bool fps_update_scheduled_ = false;
  bool native_hints_active_ = false;
  bool page_search_forward_ = true;
  bool page_search_highlights_visible_ = false;
  bool sidebar_search_highlights_visible_ = false;
  bool sidebar_search_committing_ = false;
  bool sidebar_search_forward_ = true;
  bool devtools_has_scroll_target_ = false;
  int devtools_scroll_target_x_ = 1;
  int devtools_scroll_target_y_ = 1;
  bool devtools_scroll_target_is_page_ = true;
  bool last_tab_close_placeholder_ = false;
  bool musescore_download_in_progress_ = false;
  bool window_close_pending_ = false;
  bool window_close_allowed_ = false;
  size_t visible_tab_index_ = static_cast<size_t>(-1);
  uint64_t active_browser_sync_generation_ = 0;
  uint64_t state_save_generation_ = 0;
  uint64_t sidebar_refresh_generation_ = 0;
  uint64_t status_output_generation_ = 0;
  uint64_t sidebar_delete_generation_ = 0;
  uint64_t next_tab_id_ = 1;
  uint64_t next_folder_id_ = 1;
  uint64_t next_ipc_request_id_ = 1;
  int page_search_browser_id_ = 0;
  std::atomic<bool> sidebar_mouse_watcher_running_{false};
  std::atomic<int> sidebar_mouse_screen_x_{0};
  std::atomic<int> sidebar_mouse_screen_y_{0};
  std::atomic<int> sidebar_mouse_width_{0};
  std::atomic<int> sidebar_mouse_height_{0};
  std::atomic<int> sidebar_mouse_row_count_{0};
  std::atomic<unsigned long> sidebar_mouse_window_{0};
  std::atomic<int> context_menu_mouse_screen_x_{0};
  std::atomic<int> context_menu_mouse_screen_y_{0};
  std::atomic<int> context_menu_mouse_width_{0};
  std::atomic<int> context_menu_mouse_height_{0};
  std::atomic<int> context_menu_mouse_row_count_{0};
  std::atomic<unsigned long> context_menu_mouse_window_{0};
  size_t tab_client_count_ = 0;
  mutable std::array<uint64_t, 4> cached_tab_lookup_ids_{};
  mutable std::array<size_t, 4> cached_tab_lookup_indexes_{};
  int laid_out_content_width_ = 0;
  int laid_out_content_height_ = 0;
  std::vector<Tab> tabs_;
  std::vector<SidebarFolder> sidebar_folders_;
  uint64_t current_sidebar_folder_id_ = 0;
  SidebarItemRef sidebar_selected_item_;
  SidebarItemRef sidebar_visual_anchor_;
  size_t sidebar_scroll_offset_ = 0;
  SidebarPromptContext sidebar_prompt_;
  std::unordered_map<uint64_t, IpcReplyCallback> pending_js_ipc_;
  size_t active_index_ = 0;

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> root_panel_;
  CefRefPtr<CefPanel> main_panel_;
  CefRefPtr<CefPanel> sidebar_panel_;
  CefRefPtr<CefPanel> sidebar_content_panel_;
  CefRefPtr<CefPanel> sidebar_border_panel_;
  CefRefPtr<CefPanel> sidebar_border_overlay_panel_;
  std::vector<SidebarRowViews> sidebar_rows_;
  CefRefPtr<CefTextfield> sidebar_spacer_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<CefPanel> content_inner_panel_;
  CefRefPtr<CefPanel> devtools_panel_;
  CefRefPtr<CefPanel> devtools_content_panel_;
  CefRefPtr<CefBrowserView> devtools_browser_view_;
  CefRefPtr<CefBrowserViewDelegate> devtools_browser_view_delegate_;
  CefRefPtr<CefClient> devtools_client_;
  CefRefPtr<CefPanel> status_bar_panel_;
  CefRefPtr<CefPanel> status_sidebar_spacer_panel_;
  CefRefPtr<CefPanel> status_border_panel_;
  CefRefPtr<CefPanel> status_content_panel_;
  CefRefPtr<CefTextfield> status_output_field_;
  CefRefPtr<CefTextfield> status_mode_field_;
  CefRefPtr<CefLabelButton> status_url_label_;
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
  CefRefPtr<CefOverlayController> sidebar_border_overlay_;
  CefRefPtr<CefPanel> media_permission_panel_;
  CefRefPtr<CefPanel> media_permission_top_border_panel_;
  CefRefPtr<CefPanel> media_permission_bottom_border_panel_;
  CefRefPtr<CefPanel> media_permission_left_border_panel_;
  CefRefPtr<CefPanel> media_permission_right_border_panel_;
  CefRefPtr<CefPanel> media_permission_content_panel_;
  CefRefPtr<CefTextfield> media_permission_title_field_;
  CefRefPtr<CefTextfield> media_permission_origin_field_;
  CefRefPtr<CefTextfield> media_permission_body_field_;
  CefRefPtr<CefTextfield> media_permission_hint_field_;
  CefRefPtr<CefPanel> media_permission_button_panel_;
  CefRefPtr<CefLabelButton> media_permission_allow_button_;
  CefRefPtr<CefLabelButton> media_permission_deny_button_;
  CefRefPtr<CefOverlayController> media_permission_overlay_;
  CefRefPtr<CefOverlayController> media_permission_top_border_overlay_;
  CefRefPtr<CefOverlayController> media_permission_bottom_border_overlay_;
  CefRefPtr<CefOverlayController> media_permission_left_border_overlay_;
  CefRefPtr<CefOverlayController> media_permission_right_border_overlay_;
  std::optional<NativeContextMenu> native_context_menu_;
  CefRefPtr<CefLabelButton> context_menu_backdrop_button_;
  CefRefPtr<CefOverlayController> context_menu_backdrop_overlay_;
  CefRefPtr<CefPanel> context_menu_panel_;
  std::vector<CefRefPtr<CefLabelButton>> context_menu_rows_;
  CefRefPtr<CefOverlayController> context_menu_overlay_;
  std::thread sidebar_mouse_thread_;
  std::unique_ptr<IpcServer> ipc_server_;
  uint64_t devtools_opener_tab_id_ = 0;
  bool devtools_visible_ = false;

  IMPLEMENT_REFCOUNTING(BrowserWindow);
  DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace vimbrowser
