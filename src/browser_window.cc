#include "browser_window.h"
#include "browser_window_internal.h"

#include "a26_keyboard.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>

#if defined(__linux__)
#include <sys/select.h>

#include <X11/cursorfont.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#endif

#include "config.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
#include "include/cef_cookie.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_navigation_entry.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_string_visitor.h"
#include "include/cef_urlrequest.h"
#include "include/cef_values.h"
#include "include/views/cef_button.h"
#include "include/wrapper/cef_closure_task.h"
#include "ipc_server.h"
#include "shortcuts.h"
#include "theme.h"

extern "C" void vimbrowser_send_browser_command_key_event(
    int browser_id,
    const CefKeyEvent* event);

namespace vimbrowser {

namespace {

class DevToolsClient final : public CefClient,
                             public CefDisplayHandler,
                             public CefKeyboardHandler {
 public:
  explicit DevToolsClient(BrowserWindow* owner) : owner_(owner) {}

  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }

  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                        cef_log_severity_t level,
                        const CefString& message,
                        const CefString& source,
                        int line) override {
    if (!owner_) {
      return false;
    }

    const std::string text = message.ToString();
    constexpr std::string_view kOpenTabPrefix =
        "__vimbrowser_native_hint_open_tab__";
    if (text.rfind(kOpenTabPrefix, 0) == 0) {
      owner_->OnDevToolsNativeHintOpenTab(text.substr(kOpenTabPrefix.size()));
      return true;
    }

    if (text == "__vimbrowser_native_hint_focused_editable__") {
      owner_->OnDevToolsNativeHintFocusedEditable();
      return true;
    }

    constexpr std::string_view kScrollTargetPrefix =
        "__vimbrowser_native_hint_scroll_target__";
    if (text.rfind(kScrollTargetPrefix, 0) == 0) {
      const std::string payload = text.substr(kScrollTargetPrefix.size());
      char* end = nullptr;
      const long x = std::strtol(payload.c_str(), &end, 10);
      if (end && *end == ',') {
        char* y_end = nullptr;
        const long y = std::strtol(end + 1, &y_end, 10);
        if (y_end != end + 1) {
          bool is_page_scroller = false;
          bool is_pdf_viewport = false;
          if (*y_end == ',') {
            char* page_end = nullptr;
            const long page = std::strtol(y_end + 1, &page_end, 10);
            is_page_scroller = page_end != y_end + 1 && page != 0;
            if (page_end && *page_end == ',') {
              char* pdf_end = nullptr;
              const long pdf = std::strtol(page_end + 1, &pdf_end, 10);
              is_pdf_viewport = pdf_end != page_end + 1 && pdf != 0;
            }
          }
          owner_->OnDevToolsNativeHintScrollTarget(
              static_cast<int>(x), static_cast<int>(y), is_page_scroller,
              is_pdf_viewport);
        }
      }
      return true;
    }

    if (text == "__vimbrowser_native_hints_stopped__") {
      owner_->OnDevToolsNativeHintsStopped();
      return true;
    }

    return false;
  }

  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override {
    return owner_ && owner_->HandleBrowserKeyEvent(event);
  }

 private:
  BrowserWindow* owner_ = nullptr;

  IMPLEMENT_REFCOUNTING(DevToolsClient);
  DISALLOW_COPY_AND_ASSIGN(DevToolsClient);
};

class DevToolsBrowserViewDelegate final : public CefBrowserViewDelegate {
 public:
  explicit DevToolsBrowserViewDelegate(BrowserWindow* owner) : owner_(owner) {}

  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override {
    if (owner_) {
      owner_->OnBrowserCreated(browser_view, browser);
    }
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser> browser) override {
    if (owner_) {
      owner_->OnBrowserDestroyed(browser_view, browser);
    }
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    // CEF DevTools popups are always Chrome-style. The normal page BrowserViews
    // still use BrowserWindow as their delegate and remain Alloy-style.
    return CEF_RUNTIME_STYLE_CHROME;
  }

 private:
  BrowserWindow* owner_ = nullptr;

  IMPLEMENT_REFCOUNTING(DevToolsBrowserViewDelegate);
  DISALLOW_COPY_AND_ASSIGN(DevToolsBrowserViewDelegate);
};

std::string MediaPermissionNameList(uint32_t permissions) {
  std::vector<std::string> names;
  if (permissions & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) {
    names.emplace_back("microphone");
  }
  if (permissions & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) {
    names.emplace_back("camera");
  }
  if (permissions & CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE) {
    names.emplace_back("screen audio");
  }
  if (permissions & CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE) {
    names.emplace_back("screen video");
  }
  if (names.empty()) {
    return "media devices";
  }
  if (names.size() == 1) {
    return names.front();
  }

  std::string out;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0) {
      out += i + 1 == names.size() ? " and " : ", ";
    }
    out += names[i];
  }
  return out;
}

std::string DisplayMediaPermissionOrigin(const std::string& origin) {
  if (origin.empty()) {
    return "this page";
  }
  std::string display = origin;
  while (display.size() > 1 && display.back() == '/') {
    display.pop_back();
  }
  return display;
}

void StylePermissionButton(CefRefPtr<CefLabelButton> button,
                           cef_color_t text,
                           cef_color_t background) {
  if (!button) {
    return;
  }
  button->SetFontList("monospace, 12px");
  button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  button->SetFocusable(false);
  button->SetInkDropEnabled(false);
  button->SetBackgroundColor(background);
  button->SetEnabledTextColors(text);
  button->SetTextColor(CEF_BUTTON_STATE_NORMAL, text);
  button->SetTextColor(CEF_BUTTON_STATE_HOVERED, text);
  button->SetTextColor(CEF_BUTTON_STATE_PRESSED, text);
  button->SetState(CEF_BUTTON_STATE_NORMAL);
}

void StyleA26Button(CefRefPtr<CefLabelButton> button) {
  if (!button) {
    return;
  }
  button->SetFontList("sans-serif, 14px");
  button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  button->SetFocusable(false);
  button->SetInkDropEnabled(true);
  button->SetBackgroundColor(theme::kSidebarBg);
  button->SetEnabledTextColors(theme::kText);
  button->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
  button->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
  button->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
  button->SetTextColor(CEF_BUTTON_STATE_DISABLED, theme::kMuted);
}

void StyleA26UrlField(CefRefPtr<CefTextfield> field) {
  if (!field) {
    return;
  }
  // Do not reuse StyleTextfield here: it temporarily makes a field read-only
  // and non-focusable, which would blur this live editor on every layout/theme
  // refresh before those flags were restored.
  field->SetReadOnly(false);
  field->SetFocusable(true);
  field->SetFontList("sans-serif, 15px");
  field->SetBackgroundColor(theme::kAppBg);
  field->SetTextColor(theme::kText);
  field->SetSelectionTextColor(theme::kText);
  field->SetSelectionBackgroundColor(theme::kSelectionBg);
  field->SetPlaceholderTextColor(theme::kMuted);
}

bool HasKeyModifier(const CefKeyEvent& event) {
  constexpr uint32_t kKeyModifierMask =
      EVENTFLAG_SHIFT_DOWN | EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN |
      EVENTFLAG_COMMAND_DOWN | EVENTFLAG_ALTGR_DOWN;
  return event.modifiers & kKeyModifierMask;
}

bool IsLoneSpaceShortcutEvent(const CefKeyEvent& event) {
  return IsSpaceKey(event) && !HasKeyModifier(event);
}

// Keep o/O interpretation identical everywhere normal-mode browser chrome owns
// the key, regardless of which CEF key path produced it.
std::optional<bool> OpenCommandNewTabForKey(const CefKeyEvent& event) {
  if (!IsRawKeyDown(event) || !IsPlain(event)) {
    return std::nullopt;
  }

  switch (PlainKeyChar(event)) {
    case 'o':
      return false;
    case 'O':
      return true;
    default:
      return std::nullopt;
  }
}

}  // namespace

BrowserWindow::BrowserWindow(std::vector<std::string> initial_urls,
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
                             std::string root_cache_path,
                             bool a26_shell)
    : initial_urls_(std::move(initial_urls)),
      initial_tab_folder_ids_(std::move(initial_tab_folder_ids)),
      initial_tab_sort_orders_(std::move(initial_tab_sort_orders)),
      initial_tab_pinned_(std::move(initial_tab_pinned)),
      state_path_(std::move(state_path)),
      dwm_save_argv_(std::move(dwm_save_argv)),
      root_cache_path_(std::move(root_cache_path)),
      initial_active_index_(active_index),
      show_mode_indicator_(show_mode_indicator),
      show_fps_indicator_(show_fps_indicator),
      show_statusline_(show_statusline),
      shader_enabled_(shader_enabled),
      a26_shell_(a26_shell) {
  const char* xtest_workaround = std::getenv("A26_VIMBROWSER_XTEST_CHAR_WORKAROUND");
  a26_xtest_char_workaround_ =
      a26_shell_ && xtest_workaround && std::string_view(xtest_workaround) == "1";
  sidebar_visible_ = !a26_shell_;
  const AppState state = ReadAppState(state_path_);
  open_history_ = state.open_history;
  search_history_ = state.search_history;
  media_permission_grants_.insert(state.media_permission_grants.begin(),
                                  state.media_permission_grants.end());
  media_permission_denials_.insert(state.media_permission_denials.begin(),
                                   state.media_permission_denials.end());
  std::unordered_set<uint64_t> folder_ids;
  for (const SavedSidebarFolder& saved : state.sidebar_folders) {
    if (saved.id == 0 || saved.name.empty() ||
        !folder_ids.insert(saved.id).second) {
      continue;
    }
    sidebar_folders_.push_back({saved.id, saved.parent_id, saved.sort_order,
                                saved.name, saved.pinned});
    next_folder_id_ = std::max(next_folder_id_, saved.id + 1);
  }
  next_folder_id_ = std::max(next_folder_id_, state.next_sidebar_folder_id);
  for (SidebarFolder& folder : sidebar_folders_) {
    if (folder.parent_id != 0 && !folder_ids.contains(folder.parent_id)) {
      folder.parent_id = 0;
    }
    if (folder.sort_order == 0) {
      folder.sort_order = folder.id * 1024;
    }
    if (folder.parent_id == folder.id ||
        SidebarFolderIsDescendantOf(folder.parent_id, folder.id)) {
      folder.parent_id = 0;
    }
  }
  current_sidebar_folder_id_ = folder_ids.contains(state.sidebar_folder_id)
                                   ? state.sidebar_folder_id
                                   : 0;
  if (initial_urls_.empty()) {
    initial_urls_.push_back(ResolveUrlOrSearch(""));
  }
  initial_tab_folder_ids_.resize(initial_urls_.size(), 0);
  initial_tab_sort_orders_.resize(initial_urls_.size(), 0);
  initial_tab_pinned_.resize(initial_urls_.size(), false);
  for (uint64_t& folder_id : initial_tab_folder_ids_) {
    if (folder_id != 0 && !folder_ids.contains(folder_id)) {
      folder_id = 0;
    }
  }
  if (initial_active_index_ >= initial_urls_.size()) {
    initial_active_index_ = 0;
  }
}

void BrowserWindow::Create() {
  CefWindow::CreateTopLevelWindow(this);
}

void BrowserWindow::OnClientBrowserCreated(BrowserClient* client) {
  RefreshSidebar();
  if (client && client->browser()) {
    for (size_t i = 0; i < tabs_.size(); ++i) {
      if (tabs_[i].client.get() != client) {
        continue;
      }
      tabs_[i].is_loading = client->browser()->IsLoading();
      tabs_[i].can_go_back = client->browser()->CanGoBack();
      tabs_[i].can_go_forward = client->browser()->CanGoForward();
      if (i == active_index_) {
        UpdateA26Chrome();
      }
      break;
    }
  }
  if (client && client->browser() && client->browser()->GetHost()) {
    client->browser()->GetHost()->NotifyScreenInfoChanged();
  }
}

bool BrowserWindow::GetRootWindowScreenRectForClient(BrowserClient* client,
                                                     CefRect& rect) const {
  if (!client || !content_inner_panel_) {
    return false;
  }

  bool owns_client = false;
  for (const Tab& tab : tabs_) {
    if (tab.client.get() == client) {
      owns_client = true;
      break;
    }
  }
  if (!owns_client) {
    return false;
  }

  // Vimbrowser hosts its tab sidebar/status UI inside the same top-level
  // CefWindow as the page BrowserView. Reporting that whole root window to
  // Blink makes JavaScript window.outerWidth/outerHeight include browser chrome
  // that is not part of the web page. Some sites, notably Discord, treat a
  // large outerWidth-innerWidth delta as docked DevTools and deliberately hide
  // authentication tokens from persistent storage. The browser page's root
  // window for web-observable geometry is the content pane, not vimbrowser's
  // chrome shell.
  rect = content_inner_panel_->GetBoundsInScreen();
  if (rect.width > 0 && rect.height > 0) {
    return true;
  }

  CefRect local_bounds = content_inner_panel_->GetBounds();
  CefPoint origin(0, 0);
  if (!content_inner_panel_->ConvertPointToScreen(origin)) {
    return false;
  }
  rect = CefRect(origin.x, origin.y, local_bounds.width, local_bounds.height);
  return rect.width > 0 && rect.height > 0;
}

void BrowserWindow::OnClientBeforeClose(BrowserClient* client) {
  CancelFileChooserUploadForClient(client, "tab_closed",
                                   "armed tab closed before file selection");
  CancelMediaPermissionRequestsForClient(client);
  if (!window_close_pending_ || !AllTabBrowsersClosed()) {
    return;
  }

  window_close_allowed_ = true;
  if (window_) {
    window_->Close();
  } else {
    CefQuitMessageLoop();
  }
}

bool BrowserWindow::OnClientDoClose(BrowserClient* client) {
  if (window_close_pending_) {
    return false;
  }

  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].client.get() == client) {
      CloseTabAtIndex(i, CloseFocus::kPreviousTab);
      return true;
    }
  }
  return false;
}

void BrowserWindow::OnClientLoadStart(BrowserClient* client, const std::string& url) {
  // Blink tears down the renderer-side hint overlay with the old Document. Clear
  // the browser-side latch at the same document boundary so a pending navigation
  // can never leave the shell routing keys to an obsolete hint matcher.
  StopPageNativeHintsForClient(client);
  CancelFileChooserUploadForClient(
      client, "navigation", "armed tab navigated before file selection");
  if (a26_shell_ && ActiveTab() && ActiveTab()->client.get() == client) {
    website_mode_ = vim::Mode::kWebsiteNormal;
    RequestA26Keyboard(A26KeyboardPurpose::kHide);
  }
  UpdateClientUrl(client, url, true);
}

void BrowserWindow::OnClientAddressChange(BrowserClient* client,
                                          const std::string& url) {
  UpdateClientUrl(client, url, false);
}

void BrowserWindow::OnClientTitleChange(BrowserClient* client,
                                        const std::string& title) {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].client.get() != client) {
      continue;
    }
    tabs_[i].title = title;
    if (i == active_index_) {
      UpdateA26Chrome();
    }
    return;
  }
}

void BrowserWindow::OnClientLoadingStateChange(BrowserClient* client,
                                               bool is_loading,
                                               bool can_go_back,
                                               bool can_go_forward) {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].client.get() != client) {
      continue;
    }
    tabs_[i].is_loading = is_loading;
    tabs_[i].can_go_back = can_go_back;
    tabs_[i].can_go_forward = can_go_forward;
    if (i == active_index_) {
      UpdateA26Chrome();
    }
    return;
  }
}

void BrowserWindow::UpdateClientUrl(BrowserClient* client,
                                    const std::string& url,
                                    bool force_update) {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    Tab& tab = tabs_[i];
    if (tab.client.get() == client) {
      if (tab.deferred_load && url == "about:blank") {
        return;
      }
      if (!force_update && tab.url == url) {
        return;
      }
      tab.deferred_load = false;
      SetTabUrl(tab, url);
      if (force_update) {
        tab.focused_editable_node = false;
        tab.focused_editable_purpose = "text";
      }
      tab.has_scroll_target = false;
      tab.scroll_target_is_pdf_viewport = false;
      if (url != "about:blank") {
        last_tab_close_placeholder_ = false;
      }
      if (i == active_index_) {
        UpdateStatusBar();
        if (force_update) {
          RequestA26Keyboard(A26KeyboardPurpose::kHide);
        }
      }
      SaveState();
      RefreshSidebar();
      return;
    }
  }
}

bool BrowserWindow::OnClientProcessMessage(BrowserClient* client,
                                           CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame,
                                           CefProcessId source_process,
                                           CefRefPtr<CefProcessMessage> message) {
  if (!message) {
    return false;
  }
  const std::string name = message->GetName().ToString();
  if (name == kFocusedEditableMessage) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    const bool focused_editable = args && args->GetSize() >= 1 && args->GetBool(0);
    std::string purpose =
        args && args->GetSize() >= 2 ? args->GetString(1).ToString() : "text";
    if (purpose != "password" && purpose != "search" && purpose != "url" &&
        purpose != "number") {
      purpose = "text";
    }
    bool active_tab_focused_editable = false;
    bool message_for_active_tab = false;
    for (Tab& tab : tabs_) {
      if (tab.client.get() == client) {
        tab.focused_editable_node = focused_editable;
        tab.focused_editable_purpose = purpose;
        message_for_active_tab = ActiveTab() == &tab;
        active_tab_focused_editable = focused_editable && ActiveTab() == &tab;
        break;
      }
    }
    if (active_tab_focused_editable &&
        ((native_hints_active_ && mode_ == Mode::kNormal &&
          focus_area_ == FocusArea::kWebView) ||
         a26_shell_)) {
      // Only native hints turn focused page text controls into vimbrowser insert
      // mode.  Ordinary mouse clicks, tab traversal, autofocus, and page script
      // focus must leave the website vim mode alone so normal-mode keys remain
      // under vimbrowser's control until the user explicitly enters insert mode.
      // A26 is the deliberate exception: Moon injects XTEST key events after a
      // touch-focused DOM input, so that input must already be on the existing
      // insert/forwarding path before the global keyboard becomes visible.
      website_mode_ = vim::Mode::kInsert;
      ResetWebsitePendingKeys();
      suppress_next_website_char_.reset();
      if (a26_shell_) {
        focus_area_ = FocusArea::kWebView;
        a26_url_focused_ = false;
        a26_url_editing_ = false;
        SyncA26KeyboardForActivePage();
        if (frame && frame->IsValid()) {
          // Moon resizes the app above its global keyboard. Re-center only the
          // focused element after that configure settles; never inspect or
          // return its value.
          frame->ExecuteJavaScript(
              "setTimeout(()=>{const e=document.activeElement;"
              "if(e&&e.scrollIntoView)e.scrollIntoView({block:'center',"
              "inline:'nearest'});},180);",
              frame->GetURL(), 0);
        }
      }
      UpdateModeIndicator();
    } else if (a26_shell_ && message_for_active_tab && !focused_editable &&
               !a26_url_focused_) {
      RequestA26Keyboard(A26KeyboardPurpose::kHide);
    }
    return true;
  }

  if (name != kJsResultMessage) {
    return false;
  }
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (!args || args->GetSize() < 2) {
    return true;
  }
  uint64_t request_id = 0;
  if (!ParseUint64Arg(args->GetString(0).ToString(), &request_id)) {
    return true;
  }
  CompleteJsIpcRequest(request_id, args->GetString(1).ToString());
  return true;
}

bool BrowserWindow::OnClientBeforePopup(BrowserClient* client,
                                        CefRefPtr<BrowserClient> popup_client,
                                        int popup_id,
                                        const std::string& target_url,
                                        bool activate) {
  const auto source = std::find_if(
      tabs_.begin(), tabs_.end(),
      [client](const Tab& tab) { return tab.client.get() == client; });
  if (source == tabs_.end()) {
    // Unknown popups should never escape into CEF-owned top-level windows.
    return true;
  }

  const bool hint_open_tab = native_hints_active_ && ActiveTab() &&
                             ActiveTab()->client.get() == client;
  const uint64_t opener_tab_id = hint_open_tab ? ActiveTab()->id : 0;
  const std::string source_context = source->context;

  if (!popup_client) {
    if (target_url.empty()) {
      return true;
    }
    native_hints_active_ = false;
    if (hint_open_tab) {
      AddTabAfterActive(target_url, activate);
    } else {
      AddTab(target_url, activate, source_context);
    }
    UpdateModeIndicator();
    return true;
  }

  pending_popups_.push_back(
      {popup_client, popup_id, target_url, activate, opener_tab_id,
       hint_open_tab, source_context});
  return false;
}

void BrowserWindow::OnClientBeforePopupAborted(BrowserClient*, int popup_id) {
  pending_popups_.erase(
      std::remove_if(pending_popups_.begin(), pending_popups_.end(),
                     [popup_id](const PendingPopup& popup) {
                       return popup.popup_id == popup_id;
                     }),
      pending_popups_.end());
}

bool BrowserWindow::OnClientMediaAccessRequest(
    BrowserClient* client,
    CefRefPtr<CefBrowser>,
    CefRefPtr<CefFrame> frame,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefMediaAccessCallback> callback) {
  if (!callback) {
    return true;
  }

  if (requested_permissions == CEF_MEDIA_PERMISSION_NONE) {
    callback->Continue(CEF_MEDIA_PERMISSION_NONE);
    return true;
  }

  std::string origin = requesting_origin.ToString();
  if (origin.empty() && frame && frame->IsValid()) {
    origin = frame->GetURL().ToString();
  }
  if (origin.empty()) {
    origin = ActiveTabUrl();
  }

  if (const auto granted = media_permission_grants_.find(origin);
      granted != media_permission_grants_.end() &&
      (granted->second & requested_permissions) == requested_permissions) {
    callback->Continue(requested_permissions);
    return true;
  }
  if (const auto denied = media_permission_denials_.find(origin);
      denied != media_permission_denials_.end() &&
      (denied->second & requested_permissions) == requested_permissions) {
    callback->Continue(CEF_MEDIA_PERMISSION_NONE);
    return true;
  }

  if (!window_ || window_close_pending_) {
    callback->Cancel();
    return true;
  }

  queued_media_permissions_.push_back(
      {client, origin, requested_permissions, callback});
  ShowNextMediaPermissionRequest();
  return true;
}

bool BrowserWindow::OnPopupBrowserViewCreated(
    CefRefPtr<CefBrowserView> browser_view,
    CefRefPtr<CefBrowserView> popup_browser_view,
    bool is_devtools) {
  if (is_devtools) {
    if (!popup_browser_view || !content_panel_) {
      return false;
    }

    uint64_t opener_tab_id = 0;
    for (const Tab& tab : tabs_) {
      if (tab.view && browser_view && tab.view->IsSame(browser_view)) {
        opener_tab_id = tab.id;
        break;
      }
    }

    devtools_browser_view_ = popup_browser_view;
    devtools_browser_view_->SetID(kDevToolsBrowserViewId);
    devtools_browser_view_->SetPreferAccelerators(true);
    devtools_browser_view_->SetVisible(true);
    devtools_opener_tab_id_ = opener_tab_id;
    devtools_visible_ = true;
    if (devtools_panel_ && devtools_content_panel_) {
      devtools_panel_->SetVisible(true);
      if (!devtools_browser_view_->GetParentView()) {
        devtools_content_panel_->AddChildView(devtools_browser_view_);
      }
    }
    SetFocusArea(FocusArea::kDevTools);
    return true;
  }

  if (!popup_browser_view) {
    return false;
  }

  CefRefPtr<CefBrowser> popup_browser = popup_browser_view->GetBrowser();
  CefRefPtr<CefClient> cef_client =
      popup_browser && popup_browser->GetHost()
          ? popup_browser->GetHost()->GetClient()
          : nullptr;
  if (!cef_client) {
    return false;
  }

  auto pending = std::find_if(
      pending_popups_.begin(), pending_popups_.end(),
      [cef_client](const PendingPopup& popup) {
        return static_cast<CefClient*>(popup.client.get()) == cef_client.get();
      });
  if (pending == pending_popups_.end()) {
    return false;
  }

  std::string url = pending->target_url;
  const bool activate = pending->activate;
  const bool insert_after_opener = pending->insert_after_opener;
  const uint64_t opener_tab_id = pending->opener_tab_id;
  std::string popup_context = pending->context;
  CefRefPtr<BrowserClient> retained_popup_client = pending->client;
  pending_popups_.erase(pending);

  if (popup_browser && popup_browser->GetMainFrame()) {
    const std::string frame_url = popup_browser->GetMainFrame()->GetURL();
    if (!frame_url.empty()) {
      url = frame_url;
    }
  }
  if (url.empty()) {
    url = "about:blank";
  }

  native_hints_active_ = false;
  size_t insert_index = tabs_.size();
  uint64_t popup_folder_id = NewTabFolderId();
  uint64_t popup_sort_order = 0;
  const std::optional<size_t> opener_index = FindTabIndexById(opener_tab_id);
  if (opener_index) {
    popup_folder_id = tabs_[*opener_index].folder_id;
    popup_sort_order = SidebarSortOrderAfterItem(
        {SidebarItemType::kTab, tabs_[*opener_index].id});
  }
  if (insert_after_opener) {
    if (opener_index) {
      insert_index = *opener_index + 1;
    } else if (active_index_ < tabs_.size()) {
      insert_index = active_index_ + 1;
    }
  }
  InsertPopupTab(popup_browser_view, retained_popup_client, std::move(url),
                 insert_index, activate, popup_folder_id, popup_sort_order,
                 std::move(popup_context));
  UpdateModeIndicator();
  return true;
}

void BrowserWindow::OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                                     CefRefPtr<CefBrowser> browser) {
  if (!browser_view || !devtools_browser_view_ ||
      !browser_view->IsSame(devtools_browser_view_)) {
    return;
  }

  Layout();
  if (focus_area_ == FocusArea::kDevTools) {
    devtools_browser_view_->RequestFocus();
  }
}

void BrowserWindow::OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                                       CefRefPtr<CefBrowser> browser) {
  if (!browser_view || browser_view->GetID() != kDevToolsBrowserViewId) {
    return;
  }

  if (devtools_browser_view_ && devtools_browser_view_->IsSame(browser_view)) {
    if (devtools_content_panel_ && browser_view->GetParentView()) {
      devtools_content_panel_->RemoveChildView(browser_view);
    }
    if (devtools_panel_) {
      devtools_panel_->SetVisible(false);
    }
    devtools_browser_view_ = nullptr;
    devtools_browser_view_delegate_ = nullptr;
    devtools_client_ = nullptr;
    devtools_opener_tab_id_ = 0;
    devtools_visible_ = false;
    if (focus_area_ == FocusArea::kDevTools) {
      focus_area_ = FocusArea::kWebView;
      if (Tab* tab = ActiveTab(); tab && tab->view) {
        tab->view->RequestFocus();
      }
    }
    UpdateModeIndicator();
    Layout();
  }
}

CefRefPtr<CefBrowserViewDelegate> BrowserWindow::GetDelegateForPopupBrowserView(
    CefRefPtr<CefBrowserView> browser_view,
    const CefBrowserSettings& settings,
    CefRefPtr<CefClient> client,
    bool is_devtools) {
  if (!is_devtools) {
    return this;
  }

  if (!devtools_browser_view_delegate_) {
    devtools_browser_view_delegate_ = new DevToolsBrowserViewDelegate(this);
  }
  return devtools_browser_view_delegate_;
}

void BrowserWindow::ShowDevToolsForClient(BrowserClient* client,
                                          const CefPoint& inspect_element_at) {
  if (!client || !client->browser() || !client->browser()->GetHost()) {
    return;
  }

  uint64_t opener_tab_id = 0;
  for (const Tab& tab : tabs_) {
    if (tab.client.get() == client) {
      opener_tab_id = tab.id;
      break;
    }
  }

  CefRefPtr<CefBrowser> browser = client->browser();
  if (!devtools_client_) {
    devtools_client_ = new DevToolsClient(this);
  }

  CefWindowInfo window_info;
  // Leave CefWindowInfo unparented here so the DevTools popup stays in the CEF
  // Views hierarchy via OnPopupBrowserViewCreated(). Native child-window hosting
  // sits above the Views compositor, hides separator panels, and flickers on
  // focus transitions. Keeping DevTools as a BrowserView sibling of the main
  // content puts it at the same UI level as the sidebar.
  CefBrowserSettings settings;
  settings.background_color = theme::kAppBg;
  browser->GetHost()->ShowDevTools(window_info, devtools_client_, settings,
                                   inspect_element_at);

  // ShowDevTools focuses an already-open DevTools browser without recreating its
  // BrowserView. Re-attach the existing docked view to the carousel in that path.
  if (devtools_browser_view_ &&
      (devtools_opener_tab_id_ == 0 || devtools_opener_tab_id_ == opener_tab_id)) {
    devtools_opener_tab_id_ = opener_tab_id;
    devtools_visible_ = true;
    devtools_browser_view_->SetVisible(true);
    if (devtools_panel_ && devtools_content_panel_) {
      devtools_panel_->SetVisible(true);
      if (!devtools_browser_view_->GetParentView()) {
        devtools_content_panel_->AddChildView(devtools_browser_view_);
      }
    }
    SetFocusArea(FocusArea::kDevTools);
  }
}

void BrowserWindow::OnNativeHintOpenTab(BrowserClient* client,
                                        const std::string& url) {
  if (url.empty()) {
    return;
  }

  Tab* tab = ActiveTab();
  if (!native_hints_active_ || !tab || tab->client.get() != client) {
    return;
  }

  native_hints_active_ = false;
  website_mode_ = vim::Mode::kWebsiteNormal;
  ResetWebsitePendingKeys();
  AddTabAfterActive(url, true);
  UpdateModeIndicator();
}

void BrowserWindow::OnNativeHintScrollTarget(BrowserClient* client,
                                             int x,
                                             int y,
                                             bool is_page_scroller,
                                             bool is_pdf_viewport) {
  Tab* tab = ActiveTab();
  if (!tab || tab->client.get() != client) {
    return;
  }

  tab->has_scroll_target = true;
  tab->scroll_target_x = std::max(1, x);
  tab->scroll_target_y = std::max(1, y);
  tab->scroll_target_is_page = is_page_scroller;
  tab->scroll_target_is_pdf_viewport = is_pdf_viewport;
}

void BrowserWindow::OnNativeHintFocusedEditable(BrowserClient* client) {
  Tab* tab = ActiveTab();
  if (!native_hints_active_ || !tab || tab->client.get() != client ||
      mode_ != Mode::kNormal || focus_area_ != FocusArea::kWebView) {
    return;
  }

  tab->focused_editable_node = true;
  website_mode_ = vim::Mode::kInsert;
  ResetWebsitePendingKeys();
  suppress_next_website_char_.reset();
  UpdateModeIndicator();
}

void BrowserWindow::OnNativeHintsStopped(BrowserClient* client) {
  StopPageNativeHintsForClient(client);
}

void BrowserWindow::OnDevToolsNativeHintScrollTarget(int x,
                                                     int y,
                                                     bool is_page_scroller,
                                                     bool is_pdf_viewport) {
  if (focus_area_ != FocusArea::kDevTools || !devtools_browser_view_) {
    return;
  }

  devtools_has_scroll_target_ = true;
  devtools_scroll_target_x_ = std::max(1, x);
  devtools_scroll_target_y_ = std::max(1, y);
  devtools_scroll_target_is_page_ = is_page_scroller || is_pdf_viewport;
}

void BrowserWindow::OnDevToolsNativeHintOpenTab(const std::string& url) {
  if (!native_hints_active_ || focus_area_ != FocusArea::kDevTools ||
      url.empty()) {
    return;
  }

  native_hints_active_ = false;
  ResetWebsitePendingKeys();
  AddTabAfterActive(url, true);
  UpdateModeIndicator();
}

void BrowserWindow::OnDevToolsNativeHintFocusedEditable() {
  if (!native_hints_active_ || focus_area_ != FocusArea::kDevTools ||
      mode_ != Mode::kNormal) {
    return;
  }

  devtools_mode_ = vim::Mode::kInsert;
  suppress_next_devtools_char_.reset();
  ResetWebsitePendingKeys();
  UpdateModeIndicator();
}

void BrowserWindow::OnDevToolsNativeHintsStopped() {
  if (focus_area_ != FocusArea::kDevTools) {
    return;
  }
  native_hints_active_ = false;
  devtools_mode_ = devtools_mode_ == vim::Mode::kInsert ? vim::Mode::kInsert
                                                        : vim::Mode::kNormal;
  ResetWebsitePendingKeys();
  UpdateModeIndicator();
}

void BrowserWindow::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window_ = window;
  if (a26_shell_) {
    sidebar_visible_ = false;
    a26_keyboard_ = std::make_unique<A26KeyboardClient>();
    RequestA26Keyboard(A26KeyboardPurpose::kHide);
  }
  window_->SetTitle("vimbrowser");
  window_->SetThemeColor(CEF_ColorPrimaryBackground, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorPrimaryForeground, theme::kText);
  window_->SetThemeColor(CEF_ColorSecondaryForeground, theme::kMuted);
  window_->SetThemeColor(CEF_ColorAccent, theme::kAccent);
  window_->SetThemeColor(CEF_ColorTextfieldBackground, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorTextfieldBackgroundDisabled, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorTextfieldFilledBackground, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorTextfieldForeground, theme::kText);
  window_->SetThemeColor(CEF_ColorTextfieldFilledForegroundInvalid, theme::kText);
  window_->SetThemeColor(CEF_ColorTextfieldForegroundIcon, theme::kText);
  window_->SetThemeColor(CEF_ColorTextfieldForegroundLabel, theme::kText);
  window_->SetThemeColor(CEF_ColorTextfieldForegroundPlaceholder, theme::kMuted);
  window_->SetThemeColor(CEF_ColorTextfieldForegroundPlaceholderInvalid, theme::kMuted);
  window_->SetThemeColor(CEF_ColorTextfieldHover, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorTextfieldSelectionBackground, theme::kSelectionBg);
  window_->SetThemeColor(CEF_ColorTextfieldSelectionForeground, theme::kText);
  window_->SetThemeColor(CEF_ColorTextfieldFilledUnderline, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorTextfieldFilledUnderlineFocused, theme::kAppBg);
  // Hide CEF's rounded textfield outline. We draw a square one-pixel separator
  // ourselves so command mode remains terminal-esque and has no rounded corners.
  window_->SetThemeColor(CEF_ColorTextfieldOutline, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorTextfieldOutlineDisabled, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorTextfieldOutlineInvalid, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorNativeTextfieldBorderUnfocused, theme::kAppBg);
  window_->SetThemeColor(CEF_ColorLabelForeground, theme::kText);
  window_->SetThemeColor(CEF_ColorButtonBackground, theme::kSidebarBg);
  window_->SetThemeColor(CEF_ColorButtonForeground, theme::kText);
  window_->ThemeChanged();
  window_->SetToFillLayout();
  window_->SetAccelerator(kAcceleratorCommandTab, 0x09, false, false, false, true);
  window_->SetAccelerator(kAcceleratorCommandBacktab, 0x09, true, false, false, true);
  window_->SetAccelerator(kAcceleratorTabNext, 'J', true, false, false, true);
  window_->SetAccelerator(kAcceleratorTabPrevious, 'K', true, false, false, true);
  window_->SetAccelerator(kAcceleratorSidebarSpace, 0x20, false, false, false, true);
  window_->SetAccelerator(kAcceleratorHintRightClick, 'L', false, true, false,
                          true);
  window_->SetAccelerator(kAcceleratorHintHover, 'H', false, true, false, true);
  window_->SetAccelerator(kAcceleratorFocusNext, 'J', false, true, false, true);
  window_->SetAccelerator(kAcceleratorFocusPrevious, 'K', false, true, false,
                          true);
  window_->SetAccelerator(kAcceleratorToggleDevToolsSemicolon, ';', false, true,
                          false, true);
  window_->SetAccelerator(kAcceleratorToggleDevToolsOem1, 0xBA, false, true,
                          false, true);
  window_->SetAccelerator(kAcceleratorCommandDeleteCompletion, 'X', false, true,
                          false, true);
  ipc_server_ = std::make_unique<IpcServer>(this, IpcSocketPathForStatePath(state_path_));
  ipc_server_->Start();
  BuildChrome();
  const bool lazy_restore_background_tabs =
      initial_urls_.size() >= kLazyRestoreBackgroundTabThreshold;
  bulk_tab_update_ = true;
  for (size_t i = 0; i < initial_urls_.size(); ++i) {
    const bool activate = i == initial_active_index_;
    InsertTab(initial_urls_[i], tabs_.size(), activate,
              lazy_restore_background_tabs && !activate,
              initial_tab_folder_ids_[i], initial_tab_sort_orders_[i],
              initial_tab_pinned_[i]);
  }
  bulk_tab_update_ = false;
  RefreshSidebar();

  window_->CenterWindow(a26_shell_ ? CefSize(1080, 2340)
                                   : CefSize(1200, 800));
  window_->Show();
  if (a26_shell_) {
    window_->SetFullscreen(true);
  }
  RegisterDwmSaveArgv();
  Layout();
  SetFocusArea(FocusArea::kWebView);
  StartSidebarMouseWatcher();
  ScheduleFpsIndicatorUpdate();
}

void BrowserWindow::RegisterDwmSaveArgv() {
  if (dwm_save_registered_ || dwm_save_argv_.empty() || !window_) {
    return;
  }
  dwm_save_registered_ = true;

#if defined(__linux__)
  const CefWindowHandle handle = window_->GetWindowHandle();
  if (!handle) {
    return;
  }

  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    return;
  }

  Atom save_atom = XInternAtom(display, "_DWM_SAVE_ARGV", False);
  Atom utf8_atom = XInternAtom(display, "UTF8_STRING", False);
  XChangeProperty(display, static_cast<Window>(handle), save_atom, utf8_atom, 8,
                  PropModeReplace,
                  reinterpret_cast<const unsigned char*>(dwm_save_argv_.data()),
                  static_cast<int>(dwm_save_argv_.size()));
  XFlush(display);
  XCloseDisplay(display);
#endif
}

void BrowserWindow::BuildChrome() {
  root_panel_ = CefPanel::CreatePanel(this);
  root_panel_->SetID(kRootPanelId);
  root_panel_->SetBackgroundColor(theme::kAppBg);
  CefBoxLayoutSettings root_settings = {};
  root_settings.size = sizeof(root_settings);
  root_settings.horizontal = false;
  root_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> root_layout = root_panel_->SetToBoxLayout(root_settings);
  window_->AddChildView(root_panel_);

  main_panel_ = CefPanel::CreatePanel(this);
  main_panel_->SetID(kMainPanelId);
  main_panel_->SetBackgroundColor(theme::kAppBg);
  CefBoxLayoutSettings main_settings = {};
  main_settings.size = sizeof(main_settings);
  main_settings.horizontal = true;
  main_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> main_layout = main_panel_->SetToBoxLayout(main_settings);
  root_panel_->AddChildView(main_panel_);
  root_layout->SetFlexForView(main_panel_, 1);

  sidebar_panel_ = CefPanel::CreatePanel(this);
  sidebar_panel_->SetID(kSidebarPanelId);
  sidebar_panel_->SetBackgroundColor(theme::kSidebarBg);
  sidebar_panel_->SetToFillLayout();
  main_panel_->AddChildView(sidebar_panel_);

  sidebar_content_panel_ = CefPanel::CreatePanel(nullptr);
  sidebar_content_panel_->SetID(kSidebarContentPanelId);
  sidebar_content_panel_->SetBackgroundColor(theme::kSidebarBg);
  CefBoxLayoutSettings sidebar_content_settings = {};
  sidebar_content_settings.size = sizeof(sidebar_content_settings);
  sidebar_content_settings.horizontal = false;
  sidebar_content_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  sidebar_content_panel_->SetToBoxLayout(sidebar_content_settings);
  sidebar_panel_->AddChildView(sidebar_content_panel_);

  content_panel_ = CefPanel::CreatePanel(nullptr);
  content_panel_->SetID(kContentPanelId);
  // The sidebar's right-hand separator intentionally lives inside the BrowserView
  // parent panel, immediately before content_inner_panel_. CEF BrowserViews are
  // native child surfaces on Linux and can repaint above sibling Views during
  // tab/page activation; when the active page is white that race made the old
  // main-panel-level sidebar_border_panel_ appear as #ffffff. Keeping the
  // separator in the BrowserView parent clips the native page to its right.
  content_panel_->SetBackgroundColor(theme::kAppBg);
  CefBoxLayoutSettings content_settings = {};
  content_settings.size = sizeof(content_settings);
  content_settings.horizontal = true;
  content_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> content_layout =
      content_panel_->SetToBoxLayout(content_settings);
  main_panel_->AddChildView(content_panel_);
  main_layout->SetFlexForView(content_panel_, 1);

  sidebar_border_panel_ = CefPanel::CreatePanel(nullptr);
  sidebar_border_panel_->SetID(kSidebarBorderPanelId);
  sidebar_border_panel_->SetBackgroundColor(SidebarBorderColor());
  content_panel_->AddChildView(sidebar_border_panel_);

  content_inner_panel_ = CefPanel::CreatePanel(nullptr);
  content_inner_panel_->SetID(kContentInnerPanelId);
  content_inner_panel_->SetBackgroundColor(theme::kAppBg);
  content_inner_panel_->SetToFillLayout();
  content_panel_->AddChildView(content_inner_panel_);
  content_layout->SetFlexForView(content_inner_panel_, 1);

  devtools_panel_ = CefPanel::CreatePanel(this);
  devtools_panel_->SetID(kDevToolsPanelId);
  devtools_panel_->SetBackgroundColor(theme::kBorderUnfocused);
  CefBoxLayoutSettings devtools_settings = {};
  devtools_settings.size = sizeof(devtools_settings);
  devtools_settings.horizontal = true;
  devtools_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  // The left border is the outer panel's background exposed by this inset. That
  // makes the separator part of the DevTools pane itself, so a Chrome-style
  // BrowserView can never overlap or intermittently cover it.
  devtools_settings.inside_border_insets =
      CefInsets(0, kDevToolsBorderWidth, 0, 0);
  CefRefPtr<CefBoxLayout> devtools_layout =
      devtools_panel_->SetToBoxLayout(devtools_settings);
  devtools_panel_->SetVisible(false);
  main_panel_->AddChildView(devtools_panel_);

  devtools_content_panel_ = CefPanel::CreatePanel(nullptr);
  devtools_content_panel_->SetID(kDevToolsContentPanelId);
  devtools_content_panel_->SetBackgroundColor(theme::kAppBg);
  devtools_content_panel_->SetToFillLayout();
  devtools_panel_->AddChildView(devtools_content_panel_);
  devtools_layout->SetFlexForView(devtools_content_panel_, 1);

  status_bar_panel_ = CefPanel::CreatePanel(this);
  status_bar_panel_->SetID(kStatusBarPanelId);
  status_bar_panel_->SetBackgroundColor(theme::kSidebarBg);
  CefBoxLayoutSettings status_settings = {};
  status_settings.size = sizeof(status_settings);
  status_settings.horizontal = true;
  status_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> status_layout =
      status_bar_panel_->SetToBoxLayout(status_settings);

  status_sidebar_spacer_panel_ = CefPanel::CreatePanel(nullptr);
  status_sidebar_spacer_panel_->SetID(kStatusSidebarSpacerPanelId);
  status_sidebar_spacer_panel_->SetBackgroundColor(theme::kSidebarBg);
  status_bar_panel_->AddChildView(status_sidebar_spacer_panel_);

  status_border_panel_ = CefPanel::CreatePanel(nullptr);
  status_border_panel_->SetID(kStatusBorderPanelId);
  status_border_panel_->SetBackgroundColor(theme::kBorderUnfocused);
  status_bar_panel_->AddChildView(status_border_panel_);

  status_content_panel_ = CefPanel::CreatePanel(nullptr);
  status_content_panel_->SetID(kStatusContentPanelId);
  status_content_panel_->SetBackgroundColor(StatusBarBackgroundColor());
  CefBoxLayoutSettings status_content_settings = {};
  status_content_settings.size = sizeof(status_content_settings);
  status_content_settings.horizontal = true;
  status_content_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> status_content_layout =
      status_content_panel_->SetToBoxLayout(status_content_settings);
  status_bar_panel_->AddChildView(status_content_panel_);
  status_layout->SetFlexForView(status_content_panel_, 1);

  status_output_field_ = CefTextfield::CreateTextfield(this);
  status_output_field_->SetID(kStatusOutputFieldId);
  StyleTextfield(status_output_field_, theme::kText,
                 StatusBarBackgroundColor(), "monospace, 12px");
  status_output_field_->SetAccessibleName("vimbrowser status output");
  status_content_panel_->AddChildView(status_output_field_);

  status_url_label_ = CefLabelButton::CreateLabelButton(this, "");
  status_url_label_->SetID(kStatusUrlFieldId);
  status_url_label_->SetFontList("monospace, 12px");
  status_url_label_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_RIGHT);
  status_url_label_->SetFocusable(false);
  status_url_label_->SetInkDropEnabled(false);
  status_url_label_->SetBackgroundColor(StatusBarBackgroundColor());
  status_url_label_->SetEnabledTextColors(theme::kText);
  status_url_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
  status_url_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
  status_url_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
  status_url_label_->SetAccessibleName("vimbrowser current tab URL");
  status_content_panel_->AddChildView(status_url_label_);
  status_content_layout->SetFlexForView(status_url_label_, 1);

  status_mode_field_ = CefTextfield::CreateTextfield(this);
  status_mode_field_->SetID(kStatusModeFieldId);
  StyleTextfield(status_mode_field_, theme::kVimNormal,
                 StatusBarBackgroundColor(), "monospace, 12px");
  status_mode_field_->SetAccessibleName("vimbrowser current mode");
  status_content_panel_->AddChildView(status_mode_field_);

  root_panel_->AddChildView(status_bar_panel_);

  if (a26_shell_) {
    BuildA26Chrome();
  }

  command_panel_ = CefPanel::CreatePanel(this);
  command_panel_->SetID(kCommandPanelId);
  command_panel_->SetBackgroundColor(theme::kAppBg);

  command_separator_panel_ = CefPanel::CreatePanel(nullptr);
  command_separator_panel_->SetID(kCommandSeparatorPanelId);
  command_separator_panel_->SetBackgroundColor(theme::kAccent);

  command_content_panel_ = CefPanel::CreatePanel(this);
  command_content_panel_->SetID(kCommandContentPanelId);
  command_content_panel_->SetBackgroundColor(theme::kAppBg);
  command_panel_->AddChildView(command_content_panel_);

  command_field_ = CefTextfield::CreateTextfield(this);
  command_field_->SetID(kCommandFieldId);
  StyleCommandField(command_field_);
  command_field_->SetAccessibleName("vimbrowser command line");
  command_content_panel_->AddChildView(command_field_);

  command_overlay_ = window_->AddOverlayView(command_panel_, CEF_DOCKING_MODE_CUSTOM,
                                            false);
  command_overlay_->SetVisible(false);
  command_separator_overlay_ = window_->AddOverlayView(
      command_separator_panel_, CEF_DOCKING_MODE_CUSTOM, false);
  command_separator_overlay_->SetVisible(false);

  autocomplete_panel_ = CefPanel::CreatePanel(this);
  autocomplete_panel_->SetID(kCommandAutocompletePanelId);
  autocomplete_panel_->SetBackgroundColor(theme::kSidebarBg);
  CefBoxLayoutSettings autocomplete_settings = {};
  autocomplete_settings.size = sizeof(autocomplete_settings);
  autocomplete_settings.horizontal = false;
  autocomplete_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  autocomplete_settings.inside_border_insets =
      CefInsets(kCommandAutocompleteBorder, kCommandAutocompleteBorder,
                kCommandAutocompleteBorder, kCommandAutocompleteBorder);
  autocomplete_panel_->SetToBoxLayout(autocomplete_settings);
  autocomplete_overlay_ = window_->AddOverlayView(
      autocomplete_panel_, CEF_DOCKING_MODE_CUSTOM, false);
  autocomplete_overlay_->SetVisible(false);

  if (kModeIndicatorEnabled) {
    mode_indicator_panel_ = CefPanel::CreatePanel(this);
    mode_indicator_panel_->SetID(kModeIndicatorPanelId);
    mode_indicator_panel_->SetBackgroundColor(theme::kUserBg);
    mode_indicator_panel_->SetToFillLayout();

    mode_indicator_label_ = CefLabelButton::CreateLabelButton(this, "");
    mode_indicator_label_->SetID(kModeIndicatorFieldId);
    mode_indicator_label_->SetFontList("monospace, 12px");
    mode_indicator_label_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
    mode_indicator_label_->SetFocusable(false);
    mode_indicator_label_->SetInkDropEnabled(false);
    mode_indicator_label_->SetBackgroundColor(theme::kUserBg);
    mode_indicator_label_->SetEnabledTextColors(theme::kText);
    mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
    mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
    mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
    mode_indicator_panel_->AddChildView(mode_indicator_label_);

    mode_indicator_overlay_ = window_->AddOverlayView(
        mode_indicator_panel_, CEF_DOCKING_MODE_CUSTOM, false);
    mode_indicator_overlay_->SetVisible(true);

    fps_indicator_panel_ = CefPanel::CreatePanel(this);
    fps_indicator_panel_->SetID(kFpsIndicatorPanelId);
    fps_indicator_panel_->SetBackgroundColor(theme::kUserBg);
    fps_indicator_panel_->SetToFillLayout();

    fps_indicator_label_ = CefLabelButton::CreateLabelButton(this, "");
    fps_indicator_label_->SetID(kFpsIndicatorFieldId);
    fps_indicator_label_->SetFontList("monospace, 12px");
    fps_indicator_label_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
    fps_indicator_label_->SetFocusable(false);
    fps_indicator_label_->SetInkDropEnabled(false);
    fps_indicator_label_->SetBackgroundColor(theme::kUserBg);
    fps_indicator_label_->SetEnabledTextColors(theme::kText);
    fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
    fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
    fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
    fps_indicator_panel_->AddChildView(fps_indicator_label_);

    fps_indicator_overlay_ = window_->AddOverlayView(
        fps_indicator_panel_, CEF_DOCKING_MODE_CUSTOM, false);
    fps_indicator_overlay_->SetVisible(show_fps_indicator_);
  }

  // Draw the sidebar/page separator as a Window overlay, above Chromium's
  // native BrowserView surface.  The in-layout separator reserves the pixel so
  // content geometry stays correct, but tab activation/new-tab BrowserView
  // remapping can momentarily repaint that native surface over sibling Views.
  // Keeping an overlay copy on top makes the separator immune to that native
  // child-window/compositor race.
  sidebar_border_overlay_panel_ = CefPanel::CreatePanel(this);
  sidebar_border_overlay_panel_->SetID(kSidebarBorderOverlayPanelId);
  sidebar_border_overlay_panel_->SetBackgroundColor(SidebarBorderColor());
  sidebar_border_overlay_ = window_->AddOverlayView(
      sidebar_border_overlay_panel_, CEF_DOCKING_MODE_CUSTOM, false);
  sidebar_border_overlay_->SetVisible(false);

  media_permission_panel_ = CefPanel::CreatePanel(this);
  media_permission_panel_->SetID(kMediaPermissionPromptPanelId);
  media_permission_panel_->SetBackgroundColor(theme::kAccent);

  media_permission_top_border_panel_ = CefPanel::CreatePanel(this);
  media_permission_top_border_panel_->SetID(kMediaPermissionBorderTopPanelId);
  media_permission_top_border_panel_->SetBackgroundColor(theme::kAccent);

  media_permission_bottom_border_panel_ = CefPanel::CreatePanel(this);
  media_permission_bottom_border_panel_->SetID(kMediaPermissionBorderBottomPanelId);
  media_permission_bottom_border_panel_->SetBackgroundColor(theme::kAccent);

  media_permission_left_border_panel_ = CefPanel::CreatePanel(this);
  media_permission_left_border_panel_->SetID(kMediaPermissionBorderLeftPanelId);
  media_permission_left_border_panel_->SetBackgroundColor(theme::kAccent);

  media_permission_right_border_panel_ = CefPanel::CreatePanel(this);
  media_permission_right_border_panel_->SetID(kMediaPermissionBorderRightPanelId);
  media_permission_right_border_panel_->SetBackgroundColor(theme::kAccent);

  media_permission_content_panel_ = CefPanel::CreatePanel(this);
  media_permission_content_panel_->SetID(kMediaPermissionPromptContentPanelId);
  media_permission_content_panel_->SetBackgroundColor(theme::kAppBg);
  CefBoxLayoutSettings media_permission_content_settings = {};
  media_permission_content_settings.size = sizeof(media_permission_content_settings);
  media_permission_content_settings.horizontal = false;
  media_permission_content_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  media_permission_content_settings.inside_border_insets = CefInsets(8, 12, 8, 12);
  media_permission_content_settings.between_child_spacing = 2;
  media_permission_content_panel_->SetToBoxLayout(
      media_permission_content_settings);
  media_permission_panel_->AddChildView(media_permission_content_panel_);

  media_permission_title_field_ = CefTextfield::CreateTextfield(this);
  media_permission_title_field_->SetID(kMediaPermissionTitleFieldId);
  StyleTextfield(media_permission_title_field_, theme::kCommand, theme::kAppBg,
                 "monospace, 13px");
  media_permission_title_field_->SetAccessibleName(
      "vimbrowser media permission title");
  media_permission_content_panel_->AddChildView(media_permission_title_field_);

  media_permission_origin_field_ = CefTextfield::CreateTextfield(this);
  media_permission_origin_field_->SetID(kMediaPermissionOriginFieldId);
  StyleTextfield(media_permission_origin_field_, theme::kMuted, theme::kAppBg,
                 "monospace, 12px");
  media_permission_origin_field_->SetAccessibleName(
      "vimbrowser media permission origin");
  media_permission_content_panel_->AddChildView(media_permission_origin_field_);

  media_permission_body_field_ = CefTextfield::CreateTextfield(this);
  media_permission_body_field_->SetID(kMediaPermissionBodyFieldId);
  StyleTextfield(media_permission_body_field_, theme::kText, theme::kAppBg,
                 "monospace, 12px");
  media_permission_body_field_->SetAccessibleName(
      "vimbrowser media permission request");
  media_permission_content_panel_->AddChildView(media_permission_body_field_);

  media_permission_hint_field_ = CefTextfield::CreateTextfield(this);
  media_permission_hint_field_->SetID(kMediaPermissionHintFieldId);
  StyleTextfield(media_permission_hint_field_, theme::kMuted, theme::kAppBg,
                 "monospace, 12px");
  media_permission_hint_field_->SetAccessibleName(
      "vimbrowser media permission shortcuts");

  media_permission_button_panel_ = CefPanel::CreatePanel(this);
  media_permission_button_panel_->SetID(kMediaPermissionButtonPanelId);
  media_permission_button_panel_->SetBackgroundColor(theme::kAppBg);
  CefBoxLayoutSettings media_permission_button_settings = {};
  media_permission_button_settings.size = sizeof(media_permission_button_settings);
  media_permission_button_settings.horizontal = true;
  media_permission_button_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_END;
  media_permission_button_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  media_permission_button_settings.between_child_spacing = 8;
  media_permission_button_panel_->SetToBoxLayout(media_permission_button_settings);
  media_permission_content_panel_->AddChildView(media_permission_button_panel_);

  media_permission_allow_button_ =
      CefLabelButton::CreateLabelButton(this, "[a] allow");
  media_permission_allow_button_->SetID(kMediaPermissionAllowButtonId);
  StylePermissionButton(media_permission_allow_button_, theme::kText,
                        theme::kAccent);
  media_permission_allow_button_->SetAccessibleName(
      "allow media permission for this origin");
  media_permission_button_panel_->AddChildView(media_permission_allow_button_);

  media_permission_deny_button_ =
      CefLabelButton::CreateLabelButton(this, "[d] deny");
  media_permission_deny_button_->SetID(kMediaPermissionDenyButtonId);
  StylePermissionButton(media_permission_deny_button_, theme::kText,
                        theme::kSidebarSelBg);
  media_permission_deny_button_->SetAccessibleName(
      "deny media permission for this origin");
  media_permission_button_panel_->AddChildView(media_permission_deny_button_);

  media_permission_overlay_ = window_->AddOverlayView(
      media_permission_panel_, CEF_DOCKING_MODE_CUSTOM, true);
  media_permission_overlay_->SetVisible(false);

  // Keep the accent border as independent window overlays instead of child
  // panels inside the prompt. CEF can relayout/repaint custom overlay children
  // during activation changes in a way that clips non-layout children; separate
  // overlays have stable absolute window bounds and z-order above the prompt.
  media_permission_top_border_overlay_ = window_->AddOverlayView(
      media_permission_top_border_panel_, CEF_DOCKING_MODE_CUSTOM, false);
  media_permission_bottom_border_overlay_ = window_->AddOverlayView(
      media_permission_bottom_border_panel_, CEF_DOCKING_MODE_CUSTOM, false);
  media_permission_left_border_overlay_ = window_->AddOverlayView(
      media_permission_left_border_panel_, CEF_DOCKING_MODE_CUSTOM, false);
  media_permission_right_border_overlay_ = window_->AddOverlayView(
      media_permission_right_border_panel_, CEF_DOCKING_MODE_CUSTOM, false);
  media_permission_top_border_overlay_->SetVisible(false);
  media_permission_bottom_border_overlay_->SetVisible(false);
  media_permission_left_border_overlay_->SetVisible(false);
  media_permission_right_border_overlay_->SetVisible(false);
}

void BrowserWindow::BuildA26Chrome() {
  if (!a26_shell_ || !root_panel_) {
    return;
  }

  a26_chrome_panel_ = CefPanel::CreatePanel(this);
  a26_chrome_panel_->SetID(kA26ChromePanelId);
  a26_chrome_panel_->SetBackgroundColor(theme::kAppBg);
  // This ordinary root child reserves the bottom navigation space. The
  // interactive navigation row is a Window overlay above Chromium's native X11
  // child surface; otherwise the BrowserView can win hit-testing even though a
  // sibling Views panel paints on top of it.
  root_panel_->AddChildView(a26_chrome_panel_);

  a26_navigation_panel_ = CefPanel::CreatePanel(this);
  a26_navigation_panel_->SetID(kA26NavigationPanelId);
  a26_navigation_panel_->SetBackgroundColor(theme::kSidebarBg);
  CefBoxLayoutSettings navigation_settings = {};
  navigation_settings.size = sizeof(navigation_settings);
  navigation_settings.horizontal = true;
  navigation_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  navigation_settings.inside_border_insets = CefInsets(6, 6, 6, 6);
  navigation_settings.between_child_spacing = 4;
  CefRefPtr<CefBoxLayout> navigation_layout =
      a26_navigation_panel_->SetToBoxLayout(navigation_settings);
  a26_navigation_overlay_ = window_->AddOverlayView(
      a26_navigation_panel_, CEF_DOCKING_MODE_CUSTOM, true);
  a26_navigation_overlay_->SetVisible(true);

  a26_back_button_ = CefLabelButton::CreateLabelButton(this, "\u2190");
  a26_back_button_->SetID(kA26BackButtonId);
  a26_back_button_->SetAccessibleName("Back");
  StyleA26Button(a26_back_button_);
  a26_navigation_panel_->AddChildView(a26_back_button_);

  a26_forward_button_ = CefLabelButton::CreateLabelButton(this, "\u2192");
  a26_forward_button_->SetID(kA26ForwardButtonId);
  a26_forward_button_->SetAccessibleName("Forward");
  StyleA26Button(a26_forward_button_);
  a26_navigation_panel_->AddChildView(a26_forward_button_);

  a26_url_field_ = CefTextfield::CreateTextfield(this);
  a26_url_field_->SetID(kA26UrlFieldId);
  StyleA26UrlField(a26_url_field_);
  a26_url_field_->SetPlaceholderText("URL or search");
  a26_url_field_->SetPlaceholderTextColor(theme::kMuted);
  a26_url_field_->SetAccessibleName("Address");
  a26_navigation_panel_->AddChildView(a26_url_field_);
  navigation_layout->SetFlexForView(a26_url_field_, 1);

  a26_reload_button_ = CefLabelButton::CreateLabelButton(this, "Reload");
  a26_reload_button_->SetID(kA26ReloadButtonId);
  a26_reload_button_->SetAccessibleName("Reload");
  StyleA26Button(a26_reload_button_);
  a26_navigation_panel_->AddChildView(a26_reload_button_);

  a26_tabs_button_ = CefLabelButton::CreateLabelButton(this, "Tabs 0/0");
  a26_tabs_button_->SetID(kA26TabsButtonId);
  a26_tabs_button_->SetAccessibleName("Activate next tab");
  StyleA26Button(a26_tabs_button_);
  a26_navigation_panel_->AddChildView(a26_tabs_button_);

  // Keep the legacy child at zero height so old layout delegates remain stable.
  // Moon classifies a tap versus an upward close swipe before forwarding input,
  // so no empty gesture-only strip is needed below the navigation row.
  a26_bottom_reserve_panel_ = CefPanel::CreatePanel(this);
  a26_bottom_reserve_panel_->SetID(kA26BottomReservePanelId);
  a26_bottom_reserve_panel_->SetBackgroundColor(theme::kAppBg);
  a26_chrome_panel_->AddChildView(a26_bottom_reserve_panel_);

  // main_panel_ is the root layout's only flex child, so this panel keeps its
  // fixed preferred height without an explicit (and toolkit-dependent) flex 0.
  UpdateA26Chrome();
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  CancelNativeContextMenu();
  RequestA26Keyboard(A26KeyboardPurpose::kHide);
  StopSidebarMouseWatcher();
  ++active_browser_sync_generation_;
  ++state_save_generation_;
  SaveState();
  CancelAllMediaPermissionRequests();
  if (ipc_server_) {
    ipc_server_->Stop();
    ipc_server_.reset();
  }
  tabs_.clear();
  media_permission_right_border_overlay_ = nullptr;
  media_permission_left_border_overlay_ = nullptr;
  media_permission_bottom_border_overlay_ = nullptr;
  media_permission_top_border_overlay_ = nullptr;
  sidebar_border_overlay_ = nullptr;
  media_permission_overlay_ = nullptr;
  fps_indicator_overlay_ = nullptr;
  mode_indicator_overlay_ = nullptr;
  autocomplete_overlay_ = nullptr;
  command_separator_overlay_ = nullptr;
  command_overlay_ = nullptr;
  fps_indicator_label_ = nullptr;
  fps_indicator_panel_ = nullptr;
  media_permission_deny_button_ = nullptr;
  media_permission_allow_button_ = nullptr;
  media_permission_hint_field_ = nullptr;
  media_permission_body_field_ = nullptr;
  media_permission_origin_field_ = nullptr;
  media_permission_title_field_ = nullptr;
  media_permission_button_panel_ = nullptr;
  media_permission_content_panel_ = nullptr;
  media_permission_right_border_panel_ = nullptr;
  media_permission_left_border_panel_ = nullptr;
  media_permission_bottom_border_panel_ = nullptr;
  media_permission_top_border_panel_ = nullptr;
  media_permission_panel_ = nullptr;
  mode_indicator_label_ = nullptr;
  mode_indicator_panel_ = nullptr;
  sidebar_border_overlay_panel_ = nullptr;
  devtools_browser_view_delegate_ = nullptr;
  devtools_client_ = nullptr;
  devtools_browser_view_ = nullptr;
  devtools_content_panel_ = nullptr;
  devtools_panel_ = nullptr;
  autocomplete_rows_.clear();
  autocomplete_panel_ = nullptr;
  command_field_ = nullptr;
  command_content_panel_ = nullptr;
  command_panel_ = nullptr;
  a26_navigation_overlay_ = nullptr;
  status_url_label_ = nullptr;
  a26_tabs_button_ = nullptr;
  a26_reload_button_ = nullptr;
  a26_url_field_ = nullptr;
  a26_forward_button_ = nullptr;
  a26_back_button_ = nullptr;
  a26_bottom_reserve_panel_ = nullptr;
  a26_navigation_panel_ = nullptr;
  a26_chrome_panel_ = nullptr;
  status_mode_field_ = nullptr;
  status_output_field_ = nullptr;
  status_content_panel_ = nullptr;
  status_border_panel_ = nullptr;
  status_sidebar_spacer_panel_ = nullptr;
  status_bar_panel_ = nullptr;
  content_inner_panel_ = nullptr;
  content_panel_ = nullptr;
  command_separator_panel_ = nullptr;
  sidebar_border_panel_ = nullptr;
  sidebar_rows_.clear();
  sidebar_spacer_ = nullptr;
  sidebar_content_panel_ = nullptr;
  sidebar_panel_ = nullptr;
  main_panel_ = nullptr;
  root_panel_ = nullptr;
  window_ = nullptr;
  a26_keyboard_.reset();
  CefQuitMessageLoop();
}

void BrowserWindow::OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                                          const CefRect& new_bounds) {
  RefreshSidebar();
  Layout();
}

bool BrowserWindow::CanClose(CefRefPtr<CefWindow> window) {
  if (window_close_allowed_ || AllTabBrowsersClosed()) {
    StopSidebarMouseWatcher();
    window_close_allowed_ = true;
    return true;
  }

  if (!window_close_pending_) {
    window_close_pending_ = true;
    ++active_browser_sync_generation_;
    ++state_save_generation_;
    SaveState();
    StopSidebarMouseWatcher();
    if (file_chooser_upload_.phase == FileChooserUploadPhase::kArmed ||
        file_chooser_upload_.phase == FileChooserUploadPhase::kValidating) {
      CancelFileChooserUpload(file_chooser_upload_.tab_id);
    }
    if (ipc_server_) {
      ipc_server_->Stop();
      ipc_server_.reset();
    }
  }

  bool all_ready_to_close = true;
  for (Tab& tab : tabs_) {
    if (tab.client && tab.client->browser()) {
      all_ready_to_close &= tab.client->browser()->GetHost()->TryCloseBrowser();
    }
  }
  if (!all_ready_to_close) {
    return false;
  }

  window_close_allowed_ = true;
  return true;
}

void BrowserWindow::MaybeArmModifiedSpaceKeyUpSuppression(
    const CefKeyEvent& event) {
  if (!IsRawKeyDown(event) || !IsSpaceKey(event) || !HasKeyModifier(event) ||
      mode_ != Mode::kNormal) {
    return;
  }

  const bool page_shortcuts_active =
      focus_area_ == FocusArea::kWebView &&
      (website_mode_ == vim::Mode::kWebsiteNormal ||
       website_mode_ == vim::Mode::kNormal);
  const bool devtools_hints_active =
      focus_area_ == FocusArea::kDevTools &&
      devtools_mode_ != vim::Mode::kInsert;
  if (!page_shortcuts_active && !devtools_hints_active) {
    return;
  }

  // The raw modified-Space keydown is owned by browser chrome. Its physical
  // keyup can arrive after the modifier keyup and therefore look like a lone
  // Space to the page. YouTube reacts to that orphaned release when its play
  // button is focused, so keep ownership through the end of the key sequence.
  suppress_modified_space_key_up_ = true;
  modified_space_key_up_clear_scheduled_ = false;
  ++modified_space_key_up_generation_;
}

bool BrowserWindow::HandleModifiedSpaceKeyUpSuppression(
    const CefKeyEvent& event) {
  if (!suppress_modified_space_key_up_ || event.type != KEYEVENT_KEYUP ||
      !IsSpaceKey(event)) {
    return false;
  }

  // Both the Views window delegate and CEF keyboard handler may observe this
  // release. Delay clearing so every callback for the physical event consumes
  // it, while a later Ctrl+Space sequence can invalidate this task by generation.
  if (!modified_space_key_up_clear_scheduled_) {
    modified_space_key_up_clear_scheduled_ = true;
    const uint64_t generation = modified_space_key_up_generation_;
    CefRefPtr<BrowserWindow> self = this;
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&BrowserWindow::ClearModifiedSpaceKeyUpSuppression,
                       self, generation),
        50);
  }
  return true;
}

void BrowserWindow::ClearModifiedSpaceKeyUpSuppression(uint64_t generation) {
  if (generation != modified_space_key_up_generation_) {
    return;
  }
  suppress_modified_space_key_up_ = false;
  modified_space_key_up_clear_scheduled_ = false;
}

bool BrowserWindow::OnKeyEvent(CefRefPtr<CefWindow> window,
                               const CefKeyEvent& event) {
  MaybeArmModifiedSpaceKeyUpSuppression(event);
  if (HandleModifiedSpaceKeyUpSuppression(event)) {
    return true;
  }
  if (HandleNativeContextMenuKey(event)) {
    return true;
  }
  if (HandleMediaPermissionPromptKey(event)) {
    return true;
  }
  if (forwarding_key_to_page_ && (IsEscapeKey(event) || IsSpaceKey(event))) {
    return false;
  }
  if (mode_ != Mode::kNormal && IsCharEvent(event) && PlainKeyChar(event) == ':') {
    return true;
  }
  if (mode_ != Mode::kNormal) {
    return HandleCommandModeKey(event);
  }
  if (a26_shell_ && focus_area_ == FocusArea::kA26Url) {
    return false;
  }

  if (native_hints_active_ &&
      (focus_area_ == FocusArea::kWebView || focus_area_ == FocusArea::kDevTools)) {
    // Native hints live in Blink and own the full key stream until they stop.
    // Do not let shell/page shortcuts (including YouTube h/j/k/l) race the hint
    // label matcher.
    return false;
  }

  if (HandleGlobalFocusKey(event)) {
    return true;
  }

  if (focus_area_ == FocusArea::kWebView) {
    return HandleWebsiteModeKey(event);
  }

  if (focus_area_ == FocusArea::kDevTools) {
    return HandleDevToolsModeKey(event);
  }

  if (focus_area_ == FocusArea::kTabSidebar && IsEscapeKey(event) &&
      (sidebar_visual_anchor_.type != SidebarItemType::kNone ||
       !sidebar_pending_keys_.empty())) {
    sidebar_visual_anchor_ = {};
    sidebar_pending_keys_.clear();
    RefreshSidebar();
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar &&
      (IsEscapeKey(event) || IsSpaceKey(event))) {
    ForwardKeyToActivePage(event);
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar && !IsRawKeyDown(event)) {
    return IsCharEvent(event);
  }

  if (!IsRawKeyDown(event)) {
    return false;
  }

  return HandleNormalModeKey(event);
}

bool BrowserWindow::OnAccelerator(CefRefPtr<CefWindow> window, int command_id) {
  if (native_context_menu_) {
    return true;
  }
  if (active_media_permission_) {
    return true;
  }
  if (forwarding_key_to_page_) {
    return false;
  }
  if (mode_ != Mode::kNormal && command_vim_.mode == vim::Mode::kInsert) {
    if (command_id == kAcceleratorCommandTab ||
        command_id == kAcceleratorCommandBacktab) {
      return CycleCommandAutocomplete(command_id == kAcceleratorCommandBacktab ? -1 : 1);
    }
    if (command_id == kAcceleratorCommandDeleteCompletion) {
      DeleteSelectedCommandAutocomplete();
      return true;
    }
  }
  if (a26_shell_ && focus_area_ == FocusArea::kA26Url) {
    return false;
  }
  if (mode_ == Mode::kNormal && !native_hints_active_) {
    if (command_id == kAcceleratorFocusNext) {
      FocusRelative(1);
      return true;
    }
    if (command_id == kAcceleratorFocusPrevious) {
      FocusRelative(-1);
      return true;
    }
    if (command_id == kAcceleratorToggleDevToolsSemicolon ||
        command_id == kAcceleratorToggleDevToolsOem1) {
      ToggleDevTools();
      return true;
    }
  }
  if (focus_area_ == FocusArea::kDevTools &&
      (command_id == kAcceleratorTabNext ||
       command_id == kAcceleratorTabPrevious)) {
    return false;
  }
  if (mode_ == Mode::kNormal && focus_area_ == FocusArea::kWebView &&
      !native_hints_active_ &&
      (command_id == kAcceleratorCommandTab ||
       command_id == kAcceleratorCommandBacktab)) {
    CefKeyEvent event;
    event.type = KEYEVENT_RAWKEYDOWN;
    event.windows_key_code = 0x09;
    event.native_key_code = 23;
    event.character = 0;
    event.unmodified_character = 0;
    event.modifiers = command_id == kAcceleratorCommandBacktab
                          ? EVENTFLAG_SHIFT_DOWN
                          : 0;
    ForwardKeyToActivePage(event);
    return true;
  }
  if (mode_ == Mode::kNormal && !native_hints_active_ &&
      !(focus_area_ == FocusArea::kWebView &&
        website_mode_ == vim::Mode::kInsert) &&
      !(focus_area_ == FocusArea::kDevTools &&
        devtools_mode_ == vim::Mode::kInsert)) {
    if ((command_id == kAcceleratorHintRightClick ||
         command_id == kAcceleratorHintHover) &&
        focus_area_ == FocusArea::kDevTools) {
      CefKeyEvent event;
      event.type = KEYEVENT_RAWKEYDOWN;
      event.windows_key_code =
          command_id == kAcceleratorHintRightClick ? 'L' : 'H';
      event.native_key_code = event.windows_key_code;
      event.character = 0;
      event.unmodified_character =
          command_id == kAcceleratorHintRightClick ? 'l' : 'h';
      event.modifiers = EVENTFLAG_CONTROL_DOWN;
      return StartDevToolsNativeHints(event);
    }
    if ((command_id == kAcceleratorHintRightClick ||
         command_id == kAcceleratorHintHover) &&
        focus_area_ == FocusArea::kWebView) {
      CefKeyEvent event;
      event.type = KEYEVENT_RAWKEYDOWN;
      event.windows_key_code =
          command_id == kAcceleratorHintRightClick ? 'L' : 'H';
      event.native_key_code = event.windows_key_code;
      event.character = 0;
      event.unmodified_character =
          command_id == kAcceleratorHintRightClick ? 'l' : 'h';
      event.modifiers = EVENTFLAG_CONTROL_DOWN;
      return StartNativeHints(event);
    }
    if (command_id == kAcceleratorSidebarSpace &&
        focus_area_ == FocusArea::kTabSidebar) {
      CefKeyEvent event;
      event.type = KEYEVENT_RAWKEYDOWN;
      event.windows_key_code = 0x20;
      event.native_key_code = 65;
      event.character = 0x20;
      event.unmodified_character = 0x20;
      ForwardKeyToActivePage(event);
      return true;
    }
    if (command_id == kAcceleratorTabNext) {
      SetFocusArea(FocusArea::kTabSidebar);
      MoveSidebarSelection(1);
      return true;
    }
    if (command_id == kAcceleratorTabPrevious) {
      SetFocusArea(FocusArea::kTabSidebar);
      MoveSidebarSelection(-1);
      return true;
    }
  }
  return false;
}

bool BrowserWindow::HandleBrowserKeyEvent(const CefKeyEvent& event) {
  MaybeArmModifiedSpaceKeyUpSuppression(event);
  if (HandleModifiedSpaceKeyUpSuppression(event)) {
    return true;
  }
  if (HandleNativeContextMenuKey(event)) {
    return true;
  }
  if (HandleMediaPermissionPromptKey(event)) {
    return true;
  }
  if (forwarding_key_to_devtools_) {
    return false;
  }
  if (forwarding_key_to_page_ && (IsEscapeKey(event) || IsSpaceKey(event))) {
    return false;
  }
  if (mode_ != Mode::kNormal) {
    return HandleCommandModeKey(event);
  }
  if (a26_shell_ && focus_area_ == FocusArea::kA26Url) {
    return false;
  }

  if (native_hints_active_ &&
      (focus_area_ == FocusArea::kWebView || focus_area_ == FocusArea::kDevTools)) {
    // Native hints live in Blink and own the full key stream until they stop.
    // Do not let shell/page shortcuts (including YouTube h/j/k/l) race the hint
    // label matcher.
    return false;
  }

  if (HandleGlobalFocusKey(event)) {
    return true;
  }

  if (focus_area_ == FocusArea::kWebView) {
    return HandleWebsiteModeKey(event);
  }

  if (focus_area_ == FocusArea::kDevTools) {
    return HandleDevToolsModeKey(event);
  }

  if (focus_area_ == FocusArea::kTabSidebar && IsEscapeKey(event) &&
      (sidebar_visual_anchor_.type != SidebarItemType::kNone ||
       !sidebar_pending_keys_.empty())) {
    sidebar_visual_anchor_ = {};
    sidebar_pending_keys_.clear();
    RefreshSidebar();
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar &&
      (IsEscapeKey(event) || IsSpaceKey(event))) {
    ForwardKeyToActivePage(event);
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar && !IsRawKeyDown(event)) {
    return IsCharEvent(event);
  }

  if (!IsRawKeyDown(event)) {
    return false;
  }

  return HandleNormalModeKey(event);
}

bool BrowserWindow::HandleNormalModeKey(const CefKeyEvent& event) {
  if (!IsRawKeyDown(event)) {
    return false;
  }

  const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
  const bool shift = event.modifiers & EVENTFLAG_SHIFT_DOWN;

  if (ctrl && shift && event.windows_key_code == 'I') {
    if (Tab* tab = ActiveTab(); tab && tab->client) {
      tab->client->ShowDevTools();
    }
    return true;
  }

  if (mode_ != Mode::kNormal) {
    return false;
  }

  if (focus_area_ == FocusArea::kTabSidebar &&
      HasOnlyControlModifier(event)) {
    if (IsCtrlKey(event, 'E')) return ScrollSidebarByKey('E');
    if (IsCtrlKey(event, 'Y')) return ScrollSidebarByKey('Y');
    if (IsCtrlKey(event, 'D')) return ScrollSidebarByKey('D');
    if (IsCtrlKey(event, 'U')) return ScrollSidebarByKey('U');
    if (IsCtrlKey(event, 'F')) return ScrollSidebarByKey('F');
    if (IsCtrlKey(event, 'B')) return ScrollSidebarByKey('B');
  }

  if (PlainKeyChar(event) == ':') {
    BeginCommandText(":");
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar && IsPlain(event)) {
    if (IsEnterKey(event)) {
      sidebar_pending_keys_.clear();
      ActivateSidebarItem(sidebar_selected_item_);
      return true;
    }
    if (IsBackspaceKey(event)) {
      sidebar_pending_keys_.clear();
      LeaveSidebarFolder();
      return true;
    }
    switch (PlainKeyChar(event)) {
    case 'i':
    case 'I':
      ResetWebsitePendingKeys();
      website_mode_ = vim::Mode::kInsert;
      // The sidebar owns the mode-entry key. If the toolkit still emits a
      // trailing CHAR after focus moves to the page, consume only that CHAR so
      // `i` does not type into the newly-focused webview.
      suppress_next_website_char_ = 'i';
      SetFocusArea(FocusArea::kWebView);
      return true;
    case 'j':
      ResetWebsitePendingKeys();
      MoveSidebarSelection(1);
      return true;
    case 'k':
      ResetWebsitePendingKeys();
      MoveSidebarSelection(-1);
      return true;
    }
  }

  if (focus_area_ != FocusArea::kTabSidebar && HandleWebsiteCommandKey(event)) {
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar) {
    const char shared_website_key = PlainKeyChar(event);
    if (shared_website_key == 'r' || shared_website_key == 'p' ||
        shared_website_key == 'P') {
      sidebar_pending_keys_.clear();
      return HandleWebsiteCommandKey(event);
    }

    if (const std::optional<bool> open_new_tab =
            OpenCommandNewTabForKey(event)) {
      BeginCommand(*open_new_tab ? Mode::kCommandOpenNext
                                 : Mode::kCommandOpenCurrent);
      return true;
    }
  }

  if (shift && event.windows_key_code == 'J') {
    if (focus_area_ != FocusArea::kTabSidebar) {
      return false;
    }
    MoveSidebarSelection(1);
    return true;
  }

  if (shift && event.windows_key_code == 'K') {
    if (focus_area_ != FocusArea::kTabSidebar) {
      return false;
    }
    MoveSidebarSelection(-1);
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar && IsPlain(event)) {
    const char sidebar_key = PlainKeyChar(event);
    if (sidebar_pending_keys_ == "g") {
      sidebar_pending_keys_.clear();
      if (sidebar_key == 'g' || sidebar_key == '0') {
        MoveSidebarSelectionToEdge(false);
      } else if (sidebar_key == '$') {
        MoveSidebarSelectionToEdge(true);
      }
      return true;
    }
    if (sidebar_key != 'd' && sidebar_key != 'D') {
      sidebar_pending_keys_.clear();
    }
    switch (PlainKeyChar(event)) {
    case 'd':
    case 'D':
      DeleteSelectedSidebarItems();
      return true;
    case 'u':
      UndoCloseTab();
      return true;
    case 't':
      BeginCommandText(":tab-focus ");
      return true;
    case 'c':
      if (sidebar_selected_item_.type == SidebarItemType::kTab) {
        if (const std::optional<size_t> index =
                FindTabIndexById(sidebar_selected_item_.id)) {
          InsertTab(tabs_[*index].url, *index + 1, true, false,
                    tabs_[*index].folder_id,
                    SidebarSortOrderAfterItem(
                        {SidebarItemType::kTab, tabs_[*index].id}),
                    false, tabs_[*index].context);
        }
      }
      return true;
    case '[':
      ActivateRelativeAudible(-1);
      return true;
    case ']':
      ActivateRelativeAudible(1);
      return true;
    case 'h':
      LeaveSidebarFolder();
      return true;
    case 'l':
      if (sidebar_selected_item_.type == SidebarItemType::kFolder ||
          sidebar_selected_item_.type == SidebarItemType::kParent) {
        ActivateSidebarItem(sidebar_selected_item_);
      }
      return true;
    case 'e':
      MoveSelectedSidebarItem(-1);
      return true;
    case 'E':
      MoveSelectedSidebarItem(1);
      return true;
    case 'f':
      BeginCreateFolderPrompt();
      return true;
    case 'F':
      BeginMoveSidebarItemsPrompt();
      return true;
    case 'x':
      UnwrapSelectedSidebarFolder();
      return true;
    case 'v':
    case 'V':
      ToggleSidebarVisualSelection();
      return true;
    case '/':
      BeginSidebarSearch(true);
      return true;
    case '?':
      BeginSidebarSearch(false);
      return true;
    case 'n':
      JumpSidebarSearch(sidebar_search_forward_);
      return true;
    case 'N':
      JumpSidebarSearch(!sidebar_search_forward_);
      return true;
    case 'g':
      sidebar_pending_keys_ = "g";
      return true;
    case 'G':
      MoveSidebarSelectionToEdge(true);
      return true;
    }
  }

  if (focus_area_ == FocusArea::kTabSidebar) {
    return true;
  }

  return false;
}

void BrowserWindow::OnAfterUserAction(CefRefPtr<CefTextfield> textfield) {
  if (a26_shell_ && textfield && textfield->GetID() == kA26UrlFieldId) {
    a26_url_editing_ = true;
    return;
  }
  if ((textfield != command_field_ &&
       (!textfield || textfield->GetID() != kCommandFieldId)) ||
      mode_ == Mode::kNormal || command_vim_.mode != vim::Mode::kInsert ||
      suppress_next_char_event_) {
    return;
  }

  // CEF can apply Backspace natively without routing a modeled key event. In
  // sidebar search the / or ? prompt is not query text, so deleting the empty
  // field is the native equivalent of Exocortex's "Backspace closes search".
  if (IsSidebarSearchMode() && textfield->GetText().ToString().empty()) {
    CancelCommand();
    return;
  }

  if (!SyncCommandTextFromField()) {
    return;
  }
  Layout();
  SetCommandText(command_text_);
}

void BrowserWindow::OnButtonPressed(CefRefPtr<CefButton> button) {
  const int id = button ? button->GetID() : 0;
  if (a26_shell_ &&
      (id == kA26BackButtonId || id == kA26ForwardButtonId ||
       id == kA26ReloadButtonId || id == kA26TabsButtonId)) {
    // X11 BrowserViews may cover sibling Views for pointer hit-testing. The raw
    // watcher below guarantees phone touches work, while some CEF versions also
    // deliver the native button callback. Coalesce those two observations of the
    // same press without making normal double-tap cadence feel unresponsive.
    if (a26_last_control_id_ == id) {
      return;
    }
    a26_last_control_id_ = id;
    const uint64_t generation = ++a26_control_dedup_generation_;
    CefRefPtr<BrowserWindow> self = this;
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&BrowserWindow::ClearA26ControlDedup, self, id,
                       generation),
        100);
  }
  if (a26_shell_ && id == kA26BackButtonId) {
    if (CefRefPtr<CefBrowser> browser = ActiveBrowser();
        browser && browser->CanGoBack()) {
      browser->GoBack();
    }
    FinishA26ChromeAction();
    return;
  }
  if (a26_shell_ && id == kA26ForwardButtonId) {
    if (CefRefPtr<CefBrowser> browser = ActiveBrowser();
        browser && browser->CanGoForward()) {
      browser->GoForward();
    }
    FinishA26ChromeAction();
    return;
  }
  if (a26_shell_ && id == kA26ReloadButtonId) {
    if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
      if (browser->IsLoading()) {
        browser->StopLoad();
      } else {
        browser->Reload();
      }
    }
    FinishA26ChromeAction();
    UpdateA26Chrome();
    return;
  }
  if (a26_shell_ && id == kA26TabsButtonId) {
    FinishA26ChromeAction();
    ActivateRelative(1);
    UpdateA26Chrome();
    return;
  }
  if (id == kContextMenuBackdropButtonId) {
    CancelNativeContextMenu();
    return;
  }
  if (InIdRange(id, kContextMenuRowBaseId, 1000)) {
    ActivateNativeContextMenuRow(static_cast<size_t>(id - kContextMenuRowBaseId));
    return;
  }
  if (InIdRange(id, kSidebarRowBaseId, 1000)) {
    const size_t row_index = static_cast<size_t>(id - kSidebarRowBaseId);
    if (row_index < sidebar_rows_.size()) {
      const SidebarRowViews& row = sidebar_rows_[row_index];
      if (row.kind == SidebarRowKind::kEntry) {
        ActivateSidebarItem(row.item);
      }
      SetFocusArea(FocusArea::kTabSidebar);
    }
    return;
  }
  if (InIdRange(id, kAutocompleteRowBaseId, 1000)) {
    const int index = id - kAutocompleteRowBaseId;
    if (index >= 0 && index < static_cast<int>(command_autocomplete_.matches.size())) {
      command_autocomplete_.selection = index;
      FillCommandAutocomplete(command_autocomplete_.matches[index]);
      SetCommandText(command_text_);
    }
    return;
  }
  if (id == kMediaPermissionAllowButtonId) {
    ResolveActiveMediaPermissionRequest(true, true);
    return;
  }
  if (id == kMediaPermissionDenyButtonId) {
    ResolveActiveMediaPermissionRequest(false, true);
    return;
  }
  // The mode indicator is implemented as a CefLabelButton because CEF exposes
  // centering for labels/buttons but not textfields. It is display-only.
}

void BrowserWindow::OnButtonStateChanged(CefRefPtr<CefButton> button) {
  const int id = button ? button->GetID() : 0;
  if (!native_context_menu_ || !InIdRange(id, kContextMenuRowBaseId, 1000)) {
    return;
  }

  const cef_button_state_t state = button->GetState();
  if (state == CEF_BUTTON_STATE_HOVERED || state == CEF_BUTTON_STATE_PRESSED) {
    HoverNativeContextMenuRow(static_cast<size_t>(id - kContextMenuRowBaseId));
  }
}

bool BrowserWindow::OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                               const CefKeyEvent& event) {
  if (HandleNativeContextMenuKey(event)) {
    return true;
  }
  if (HandleMediaPermissionPromptKey(event)) {
    return true;
  }
  if (a26_shell_ && textfield && textfield->GetID() == kA26UrlFieldId) {
    if (IsEnterKey(event)) {
      if (IsRawKeyDown(event)) {
        CommitA26Url();
      }
      return true;
    }
    if (IsEscapeKey(event)) {
      if (IsRawKeyDown(event)) {
        CancelA26Url();
      }
      return true;
    }
    return false;
  }
  if (textfield != command_field_ || mode_ == Mode::kNormal) {
    return false;
  }
  return HandleCommandModeKey(event);
}

CefSize BrowserWindow::GetPreferredSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  const bool sidebar_search = IsSidebarSearchMode();
  const int command_height = sidebar_search ? kStatusBarHeight : kCommandHeight;
  if (id == kSidebarPanelId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kSidebarContentPanelId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kSidebarBorderPanelId) {
    return CefSize(sidebar_visible_ ? kSidebarBorderWidth : 0, 1);
  }
  if (id == kSidebarBorderOverlayPanelId) {
    return CefSize(kSidebarBorderWidth, 1);
  }
  if (id == kMainPanelId || id == kRootPanelId) {
    return CefSize(1200, 800);
  }
  if (id == kDevToolsPanelId) {
    if (!devtools_visible_) {
      return CefSize(0, 800);
    }
    const int window_width = window_ ? window_->GetBounds().width : 1200;
    const int content_x = sidebar_visible_ ? kSidebarWidth : 0;
    const int available_content_width = std::max(1, window_width - content_x);
    const int desired_width = std::max(
        kDevToolsMinWidth,
        available_content_width * kDevToolsDefaultWidthPercent / 100);
    const int max_width = std::max(
        1, available_content_width - kDevToolsBorderWidth -
               kDevToolsMinPageWidth);
    int devtools_width = std::min(desired_width, max_width);
    if (available_content_width <=
        kDevToolsMinPageWidth + kDevToolsBorderWidth + 1) {
      devtools_width = std::max(1, available_content_width / 2);
    }
    return CefSize(kDevToolsBorderWidth + std::max(1, devtools_width), 800);
  }
  if (id == kDevToolsContentPanelId) {
    return CefSize(800, 800);
  }
  if (id == kCommandPanelId) {
    return CefSize(1200, command_height + (sidebar_search ? 0 : 1));
  }
  if (id == kCommandContentPanelId) {
    return CefSize(1200, command_height);
  }
  if (id == kCommandSeparatorPanelId) {
    return CefSize(1200, 1);
  }
  if (id == kCommandAutocompletePanelId) {
    return CefSize(std::max(1, CommandAutocompleteWidth()),
                   std::max(1, CommandAutocompleteHeight()));
  }
  if (id == kStatusBarPanelId) {
    return CefSize(1200, kStatusBarHeight);
  }
  if (id == kStatusSidebarSpacerPanelId) {
    return CefSize(sidebar_visible_ ? kSidebarContentWidth : 0,
                   kStatusBarHeight);
  }
  if (id == kStatusContentPanelId) {
    return CefSize(1200, kStatusBarHeight);
  }
  if (id == kStatusBorderPanelId) {
    return CefSize(sidebar_visible_ ? 1 : 0, kStatusBarHeight);
  }
  if (id == kStatusOutputFieldId) {
    if (status_output_text_.empty()) {
      return CefSize(0, kStatusBarHeight);
    }
    std::string status_url_text = ActiveTabUrl();
    if (status_url_text.empty()) {
      status_url_text = "about:blank";
    }
    status_url_text += "  ";
    const int content_width = std::max(1, laid_out_content_width_);
    const int status_url_text_width =
        std::max(1, TextColumns(status_url_text) * kCommandCharWidth);
    const int max_status_output_width = std::max(
        0, content_width - kStatusModeWidth - status_url_text_width -
               kCommandCharWidth);
    return CefSize(std::min(max_status_output_width,
                            std::max(1, TextColumns(status_output_text_) *
                                            kCommandCharWidth + 8)),
                   kStatusBarHeight);
  }
  if (id == kStatusModeFieldId) {
    return CefSize(kStatusModeWidth, kStatusBarHeight);
  }
  if (id == kStatusUrlFieldId) {
    return CefSize(1200, kStatusBarHeight);
  }
  if (id == kA26ChromePanelId) {
    return CefSize(1200, kA26ChromeHeight);
  }
  if (id == kA26NavigationPanelId) {
    return CefSize(1200, kA26NavigationHeight);
  }
  if (id == kA26BottomReservePanelId) {
    return CefSize(1200, kA26BottomReserveHeight);
  }
  if (id == kA26BackButtonId || id == kA26ForwardButtonId) {
    return CefSize(kA26HistoryButtonWidth, kA26TouchControlHeight);
  }
  if (id == kA26UrlFieldId) {
    return CefSize(240, kA26TouchControlHeight);
  }
  if (id == kA26ReloadButtonId) {
    return CefSize(kA26ReloadButtonWidth, kA26TouchControlHeight);
  }
  if (id == kA26TabsButtonId) {
    return CefSize(kA26TabsButtonWidth, kA26TouchControlHeight);
  }
  if (InIdRange(id, kSidebarRowBaseId, 1000)) {
    return CefSize(kSidebarContentWidth, kSidebarRowHeight);
  }
  if (id == kSidebarSpacerId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kCommandFieldId) {
    return CefSize(1200, command_height);
  }
  if (InIdRange(id, kAutocompleteRowBaseId, 1000)) {
    return CefSize(std::max(1, CommandAutocompleteWidth()),
                   kCommandAutocompleteRowHeight);
  }
  if (id == kModeIndicatorPanelId || id == kFpsIndicatorPanelId) {
    return CefSize(kModeIndicatorWidth, kModeIndicatorHeight);
  }
  if (id == kModeIndicatorFieldId || id == kFpsIndicatorFieldId) {
    return CefSize(kModeIndicatorWidth, kModeIndicatorHeight);
  }
  if (id == kMediaPermissionPromptPanelId) {
    return CefSize(kMediaPermissionPromptWidth, kMediaPermissionPromptHeight);
  }
  if (id == kMediaPermissionBorderTopPanelId ||
      id == kMediaPermissionBorderBottomPanelId) {
    return CefSize(kMediaPermissionPromptWidth,
                   kMediaPermissionPromptBorderWidth);
  }
  if (id == kMediaPermissionBorderLeftPanelId ||
      id == kMediaPermissionBorderRightPanelId) {
    return CefSize(kMediaPermissionPromptBorderWidth,
                   kMediaPermissionPromptHeight);
  }
  if (id == kMediaPermissionPromptContentPanelId) {
    return CefSize(kMediaPermissionPromptWidth -
                       2 * kMediaPermissionPromptBorderWidth,
                   kMediaPermissionPromptHeight -
                       2 * kMediaPermissionPromptBorderWidth);
  }
  if (id == kMediaPermissionTitleFieldId ||
      id == kMediaPermissionOriginFieldId ||
      id == kMediaPermissionBodyFieldId ||
      id == kMediaPermissionHintFieldId) {
    return CefSize(1, 20);
  }
  if (id == kMediaPermissionButtonPanelId) {
    return CefSize(1, 24);
  }
  if (id == kMediaPermissionAllowButtonId ||
      id == kMediaPermissionDenyButtonId) {
    return CefSize(112, 24);
  }
  if (id == kContextMenuBackdropButtonId) {
    const CefRect bounds = window_ ? window_->GetBounds() : CefRect(0, 0, 1, 1);
    return CefSize(std::max(1, bounds.width), std::max(1, bounds.height));
  }
  if (id == kContextMenuPanelId) {
    return CefSize(std::max(1, NativeContextMenuWidth()),
                   std::max(1, NativeContextMenuHeight()));
  }
  if (InIdRange(id, kContextMenuRowBaseId, 1000)) {
    return CefSize(std::max(1, NativeContextMenuWidth()),
                   kContextMenuRowHeight);
  }
  return CefSize(1200, 800);
}

CefSize BrowserWindow::GetMinimumSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  const bool sidebar_search = IsSidebarSearchMode();
  const int command_height = sidebar_search ? kStatusBarHeight : kCommandHeight;
  if (id == kSidebarPanelId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kSidebarContentPanelId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kSidebarBorderPanelId) {
    return CefSize(sidebar_visible_ ? kSidebarBorderWidth : 0, 1);
  }
  if (id == kSidebarBorderOverlayPanelId) {
    return CefSize(kSidebarBorderWidth, 1);
  }
  if (id == kCommandPanelId) {
    return CefSize(1, command_height + (sidebar_search ? 0 : 1));
  }
  if (id == kCommandContentPanelId) {
    return CefSize(1, command_height);
  }
  if (id == kCommandSeparatorPanelId) {
    return CefSize(1, 1);
  }
  if (id == kCommandAutocompletePanelId) {
    return CefSize(1, 1);
  }
  if (id == kDevToolsPanelId) {
    return CefSize(devtools_visible_ ? 1 : 0, 1);
  }
  if (id == kDevToolsContentPanelId) {
    return CefSize(1, 1);
  }
  if (id == kStatusBarPanelId) {
    return CefSize(1, kStatusBarHeight);
  }
  if (id == kStatusSidebarSpacerPanelId) {
    return CefSize(sidebar_visible_ ? kSidebarContentWidth : 0,
                   kStatusBarHeight);
  }
  if (id == kStatusContentPanelId) {
    return CefSize(1, kStatusBarHeight);
  }
  if (id == kStatusBorderPanelId) {
    return CefSize(sidebar_visible_ ? 1 : 0, kStatusBarHeight);
  }
  if (id == kStatusOutputFieldId) {
    return CefSize(1, kStatusBarHeight);
  }
  if (id == kStatusModeFieldId) {
    return CefSize(kStatusModeWidth, kStatusBarHeight);
  }
  if (id == kStatusUrlFieldId) {
    return CefSize(1, kStatusBarHeight);
  }
  if (id == kA26ChromePanelId) {
    return CefSize(1, kA26ChromeHeight);
  }
  if (id == kA26NavigationPanelId) {
    return CefSize(1, kA26NavigationHeight);
  }
  if (id == kA26BottomReservePanelId) {
    return CefSize(1, kA26BottomReserveHeight);
  }
  if (id == kA26BackButtonId || id == kA26ForwardButtonId) {
    return CefSize(kA26HistoryButtonWidth, kA26TouchControlHeight);
  }
  if (id == kA26UrlFieldId) {
    return CefSize(1, kA26TouchControlHeight);
  }
  if (id == kA26ReloadButtonId) {
    return CefSize(kA26ReloadButtonWidth, kA26TouchControlHeight);
  }
  if (id == kA26TabsButtonId) {
    return CefSize(kA26TabsButtonWidth, kA26TouchControlHeight);
  }
  if (InIdRange(id, kSidebarRowBaseId, 1000) ||
      id == kSidebarSpacerId ||
      id == kCommandFieldId ||
      InIdRange(id, kAutocompleteRowBaseId, 1000)) {
    return CefSize(1, 1);
  }
  if (id == kModeIndicatorPanelId || id == kModeIndicatorFieldId ||
      id == kFpsIndicatorPanelId || id == kFpsIndicatorFieldId) {
    return CefSize(kModeIndicatorWidth, kModeIndicatorHeight);
  }
  if (id == kMediaPermissionPromptPanelId) {
    return CefSize(kMediaPermissionPromptWidth, kMediaPermissionPromptHeight);
  }
  if (id == kMediaPermissionBorderTopPanelId ||
      id == kMediaPermissionBorderBottomPanelId ||
      id == kMediaPermissionBorderLeftPanelId ||
      id == kMediaPermissionBorderRightPanelId) {
    return CefSize(kMediaPermissionPromptBorderWidth,
                   kMediaPermissionPromptBorderWidth);
  }
  if (id == kMediaPermissionPromptContentPanelId) {
    return CefSize(1, 1);
  }
  if (id == kMediaPermissionTitleFieldId ||
      id == kMediaPermissionOriginFieldId ||
      id == kMediaPermissionBodyFieldId ||
      id == kMediaPermissionHintFieldId) {
    return CefSize(1, 20);
  }
  if (id == kMediaPermissionButtonPanelId) {
    return CefSize(1, 24);
  }
  if (id == kMediaPermissionAllowButtonId ||
      id == kMediaPermissionDenyButtonId) {
    return CefSize(112, 24);
  }
  if (id == kContextMenuBackdropButtonId || id == kContextMenuPanelId ||
      InIdRange(id, kContextMenuRowBaseId, 1000)) {
    return CefSize(1, 1);
  }
  return CefSize();
}

CefSize BrowserWindow::GetMaximumSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  const bool sidebar_search = IsSidebarSearchMode();
  const int command_height = sidebar_search ? kStatusBarHeight : kCommandHeight;
  if (id == kCommandPanelId) {
    return CefSize(0, command_height + (sidebar_search ? 0 : 1));
  }
  if (id == kCommandContentPanelId) {
    return CefSize(0, command_height);
  }
  if (id == kCommandSeparatorPanelId) {
    return CefSize(0, 1);
  }
  if (id == kCommandAutocompletePanelId) {
    return CefSize(0, 0);
  }
  if (id == kDevToolsPanelId) {
    return CefSize(0, 0);
  }
  if (id == kDevToolsContentPanelId) {
    return CefSize(0, 0);
  }
  if (id == kStatusBarPanelId) {
    return CefSize(0, kStatusBarHeight);
  }
  if (id == kStatusSidebarSpacerPanelId) {
    return CefSize(sidebar_visible_ ? kSidebarContentWidth : 0,
                   kStatusBarHeight);
  }
  if (id == kStatusContentPanelId) {
    return CefSize(0, kStatusBarHeight);
  }
  if (id == kStatusBorderPanelId) {
    return CefSize(sidebar_visible_ ? 1 : 0, kStatusBarHeight);
  }
  if (id == kStatusOutputFieldId) {
    return GetPreferredSize(view);
  }
  if (id == kStatusModeFieldId) {
    return CefSize(kStatusModeWidth, kStatusBarHeight);
  }
  if (id == kStatusUrlFieldId) {
    return CefSize(0, kStatusBarHeight);
  }
  if (id == kA26ChromePanelId) {
    return CefSize(0, kA26ChromeHeight);
  }
  if (id == kA26NavigationPanelId) {
    return CefSize(0, kA26NavigationHeight);
  }
  if (id == kA26BottomReservePanelId) {
    return CefSize(0, kA26BottomReserveHeight);
  }
  if (id == kA26BackButtonId || id == kA26ForwardButtonId) {
    return CefSize(kA26HistoryButtonWidth, kA26TouchControlHeight);
  }
  if (id == kA26UrlFieldId) {
    return CefSize(0, kA26TouchControlHeight);
  }
  if (id == kA26ReloadButtonId) {
    return CefSize(kA26ReloadButtonWidth, kA26TouchControlHeight);
  }
  if (id == kA26TabsButtonId) {
    return CefSize(kA26TabsButtonWidth, kA26TouchControlHeight);
  }
  if (id == kModeIndicatorPanelId || id == kModeIndicatorFieldId ||
      id == kFpsIndicatorPanelId || id == kFpsIndicatorFieldId) {
    return CefSize(kModeIndicatorWidth, kModeIndicatorHeight);
  }
  if (id == kMediaPermissionPromptPanelId) {
    return CefSize(kMediaPermissionPromptWidth, kMediaPermissionPromptHeight);
  }
  if (id == kMediaPermissionBorderTopPanelId ||
      id == kMediaPermissionBorderBottomPanelId ||
      id == kMediaPermissionBorderLeftPanelId ||
      id == kMediaPermissionBorderRightPanelId) {
    return CefSize(0, 0);
  }
  if (id == kMediaPermissionTitleFieldId ||
      id == kMediaPermissionOriginFieldId ||
      id == kMediaPermissionBodyFieldId ||
      id == kMediaPermissionHintFieldId) {
    return CefSize(0, 20);
  }
  if (id == kMediaPermissionButtonPanelId) {
    return CefSize(0, 24);
  }
  if (id == kMediaPermissionAllowButtonId ||
      id == kMediaPermissionDenyButtonId) {
    return CefSize(112, 24);
  }
  if (id == kContextMenuBackdropButtonId || id == kContextMenuPanelId ||
      InIdRange(id, kContextMenuRowBaseId, 1000)) {
    return CefSize(0, 0);
  }
  return CefSize();
}

void BrowserWindow::OnThemeChanged(CefRefPtr<CefView> view) {
  RestyleView(view);
}

void BrowserWindow::OnFocus(CefRefPtr<CefView> view) {
  if (!a26_shell_ || !view || view->GetID() != kA26UrlFieldId ||
      !a26_url_field_) {
    return;
  }
  a26_url_focused_ = true;
  a26_url_editing_ = true;
  focus_area_ = FocusArea::kA26Url;
  ResetWebsitePendingKeys();
  a26_url_field_->SelectAll(false);
  RequestA26Keyboard(A26KeyboardPurpose::kUrl);
  CefRefPtr<BrowserWindow> self = this;
  CefPostTask(TID_UI,
              base::BindOnce(&BrowserWindow::SelectA26UrlAfterFocus, self));
}

void BrowserWindow::OnBlur(CefRefPtr<CefView> view) {
  if (!a26_shell_ || !view || view->GetID() != kA26UrlFieldId) {
    return;
  }
  a26_url_focused_ = false;
  a26_url_editing_ = false;
  if (focus_area_ == FocusArea::kA26Url) {
    focus_area_ = FocusArea::kWebView;
  }
  UpdateA26Chrome();
  RequestA26Keyboard(A26KeyboardPurpose::kHide);
}

cef_runtime_style_t BrowserWindow::GetWindowRuntimeStyle() {
  // DevTools BrowserViews are Chrome-style in CEF. A Chrome-style Window can
  // still host our normal Alloy BrowserViews, and it is the only supported
  // single-window host for a docked Chrome DevTools BrowserView.
  return CEF_RUNTIME_STYLE_CHROME;
}

cef_runtime_style_t BrowserWindow::GetBrowserRuntimeStyle() {
  return CEF_RUNTIME_STYLE_ALLOY;
}

CefRefPtr<CefBrowser> BrowserWindow::ActiveBrowser() const {
  if (tabs_.empty() || active_index_ >= tabs_.size()) {
    return nullptr;
  }
  const Tab& tab = tabs_[active_index_];
  if (!tab.client) {
    return nullptr;
  }
  return tab.client->browser();
}

bool BrowserWindow::PageHasFocusedEditable(const CefKeyEvent& event) {
  if (event.focus_on_editable_field) {
    // Persist a true per-key focus signal. CEF can invoke us both before and
    // after renderer key handling for the same physical key; keeping the state
    // true prevents the post-renderer callback from running a page shortcut
    // after the input box already received the text event.
    if (Tab* tab = ActiveTab()) {
      tab->focused_editable_node = true;
    }
    return true;
  }
  return !tabs_.empty() && active_index_ < tabs_.size() &&
         tabs_[active_index_].focused_editable_node;
}

bool BrowserWindow::AllTabBrowsersClosed() const {
  for (const Tab& tab : tabs_) {
    if (tab.client && tab.client->browser()) {
      return false;
    }
  }
  return true;
}

void BrowserWindow::ShowNextMediaPermissionRequest() {
  if (active_media_permission_ || !window_ || window_close_pending_) {
    return;
  }

  while (!queued_media_permissions_.empty()) {
    MediaPermissionRequest request = std::move(queued_media_permissions_.front());
    queued_media_permissions_.erase(queued_media_permissions_.begin());
    if (!request.callback) {
      continue;
    }

    if (const auto granted = media_permission_grants_.find(request.origin);
        granted != media_permission_grants_.end() &&
        (granted->second & request.requested_permissions) ==
            request.requested_permissions) {
      request.callback->Continue(request.requested_permissions);
      continue;
    }
    if (const auto denied = media_permission_denials_.find(request.origin);
        denied != media_permission_denials_.end() &&
        (denied->second & request.requested_permissions) ==
            request.requested_permissions) {
      request.callback->Continue(CEF_MEDIA_PERMISSION_NONE);
      continue;
    }

    active_media_permission_ = std::move(request);
    break;
  }

  UpdateMediaPermissionPrompt();
  Layout();
}

void BrowserWindow::ShowMockMediaPermissionPrompt() {
  if (active_media_permission_ && !active_media_permission_->mock) {
    SetStatusOutput("test permission modal: real permission request is active");
    return;
  }

  active_media_permission_ = MediaPermissionRequest{
      nullptr,
      "vimbrowser://test/permission-modal",
      CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE |
          CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE,
      nullptr,
      true,
  };
  UpdateMediaPermissionPrompt();
  Layout();
}

void BrowserWindow::UpdateMediaPermissionPrompt() {
  const bool visible = active_media_permission_.has_value();
  if (media_permission_overlay_) {
    media_permission_overlay_->SetVisible(visible);
  }
  if (media_permission_panel_) {
    media_permission_panel_->SetVisible(visible);
  }
  if (media_permission_content_panel_) {
    media_permission_content_panel_->SetVisible(visible);
  }
  if (!visible) {
    return;
  }

  const MediaPermissionRequest& request = *active_media_permission_;
  const std::string devices = MediaPermissionNameList(request.requested_permissions);
  const std::string origin = DisplayMediaPermissionOrigin(request.origin);
  const std::string short_origin = Ellipsize(origin, 76);

  if (media_permission_title_field_) {
    media_permission_title_field_->SetText(
        request.mock ? "test: permission modal" : "permission: " + devices);
  }
  if (media_permission_origin_field_) {
    media_permission_origin_field_->SetText("origin: " + short_origin);
  }
  if (media_permission_body_field_) {
    media_permission_body_field_->SetText(Ellipsize(
        request.mock ? "mock page wants to use " + devices + "."
                     : origin + " wants to use " + devices + ".",
        84));
  }
  if (media_permission_allow_button_) {
    media_permission_allow_button_->SetText("[a] allow");
  }
  if (media_permission_deny_button_) {
    media_permission_deny_button_->SetText("[d] deny");
  }
}

void BrowserWindow::ResolveActiveMediaPermissionRequest(bool allow,
                                                       bool remember) {
  if (!active_media_permission_) {
    return;
  }

  MediaPermissionRequest request = std::move(*active_media_permission_);
  active_media_permission_.reset();
  UpdateMediaPermissionPrompt();

  if (remember && !request.mock) {
    auto& decisions = allow ? media_permission_grants_ : media_permission_denials_;
    auto& opposite = allow ? media_permission_denials_ : media_permission_grants_;
    decisions[request.origin] |= request.requested_permissions;
    if (auto it = opposite.find(request.origin); it != opposite.end()) {
      it->second &= ~request.requested_permissions;
      if (it->second == 0) {
        opposite.erase(it);
      }
    }
    SaveState();
  }

  const std::string devices = MediaPermissionNameList(request.requested_permissions);
  const std::string origin = DisplayMediaPermissionOrigin(request.origin);
  SetStatusOutput(std::string(request.mock ? "test permission modal: "
                                           : "media permission: ") +
                  (allow ? "allowed " : "denied ") + devices +
                  (request.mock ? "" : " for " + Ellipsize(origin, 72)));

  if (request.callback) {
    request.callback->Continue(allow ? request.requested_permissions
                                     : CEF_MEDIA_PERMISSION_NONE);
  }
  ShowNextMediaPermissionRequest();
}

void BrowserWindow::DismissActiveMediaPermissionRequest() {
  if (!active_media_permission_) {
    return;
  }

  MediaPermissionRequest request = std::move(*active_media_permission_);
  active_media_permission_.reset();
  UpdateMediaPermissionPrompt();

  const std::string devices = MediaPermissionNameList(request.requested_permissions);
  const std::string origin = DisplayMediaPermissionOrigin(request.origin);
  SetStatusOutput(std::string(request.mock ? "test permission modal: dismissed "
                                           : "media permission: dismissed ") +
                  devices +
                  (request.mock ? "" : " for " + Ellipsize(origin, 72)));

  if (request.callback) {
    request.callback->Cancel();
  }
  ShowNextMediaPermissionRequest();
}

void BrowserWindow::CancelMediaPermissionRequestsForClient(BrowserClient* client) {
  if (!client) {
    return;
  }

  std::vector<CefRefPtr<CefMediaAccessCallback>> callbacks;
  if (active_media_permission_ && active_media_permission_->client == client) {
    callbacks.push_back(active_media_permission_->callback);
    active_media_permission_.reset();
  }

  std::vector<MediaPermissionRequest> kept;
  kept.reserve(queued_media_permissions_.size());
  for (MediaPermissionRequest& request : queued_media_permissions_) {
    if (request.client == client) {
      callbacks.push_back(request.callback);
    } else {
      kept.push_back(std::move(request));
    }
  }
  queued_media_permissions_ = std::move(kept);

  if (!callbacks.empty()) {
    UpdateMediaPermissionPrompt();
  }
  for (CefRefPtr<CefMediaAccessCallback> callback : callbacks) {
    if (callback) {
      callback->Cancel();
    }
  }
  if (!callbacks.empty()) {
    ShowNextMediaPermissionRequest();
  }
}

void BrowserWindow::CancelAllMediaPermissionRequests() {
  std::vector<CefRefPtr<CefMediaAccessCallback>> callbacks;
  if (active_media_permission_) {
    callbacks.push_back(active_media_permission_->callback);
    active_media_permission_.reset();
  }
  for (MediaPermissionRequest& request : queued_media_permissions_) {
    callbacks.push_back(request.callback);
  }
  queued_media_permissions_.clear();
  UpdateMediaPermissionPrompt();
  for (CefRefPtr<CefMediaAccessCallback> callback : callbacks) {
    if (callback) {
      callback->Cancel();
    }
  }
}

bool BrowserWindow::HandleMediaPermissionPromptKey(const CefKeyEvent& event) {
  if (!active_media_permission_) {
    return false;
  }
  if (!IsRawKeyDown(event)) {
    return true;
  }

  if (IsEscapeKey(event)) {
    DismissActiveMediaPermissionRequest();
    return true;
  }
  if (IsEnterKey(event)) {
    ResolveActiveMediaPermissionRequest(true, true);
    return true;
  }

  const char key = LowerAsciiChar(PlainKeyChar(event));
  switch (key) {
    case 'a':
    case 'y':
      ResolveActiveMediaPermissionRequest(true, true);
      return true;
    case 'd':
    case 'n':
      ResolveActiveMediaPermissionRequest(false, true);
      return true;
    default:
      return true;
  }
}

std::string BrowserWindow::ActiveTabUrl() const {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser && browser->GetMainFrame()) {
    const std::string url = browser->GetMainFrame()->GetURL().ToString();
    if (!url.empty()) {
      return url;
    }
  }
  if (!tabs_.empty() && active_index_ < tabs_.size()) {
    return tabs_[active_index_].url;
  }
  return "";
}

std::string BrowserWindow::ActiveTabTitle() const {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser && browser->GetHost()) {
    CefRefPtr<CefNavigationEntry> entry = browser->GetHost()->GetVisibleNavigationEntry();
    if (entry) {
      const std::string title = entry->GetTitle().ToString();
      if (!title.empty()) {
        return title;
      }
    }
  }
  return ActiveTabUrl();
}

void BrowserWindow::Layout() {
  if (!window_ || !root_panel_) {
    return;
  }

  if (a26_shell_) {
    // Phone chrome is bottom-only. Never allow desktop sidebar state, keyboard
    // shortcuts, or IPC toggles to introduce side UI in the A26 shell.
    sidebar_visible_ = false;
  }

  const CefRect bounds = window_->GetBounds();
  const int width = std::max(1, bounds.width);
  const int height = std::max(1, bounds.height);
  const bool sidebar_search = IsSidebarSearchMode();
  const int command_surface_height =
      sidebar_search ? kStatusBarHeight : kCommandHeight;
  const int command_total_height =
      command_surface_height + (sidebar_search ? 0 : 1);
  const int autocomplete_height = CommandAutocompleteHeight();
  const int autocomplete_width = std::min(width, std::max(1, CommandAutocompleteWidth()));
  const int a26_chrome_height = a26_shell_ ? kA26ChromeHeight : 0;
  const int main_height =
      std::max(1, height - (show_statusline_ ? kStatusBarHeight : 0) -
                      a26_chrome_height);
  const int sidebar_content_width = sidebar_visible_ ? kSidebarContentWidth : 0;
  const int sidebar_border_width = sidebar_visible_ ? kSidebarBorderWidth : 0;
  const int content_x = sidebar_visible_ ? kSidebarWidth : 0;
  const bool command_active = mode_ != Mode::kNormal;
  const int command_surface_width =
      sidebar_search ? std::max(1, sidebar_content_width) : width;
  // Sidebar search owns the otherwise-empty left side of the status row. Dock
  // it to the actual window bottom instead of above the statusline so there is
  // no dead strip below / or ?.
  const int command_surface_bottom = height - a26_chrome_height;
  const bool autocomplete_visible = command_active && !sidebar_search &&
                                    command_autocomplete_.active &&
                                    !command_autocomplete_.matches.empty();
  if (command_overlay_) {
    command_overlay_->SetVisible(command_active);
  }
  if (command_separator_overlay_) {
    command_separator_overlay_->SetVisible(command_active && !sidebar_search);
  }
  if (autocomplete_overlay_) {
    autocomplete_overlay_->SetVisible(autocomplete_visible);
  }
  sidebar_panel_->SetVisible(sidebar_visible_);

  root_panel_->SetBounds(CefRect(0, 0, width, height));
  RestyleView(root_panel_);
  RestyleView(main_panel_);
  RestyleView(sidebar_panel_);
  RestyleView(sidebar_content_panel_);
  RestyleView(sidebar_spacer_);
  RestyleView(sidebar_border_panel_);
  RestyleView(sidebar_border_overlay_panel_);
  RestyleView(content_panel_);
  RestyleView(content_inner_panel_);
  RestyleView(devtools_panel_);
  RestyleView(devtools_content_panel_);
  RestyleView(command_panel_);
  RestyleView(command_content_panel_);
  RestyleView(command_separator_panel_);
  RestyleView(autocomplete_panel_);
  RestyleView(status_bar_panel_);
  RestyleView(status_sidebar_spacer_panel_);
  RestyleView(status_border_panel_);
  RestyleView(status_content_panel_);
  RestyleView(status_output_field_);
  RestyleView(status_mode_field_);
  RestyleView(status_url_label_);
  RestyleView(a26_chrome_panel_);
  RestyleView(a26_navigation_panel_);
  RestyleView(a26_bottom_reserve_panel_);
  RestyleView(a26_back_button_);
  RestyleView(a26_forward_button_);
  RestyleView(a26_url_field_);
  RestyleView(a26_reload_button_);
  RestyleView(a26_tabs_button_);
  RestyleView(mode_indicator_panel_);
  RestyleView(mode_indicator_label_);
  RestyleView(fps_indicator_panel_);
  RestyleView(fps_indicator_label_);
  RestyleView(media_permission_panel_);
  RestyleView(media_permission_top_border_panel_);
  RestyleView(media_permission_bottom_border_panel_);
  RestyleView(media_permission_left_border_panel_);
  RestyleView(media_permission_right_border_panel_);
  RestyleView(media_permission_content_panel_);
  RestyleView(media_permission_title_field_);
  RestyleView(media_permission_origin_field_);
  RestyleView(media_permission_body_field_);
  RestyleView(media_permission_hint_field_);
  RestyleView(media_permission_button_panel_);
  RestyleView(media_permission_allow_button_);
  RestyleView(media_permission_deny_button_);
  main_panel_->SetSize(CefSize(width, main_height));
  sidebar_panel_->SetSize(CefSize(sidebar_content_width, main_height));
  sidebar_content_panel_->SetSize(CefSize(sidebar_content_width, main_height));
  if (sidebar_border_panel_) {
    sidebar_border_panel_->SetVisible(sidebar_visible_);
    sidebar_border_panel_->SetSize(CefSize(sidebar_border_width, main_height));
    sidebar_border_panel_->SetBackgroundColor(SidebarBorderColor());
  }
  const int available_content_width = std::max(1, width - content_x);
  const bool devtools_docked = devtools_visible_ && devtools_browser_view_;
  const int devtools_border_width = devtools_docked ? kDevToolsBorderWidth : 0;
  int devtools_width = 0;
  if (devtools_docked) {
    const int desired_width = std::max(
        kDevToolsMinWidth,
        available_content_width * kDevToolsDefaultWidthPercent / 100);
    const int max_width = std::max(
        1, available_content_width - devtools_border_width -
               kDevToolsMinPageWidth);
    devtools_width = std::min(desired_width, max_width);
    if (available_content_width <=
        kDevToolsMinPageWidth + devtools_border_width + 1) {
      devtools_width = std::max(1, available_content_width / 2);
    }
  }
  const int content_inner_width = std::max(
      1, available_content_width - devtools_border_width - devtools_width);
  const bool content_size_changed = content_inner_width != laid_out_content_width_ ||
                                    main_height != laid_out_content_height_;
  content_panel_->SetSize(
      CefSize(sidebar_border_width + content_inner_width, main_height));
  content_panel_->SetBackgroundColor(theme::kAppBg);
  content_inner_panel_->SetBounds(
      CefRect(sidebar_border_width, 0, content_inner_width, main_height));
  if (devtools_panel_) {
    devtools_panel_->SetVisible(devtools_docked);
    devtools_panel_->SetSize(CefSize(
        devtools_border_width + std::max(1, devtools_width), main_height));
    devtools_panel_->SetBackgroundColor(
        focus_area_ == FocusArea::kDevTools ? theme::kAccent
                                            : theme::kBorderUnfocused);
  }
  if (devtools_content_panel_) {
    devtools_content_panel_->SetSize(
        CefSize(std::max(1, devtools_width), main_height));
  }
  if (devtools_browser_view_) {
    devtools_browser_view_->SetVisible(devtools_docked);
    if (devtools_docked) {
      devtools_browser_view_->SetBounds(
          CefRect(0, 0, std::max(1, devtools_width), main_height));
    }
  }
  if (status_bar_panel_) {
    status_bar_panel_->SetVisible(show_statusline_);
    status_bar_panel_->SetSize(CefSize(width, kStatusBarHeight));
    status_bar_panel_->SetBounds(CefRect(0, main_height, width, kStatusBarHeight));
  }
  if (a26_chrome_panel_) {
    const int chrome_y = main_height + (show_statusline_ ? kStatusBarHeight : 0);
    a26_chrome_panel_->SetVisible(a26_shell_);
    a26_chrome_panel_->SetSize(CefSize(width, kA26ChromeHeight));
    a26_chrome_panel_->SetBounds(
        CefRect(0, chrome_y, width, kA26ChromeHeight));
  }
  if (a26_navigation_panel_) {
    a26_navigation_panel_->SetSize(CefSize(width, kA26NavigationHeight));
    if (a26_navigation_overlay_) {
      const int navigation_y =
          main_height + (show_statusline_ ? kStatusBarHeight : 0);
      a26_navigation_overlay_->SetVisible(a26_shell_);
      a26_navigation_overlay_->SetBounds(
          CefRect(0, navigation_y, width, kA26NavigationHeight));
    }
  }
  if (a26_bottom_reserve_panel_) {
    a26_bottom_reserve_panel_->SetSize(
        CefSize(width, kA26BottomReserveHeight));
    a26_bottom_reserve_panel_->SetBounds(
        CefRect(0, kA26NavigationHeight, width, kA26BottomReserveHeight));
  }
  command_panel_->SetSize(
      CefSize(command_surface_width, command_surface_height));
  command_separator_panel_->SetSize(CefSize(command_surface_width, 1));
  command_content_panel_->SetSize(
      CefSize(command_surface_width, command_surface_height));
  command_panel_->SetBackgroundColor(sidebar_search ? theme::kSidebarBg
                                                    : theme::kAppBg);
  command_content_panel_->SetBackgroundColor(sidebar_search ? theme::kSidebarBg
                                                            : theme::kAppBg);
  command_separator_panel_->SetBounds(CefRect(0, 0, command_surface_width, 1));
  command_content_panel_->SetBounds(
      CefRect(0, 0, command_surface_width, command_surface_height));
  if (command_overlay_) {
    command_overlay_->SetBounds(
        CefRect(0, std::max(0, command_surface_bottom - command_surface_height),
                command_surface_width, command_surface_height));
  }
  if (command_separator_overlay_) {
    command_separator_overlay_->SetBounds(
        CefRect(0, std::max(0, command_surface_bottom - command_total_height),
                command_surface_width, 1));
  }
  if (autocomplete_panel_ && autocomplete_overlay_) {
    autocomplete_panel_->SetSize(CefSize(autocomplete_width, std::max(1, autocomplete_height)));
    autocomplete_overlay_->SetBounds(
        CefRect(0, std::max(0, command_surface_bottom - command_total_height -
                                  autocomplete_height),
                autocomplete_width, std::max(1, autocomplete_height)));
  }
  if (sidebar_border_overlay_ && sidebar_border_overlay_panel_) {
    int overlay_height = main_height;
    if (command_active && !sidebar_search) {
      overlay_height = std::min(
          overlay_height,
          std::max(0, height - command_total_height -
                          (autocomplete_visible ? autocomplete_height : 0)));
    }
    const bool show_sidebar_border_overlay =
        sidebar_visible_ && sidebar_border_width > 0 && overlay_height > 0;
    sidebar_border_overlay_->SetVisible(show_sidebar_border_overlay);
    sidebar_border_overlay_panel_->SetVisible(show_sidebar_border_overlay);
    sidebar_border_overlay_panel_->SetBackgroundColor(SidebarBorderColor());
    sidebar_border_overlay_panel_->SetSize(
        CefSize(kSidebarBorderWidth, std::max(1, overlay_height)));
    sidebar_border_overlay_->SetBounds(CefRect(
        kSidebarContentWidth, 0, kSidebarBorderWidth,
        std::max(1, overlay_height)));
  }
  if (mode_indicator_overlay_ && mode_indicator_panel_ && mode_indicator_label_) {
    mode_indicator_overlay_->SetVisible(show_mode_indicator_);
    mode_indicator_overlay_->SetBounds(
        CefRect(std::max(0, width - kModeIndicatorWidth), 0,
                kModeIndicatorWidth, kModeIndicatorHeight));
    mode_indicator_panel_->SetSize(CefSize(kModeIndicatorWidth, kModeIndicatorHeight));
    mode_indicator_label_->SetSize(CefSize(kModeIndicatorWidth, kModeIndicatorHeight));
    mode_indicator_label_->SetBounds(CefRect(0, 0, kModeIndicatorWidth,
                                             kModeIndicatorHeight));
    UpdateModeIndicator();
  }
  if (fps_indicator_overlay_ && fps_indicator_panel_ && fps_indicator_label_) {
    fps_indicator_overlay_->SetVisible(show_fps_indicator_);
    fps_indicator_overlay_->SetBounds(
        CefRect(std::max(0, width - kModeIndicatorWidth), kModeIndicatorHeight,
                kModeIndicatorWidth, kModeIndicatorHeight));
    fps_indicator_panel_->SetSize(CefSize(kModeIndicatorWidth, kModeIndicatorHeight));
    fps_indicator_label_->SetSize(CefSize(kModeIndicatorWidth, kModeIndicatorHeight));
    fps_indicator_label_->SetBounds(CefRect(0, 0, kModeIndicatorWidth,
                                            kModeIndicatorHeight));
  }

  if (media_permission_overlay_ && media_permission_panel_ &&
      media_permission_content_panel_) {
    const bool prompt_visible = active_media_permission_.has_value();
    media_permission_overlay_->SetVisible(prompt_visible);
    media_permission_panel_->SetVisible(prompt_visible);
    if (media_permission_top_border_overlay_) {
      media_permission_top_border_overlay_->SetVisible(prompt_visible);
    }
    if (media_permission_bottom_border_overlay_) {
      media_permission_bottom_border_overlay_->SetVisible(prompt_visible);
    }
    if (media_permission_left_border_overlay_) {
      media_permission_left_border_overlay_->SetVisible(prompt_visible);
    }
    if (media_permission_right_border_overlay_) {
      media_permission_right_border_overlay_->SetVisible(prompt_visible);
    }
    if (media_permission_top_border_panel_) {
      media_permission_top_border_panel_->SetVisible(prompt_visible);
    }
    if (media_permission_bottom_border_panel_) {
      media_permission_bottom_border_panel_->SetVisible(prompt_visible);
    }
    if (media_permission_left_border_panel_) {
      media_permission_left_border_panel_->SetVisible(prompt_visible);
    }
    if (media_permission_right_border_panel_) {
      media_permission_right_border_panel_->SetVisible(prompt_visible);
    }
    media_permission_content_panel_->SetVisible(prompt_visible);
    if (prompt_visible) {
      const int prompt_width = std::max(
          1, std::min(kMediaPermissionPromptWidth, std::max(1, width - 32)));
      const int prompt_height = std::min(kMediaPermissionPromptHeight,
                                         std::max(1, height - 32));
      const int prompt_x = std::max(0, (width - prompt_width) / 2);
      const int prompt_bottom_margin =
          8 + (show_statusline_ ? kStatusBarHeight : 0) + a26_chrome_height;
      const int prompt_y = std::max(
          0, height - prompt_height - prompt_bottom_margin);
      const int content_width = std::max(
          1, prompt_width - 2 * kMediaPermissionPromptBorderWidth);
      const int content_height = std::max(
          1, prompt_height - 2 * kMediaPermissionPromptBorderWidth);

      media_permission_panel_->SetBackgroundColor(theme::kAppBg);
      media_permission_panel_->SetSize(CefSize(content_width, content_height));
      media_permission_overlay_->SetBounds(CefRect(
          prompt_x + kMediaPermissionPromptBorderWidth,
          prompt_y + kMediaPermissionPromptBorderWidth,
          content_width, content_height));
      if (media_permission_top_border_panel_ &&
          media_permission_top_border_overlay_) {
        media_permission_top_border_panel_->SetBackgroundColor(theme::kAccent);
        media_permission_top_border_panel_->SetSize(
            CefSize(prompt_width, kMediaPermissionPromptBorderWidth));
        media_permission_top_border_overlay_->SetBounds(CefRect(
            prompt_x, prompt_y, prompt_width,
            kMediaPermissionPromptBorderWidth));
      }
      if (media_permission_bottom_border_panel_ &&
          media_permission_bottom_border_overlay_) {
        media_permission_bottom_border_panel_->SetBackgroundColor(theme::kAccent);
        media_permission_bottom_border_panel_->SetSize(
            CefSize(prompt_width, kMediaPermissionPromptBorderWidth));
        media_permission_bottom_border_overlay_->SetBounds(CefRect(
            prompt_x,
            prompt_y + std::max(0, prompt_height -
                                       kMediaPermissionPromptBorderWidth),
            prompt_width, kMediaPermissionPromptBorderWidth));
      }
      if (media_permission_left_border_panel_ &&
          media_permission_left_border_overlay_) {
        media_permission_left_border_panel_->SetBackgroundColor(theme::kAccent);
        media_permission_left_border_panel_->SetSize(
            CefSize(kMediaPermissionPromptBorderWidth, prompt_height));
        media_permission_left_border_overlay_->SetBounds(CefRect(
            prompt_x, prompt_y, kMediaPermissionPromptBorderWidth,
            prompt_height));
      }
      if (media_permission_right_border_panel_ &&
          media_permission_right_border_overlay_) {
        media_permission_right_border_panel_->SetBackgroundColor(theme::kAccent);
        media_permission_right_border_panel_->SetSize(
            CefSize(kMediaPermissionPromptBorderWidth, prompt_height));
        media_permission_right_border_overlay_->SetBounds(CefRect(
            prompt_x + std::max(0, prompt_width -
                                       kMediaPermissionPromptBorderWidth),
            prompt_y, kMediaPermissionPromptBorderWidth, prompt_height));
      }
      media_permission_content_panel_->SetBackgroundColor(theme::kAppBg);
      media_permission_content_panel_->SetBounds(
          CefRect(0, 0, content_width, content_height));
      if (media_permission_button_panel_) {
        media_permission_button_panel_->SetBackgroundColor(theme::kAppBg);
      }
      if (media_permission_content_panel_->GetLayout()) {
        media_permission_content_panel_->Layout();
      }
      if (media_permission_button_panel_ &&
          media_permission_button_panel_->GetLayout()) {
        media_permission_button_panel_->Layout();
      }
    }
  }

  LayoutNativeContextMenu(width, height);

  if (root_panel_->GetLayout()) {
    root_panel_->Layout();
  }
  if (main_panel_->GetLayout()) {
    main_panel_->Layout();
  }
  if (sidebar_panel_->GetLayout()) {
    sidebar_panel_->Layout();
  }
  if (sidebar_content_panel_->GetLayout()) {
    sidebar_content_panel_->Layout();
  }
  // Parent box layouts above can resize the docked panes after the initial
  // child bounds assignment. Re-apply split bounds afterwards so the page and
  // DevTools stay as siblings at the main-panel level.
  const int actual_content_width =
      std::max(1, content_panel_->GetBounds().width - sidebar_border_width);
  const int actual_devtools_content_width =
      devtools_panel_ && devtools_docked
          ? std::max(1, devtools_panel_->GetBounds().width - kDevToolsBorderWidth)
          : std::max(1, devtools_width);
  content_panel_->SetBackgroundColor(theme::kAppBg);
  if (sidebar_border_panel_) {
    sidebar_border_panel_->SetVisible(sidebar_visible_);
    sidebar_border_panel_->SetBounds(
        CefRect(0, 0, sidebar_border_width, main_height));
    sidebar_border_panel_->SetBackgroundColor(SidebarBorderColor());
  }
  content_inner_panel_->SetBounds(
      CefRect(sidebar_border_width, 0, actual_content_width, main_height));
  if (devtools_panel_) {
    devtools_panel_->SetBackgroundColor(
        focus_area_ == FocusArea::kDevTools ? theme::kAccent
                                            : theme::kBorderUnfocused);
  }
  if (devtools_panel_ && devtools_panel_->GetLayout()) {
    devtools_panel_->Layout();
  }
  if (devtools_content_panel_) {
    devtools_content_panel_->SetBounds(
        CefRect(kDevToolsBorderWidth, 0, actual_devtools_content_width,
                main_height));
  }
  if (devtools_browser_view_ && devtools_docked) {
    devtools_browser_view_->SetBounds(
        CefRect(0, 0, actual_devtools_content_width, main_height));
  }
  if (command_field_) {
    // Match the sidebar/status cmdline font exactly while / or ? is active;
    // regular full-width commands retain their established 13px editing font.
    command_field_->SetFontList(sidebar_search ? "monospace, 12px"
                                               : "monospace, 13px");
    command_field_->SetBounds(
        CefRect(kCommandTextInsetX, 0,
                std::max(1, command_surface_width - kCommandTextInsetX),
                command_surface_height));
  }
  const int autocomplete_row_width = std::max(1, autocomplete_width -
                                                kCommandAutocompleteBorder * 2);
  for (size_t i = 0; i < autocomplete_rows_.size(); ++i) {
    autocomplete_rows_[i]->SetBounds(
        CefRect(kCommandAutocompleteBorder,
                kCommandAutocompleteBorder +
                    static_cast<int>(i) * kCommandAutocompleteRowHeight,
                autocomplete_row_width, kCommandAutocompleteRowHeight));
  }
  if (content_panel_->GetLayout()) {
    content_panel_->Layout();
  }
  if (content_size_changed && content_inner_panel_->GetLayout()) {
    content_inner_panel_->Layout();
  }
  if (status_bar_panel_ && status_bar_panel_->GetLayout()) {
    status_bar_panel_->Layout();
  }
  if (a26_chrome_panel_ && a26_chrome_panel_->GetLayout()) {
    a26_chrome_panel_->Layout();
  }
  if (a26_navigation_panel_ && a26_navigation_panel_->GetLayout()) {
    a26_navigation_panel_->Layout();
  }
  if (status_sidebar_spacer_panel_) {
    status_sidebar_spacer_panel_->SetVisible(sidebar_visible_);
    status_sidebar_spacer_panel_->SetBounds(
        CefRect(0, 0, sidebar_content_width, kStatusBarHeight));
    status_sidebar_spacer_panel_->SetBackgroundColor(theme::kSidebarBg);
  }
  if (status_content_panel_) {
    status_content_panel_->SetBounds(
        CefRect(content_x, 0, available_content_width, kStatusBarHeight));
    status_content_panel_->SetBackgroundColor(StatusBarBackgroundColor());
  }
  if (status_border_panel_) {
    status_border_panel_->SetVisible(sidebar_visible_);
    status_border_panel_->SetBounds(
        CefRect(sidebar_content_width, 0, sidebar_border_width,
                kStatusBarHeight));
    status_border_panel_->SetBackgroundColor(SidebarBorderColor());
  }
  if (status_output_field_) {
    status_output_field_->SetVisible(!status_output_text_.empty());
  }
  if (status_content_panel_ && status_content_panel_->GetLayout()) {
    status_content_panel_->Layout();
  }
  UpdateStatusBar();
  laid_out_content_width_ = actual_content_width;
  laid_out_content_height_ = main_height;
  if (content_size_changed) {
    for (const Tab& tab : tabs_) {
      if (tab.client && tab.client->browser() &&
          tab.client->browser()->GetHost()) {
        tab.client->browser()->GetHost()->NotifyScreenInfoChanged();
      }
    }
  }
  UpdateSidebarMouseBounds();
  UpdateA26MouseBounds();
  if (mode_indicator_panel_ && mode_indicator_panel_->GetLayout()) {
    mode_indicator_panel_->Layout();
  }
  if (fps_indicator_panel_ && fps_indicator_panel_->GetLayout()) {
    fps_indicator_panel_->Layout();
  }
  if (autocomplete_panel_ && autocomplete_panel_->GetLayout()) {
    autocomplete_panel_->Layout();
  }
}

bool BrowserWindow::RefreshSidebar() {
  if (!sidebar_content_panel_) {
    return false;
  }

  EnsureSidebarSelection();
  const std::vector<SidebarDisplayRow> all_rows = BuildSidebarDisplayRows();
  EnsureSidebarSelectionVisible(all_rows);
  std::vector<SidebarDisplayRow> rendered_rows;
  const size_t fixed_rows = SidebarFixedRowCount(all_rows);
  const size_t viewport_rows = SidebarViewportRowCapacity(fixed_rows);
  rendered_rows.reserve(std::min(all_rows.size(), fixed_rows + viewport_rows));
  rendered_rows.insert(rendered_rows.end(), all_rows.begin(),
                       all_rows.begin() +
                           static_cast<std::ptrdiff_t>(fixed_rows));
  const size_t scrollable_rows = all_rows.size() - fixed_rows;
  const size_t entry_start = std::min(sidebar_scroll_offset_, scrollable_rows);
  const size_t capped_entry_count =
      std::min(viewport_rows, scrollable_rows - entry_start);
  rendered_rows.insert(
      rendered_rows.end(),
      all_rows.begin() + static_cast<std::ptrdiff_t>(fixed_rows + entry_start),
      all_rows.begin() + static_cast<std::ptrdiff_t>(fixed_rows + entry_start +
                                                     capped_entry_count));

  const std::vector<SidebarItemRef> visual_items = SelectedSidebarItems();
  const bool visual_active =
      sidebar_visual_anchor_.type != SidebarItemType::kNone;
  auto is_visual = [&](const SidebarItemRef& item) {
    return visual_active && std::find(visual_items.begin(), visual_items.end(),
                                      item) != visual_items.end();
  };
  auto style_display_row = [&](CefRefPtr<CefTextfield> row,
                               const SidebarDisplayRow& display,
                               cef_color_t background) {
    cef_color_t text_color = theme::kText;
    CefString font = "monospace, 12px";
    if (display.kind == SidebarRowKind::kFolderHeader ||
        display.kind == SidebarRowKind::kSectionLabel) {
      font = "bold monospace, 12px";
    } else if (display.kind == SidebarRowKind::kSeparator ||
               ((display.item.type == SidebarItemType::kFolder ||
                 display.item.type == SidebarItemType::kParent) &&
                !display.selected)) {
      text_color = theme::kMuted;
    }
    StyleTextfield(row, text_color, background, font);
    if (!row) {
      return text_color;
    }
    if (display.selected || display.active) {
      row->ApplyTextColor(theme::kAccent, CefRange(0, 1));
    }
    if (display.audible) {
      row->ApplyTextColor(theme::kAccent,
                          CefRange(display.audible_utf16_offset,
                                   display.audible_utf16_offset + 1));
    }
    return text_color;
  };

  if (sidebar_spacer_ && sidebar_rows_.size() == rendered_rows.size()) {
    for (size_t row_index = 0; row_index < rendered_rows.size(); ++row_index) {
      const SidebarDisplayRow& display = rendered_rows[row_index];
      SidebarRowViews& views = sidebar_rows_[row_index];
      views.kind = display.kind;
      views.item = display.item;
      views.tab_index = display.tab_index;
      const cef_color_t background = display.selected || is_visual(display.item)
                                         ? theme::kSidebarSelBg
                                         : theme::kSidebarBg;
      bool text_changed = false;
      if (views.text != display.text) {
        views.row->SetText(display.text);
        views.row->SelectRange(CefRange(0, 0));
        views.text = display.text;
        text_changed = true;
      }
      const cef_color_t text_color =
          style_display_row(views.row, display, background);
      views.text_color = text_color;
      views.background_color = background;
      (void)text_changed;
    }
    UpdateSidebarMouseBounds();
    return false;
  }

  for (auto& row : sidebar_rows_) {
    if (row.row) {
      sidebar_content_panel_->RemoveChildView(row.row);
    }
  }
  sidebar_rows_.clear();
  if (sidebar_spacer_) {
    sidebar_content_panel_->RemoveChildView(sidebar_spacer_);
    sidebar_spacer_ = nullptr;
  }

  CefRefPtr<CefBoxLayout> sidebar_content_layout;
  if (sidebar_content_panel_->GetLayout()) {
    sidebar_content_layout = sidebar_content_panel_->GetLayout()->AsBoxLayout();
  }

  for (size_t row_index = 0; row_index < rendered_rows.size(); ++row_index) {
    const SidebarDisplayRow& display = rendered_rows[row_index];
    const cef_color_t row_bg = display.selected || is_visual(display.item)
                                   ? theme::kSidebarSelBg
                                   : theme::kSidebarBg;
    CefRefPtr<CefTextfield> row = CefTextfield::CreateTextfield(this);
    row->SetText(display.text);
    row->SelectRange(CefRange(0, 0));
    row->SetID(kSidebarRowBaseId + static_cast<int>(row_index));
    const cef_color_t row_text = style_display_row(row, display, row_bg);
    sidebar_content_panel_->AddChildView(row);
    sidebar_rows_.push_back({row, display.kind, display.item, display.tab_index,
                             display.text, row_text, row_bg});
  }

  sidebar_spacer_ = CefTextfield::CreateTextfield(this);
  sidebar_spacer_->SetText("");
  sidebar_spacer_->SetID(kSidebarSpacerId);
  StyleTextfield(sidebar_spacer_, theme::kText, theme::kSidebarBg,
                 "monospace, 12px");
  sidebar_content_panel_->AddChildView(sidebar_spacer_);
  if (sidebar_content_layout) {
    sidebar_content_layout->SetFlexForView(sidebar_spacer_, 1);
  }

  sidebar_content_panel_->InvalidateLayout();
  return true;
}

void BrowserWindow::ScheduleSidebarRefresh() {
  if (!window_) {
    return;
  }

  const uint64_t generation = ++sidebar_refresh_generation_;
  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(&BrowserWindow::RefreshSidebarForGeneration,
                                    self, generation),
                     kVirtualSidebarRefreshDelayMs);
}

void BrowserWindow::RefreshSidebarForGeneration(uint64_t generation) {
  if (window_ && generation == sidebar_refresh_generation_) {
    RefreshSidebar();
  }
}

void BrowserWindow::RefreshSidebarRow(size_t index) {
  if (index < tabs_.size()) {
    RefreshSidebar();
  }
}

void BrowserWindow::RefreshAudibleTabs() {
  bool changed = false;
  size_t clients_seen = 0;
  for (Tab& tab : tabs_) {
    if (!tab.client) {
      continue;
    }
    ++clients_seen;
    const bool audible = tab.client && tab.client->is_currently_audible();
    if (tab.audible != audible) {
      tab.audible = audible;
      changed = true;
    }
    if (clients_seen >= tab_client_count_) {
      break;
    }
  }
  if (changed) {
    RefreshSidebar();
  }
}

void BrowserWindow::SetFocusArea(FocusArea area) {
  ResetWebsitePendingKeys();
  sidebar_pending_keys_.clear();
  ++sidebar_delete_generation_;
  suppress_next_devtools_char_.reset();
  if (a26_shell_ && area == FocusArea::kTabSidebar) {
    area = FocusArea::kWebView;
    sidebar_visible_ = false;
  }
  if (area == FocusArea::kA26Url &&
      (!a26_shell_ || !a26_url_field_)) {
    area = FocusArea::kWebView;
  }
  if (a26_shell_ && focus_area_ == FocusArea::kA26Url &&
      area != FocusArea::kA26Url) {
    a26_url_focused_ = false;
    a26_url_editing_ = false;
    RequestA26Keyboard(A26KeyboardPurpose::kHide);
  }
  if (area == FocusArea::kTabSidebar && !sidebar_visible_) {
    sidebar_visible_ = true;
  }
  if (area == FocusArea::kDevTools && !FocusAreaAvailable(FocusArea::kDevTools)) {
    area = FocusArea::kWebView;
  }
  focus_area_ = area;
  if (focus_area_ == FocusArea::kWebView) {
    if (Tab* tab = ActiveTab(); tab && tab->view) {
      tab->view->RequestFocus();
    }
  } else if (focus_area_ == FocusArea::kDevTools && devtools_browser_view_) {
    devtools_browser_view_->RequestFocus();
  } else if (focus_area_ == FocusArea::kA26Url && a26_url_field_) {
    a26_url_field_->RequestFocus();
  }
  RefreshSidebar();
  UpdateModeIndicator();
  Layout();
}

bool BrowserWindow::FocusAreaAvailable(FocusArea area) const {
  switch (area) {
    case FocusArea::kTabSidebar:
      return sidebar_visible_;
    case FocusArea::kWebView:
      return !tabs_.empty() && active_index_ < tabs_.size();
    case FocusArea::kDevTools:
      return devtools_visible_ && devtools_browser_view_;
    case FocusArea::kCommandLine:
      return mode_ != Mode::kNormal;
    case FocusArea::kA26Url:
      return a26_shell_ && a26_url_field_;
  }
  return false;
}

void BrowserWindow::FocusRelative(int delta) {
  if (delta == 0) {
    return;
  }

  constexpr std::array<FocusArea, 3> kFocusOrder = {
      FocusArea::kTabSidebar, FocusArea::kWebView, FocusArea::kDevTools};
  int current = 1;
  for (size_t i = 0; i < kFocusOrder.size(); ++i) {
    if (kFocusOrder[i] == focus_area_) {
      current = static_cast<int>(i);
      break;
    }
  }

  const int direction = delta > 0 ? 1 : -1;
  const int count = static_cast<int>(kFocusOrder.size());
  for (int step = 1; step <= count; ++step) {
    const int next = (current + direction * step + count * step) % count;
    if (FocusAreaAvailable(kFocusOrder[next])) {
      SetFocusArea(kFocusOrder[next]);
      return;
    }
  }
}

void BrowserWindow::ToggleSidebar() {
  if (a26_shell_) {
    sidebar_visible_ = false;
    if (focus_area_ == FocusArea::kTabSidebar) {
      SetFocusArea(FocusArea::kWebView);
    }
    return;
  }
  sidebar_visible_ = !sidebar_visible_;
  if (!sidebar_visible_ && focus_area_ == FocusArea::kTabSidebar) {
    focus_area_ = FocusArea::kWebView;
  } else if (sidebar_visible_) {
    focus_area_ = FocusArea::kTabSidebar;
  }
  SetFocusArea(focus_area_);
}

void BrowserWindow::CloseDevTools() {
  // Treat Ctrl+; as a dock visibility toggle, not as destruction of the
  // underlying Chromium DevTools WebContents. Destroying a Chrome-style
  // BrowserView while it is embedded in our Views hierarchy is fragile and can
  // race focus/accelerator dispatch. Hiding the sibling pane is instant,
  // flicker-free, and ShowDevToolsForClient() will re-show/focus the existing
  // DevTools view on the next toggle.
  devtools_visible_ = false;
  if (devtools_browser_view_) {
    devtools_browser_view_->SetVisible(false);
  }
  if (devtools_panel_) {
    devtools_panel_->SetVisible(false);
  }
  if (focus_area_ == FocusArea::kDevTools) {
    focus_area_ = FocusArea::kWebView;
    if (Tab* tab = ActiveTab(); tab && tab->view) {
      tab->view->RequestFocus();
    }
  }
  UpdateModeIndicator();
  Layout();
}

void BrowserWindow::ToggleDevTools() {
  Tab* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) {
    return;
  }

  if (devtools_visible_ && devtools_opener_tab_id_ == tab->id) {
    CloseDevTools();
    return;
  }

  if (devtools_visible_) {
    CloseDevTools();
  }
  ShowDevToolsForClient(tab->client.get());
}

bool BrowserWindow::HandleGlobalFocusKey(const CefKeyEvent& event) {
  if (!IsRawKeyDown(event)) {
    return false;
  }

  const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
  const bool shift = event.modifiers & EVENTFLAG_SHIFT_DOWN;

  if (ctrl && shift && event.windows_key_code == 'I') {
    if (Tab* tab = ActiveTab(); tab && tab->client) {
      tab->client->ShowDevTools();
    }
    return true;
  }

  if (IsCtrlSemicolonKey(event)) {
    ToggleDevTools();
    return true;
  }

  if (IsCtrlKey(event, 'M')) {
    ToggleSidebar();
    return true;
  }

  if (IsCtrlKey(event, 'J') || IsCtrlKey(event, 'K')) {
    FocusRelative(IsCtrlKey(event, 'J') ? 1 : -1);
    return true;
  }

  return false;
}

bool BrowserWindow::HandleWebsiteModeKey(const CefKeyEvent& event) {
  if (focus_area_ != FocusArea::kWebView) {
    return false;
  }

  if (native_hints_active_) {
    return false;
  }

  if (IsRawKeyDown(event)) {
    // `i`/`a` mode-entry keydowns are consumed by browser chrome. Some CEF/X11
    // paths still deliver a following CHAR for that same physical key, so we
    // keep a one-CHAR suppressor. If another raw keydown arrives first, the
    // mode-entry CHAR never arrived; clear the suppressor so the user's first
    // real insert-mode keypress (notably another `i`/`I`) is not eaten.
    suppress_next_website_char_.reset();

    if (IsEscapeKey(event)) {
      ResetWebsitePendingKeys();
      if (website_mode_ == vim::Mode::kInsert) {
        if (a26_shell_) {
          // Moon's HIDE key emits one Escape as a dismissal signal. Unlike the
          // desktop's staged Vim transition, dismiss the focused phone field in
          // one step so a later tap produces a fresh focus event and reopens the
          // global keyboard.
          website_mode_ = vim::Mode::kWebsiteNormal;
          ScheduleActivePageBlur();
        } else {
          website_mode_ = (event.modifiers & EVENTFLAG_SHIFT_DOWN)
                              ? vim::Mode::kWebsiteNormal
                              : vim::Mode::kNormal;
        }
        UpdateModeIndicator();
        return true;
      }
      if (website_mode_ == vim::Mode::kNormal ||
          website_mode_ == vim::Mode::kVisual) {
        website_mode_ = vim::Mode::kWebsiteNormal;
        UpdateModeIndicator();
        return true;
      }
      if (!(event.modifiers & EVENTFLAG_SHIFT_DOWN)) {
        ScheduleActivePageBlur();
        return false;
      }
      UpdateModeIndicator();
      return true;
    }

    if (IsTabKey(event) && PageHasFocusedEditable(event) &&
        !(event.modifiers & (EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN |
                             EVENTFLAG_COMMAND_DOWN))) {
      ResetWebsitePendingKeys();
      return false;
    }

    const bool page_focused_editable = PageHasFocusedEditable(event);
    if (a26_xtest_char_workaround_ && website_mode_ == vim::Mode::kInsert &&
        page_focused_editable && IsPlainPrintableKey(event)) {
      // On the downstream A26 Xorg/CEF combination, XTEST reaches CEF as a raw
      // key event but does not produce the corresponding CHAR event. Consume
      // that raw event and explicitly forward one CHAR to the focused renderer.
      // The payload remains ephemeral and is never logged or sent over IPC.
      if (const char character = PlainKeyChar(event)) {
        CefKeyEvent character_event = event;
        character_event.type = KEYEVENT_CHAR;
        character_event.character = static_cast<char16_t>(character);
        character_event.unmodified_character = static_cast<char16_t>(character);
        ForwardKeyToActivePage(character_event);
        ResetWebsitePendingKeys();
        return true;
      }
    }

    if (website_mode_ == vim::Mode::kInsert &&
        ShouldForwardFocusedEditableKey(event, page_focused_editable)) {
      ResetWebsitePendingKeys();
      return false;
    }

    if (website_mode_ == vim::Mode::kWebsiteNormal) {
      if (PlainKeyChar(event) == ':') {
        BeginCommandText(":");
        return true;
      }

      if (HandleWebsiteCommandKey(event)) {
        return true;
      }

      if (StartNativeHints(event)) {
        return true;
      }

      if (std::optional<bool> shortcut = HandlePageShortcut(event, true)) {
        return *shortcut;
      }

      if (const std::optional<bool> open_new_tab =
              OpenCommandNewTabForKey(event)) {
        BeginCommand(*open_new_tab ? Mode::kCommandOpenNext
                                   : Mode::kCommandOpenCurrent);
        return true;
      }

      if (IsPlainLetterKey(event, 'i') || IsPlainLetterKey(event, 'a')) {
        website_mode_ = vim::Mode::kInsert;
        if (const char key = PlainKeyChar(event)) {
          suppress_next_website_char_ = LowerAsciiChar(key);
        }
        UpdateModeIndicator();
        return true;
      }

      // Website-normal is the future home for hinting, page scrolling, and
      // qutebrowser-like page commands. Until those bindings exist, keep plain
      // printable keys out of the page. Use insert mode for page text input.
      if (IsPlainPrintableKey(event)) {
        return true;
      }
      ResetWebsitePendingKeys();
      return true;
    }

    if (website_mode_ == vim::Mode::kNormal || website_mode_ == vim::Mode::kVisual) {
      if (website_mode_ == vim::Mode::kNormal && PlainKeyChar(event) == ':') {
        BeginCommandText(":");
        return true;
      }

      if (website_mode_ == vim::Mode::kNormal && HandleWebsiteCommandKey(event)) {
        return true;
      }

      if (website_mode_ == vim::Mode::kNormal) {
        if (StartNativeHints(event)) {
          return true;
        }

        if (std::optional<bool> shortcut = HandlePageShortcut(event, true)) {
          return *shortcut;
        }
      }

      if (website_mode_ == vim::Mode::kNormal) {
        if (const std::optional<bool> open_new_tab =
                OpenCommandNewTabForKey(event)) {
          BeginCommand(*open_new_tab ? Mode::kCommandOpenNext
                                     : Mode::kCommandOpenCurrent);
          return true;
        }
      }

      if (website_mode_ == vim::Mode::kNormal &&
          (IsPlainLetterKey(event, 'i') || IsPlainLetterKey(event, 'a'))) {
        website_mode_ = vim::Mode::kInsert;
        if (const char key = PlainKeyChar(event)) {
          suppress_next_website_char_ = LowerAsciiChar(key);
        }
        UpdateModeIndicator();
        return true;
      }

      // Regular Vim normal/visual modes are skeleton states for future operators,
      // text objects, and selections. For now they intentionally swallow plain
      // printable keys and perform no page action.
      if (IsPlainPrintableKey(event)) {
        return true;
      }
      ResetWebsitePendingKeys();
      return true;
    }

    if (website_mode_ == vim::Mode::kInsert) {
      if (std::optional<bool> shortcut = HandlePageShortcut(event, true)) {
        return *shortcut;
      }
    }

    // Insert mode lets the page handle normal input. Escape was handled above.
    return false;
  }

  if (IsCharEvent(event)) {
    if (suppress_next_website_char_) {
      const std::optional<char> suppressed = suppress_next_website_char_;
      suppress_next_website_char_.reset();
      if (const char key = PlainKeyChar(event);
          key && LowerAsciiChar(key) == *suppressed) {
        return true;
      }
    }
    if (website_mode_ == vim::Mode::kInsert &&
        ShouldForwardFocusedEditableKey(event, PageHasFocusedEditable(event))) {
      ResetWebsitePendingKeys();
      return false;
    }
    if (std::optional<bool> shortcut = HandlePageShortcut(event, true)) {
      return *shortcut;
    }
    if (website_mode_ == vim::Mode::kInsert) {
      return false;
    }
    if (website_mode_ == vim::Mode::kWebsiteNormal &&
        (IsPlainLetterKey(event, 'i') || IsPlainLetterKey(event, 'a'))) {
      website_mode_ = vim::Mode::kInsert;
      UpdateModeIndicator();
      return true;
    }
    return true;
  }

  return false;
}

std::optional<bool> BrowserWindow::HandlePageShortcut(
    const CefKeyEvent& event,
    bool allow_forward_to_page) {
  if (focus_area_ != FocusArea::kWebView || native_hints_active_) {
    // Hint mode consumes hint-label characters in Blink. Page shortcuts must be
    // completely disabled here so site-specific bindings never steal labels.
    return std::nullopt;
  }
  if (website_mode_ != vim::Mode::kWebsiteNormal &&
      website_mode_ != vim::Mode::kNormal &&
      website_mode_ != vim::Mode::kInsert) {
    return std::nullopt;
  }
  if (!IsRawKeyDown(event) && !IsCharEvent(event)) {
    return std::nullopt;
  }

  // Space has a page-specific playback binding on YouTube. Keep that binding
  // strictly narrower than Space-based browser commands such as Ctrl+Space
  // hints: any key modifier makes this a different chord, not playback input.
  if (IsSpaceKey(event) && !IsLoneSpaceShortcutEvent(event)) {
    return std::nullopt;
  }

  const char key = PlainKeyChar(event);
  if (!key) {
    return std::nullopt;
  }

  const bool focus_on_editable_field = PageHasFocusedEditable(event);
  if (website_mode_ == vim::Mode::kInsert && focus_on_editable_field) {
    return std::nullopt;
  }

  const std::string url = ActiveTabUrl();
  const bool plain_without_shift =
      IsPlain(event) && !(event.modifiers & EVENTFLAG_SHIFT_DOWN);
  unsigned int shortcut_mode = 0;
  if (website_mode_ == vim::Mode::kWebsiteNormal) {
    shortcut_mode = VIMBROWSER_SHORTCUT_MODE_WEBSITE_NORMAL;
  } else if (website_mode_ == vim::Mode::kNormal) {
    shortcut_mode = VIMBROWSER_SHORTCUT_MODE_NORMAL;
  } else if (website_mode_ == vim::Mode::kInsert) {
    shortcut_mode = VIMBROWSER_SHORTCUT_MODE_INSERT;
  }
  const VimbrowserShortcut shortcut = vimbrowser_shortcut_for_key(
      url.c_str(), static_cast<unsigned char>(key), IsRawKeyDown(event),
      IsCharEvent(event), plain_without_shift,
      focus_on_editable_field ? 1 : 0, shortcut_mode);

  switch (shortcut.action) {
    case VIMBROWSER_SHORTCUT_NONE:
      return std::nullopt;
    case VIMBROWSER_SHORTCUT_FORWARD_TO_PAGE:
      if (!allow_forward_to_page) {
        return std::nullopt;
      }
      ResetWebsitePendingKeys();
      return false;
    case VIMBROWSER_SHORTCUT_CONSUME:
      ResetWebsitePendingKeys();
      return true;
    case VIMBROWSER_SHORTCUT_EVALUATE_SCRIPT: {
      ResetWebsitePendingKeys();
      CefRefPtr<CefBrowser> browser = ActiveBrowser();
      if (browser && browser->GetMainFrame() && shortcut.script &&
          shortcut.script[0]) {
        browser->GetMainFrame()->ExecuteJavaScript(
            shortcut.script, browser->GetMainFrame()->GetURL(), 0);
      }
      return true;
    }
  }

  return std::nullopt;
}

void BrowserWindow::ResetWebsitePendingKeys() {
  website_pending_keys_.clear();
}

bool BrowserWindow::StopPageNativeHintsForClient(BrowserClient* client) {
  Tab* tab = ActiveTab();
  if (!native_hints_active_ || !tab || tab->client.get() != client ||
      focus_area_ != FocusArea::kWebView) {
    return false;
  }

  native_hints_active_ = false;
  website_mode_ = website_mode_ == vim::Mode::kInsert
                      ? vim::Mode::kInsert
                      : vim::Mode::kWebsiteNormal;
  ResetWebsitePendingKeys();
  UpdateModeIndicator();
  return true;
}

bool BrowserWindow::StartNativeHints(const CefKeyEvent& event) {
  if (!IsRawKeyDown(event)) {
    return false;
  }

  const bool click_hints = IsPlainLetterKey(event, 'f');
  const bool right_click_hints = HasOnlyControlModifier(event) &&
                                 IsCtrlKey(event, 'L');
  const bool hover_hints = HasOnlyControlModifier(event) &&
                           IsCtrlKey(event, 'H');
  const bool scrollable_hints = HasOnlyControlModifier(event) && IsSpaceKey(event);
  if (!click_hints && !right_click_hints && !hover_hints &&
      !scrollable_hints) {
    return false;
  }

  Tab* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) {
    return false;
  }

  ResetWebsitePendingKeys();
  native_hints_active_ = true;
  UpdateModeIndicator();
  CefKeyEvent browser_event = event;
  if (click_hints) {
    // Normalize toolkit-specific lower/upper keycodes to the Windows virtual-key
    // code that Blink's native hint dispatcher expects.
    browser_event.windows_key_code = 'F';
  }
  if (right_click_hints) {
    // Blink's native hint dispatcher recognizes Ctrl+L as the right-click hint
    // entry command. Preserve the semantic key explicitly for the renderer
    // round-trip just like Ctrl+Space scrollable hints.
    browser_event.windows_key_code = 'L';
    browser_event.unmodified_character = 'l';
  }
  if (hover_hints) {
    // Blink's native hint dispatcher recognizes Ctrl+H as the hover hint entry
    // command. Preserve the semantic key explicitly for the renderer round-trip.
    browser_event.windows_key_code = 'H';
    browser_event.unmodified_character = 'h';
  }
  if (scrollable_hints) {
    // Some toolkits deliver Ctrl+Space to the browser chrome as a control
    // character with no virtual key. Blink's native hint dispatcher keys off the
    // Windows virtual-key field after CEF translates this back into a web event,
    // so preserve the semantic key explicitly for the renderer round-trip.
    browser_event.windows_key_code = 0x20;
    browser_event.unmodified_character = 0x20;
  }
  tab->client->SendBrowserCommandKeyEvent(browser_event);
  return true;
}

bool BrowserWindow::StartDevToolsNativeHints(const CefKeyEvent& event) {
  if (!IsRawKeyDown(event) || !devtools_browser_view_ ||
      !devtools_browser_view_->GetBrowser()) {
    return false;
  }

  const bool ctrl_only = HasOnlyControlModifier(event);
  const bool click_hints = IsPlainLetterKey(event, 'f');
  const bool right_click_hints = ctrl_only && IsCtrlKey(event, 'L');
  const bool hover_hints = ctrl_only && IsCtrlKey(event, 'H');
  const bool scrollable_hints =
      ctrl_only && (IsSpaceKey(event) || event.windows_key_code == 0 ||
                    event.native_key_code == 65);
  if (!click_hints && !right_click_hints && !hover_hints &&
      !scrollable_hints) {
    return false;
  }

  native_hints_active_ = true;
  if (scrollable_hints) {
    devtools_has_scroll_target_ = false;
  }
  ResetWebsitePendingKeys();
  UpdateModeIndicator();

  CefKeyEvent browser_event = event;
  if (click_hints) {
    // Normalize toolkit-specific lower/upper keycodes to the Windows virtual-key
    // code that Blink's native hint dispatcher expects.
    browser_event.windows_key_code = 'F';
  }
  if (right_click_hints) {
    browser_event.windows_key_code = 'L';
    browser_event.unmodified_character = 'l';
  }
  if (hover_hints) {
    browser_event.windows_key_code = 'H';
    browser_event.unmodified_character = 'h';
  }
  if (scrollable_hints) {
    // Keep this exactly parallel to Ctrl+Space scrollable hints for normal pages:
    // toolkit/X11 paths can report Ctrl+Space as a control character, while
    // Blink's native hint dispatcher keys off VK_SPACE after CEF translates this
    // synthetic browser-command event back into a WebKeyboardEvent.
    browser_event.windows_key_code = 0x20;
    browser_event.unmodified_character = 0x20;
  }
  vimbrowser_send_browser_command_key_event(
      devtools_browser_view_->GetBrowser()->GetIdentifier(), &browser_event);
  return true;
}

bool BrowserWindow::HandleDevToolsModeKey(const CefKeyEvent& event) {
  if (native_hints_active_) {
    // DevTools native hints are implemented in Blink just like page hints. Once
    // active, Blink owns hint-label characters and Escape until it reports that
    // hint mode stopped through DevToolsClient::OnConsoleMessage().
    return false;
  }

  if (IsCharEvent(event)) {
    if (suppress_next_devtools_char_) {
      const std::optional<char> suppressed = suppress_next_devtools_char_;
      suppress_next_devtools_char_.reset();
      if (const char key = PlainKeyChar(event);
          key && LowerAsciiChar(key) == *suppressed) {
        return true;
      }
    }
    if (devtools_mode_ == vim::Mode::kInsert) {
      return false;
    }
    if (IsPlainLetterKey(event, 'i') || IsPlainLetterKey(event, 'a')) {
      devtools_mode_ = vim::Mode::kInsert;
      UpdateModeIndicator();
      return true;
    }
    // DevTools normal mode owns printable keys just like website normal mode.
    return true;
  }

  if (!IsRawKeyDown(event)) {
    return false;
  }

  suppress_next_devtools_char_.reset();

  if (IsEscapeKey(event)) {
    if (devtools_mode_ == vim::Mode::kInsert) {
      devtools_mode_ = vim::Mode::kNormal;
      UpdateModeIndicator();
      return true;
    }
    UpdateModeIndicator();
    return true;
  }

  if (devtools_mode_ == vim::Mode::kInsert) {
    return false;
  }

  if (PlainKeyChar(event) == ':') {
    BeginCommandText(":");
    return true;
  }

  if (StartDevToolsNativeHints(event)) {
    return true;
  }

  const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
  const bool shift = event.modifiers & EVENTFLAG_SHIFT_DOWN;
  if (ctrl && !shift) {
    if (IsCtrlKey(event, 'E')) { ScrollDevToolsBy(kSmallScrollPx); return true; }
    if (IsCtrlKey(event, 'Y')) { ScrollDevToolsBy(-kSmallScrollPx); return true; }
    if (IsCtrlKey(event, 'D')) { ScrollDevToolsBy(560); return true; }
    if (IsCtrlKey(event, 'U')) { ScrollDevToolsBy(-560); return true; }
    if (IsCtrlKey(event, 'F')) { ScrollDevToolsBy(1120); return true; }
    if (IsCtrlKey(event, 'B')) { ScrollDevToolsBy(-1120); return true; }
  }

  if (IsPlainLetterKey(event, 'j')) {
    ScrollDevToolsBy(kLineScrollPx);
    return true;
  }
  if (IsPlainLetterKey(event, 'k')) {
    ScrollDevToolsBy(-kLineScrollPx);
    return true;
  }
  if (IsPlainLetterKey(event, 'h')) {
    CycleDevToolsPanel(-1);
    return true;
  }
  if (IsPlainLetterKey(event, 'l')) {
    CycleDevToolsPanel(1);
    return true;
  }

  if (IsPlainLetterKey(event, 'i') || IsPlainLetterKey(event, 'a')) {
    devtools_mode_ = vim::Mode::kInsert;
    if (const char key = PlainKeyChar(event)) {
      suppress_next_devtools_char_ = LowerAsciiChar(key);
    }
    UpdateModeIndicator();
    return true;
  }

  if (IsPlainPrintableKey(event)) {
    return true;
  }

  return true;
}

bool BrowserWindow::HandleWebsiteCommandKey(const CefKeyEvent& event) {
  if (!IsRawKeyDown(event)) {
    return false;
  }

  const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
  const bool shift = event.modifiers & EVENTFLAG_SHIFT_DOWN;
  const char key = PlainKeyChar(event);

  if (ctrl && event.windows_key_code >= '1' && event.windows_key_code <= '9') {
    ActivateTab(static_cast<size_t>(event.windows_key_code - '1'));
    ResetWebsitePendingKeys();
    return true;
  }

  if (ctrl && !shift) {
    if (IsCtrlKey(event, 'E')) { ScrollActivePageBy(kSmallScrollPx); return true; }
    if (IsCtrlKey(event, 'Y')) { ScrollActivePageBy(-kSmallScrollPx); return true; }
    if (IsCtrlKey(event, 'D')) { ScrollActivePageBy(560); return true; }
    if (IsCtrlKey(event, 'U')) { ScrollActivePageBy(-560); return true; }
    if (IsCtrlKey(event, 'F')) { ScrollActivePageBy(1120); return true; }
    if (IsCtrlKey(event, 'B')) { ScrollActivePageBy(-1120); return true; }
  }

  if (ctrl && shift && IsCtrlKey(event, 'Y')) {
    YankActiveDom();
    ResetWebsitePendingKeys();
    return true;
  }

  if (!key) {
    ResetWebsitePendingKeys();
    return false;
  }

  if (website_pending_keys_ == "g") {
    ResetWebsitePendingKeys();
    if (key == 'g') {
      if (focus_area_ == FocusArea::kTabSidebar) {
        ActivateFirstTab();
      } else {
        ScrollActivePageToTop();
      }
      return true;
    }
    if (key == '0') { ActivateFirstTab(); return true; }
    if (key == '$') { ActivateLastTab(); return true; }
    return true;
  }

  if (website_pending_keys_ == "y") {
    ResetWebsitePendingKeys();
    if (key == 'y') { YankActiveUrl(); return true; }
    if (key == 't') { YankActiveTitle(); return true; }
    if (key == 'm') { YankActiveMarkdown(); return true; }
    return true;
  }

  if (focus_area_ == FocusArea::kWebView) {
    if (key == '/') {
      BeginCommandText("/");
      return true;
    }
    if (key == '?') {
      BeginCommandText("?");
      return true;
    }
    if (key == 'n') {
      FindNextPageSearch(false);
      return true;
    }
    if (key == 'N') {
      FindNextPageSearch(true);
      return true;
    }
  }

  if (std::optional<bool> shortcut = HandlePageShortcut(event, false)) {
    return *shortcut;
  }

  switch (key) {
    case 'j': ScrollActivePageBy(kLineScrollPx); return true;
    case 'k': ScrollActivePageBy(-kLineScrollPx); return true;
    case 'G':
      if (focus_area_ == FocusArea::kTabSidebar) {
        ActivateLastTab();
      } else {
        ScrollActivePageToBottom();
      }
      return true;
    case 'H':
      if (CefRefPtr<CefBrowser> browser = ActiveBrowser(); browser && browser->CanGoBack()) browser->GoBack();
      return true;
    case 'L':
      if (CefRefPtr<CefBrowser> browser = ActiveBrowser(); browser && browser->CanGoForward()) browser->GoForward();
      return true;
    case 'r':
      if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) browser->Reload();
      return true;
    case 'R':
      if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) browser->ReloadIgnoreCache();
      return true;
    case 'p': OpenClipboard(false); return true;
    case 'P': OpenClipboard(true); return true;
    case 'J':
      SetFocusArea(FocusArea::kTabSidebar);
      MoveSidebarSelection(1);
      return true;
    case 'K':
      SetFocusArea(FocusArea::kTabSidebar);
      MoveSidebarSelection(-1);
      return true;
    case 'd': CloseActiveTab(CloseFocus::kNextTab); return true;
    case 'D': CloseActiveTab(CloseFocus::kPreviousTab); return true;
    case 'u': UndoCloseTab(); return true;
    case 'e': MoveActiveTab(-1); return true;
    case 'E': MoveActiveTab(1); return true;
    case 'c': CloneActiveTab(); return true;
    case 't': BeginCommandText(":tab-focus "); return true;
    case '=': ZoomActivePage(CEF_ZOOM_COMMAND_IN); return true;
    case '-': ZoomActivePage(CEF_ZOOM_COMMAND_OUT); return true;
    case ')': ZoomActivePage(CEF_ZOOM_COMMAND_RESET); return true;
    case 'g': website_pending_keys_ = "g"; return true;
    case 'y': website_pending_keys_ = "y"; return true;
  }

  ResetWebsitePendingKeys();
  return false;
}

void BrowserWindow::RestyleView(CefRefPtr<CefView> view) {
  if (!view) {
    return;
  }

  const int id = view->GetID();
  if (id == kSidebarPanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (id == kSidebarContentPanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (InIdRange(id, kSidebarRowBaseId, 1000)) {
    // RefreshSidebar() owns per-row colors and the active marker accent.
  } else if (id == kSidebarSpacerId) {
    if (sidebar_spacer_) {
      StyleTextfield(sidebar_spacer_, theme::kText, theme::kSidebarBg,
                     "monospace, 12px");
    }
  } else if (id == kSidebarBorderPanelId ||
             id == kSidebarBorderOverlayPanelId) {
    view->SetBackgroundColor(SidebarBorderColor());
  } else if (id == kCommandPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandFieldId && command_field_) {
    StyleCommandField(command_field_);
  } else if (id == kCommandSeparatorPanelId) {
    view->SetBackgroundColor(theme::kAccent);
  } else if (id == kCommandAutocompletePanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (id == kContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kContentInnerPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kDevToolsPanelId) {
    view->SetBackgroundColor(focus_area_ == FocusArea::kDevTools
                                 ? theme::kAccent
                                 : theme::kBorderUnfocused);
  } else if (id == kDevToolsContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kStatusBarPanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (id == kStatusSidebarSpacerPanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (id == kStatusContentPanelId) {
    view->SetBackgroundColor(StatusBarBackgroundColor());
  } else if (id == kStatusBorderPanelId) {
    view->SetBackgroundColor(SidebarBorderColor());
  } else if (id == kStatusOutputFieldId && status_output_field_) {
    StyleTextfield(status_output_field_, theme::kText,
                   StatusBarBackgroundColor(), "monospace, 12px");
    UpdateStatusBar();
  } else if (id == kStatusModeFieldId && status_mode_field_) {
    StyleTextfield(status_mode_field_, ModeIndicatorColor(),
                   StatusBarBackgroundColor(), "monospace, 12px");
    UpdateStatusBar();
  } else if (id == kStatusUrlFieldId && status_url_label_) {
    status_url_label_->SetEnabledTextColors(theme::kText);
    status_url_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
    status_url_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
    status_url_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
    status_url_label_->SetBackgroundColor(StatusBarBackgroundColor());
    status_url_label_->SetState(CEF_BUTTON_STATE_NORMAL);
    UpdateStatusBar();
  } else if (id == kA26ChromePanelId ||
             id == kA26BottomReservePanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kA26NavigationPanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (id == kA26BackButtonId) {
    StyleA26Button(a26_back_button_);
    a26_back_button_->SetFontList("sans-serif, 20px");
  } else if (id == kA26ForwardButtonId) {
    StyleA26Button(a26_forward_button_);
    a26_forward_button_->SetFontList("sans-serif, 20px");
  } else if (id == kA26ReloadButtonId) {
    StyleA26Button(a26_reload_button_);
  } else if (id == kA26TabsButtonId) {
    StyleA26Button(a26_tabs_button_);
  } else if (id == kA26UrlFieldId && a26_url_field_) {
    StyleA26UrlField(a26_url_field_);
  } else if (id == kRootPanelId || id == kMainPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kModeIndicatorPanelId) {
    view->SetBackgroundColor(theme::kUserBg);
  } else if (id == kModeIndicatorFieldId && mode_indicator_label_) {
    mode_indicator_label_->SetEnabledTextColors(ModeIndicatorColor());
    mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, ModeIndicatorColor());
    mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, ModeIndicatorColor());
    mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, ModeIndicatorColor());
    mode_indicator_label_->SetBackgroundColor(theme::kUserBg);
    mode_indicator_label_->SetState(CEF_BUTTON_STATE_NORMAL);
    UpdateModeIndicator();
  } else if (id == kFpsIndicatorPanelId) {
    view->SetBackgroundColor(theme::kUserBg);
  } else if (id == kFpsIndicatorFieldId && fps_indicator_label_) {
    fps_indicator_label_->SetEnabledTextColors(theme::kText);
    fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
    fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
    fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
    fps_indicator_label_->SetBackgroundColor(theme::kUserBg);
    fps_indicator_label_->SetState(CEF_BUTTON_STATE_NORMAL);
    UpdateFpsIndicator();
  } else if (id == kMediaPermissionPromptPanelId) {
    view->SetBackgroundColor(theme::kAccent);
  } else if (id == kMediaPermissionBorderTopPanelId ||
             id == kMediaPermissionBorderBottomPanelId ||
             id == kMediaPermissionBorderLeftPanelId ||
             id == kMediaPermissionBorderRightPanelId) {
    view->SetBackgroundColor(theme::kAccent);
  } else if (id == kMediaPermissionPromptContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kMediaPermissionTitleFieldId &&
             media_permission_title_field_) {
    StyleTextfield(media_permission_title_field_, theme::kCommand,
                   theme::kAppBg, "monospace, 13px");
  } else if (id == kMediaPermissionOriginFieldId &&
             media_permission_origin_field_) {
    StyleTextfield(media_permission_origin_field_, theme::kMuted,
                   theme::kAppBg, "monospace, 12px");
  } else if (id == kMediaPermissionBodyFieldId &&
             media_permission_body_field_) {
    StyleTextfield(media_permission_body_field_, theme::kText,
                   theme::kAppBg, "monospace, 12px");
  } else if (id == kMediaPermissionHintFieldId &&
             media_permission_hint_field_) {
    StyleTextfield(media_permission_hint_field_, theme::kMuted,
                   theme::kAppBg, "monospace, 12px");
  } else if (id == kMediaPermissionButtonPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kMediaPermissionAllowButtonId &&
             media_permission_allow_button_) {
    StylePermissionButton(media_permission_allow_button_, theme::kText,
                          theme::kAccent);
  } else if (id == kMediaPermissionDenyButtonId &&
             media_permission_deny_button_) {
    StylePermissionButton(media_permission_deny_button_, theme::kText,
                          theme::kSidebarSelBg);
  } else if (id == kContextMenuBackdropButtonId &&
             context_menu_backdrop_button_) {
    context_menu_backdrop_button_->SetBackgroundColor(theme::kTransparent);
  } else if (id == kContextMenuPanelId) {
    view->SetBackgroundColor(theme::kAccent);
  } else if (InIdRange(id, kContextMenuRowBaseId, 1000)) {
    UpdateNativeContextMenuSelection();
  }
}

void BrowserWindow::StartSidebarMouseWatcher() {
#if defined(__linux__)
  if (sidebar_mouse_watcher_running_.exchange(true)) {
    return;
  }
  UpdateSidebarMouseBounds();
  sidebar_mouse_thread_ = std::thread(&BrowserWindow::RunSidebarMouseWatcher, this);
#endif
}

void BrowserWindow::StopSidebarMouseWatcher() {
#if defined(__linux__)
  sidebar_mouse_watcher_running_.store(false);
  sidebar_mouse_width_.store(0);
  sidebar_mouse_height_.store(0);
  sidebar_mouse_row_count_.store(0);
  sidebar_mouse_window_.store(0);
  a26_layout_width_.store(0);
  a26_layout_height_.store(0);
  a26_mouse_window_.store(0);
  if (sidebar_mouse_thread_.joinable() &&
      sidebar_mouse_thread_.get_id() != std::this_thread::get_id()) {
    sidebar_mouse_thread_.join();
  }
#endif
}

void BrowserWindow::RunSidebarMouseWatcher() {
#if defined(__linux__)
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    sidebar_mouse_watcher_running_.store(false);
    return;
  }

  int xi_opcode = 0;
  int xi_event = 0;
  int xi_error = 0;
  if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &xi_event,
                       &xi_error)) {
    XCloseDisplay(display);
    sidebar_mouse_watcher_running_.store(false);
    return;
  }

  int xi_major = 2;
  int xi_minor = 0;
  if (XIQueryVersion(display, &xi_major, &xi_minor) != Success) {
    XCloseDisplay(display);
    sidebar_mouse_watcher_running_.store(false);
    return;
  }

  Window root = DefaultRootWindow(display);
  Cursor hand_cursor = XCreateFontCursor(display, XC_hand2);
  Window hand_cursor_window = 0;

  auto toplevel_for_window = [display, root](Window window) {
    Window current = window;
    while (current != 0 && current != root) {
      Window query_root = 0;
      Window parent = 0;
      Window* children = nullptr;
      unsigned int child_count = 0;
      if (!XQueryTree(display, current, &query_root, &parent, &children,
                      &child_count)) {
        return Window{0};
      }
      if (children) {
        XFree(children);
      }
      if (parent == root || parent == 0) {
        return current;
      }
      current = parent;
    }
    return current;
  };

  struct SidebarMouseHit {
    bool inside_sidebar = false;
    bool over_clickable_row = false;
    size_t row_index = 0;
    Window browser_toplevel = 0;
  };

  struct ContextMenuMouseHit {
    bool menu_visible = false;
    bool inside_menu = false;
    bool over_row = false;
    size_t row_index = 0;
    Window browser_toplevel = 0;
  };

  struct A26MouseHit {
    int control_index = -1;
    Window browser_toplevel = 0;
    int root_x = 0;
    int root_y = 0;
  };

  struct ChromeMouseHits {
    ContextMenuMouseHit context_menu;
    A26MouseHit a26;
    SidebarMouseHit sidebar;
  };

  auto sidebar_hit_test = [&]() {
    SidebarMouseHit hit;
    Window root_return = 0;
    Window child_return = 0;
    int root_x = 0;
    int root_y = 0;
    int win_x = 0;
    int win_y = 0;
    unsigned int buttons = 0;
    if (!XQueryPointer(display, root, &root_return, &child_return, &root_x,
                       &root_y, &win_x, &win_y, &buttons)) {
      return hit;
    }
    const Window browser_window =
        static_cast<Window>(sidebar_mouse_window_.load());
    hit.browser_toplevel =
        browser_window ? toplevel_for_window(browser_window) : Window{0};
    const int sidebar_x = sidebar_mouse_screen_x_.load();
    const int sidebar_y = sidebar_mouse_screen_y_.load();
    const int sidebar_width = sidebar_mouse_width_.load();
    const int sidebar_height = sidebar_mouse_height_.load();
    if (hit.browser_toplevel == 0 || child_return != hit.browser_toplevel ||
        sidebar_width <= 0 || sidebar_height <= 0 || root_x < sidebar_x ||
        root_x >= sidebar_x + sidebar_width || root_y < sidebar_y ||
        root_y >= sidebar_y + sidebar_height) {
      hit.browser_toplevel = 0;
      return hit;
    }

    hit.inside_sidebar = true;
    hit.row_index = static_cast<size_t>((root_y - sidebar_y) / kSidebarRowHeight);
    const int row_count = sidebar_mouse_row_count_.load();
    hit.over_clickable_row =
        row_count > 0 && hit.row_index < static_cast<size_t>(row_count);
    return hit;
  };

  auto context_menu_hit_test = [&]() {
    ContextMenuMouseHit hit;
    const Window browser_window =
        static_cast<Window>(context_menu_mouse_window_.load());
    const int menu_x = context_menu_mouse_screen_x_.load();
    const int menu_y = context_menu_mouse_screen_y_.load();
    const int menu_width = context_menu_mouse_width_.load();
    const int menu_height = context_menu_mouse_height_.load();
    const int row_count = context_menu_mouse_row_count_.load();
    hit.menu_visible = browser_window != 0 && menu_width > 0 &&
                       menu_height > 0 && row_count > 0;
    if (!hit.menu_visible) {
      return hit;
    }

    Window root_return = 0;
    Window child_return = 0;
    int root_x = 0;
    int root_y = 0;
    int win_x = 0;
    int win_y = 0;
    unsigned int buttons = 0;
    if (!XQueryPointer(display, root, &root_return, &child_return, &root_x,
                       &root_y, &win_x, &win_y, &buttons)) {
      return hit;
    }
    hit.browser_toplevel = toplevel_for_window(browser_window);
    if (hit.browser_toplevel == 0 || child_return != hit.browser_toplevel ||
        root_x < menu_x || root_x >= menu_x + menu_width || root_y < menu_y ||
        root_y >= menu_y + menu_height) {
      return hit;
    }

    hit.inside_menu = true;
    const int row_area_y = root_y - menu_y - kContextMenuBorderWidth;
    if (row_area_y < 0) {
      return hit;
    }
    hit.row_index = static_cast<size_t>(row_area_y / kContextMenuRowHeight);
    hit.over_row = hit.row_index < static_cast<size_t>(row_count);
    return hit;
  };

  auto a26_hit_test = [&]() {
    A26MouseHit hit;
    const Window browser_window =
        static_cast<Window>(a26_mouse_window_.load());
    if (browser_window == 0) {
      return hit;
    }

    Window root_return = 0;
    Window child_return = 0;
    int root_x = 0;
    int root_y = 0;
    int win_x = 0;
    int win_y = 0;
    unsigned int buttons = 0;
    if (!XQueryPointer(display, root, &root_return, &child_return, &root_x,
                       &root_y, &win_x, &win_y, &buttons)) {
      return hit;
    }
    hit.root_x = root_x;
    hit.root_y = root_y;

    hit.browser_toplevel = toplevel_for_window(browser_window);
    if (hit.browser_toplevel == 0 || child_return != hit.browser_toplevel) {
      hit.browser_toplevel = 0;
      return hit;
    }

    const int layout_width = a26_layout_width_.load();
    const int layout_height = a26_layout_height_.load();
    XWindowAttributes attributes = {};
    int window_root_x = 0;
    int window_root_y = 0;
    Window translated_child = 0;
    if (layout_width <= 0 || layout_height <= 0 ||
        !XGetWindowAttributes(display, hit.browser_toplevel, &attributes) ||
        attributes.width <= 0 || attributes.height <= 0 ||
        !XTranslateCoordinates(display, hit.browser_toplevel, root, 0, 0,
                               &window_root_x, &window_root_y,
                               &translated_child) ||
        root_x < window_root_x || root_y < window_root_y ||
        root_x >= window_root_x + attributes.width ||
        root_y >= window_root_y + attributes.height) {
      hit.browser_toplevel = 0;
      return hit;
    }

    // XInput reports physical root pixels while CEF Views uses DIP. Derive the
    // current scale from the actual X11 top-level size so forced/non-integer
    // device scales hit the same logical rectangles that were laid out above.
    const int logical_x =
        (root_x - window_root_x) * layout_width / attributes.width;
    const int logical_y =
        (root_y - window_root_y) * layout_height / attributes.height;
    const int control_top = layout_height - kA26ChromeHeight + 6;
    if (logical_y < control_top ||
        logical_y >= control_top + kA26TouchControlHeight) {
      hit.browser_toplevel = 0;
      return hit;
    }

    const int back_left = 6;
    const int forward_left = back_left + kA26HistoryButtonWidth + 4;
    const int tabs_right = layout_width - 6;
    const int tabs_left = tabs_right - kA26TabsButtonWidth;
    const int reload_right = tabs_left - 4;
    const int reload_left = reload_right - kA26ReloadButtonWidth;
    const int url_left = forward_left + kA26HistoryButtonWidth + 4;
    const int url_right = reload_left - 4;
    if (logical_x >= back_left &&
        logical_x < back_left + kA26HistoryButtonWidth) {
      hit.control_index = 0;
    } else if (logical_x >= forward_left &&
               logical_x < forward_left + kA26HistoryButtonWidth) {
      hit.control_index = 1;
    } else if (logical_x >= url_left && logical_x < url_right) {
      hit.control_index = 2;
    } else if (logical_x >= reload_left && logical_x < reload_right) {
      hit.control_index = 3;
    } else if (logical_x >= tabs_left && logical_x < tabs_right) {
      hit.control_index = 4;
    } else {
      hit.browser_toplevel = 0;
    }
    return hit;
  };

  auto clear_hand_cursor = [&]() {
    if (hand_cursor_window != 0) {
      XUndefineCursor(display, hand_cursor_window);
      hand_cursor_window = 0;
      XFlush(display);
    }
  };

  auto update_hover_cursor = [&]() {
    ChromeMouseHits hits;
    hits.context_menu = context_menu_hit_test();

    bool use_hand_cursor = false;
    Window target_window = 0;
    if (hits.context_menu.menu_visible) {
      use_hand_cursor = hits.context_menu.over_row;
      target_window = hits.context_menu.browser_toplevel;
    } else {
      hits.a26 = a26_hit_test();
      if (hits.a26.control_index >= 0) {
        use_hand_cursor = true;
        target_window = hits.a26.browser_toplevel;
      } else {
        hits.sidebar = sidebar_hit_test();
        use_hand_cursor = hits.sidebar.over_clickable_row;
        target_window = hits.sidebar.browser_toplevel;
      }
    }

    if (use_hand_cursor && target_window != 0 && hand_cursor != None) {
      if (hand_cursor_window != target_window) {
        clear_hand_cursor();
        XDefineCursor(display, target_window, hand_cursor);
        hand_cursor_window = target_window;
        XFlush(display);
      }
    } else {
      clear_hand_cursor();
    }
    return hits;
  };

  unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
  XISetMask(mask, XI_RawButtonPress);
  XISetMask(mask, XI_RawButtonRelease);
  XISetMask(mask, XI_RawMotion);
  if (a26_shell_) {
    // Synthetic XTEST clicks used by Moon/xenv may not surface as raw XI2
    // events on every X server, so also observe their non-raw counterparts.
    XISetMask(mask, XI_ButtonPress);
    XISetMask(mask, XI_ButtonRelease);
    XISetMask(mask, XI_Motion);
  }
  XIEventMask event_mask = {};
  event_mask.deviceid = XIAllMasterDevices;
  event_mask.mask_len = sizeof(mask);
  event_mask.mask = mask;
  XISelectEvents(display, root, &event_mask, 1);
  XFlush(display);

  const int fd = ConnectionNumber(display);
  Time last_button_time = 0;
  int a26_pressed_control = -1;
  bool a26_page_pressed = false;
  int a26_press_root_x = 0;
  int a26_press_root_y = 0;
  bool a26_press_moved = false;
  while (sidebar_mouse_watcher_running_.load()) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    timeval timeout = {};
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;
    const int ready = select(fd + 1, &readfds, nullptr, nullptr, &timeout);
    if (ready <= 0) {
      update_hover_cursor();
      continue;
    }

    while (sidebar_mouse_watcher_running_.load() && XPending(display) > 0) {
      XEvent xevent;
      XNextEvent(display, &xevent);
      if (xevent.xcookie.type != GenericEvent ||
          xevent.xcookie.extension != xi_opcode ||
          !XGetEventData(display, &xevent.xcookie)) {
        continue;
      }

      if (xevent.xcookie.evtype == XI_RawMotion ||
          xevent.xcookie.evtype == XI_Motion) {
        ChromeMouseHits hits = update_hover_cursor();
        if (a26_pressed_control >= 0 || a26_page_pressed) {
          const int dx = hits.a26.root_x - a26_press_root_x;
          const int dy = hits.a26.root_y - a26_press_root_y;
          if ((a26_pressed_control >= 0 &&
               hits.a26.control_index != a26_pressed_control) ||
              dx * dx + dy * dy > 35 * 35) {
            a26_press_moved = true;
          }
        }
        if (hits.context_menu.over_row) {
          CefRefPtr<BrowserWindow> self = this;
          CefPostTask(TID_UI,
                      base::BindOnce(&BrowserWindow::HoverNativeContextMenuRow,
                                     self, hits.context_menu.row_index));
        }
      } else if (xevent.xcookie.evtype == XI_RawButtonPress ||
                 xevent.xcookie.evtype == XI_ButtonPress) {
        int detail = 0;
        Time event_time = 0;
        if (xevent.xcookie.evtype == XI_RawButtonPress) {
          auto* raw = static_cast<XIRawEvent*>(xevent.xcookie.data);
          if (raw) {
            detail = raw->detail;
            event_time = raw->time;
          }
        } else {
          auto* device = static_cast<XIDeviceEvent*>(xevent.xcookie.data);
          if (device) {
            detail = device->detail;
            event_time = device->time;
          }
        }
        if (detail == 1 && event_time != last_button_time) {
          last_button_time = event_time;
          ChromeMouseHits hits = update_hover_cursor();
          if (hits.context_menu.menu_visible) {
            CefRefPtr<BrowserWindow> self = this;
            if (hits.context_menu.over_row) {
              CefPostTask(TID_UI,
                          base::BindOnce(&BrowserWindow::ActivateNativeContextMenuRow,
                                         self, hits.context_menu.row_index));
            } else if (!hits.context_menu.inside_menu) {
              CefPostTask(TID_UI,
                          base::BindOnce(&BrowserWindow::CancelNativeContextMenu,
                                         self));
            }
          } else if (hits.a26.control_index >= 0) {
            // Phone navigation activates on release after gesture
            // classification. Acting on press races Moon's swipe-to-close
            // recognizer and can execute again when Moon forwards the tap.
            a26_pressed_control = hits.a26.control_index;
            a26_press_root_x = hits.a26.root_x;
            a26_press_root_y = hits.a26.root_y;
            a26_press_moved = false;
          } else if (a26_shell_ && hits.a26.browser_toplevel != 0) {
            // A page field can remain focused after Moon's Hide key. Remember
            // an ordinary page tap so the post-DOM-focus state can request the
            // keyboard again even when no new focus-change notification fires.
            a26_page_pressed = true;
            a26_press_root_x = hits.a26.root_x;
            a26_press_root_y = hits.a26.root_y;
            a26_press_moved = false;
          } else if (hits.sidebar.inside_sidebar) {
            CefRefPtr<BrowserWindow> self = this;
            CefPostTask(TID_UI,
                        base::BindOnce(&BrowserWindow::HandleSidebarMouseRowClick,
                                       self, hits.sidebar.row_index));
          }
        }
      } else if (xevent.xcookie.evtype == XI_RawButtonRelease ||
                 xevent.xcookie.evtype == XI_ButtonRelease) {
        int detail = 0;
        if (xevent.xcookie.evtype == XI_RawButtonRelease) {
          auto* raw = static_cast<XIRawEvent*>(xevent.xcookie.data);
          if (raw) {
            detail = raw->detail;
          }
        } else {
          auto* device = static_cast<XIDeviceEvent*>(xevent.xcookie.data);
          if (device) {
            detail = device->detail;
          }
        }
        if (detail == 1 && (a26_pressed_control >= 0 || a26_page_pressed)) {
          const int pressed_control = a26_pressed_control;
          const bool page_pressed = a26_page_pressed;
          ChromeMouseHits hits = update_hover_cursor();
          a26_pressed_control = -1;
          a26_page_pressed = false;
          if (!a26_press_moved && pressed_control >= 0 &&
              hits.a26.control_index == pressed_control) {
            CefRefPtr<BrowserWindow> self = this;
            CefPostTask(
                TID_UI,
                base::BindOnce(&BrowserWindow::HandleA26MouseControl, self,
                               static_cast<size_t>(pressed_control)));
          } else if (!a26_press_moved && page_pressed &&
                     hits.a26.browser_toplevel != 0 &&
                     hits.a26.control_index < 0) {
            CefRefPtr<BrowserWindow> self = this;
            CefPostDelayedTask(
                TID_UI,
                base::BindOnce(&BrowserWindow::SyncA26KeyboardForActivePage,
                               self),
                180);
          }
        }
      }
      XFreeEventData(display, &xevent.xcookie);
    }
  }

  clear_hand_cursor();
  if (hand_cursor != None) {
    XFreeCursor(display, hand_cursor);
  }
  XCloseDisplay(display);
#endif
}

void BrowserWindow::UpdateSidebarMouseBounds() {
  if (!window_ || !sidebar_visible_) {
    sidebar_mouse_width_.store(0);
    sidebar_mouse_height_.store(0);
    sidebar_mouse_row_count_.store(0);
    sidebar_mouse_window_.store(0);
    return;
  }

  const CefRect bounds = window_->GetClientAreaBoundsInScreen();
  const int main_height = std::max(
      0, bounds.height - (show_statusline_ ? kStatusBarHeight : 0));
  sidebar_mouse_screen_x_.store(bounds.x);
  sidebar_mouse_screen_y_.store(bounds.y);
  sidebar_mouse_width_.store(kSidebarContentWidth);
  sidebar_mouse_height_.store(main_height);
  sidebar_mouse_row_count_.store(static_cast<int>(sidebar_rows_.size()));
  sidebar_mouse_window_.store(static_cast<unsigned long>(window_->GetWindowHandle()));
}

void BrowserWindow::UpdateA26MouseBounds() {
  if (!window_ || !a26_shell_ || !a26_navigation_panel_) {
    a26_layout_width_.store(0);
    a26_layout_height_.store(0);
    a26_mouse_window_.store(0);
    return;
  }

  const CefRect bounds = window_->GetBounds();
  a26_layout_width_.store(std::max(1, bounds.width));
  a26_layout_height_.store(std::max(1, bounds.height));
  a26_mouse_window_.store(
      static_cast<unsigned long>(window_->GetWindowHandle()));
}

void BrowserWindow::HandleA26MouseControl(size_t control_index) {
  if (!window_ || !a26_shell_) {
    return;
  }
  switch (control_index) {
    case 0:
      OnButtonPressed(a26_back_button_);
      return;
    case 1:
      OnButtonPressed(a26_forward_button_);
      return;
    case 2: {
      FocusA26UrlFromTouch();
      // The raw event may also have reached Chromium's native page child. Re-ask
      // for URL focus after normal button dispatch has drained.
      CefRefPtr<BrowserWindow> self = this;
      CefPostDelayedTask(
          TID_UI, base::BindOnce(&BrowserWindow::FocusA26UrlFromTouch, self),
          25);
      return;
    }
    case 3:
      OnButtonPressed(a26_reload_button_);
      return;
    case 4:
      OnButtonPressed(a26_tabs_button_);
      return;
    default:
      return;
  }
}

void BrowserWindow::FocusA26UrlFromTouch() {
  if (!window_ || !a26_shell_ || !a26_url_field_) {
    return;
  }
  SetFocusArea(FocusArea::kA26Url);
  a26_url_field_->SelectAll(false);
  RequestA26Keyboard(A26KeyboardPurpose::kUrl);
}

void BrowserWindow::ClearA26ControlDedup(int control_id,
                                        uint64_t generation) {
  if (generation == a26_control_dedup_generation_ &&
      control_id == a26_last_control_id_) {
    a26_last_control_id_ = 0;
  }
}

void BrowserWindow::HandleSidebarMouseRowClick(size_t row_index) {
  if (!window_ || !sidebar_visible_) {
    return;
  }

  if (row_index < sidebar_rows_.size()) {
    const SidebarRowViews& row = sidebar_rows_[row_index];
    if (row.kind == SidebarRowKind::kEntry) {
      ActivateSidebarItem(row.item);
    }
  }
  SetFocusArea(FocusArea::kTabSidebar);
}

void BrowserWindow::UpdateModeIndicator() {
  UpdateStatusBar();
  if (!kModeIndicatorEnabled || !mode_indicator_label_) {
    return;
  }
  if (mode_indicator_overlay_) {
    mode_indicator_overlay_->SetVisible(show_mode_indicator_);
  }
  if (!show_mode_indicator_) {
    return;
  }

  const std::string text = ModeIndicatorText();
  mode_indicator_label_->SetText(text);
  mode_indicator_label_->SetEnabledTextColors(ModeIndicatorColor());
  mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, ModeIndicatorColor());
  mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, ModeIndicatorColor());
  mode_indicator_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, ModeIndicatorColor());
  mode_indicator_label_->SetBackgroundColor(theme::kUserBg);
  mode_indicator_label_->SetState(CEF_BUTTON_STATE_NORMAL);
}

void BrowserWindow::UpdateStatusBar() {
  UpdateA26Chrome();
  if (!status_output_field_ && !status_mode_field_ && !status_url_label_) {
    return;
  }

  const std::string mode = ModeIndicatorText();
  const cef_color_t background = StatusBarBackgroundColor();
  if (status_bar_panel_) {
    status_bar_panel_->SetBackgroundColor(theme::kSidebarBg);
  }
  if (status_sidebar_spacer_panel_) {
    status_sidebar_spacer_panel_->SetBackgroundColor(theme::kSidebarBg);
  }
  if (status_content_panel_) {
    status_content_panel_->SetBackgroundColor(background);
  }
  if (status_border_panel_) {
    status_border_panel_->SetBackgroundColor(SidebarBorderColor());
  }
  if (sidebar_border_overlay_panel_) {
    sidebar_border_overlay_panel_->SetBackgroundColor(SidebarBorderColor());
  }
  if (status_output_field_) {
    status_output_field_->SetText(status_output_text_.empty()
                                      ? ""
                                      : " " + status_output_text_);
    status_output_field_->SetTextColor(theme::kText);
    status_output_field_->SetBackgroundColor(background);
    status_output_field_->SelectRange(CefRange(0, 0));
  }
  if (status_mode_field_) {
    status_mode_field_->SetText(mode);
    status_mode_field_->SetTextColor(ModeIndicatorColor());
    status_mode_field_->SetBackgroundColor(background);
    status_mode_field_->SelectRange(CefRange(0, 0));
  }

  if (!status_url_label_) {
    return;
  }

  std::string url = ActiveTabUrl();
  if (url.empty()) {
    url = "about:blank";
  }

  status_url_label_->SetText(url + "  ");
  status_url_label_->SetEnabledTextColors(theme::kText);
  status_url_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
  status_url_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
  status_url_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
  status_url_label_->SetBackgroundColor(background);
  status_url_label_->SetState(CEF_BUTTON_STATE_NORMAL);
}

void BrowserWindow::UpdateA26Chrome() {
  if (!a26_shell_ || !a26_url_field_) {
    return;
  }

  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!a26_url_editing_) {
    std::string url = ActiveTabUrl();
    if (url.empty()) {
      url = "about:blank";
    }
    if (a26_url_field_->GetText().ToString() != url) {
      a26_url_field_->SetText(url);
    }
    a26_url_field_->SelectRange(CefRange(0, 0));
  }

  std::string title;
  if (!tabs_.empty() && active_index_ < tabs_.size()) {
    title = tabs_[active_index_].title;
  }
  if (title.empty()) {
    title = ActiveTabTitle();
  }
  a26_url_field_->SetAccessibleName(
      title.empty() ? "Address" : "Address - " + Ellipsize(title, 100));

  const Tab* active_tab =
      !tabs_.empty() && active_index_ < tabs_.size() ? &tabs_[active_index_]
                                                     : nullptr;
  const bool loading = active_tab ? active_tab->is_loading : false;
  const bool can_go_back = active_tab ? active_tab->can_go_back : false;
  const bool can_go_forward = active_tab ? active_tab->can_go_forward : false;
  if (a26_back_button_) {
    a26_back_button_->SetEnabled(can_go_back);
  }
  if (a26_forward_button_) {
    a26_forward_button_->SetEnabled(can_go_forward);
  }
  if (a26_reload_button_) {
    a26_reload_button_->SetText(loading ? "Stop" : "Reload");
    a26_reload_button_->SetAccessibleName(loading ? "Stop loading" : "Reload");
    a26_reload_button_->SetEnabled(browser != nullptr);
  }
  if (a26_tabs_button_) {
    const size_t active = tabs_.empty() ? 0 : std::min(active_index_ + 1, tabs_.size());
    a26_tabs_button_->SetText("Tabs " + std::to_string(active) + "/" +
                              std::to_string(tabs_.size()));
    a26_tabs_button_->SetAccessibleName(
        "Activate next tab; active " + std::to_string(active) + " of " +
        std::to_string(tabs_.size()));
    a26_tabs_button_->SetEnabled(!tabs_.empty());
  }
}

void BrowserWindow::SelectA26UrlAfterFocus() {
  if (a26_shell_ && a26_url_focused_ && a26_url_field_) {
    a26_url_field_->SelectAll(false);
  }
}

void BrowserWindow::CommitA26Url() {
  if (!a26_shell_ || !a26_url_field_) {
    return;
  }

  const std::string text = Trim(a26_url_field_->GetText().ToString());
  const std::string url = ResolveUrlOrSearch(text);
  RecordOpenHistory(text);
  last_tab_close_placeholder_ = false;
  if (active_index_ < tabs_.size()) {
    SetTabUrl(tabs_[active_index_], url);
    tabs_[active_index_].focused_editable_node = false;
    tabs_[active_index_].focused_editable_purpose = "text";
  }
  a26_url_editing_ = false;
  a26_url_focused_ = false;
  RequestA26Keyboard(A26KeyboardPurpose::kHide);
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser();
      browser && browser->GetMainFrame()) {
    browser->GetMainFrame()->LoadURL(url);
  }
  SaveState();
  RefreshSidebar();
  FinishA26ChromeAction();
  UpdateA26Chrome();
}

void BrowserWindow::CancelA26Url() {
  if (!a26_shell_) {
    return;
  }
  a26_url_editing_ = false;
  a26_url_focused_ = false;
  UpdateA26Chrome();
  FinishA26ChromeAction();
}

void BrowserWindow::FinishA26ChromeAction() {
  if (!a26_shell_) {
    return;
  }
  a26_url_editing_ = false;
  a26_url_focused_ = false;
  RequestA26Keyboard(A26KeyboardPurpose::kHide);
  SetFocusArea(FocusArea::kWebView);
}

void BrowserWindow::RequestA26Keyboard(A26KeyboardPurpose purpose) {
  if (a26_shell_ && a26_keyboard_) {
    a26_keyboard_->Request(purpose);
  }
}

void BrowserWindow::SyncA26KeyboardForActivePage() {
  if (!a26_shell_ || a26_url_focused_) {
    return;
  }
  const Tab* tab = ActiveTab();
  if (!tab || !tab->focused_editable_node) {
    RequestA26Keyboard(A26KeyboardPurpose::kHide);
    return;
  }

  A26KeyboardPurpose purpose = A26KeyboardPurpose::kText;
  if (tab->focused_editable_purpose == "password") {
    purpose = A26KeyboardPurpose::kPassword;
  } else if (tab->focused_editable_purpose == "search") {
    purpose = A26KeyboardPurpose::kSearch;
  } else if (tab->focused_editable_purpose == "url") {
    purpose = A26KeyboardPurpose::kUrl;
  } else if (tab->focused_editable_purpose == "number") {
    purpose = A26KeyboardPurpose::kNumber;
  }
  website_mode_ = vim::Mode::kInsert;
  RequestA26Keyboard(purpose);
}

void BrowserWindow::SetStatusOutput(std::string message, int timeout_ms) {
  status_output_text_ = std::move(message);
  const uint64_t generation = ++status_output_generation_;
  UpdateStatusBar();
  Layout();
  if (timeout_ms <= 0 || status_output_text_.empty() || !window_) {
    return;
  }
  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(&BrowserWindow::ClearStatusOutputForGeneration,
                                    self, generation),
                     timeout_ms);
}

void BrowserWindow::ClearStatusOutputForGeneration(uint64_t generation) {
  if (generation != status_output_generation_) {
    return;
  }
  status_output_text_.clear();
  UpdateStatusBar();
  Layout();
}

void BrowserWindow::SetShowModeIndicator(bool visible) {
  show_mode_indicator_ = visible;
  SaveState();
  UpdateModeIndicator();
  Layout();
}

void BrowserWindow::UpdateFpsIndicator() {
  if (!kModeIndicatorEnabled || !fps_indicator_label_) {
    return;
  }
  if (fps_indicator_overlay_) {
    fps_indicator_overlay_->SetVisible(show_fps_indicator_);
  }
  if (!show_fps_indicator_) {
    return;
  }

  bool has_sample = false;
  double fps = 0.0;
  if (Tab* tab = ActiveTab(); tab && tab->client) {
    has_sample = tab->client->fps_has_sample();
    fps = tab->client->current_fps();
  }
  const std::string text = has_sample
                               ? "fps " + std::to_string(static_cast<int>(std::round(fps)))
                               : "fps --";
  fps_indicator_label_->SetText(text);
  fps_indicator_label_->SetEnabledTextColors(theme::kText);
  fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
  fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
  fps_indicator_label_->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
  fps_indicator_label_->SetBackgroundColor(theme::kUserBg);
  fps_indicator_label_->SetState(CEF_BUTTON_STATE_NORMAL);
}

void BrowserWindow::ScheduleFpsIndicatorUpdate() {
  if (fps_update_scheduled_ || !window_) {
    return;
  }
  fps_update_scheduled_ = true;
  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(&BrowserWindow::OnFpsIndicatorUpdateTimer,
                                    self),
                     500);
}

void BrowserWindow::OnFpsIndicatorUpdateTimer() {
  fps_update_scheduled_ = false;
  if (!window_) {
    return;
  }
  // Reuse the existing lightweight chrome-status tick to keep sidebar audio
  // indicators in sync even when the optional FPS overlay is hidden.
  RefreshAudibleTabs();
  UpdateFpsIndicator();
  ScheduleFpsIndicatorUpdate();
}

void BrowserWindow::SetShowFpsIndicator(bool visible) {
  show_fps_indicator_ = visible;
  SaveState();
  UpdateFpsIndicator();
  Layout();
}

void BrowserWindow::SetShowStatusLine(bool visible) {
  show_statusline_ = visible;
  SaveState();
  UpdateStatusBar();
  RefreshSidebar();
  Layout();
}

void BrowserWindow::SetShaderEnabled(bool enabled) {
  shader_enabled_ = enabled;
  SaveState();
  BroadcastShaderState();
}

void BrowserWindow::BroadcastShaderState() {
  for (Tab& tab : tabs_) {
    if (!tab.client || !tab.client->browser()) {
      continue;
    }
    CefRefPtr<CefBrowser> browser = tab.client->browser();
    std::vector<CefString> frame_ids;
    browser->GetFrameIdentifiers(frame_ids);
    if (frame_ids.empty() && browser->GetMainFrame()) {
      frame_ids.push_back(browser->GetMainFrame()->GetIdentifier());
    }
    for (const CefString& frame_id : frame_ids) {
      CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
      if (!frame) {
        continue;
      }
      frame->ExecuteJavaScript(kShaderRefreshScript, frame->GetURL(), 0);
    }
  }
}

void BrowserWindow::SaveState() const {
  AppState state;
  // Named request-context tabs are intentionally transient shell state. Their
  // request-context data remains persistent on disk, but excluding the tabs
  // from this URL-only state format makes it impossible to restore one in the
  // default context after restart.
  state.active_index = 0;
  state.show_mode_indicator = show_mode_indicator_;
  state.show_fps_indicator = show_fps_indicator_;
  state.show_statusline = show_statusline_;
  state.shader_enabled = shader_enabled_;
  state.open_history = open_history_;
  state.search_history = search_history_;
  state.media_permission_grants.insert(media_permission_grants_.begin(),
                                       media_permission_grants_.end());
  state.media_permission_denials.insert(media_permission_denials_.begin(),
                                        media_permission_denials_.end());
  state.sidebar_folder_id = current_sidebar_folder_id_;
  state.next_sidebar_folder_id = next_folder_id_;
  state.sidebar_folders.reserve(sidebar_folders_.size());
  for (const SidebarFolder& folder : sidebar_folders_) {
    state.sidebar_folders.push_back({folder.id, folder.parent_id,
                                     folder.sort_order, folder.name,
                                     folder.pinned});
  }
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const Tab& tab = tabs_[i];
    if (tab.context.empty() && !tab.url.empty()) {
      if (i <= active_index_) {
        state.active_index = state.tabs.size();
      }
      state.tabs.push_back(tab.url);
      state.tab_folder_ids.push_back(tab.folder_id);
      state.tab_sort_orders.push_back(tab.sidebar_sort_order);
      state.tab_pinned.push_back(tab.pinned);
    }
  }
  if (!state.tabs.empty() && state.active_index >= state.tabs.size()) {
    state.active_index = state.tabs.size() - 1;
  }
  WriteAppState(state_path_, state);
}

std::string BrowserWindow::ModeIndicatorText() const {
  if (IsSidebarSearchMode()) {
    return "SIDEBAR";
  }
  if (focus_area_ == FocusArea::kCommandLine || mode_ != Mode::kNormal) {
    return command_vim_.mode == vim::Mode::kNormal ? "CMD-N" : "CMD-I";
  }
  if (focus_area_ == FocusArea::kTabSidebar) {
    return "SIDEBAR";
  }
  if (native_hints_active_) {
    return "HINT";
  }
  if (focus_area_ == FocusArea::kDevTools) {
    return devtools_mode_ == vim::Mode::kInsert ? "DEV-I" : "DEV-N";
  }

  switch (website_mode_) {
    case vim::Mode::kWebsiteNormal:
      return "WEBSITE";
    case vim::Mode::kNormal:
      return "NORMAL";
    case vim::Mode::kInsert:
      return "INSERT";
    case vim::Mode::kVisual:
      return "VISUAL";
  }
  return "WEBSITE";
}

cef_color_t BrowserWindow::ModeIndicatorColor() const {
  if (IsSidebarSearchMode()) {
    return theme::kBorderFocused;
  }
  if (focus_area_ == FocusArea::kCommandLine || mode_ != Mode::kNormal) {
    return theme::kCommand;
  }
  if (focus_area_ == FocusArea::kTabSidebar) {
    return theme::kBorderFocused;
  }
  if (native_hints_active_) {
    return theme::kAccent;
  }
  if (focus_area_ == FocusArea::kDevTools) {
    return devtools_mode_ == vim::Mode::kInsert ? theme::kVimInsert
                                                : theme::kVimNormal;
  }

  switch (website_mode_) {
    case vim::Mode::kWebsiteNormal:
      return theme::kVimNormal;
    case vim::Mode::kNormal:
      return theme::kVimNormal;
    case vim::Mode::kInsert:
      return theme::kVimInsert;
    case vim::Mode::kVisual:
      return theme::kVimVisual;
  }
  return theme::kVimNormal;
}

cef_color_t BrowserWindow::SidebarBorderColor() const {
  return focus_area_ == FocusArea::kTabSidebar ? theme::kAccent
                                               : theme::kBorderUnfocused;
}

cef_color_t BrowserWindow::StatusBarBackgroundColor() const {
  if (mode_ == Mode::kNormal && focus_area_ == FocusArea::kWebView) {
    if (website_mode_ == vim::Mode::kInsert) {
      return theme::kAccent;
    }
    if (website_mode_ == vim::Mode::kNormal) {
      return theme::kSidebarSelBg;
    }
  }
  if (mode_ == Mode::kNormal && focus_area_ == FocusArea::kDevTools) {
    return devtools_mode_ == vim::Mode::kInsert ? theme::kAccent
                                                : theme::kSidebarSelBg;
  }
  return theme::kSidebarBg;
}

Tab* BrowserWindow::ActiveTab() {
  if (tabs_.empty() || active_index_ >= tabs_.size()) {
    return nullptr;
  }
  return &tabs_[active_index_];
}

}  // namespace vimbrowser
