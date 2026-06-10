#include "browser_window.h"
#include "browser_window_internal.h"

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

}  // namespace

BrowserWindow::BrowserWindow(std::vector<std::string> initial_urls,
                             size_t active_index,
                             bool show_mode_indicator,
                             bool show_fps_indicator,
                             bool show_statusline,
                             bool shader_enabled,
                             std::string state_path)
    : initial_urls_(std::move(initial_urls)),
      state_path_(std::move(state_path)),
      initial_active_index_(active_index),
      show_mode_indicator_(show_mode_indicator),
      show_fps_indicator_(show_fps_indicator),
      show_statusline_(show_statusline),
      shader_enabled_(shader_enabled) {
  const AppState state = ReadAppState(state_path_);
  open_history_ = state.open_history;
  search_history_ = state.search_history;
  if (initial_urls_.empty()) {
    initial_urls_.push_back(ResolveUrlOrSearch(""));
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
}

void BrowserWindow::OnClientBeforeClose(BrowserClient*) {
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
  // Native hints are bound to the renderer document that collected their
  // candidates. A main-frame navigation can destroy that document before Blink's
  // console-based "hints stopped" signal reaches the browser process, so clear
  // the browser-side latch at document boundaries too.
  StopPageNativeHintsForClient(client);
  UpdateClientUrl(client, url, true);
}

void BrowserWindow::OnClientLoadEnd(BrowserClient* client) {
  StopPageNativeHintsForClient(client);
}

void BrowserWindow::OnClientAddressChange(BrowserClient* client,
                                          const std::string& url) {
  UpdateClientUrl(client, url, false);
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
      }
      tab.has_scroll_target = false;
      tab.scroll_target_is_pdf_viewport = false;
      if (url != "about:blank") {
        last_tab_close_placeholder_ = false;
      }
      if (i == active_index_) {
        UpdateStatusBar();
      }
      SaveState();
      if (tabs_.size() <= kSidebarMaxRenderedRows &&
          sidebar_rows_.size() == tabs_.size() && sidebar_spacer_) {
        RefreshSidebarRow(i);
      } else if (tabs_.size() > kSidebarMaxRenderedRows && sidebar_spacer_) {
        const auto [render_start, render_count] =
            SidebarRenderedRange(tabs_.size(), active_index_);
        if (i >= render_start && i < render_start + render_count) {
          ScheduleSidebarRefresh();
        }
      } else {
        RefreshSidebar();
      }
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
    bool active_tab_focused_editable = false;
    for (Tab& tab : tabs_) {
      if (tab.client.get() == client) {
        tab.focused_editable_node = focused_editable;
        active_tab_focused_editable = focused_editable && ActiveTab() == &tab;
        break;
      }
    }
    if (active_tab_focused_editable && native_hints_active_ &&
        mode_ == Mode::kNormal && focus_area_ == FocusArea::kWebView) {
      // Only native hints turn focused page text controls into vimbrowser insert
      // mode.  Ordinary mouse clicks, tab traversal, autofocus, and page script
      // focus must leave the website vim mode alone so normal-mode keys remain
      // under vimbrowser's control until the user explicitly enters insert mode.
      website_mode_ = vim::Mode::kInsert;
      ResetWebsitePendingKeys();
      suppress_next_website_char_.reset();
      UpdateModeIndicator();
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
  const bool source_owned = std::any_of(
      tabs_.begin(), tabs_.end(),
      [client](const Tab& tab) { return tab.client.get() == client; });
  if (!source_owned) {
    // Unknown popups should never escape into CEF-owned top-level windows.
    return true;
  }

  const bool hint_open_tab = native_hints_active_ && ActiveTab() &&
                             ActiveTab()->client.get() == client;
  const uint64_t opener_tab_id = hint_open_tab ? ActiveTab()->id : 0;

  if (!popup_client) {
    if (target_url.empty()) {
      return true;
    }
    native_hints_active_ = false;
    if (hint_open_tab) {
      AddTabAfterActive(target_url, activate);
    } else {
      AddTab(target_url, activate);
    }
    UpdateModeIndicator();
    return true;
  }

  pending_popups_.push_back(
      {popup_client, popup_id, target_url, activate, opener_tab_id,
       hint_open_tab});
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
  if (insert_after_opener) {
    if (std::optional<size_t> opener_index = FindTabIndexById(opener_tab_id)) {
      insert_index = *opener_index + 1;
    } else if (active_index_ < tabs_.size()) {
      insert_index = active_index_ + 1;
    }
  }
  InsertPopupTab(popup_browser_view, retained_popup_client, std::move(url),
                 insert_index, activate);
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

void BrowserWindow::ShowDevToolsForClient(BrowserClient* client) {
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
                                   CefPoint());

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
  ipc_server_ = std::make_unique<IpcServer>(this, IpcSocketPathForStatePath(state_path_));
  ipc_server_->Start();
  BuildChrome();
  const bool lazy_restore_background_tabs =
      initial_urls_.size() >= kLazyRestoreBackgroundTabThreshold;
  bulk_tab_update_ = true;
  for (size_t i = 0; i < initial_urls_.size(); ++i) {
    const bool activate = i == initial_active_index_;
    InsertTab(initial_urls_[i], tabs_.size(), activate,
              lazy_restore_background_tabs && !activate);
  }
  bulk_tab_update_ = false;
  RefreshSidebar();

  window_->CenterWindow(CefSize(1200, 800));
  window_->Show();
  Layout();
  SetFocusArea(FocusArea::kWebView);
  ScheduleFpsIndicatorUpdate();
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
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  ++active_browser_sync_generation_;
  ++state_save_generation_;
  SaveState();
  if (ipc_server_) {
    ipc_server_->Stop();
    ipc_server_.reset();
  }
  tabs_.clear();
  fps_indicator_overlay_ = nullptr;
  mode_indicator_overlay_ = nullptr;
  autocomplete_overlay_ = nullptr;
  command_separator_overlay_ = nullptr;
  command_overlay_ = nullptr;
  fps_indicator_label_ = nullptr;
  fps_indicator_panel_ = nullptr;
  mode_indicator_label_ = nullptr;
  mode_indicator_panel_ = nullptr;
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
  status_url_label_ = nullptr;
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
  CefQuitMessageLoop();
}

void BrowserWindow::OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                                          const CefRect& new_bounds) {
  Layout();
}

bool BrowserWindow::CanClose(CefRefPtr<CefWindow> window) {
  if (window_close_allowed_ || AllTabBrowsersClosed()) {
    window_close_allowed_ = true;
    return true;
  }

  if (!window_close_pending_) {
    window_close_pending_ = true;
    ++active_browser_sync_generation_;
    ++state_save_generation_;
    SaveState();
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

bool BrowserWindow::OnKeyEvent(CefRefPtr<CefWindow> window,
                               const CefKeyEvent& event) {
  if (forwarding_key_to_page_ && (IsEscapeKey(event) || IsSpaceKey(event))) {
    return false;
  }
  if (mode_ != Mode::kNormal && IsCharEvent(event) && PlainKeyChar(event) == ':') {
    return true;
  }
  if (mode_ != Mode::kNormal) {
    return HandleCommandModeKey(event);
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
  if (forwarding_key_to_page_) {
    return false;
  }
  if (mode_ != Mode::kNormal && command_vim_.mode == vim::Mode::kInsert) {
    if (command_id == kAcceleratorCommandTab ||
        command_id == kAcceleratorCommandBacktab) {
      return CycleCommandAutocomplete(command_id == kAcceleratorCommandBacktab ? -1 : 1);
    }
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
      ActivateRelative(1);
      return true;
    }
    if (command_id == kAcceleratorTabPrevious) {
      ActivateRelative(-1);
      return true;
    }
  }
  return false;
}

bool BrowserWindow::HandleBrowserKeyEvent(const CefKeyEvent& event) {
  if (forwarding_key_to_devtools_) {
    return false;
  }
  if (forwarding_key_to_page_ && (IsEscapeKey(event) || IsSpaceKey(event))) {
    return false;
  }
  if (mode_ != Mode::kNormal) {
    return HandleCommandModeKey(event);
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

  if (PlainKeyChar(event) == ':') {
    BeginCommandText(":");
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar && IsPlain(event)) {
    switch (PlainKeyChar(event)) {
      case 'j':
        ResetWebsitePendingKeys();
        ActivateRelative(1);
        return true;
      case 'k':
        ResetWebsitePendingKeys();
        ActivateRelative(-1);
        return true;
    }
  }

  if (HandleWebsiteCommandKey(event)) {
    return true;
  }

  if (IsPlain(event) && event.windows_key_code == 'O') {
    if (focus_area_ != FocusArea::kTabSidebar) {
      return false;
    }
    BeginCommand(shift ? Mode::kCommandOpenNext : Mode::kCommandOpenCurrent);
    return true;
  }

  if (shift && event.windows_key_code == 'J') {
    if (focus_area_ != FocusArea::kTabSidebar) {
      return false;
    }
    ActivateRelative(1);
    return true;
  }

  if (shift && event.windows_key_code == 'K') {
    if (focus_area_ != FocusArea::kTabSidebar) {
      return false;
    }
    ActivateRelative(-1);
    return true;
  }

  if (focus_area_ == FocusArea::kTabSidebar && IsPlain(event)) {
    switch (PlainKeyChar(event)) {
      case 'd':
        CloseActiveTab(CloseFocus::kNextTab);
        return true;
      case 'D':
        CloseActiveTab(CloseFocus::kPreviousTab);
        return true;
      case 'u':
        UndoCloseTab();
        return true;
      case 'c':
        CloneActiveTab();
        return true;
      case '[':
      case 'h':
        ActivateRelativeAudible(-1);
        return true;
      case ']':
      case 'l':
        ActivateRelativeAudible(1);
        return true;
      case 'e':
        MoveActiveTab(-1);
        return true;
      case 'E':
        MoveActiveTab(1);
        return true;
    }
  }

  if (focus_area_ == FocusArea::kTabSidebar) {
    return true;
  }

  return false;
}

void BrowserWindow::OnAfterUserAction(CefRefPtr<CefTextfield> textfield) {
  if ((textfield != command_field_ &&
       (!textfield || textfield->GetID() != kCommandFieldId)) ||
      mode_ == Mode::kNormal || command_vim_.mode != vim::Mode::kInsert ||
      suppress_next_char_event_) {
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
  if (InIdRange(id, kSidebarRowBaseId, 1000)) {
    const size_t row_index = static_cast<size_t>(id - kSidebarRowBaseId);
    if (row_index < sidebar_rows_.size()) {
      const size_t index = sidebar_rows_[row_index].tab_index;
      ActivateTab(index);
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
  // The mode indicator is implemented as a CefLabelButton because CEF exposes
  // centering for labels/buttons but not textfields. It is display-only.
}

bool BrowserWindow::OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                               const CefKeyEvent& event) {
  if (textfield != command_field_ || mode_ == Mode::kNormal) {
    return false;
  }
  return HandleCommandModeKey(event);
}

CefSize BrowserWindow::GetPreferredSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  if (id == kSidebarPanelId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kSidebarContentPanelId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kSidebarBorderPanelId) {
    return CefSize(sidebar_visible_ ? kSidebarBorderWidth : 0, 1);
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
    return CefSize(1200, kCommandHeight + 1);
  }
  if (id == kCommandContentPanelId) {
    return CefSize(1200, kCommandHeight);
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
  if (InIdRange(id, kSidebarRowBaseId, 1000)) {
    return CefSize(kSidebarContentWidth, kSidebarRowHeight);
  }
  if (id == kSidebarSpacerId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kCommandFieldId) {
    return CefSize(1200, kCommandHeight);
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
  return CefSize(1200, 800);
}

CefSize BrowserWindow::GetMinimumSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  if (id == kSidebarPanelId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kSidebarContentPanelId) {
    return CefSize(kSidebarContentWidth, 1);
  }
  if (id == kSidebarBorderPanelId) {
    return CefSize(sidebar_visible_ ? kSidebarBorderWidth : 0, 1);
  }
  if (id == kCommandPanelId) {
    return CefSize(1, kCommandHeight + 1);
  }
  if (id == kCommandContentPanelId) {
    return CefSize(1, kCommandHeight);
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
  return CefSize();
}

CefSize BrowserWindow::GetMaximumSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  if (id == kCommandPanelId) {
    return CefSize(0, kCommandHeight + 1);
  }
  if (id == kCommandContentPanelId) {
    return CefSize(0, kCommandHeight);
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
  if (id == kModeIndicatorPanelId || id == kModeIndicatorFieldId ||
      id == kFpsIndicatorPanelId || id == kFpsIndicatorFieldId) {
    return CefSize(kModeIndicatorWidth, kModeIndicatorHeight);
  }
  return CefSize();
}

void BrowserWindow::OnThemeChanged(CefRefPtr<CefView> view) {
  RestyleView(view);
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

  const CefRect bounds = window_->GetBounds();
  const int width = std::max(1, bounds.width);
  const int height = std::max(1, bounds.height);
  const int command_total_height = kCommandHeight + 1;
  const int autocomplete_height = CommandAutocompleteHeight();
  const int autocomplete_width = std::min(width, std::max(1, CommandAutocompleteWidth()));
  const int main_height =
      std::max(1, height - (show_statusline_ ? kStatusBarHeight : 0));
  const int sidebar_content_width = sidebar_visible_ ? kSidebarContentWidth : 0;
  const int sidebar_border_width = sidebar_visible_ ? kSidebarBorderWidth : 0;
  const int content_x = sidebar_visible_ ? kSidebarWidth : 0;
  if (command_overlay_) {
    command_overlay_->SetVisible(mode_ != Mode::kNormal);
  }
  if (command_separator_overlay_) {
    command_separator_overlay_->SetVisible(mode_ != Mode::kNormal);
  }
  if (autocomplete_overlay_) {
    autocomplete_overlay_->SetVisible(mode_ != Mode::kNormal &&
                                      command_autocomplete_.active &&
                                      !command_autocomplete_.matches.empty());
  }
  sidebar_panel_->SetVisible(sidebar_visible_);

  root_panel_->SetBounds(CefRect(0, 0, width, height));
  RestyleView(root_panel_);
  RestyleView(main_panel_);
  RestyleView(sidebar_panel_);
  RestyleView(sidebar_content_panel_);
  RestyleView(sidebar_spacer_);
  RestyleView(sidebar_border_panel_);
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
  RestyleView(mode_indicator_panel_);
  RestyleView(mode_indicator_label_);
  RestyleView(fps_indicator_panel_);
  RestyleView(fps_indicator_label_);
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
  command_panel_->SetSize(CefSize(width, kCommandHeight));
  command_separator_panel_->SetSize(CefSize(width, 1));
  command_content_panel_->SetSize(CefSize(width, kCommandHeight));
  command_separator_panel_->SetBounds(CefRect(0, 0, width, 1));
  command_content_panel_->SetBounds(CefRect(0, 0, width, kCommandHeight));
  if (command_overlay_) {
    command_overlay_->SetBounds(CefRect(0, std::max(0, height - kCommandHeight),
                                        width, kCommandHeight));
  }
  if (command_separator_overlay_) {
    command_separator_overlay_->SetBounds(
        CefRect(0, std::max(0, height - command_total_height), width, 1));
  }
  if (autocomplete_panel_ && autocomplete_overlay_) {
    autocomplete_panel_->SetSize(CefSize(autocomplete_width, std::max(1, autocomplete_height)));
    autocomplete_overlay_->SetBounds(
        CefRect(0, std::max(0, height - command_total_height - autocomplete_height),
                autocomplete_width, std::max(1, autocomplete_height)));
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
    command_field_->SetBounds(CefRect(kCommandTextInsetX, 0,
                                      std::max(1, width - kCommandTextInsetX),
                                      kCommandHeight));
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

  if (tabs_.size() <= kSidebarMaxRenderedRows &&
      sidebar_rows_.size() == tabs_.size() && sidebar_spacer_) {
    for (size_t i = 0; i < tabs_.size(); ++i) {
      RefreshSidebarRow(i);
    }
    return false;
  }

  const auto [render_start, render_count] =
      SidebarRenderedRange(tabs_.size(), active_index_);
  if (tabs_.size() > kSidebarMaxRenderedRows && sidebar_spacer_ &&
      sidebar_rows_.size() == render_count) {
    for (size_t row_index = 0; row_index < render_count; ++row_index) {
      const size_t i = render_start + row_index;
      SidebarRowViews& row_views = sidebar_rows_[row_index];
      row_views.tab_index = i;
      CefRefPtr<CefTextfield> row = row_views.row;
      if (!row) {
        continue;
      }
      const bool active = i == active_index_;
      const std::string text = SidebarTextForTab(i, tabs_[i].url, active,
                                                tabs_[i].audible);
      const cef_color_t row_bg =
          active ? theme::kSidebarSelBg : theme::kSidebarBg;
      const cef_color_t row_text = theme::kText;
      bool text_changed = false;
      if (row_views.text != text) {
        row->SetText(text);
        row->SelectRange(CefRange(0, 0));
        row_views.text = text;
        text_changed = true;
      }
      if (text_changed || row_views.text_color != row_text ||
          row_views.background_color != row_bg) {
        StyleSidebarRow(row, i, active, tabs_[i].audible, row_bg);
        row_views.text_color = row_text;
        row_views.background_color = row_bg;
      }
    }
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

  for (size_t row_index = 0; row_index < render_count; ++row_index) {
    const size_t i = render_start + row_index;
    const bool active = i == active_index_;
    const std::string text = SidebarTextForTab(i, tabs_[i].url, active,
                                               tabs_[i].audible);

    const cef_color_t row_bg = active ? theme::kSidebarSelBg : theme::kSidebarBg;
    const cef_color_t row_text = theme::kText;
    CefRefPtr<CefTextfield> row = CefTextfield::CreateTextfield(this);
    row->SetText(text);
    row->SelectRange(CefRange(0, 0));
    row->SetID(kSidebarRowBaseId + static_cast<int>(row_index));
    StyleSidebarRow(row, i, active, tabs_[i].audible, row_bg);
    sidebar_content_panel_->AddChildView(row);
    sidebar_rows_.push_back({row, i, text, row_text, row_bg});
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
  if (index >= tabs_.size()) {
    return;
  }
  size_t row_index = index;
  if (sidebar_rows_.size() != tabs_.size() ||
      row_index >= sidebar_rows_.size() ||
      sidebar_rows_[row_index].tab_index != index) {
    row_index = sidebar_rows_.size();
    for (size_t i = 0; i < sidebar_rows_.size(); ++i) {
      if (sidebar_rows_[i].tab_index == index) {
        row_index = i;
        break;
      }
    }
  }
  if (row_index >= sidebar_rows_.size()) {
    return;
  }
  CefRefPtr<CefTextfield> row = sidebar_rows_[row_index].row;
  if (!row) {
    return;
  }
  const bool active = index == active_index_;
  const std::string text = SidebarTextForTab(index, tabs_[index].url, active,
                                            tabs_[index].audible);
  const cef_color_t row_bg = active ? theme::kSidebarSelBg : theme::kSidebarBg;
  const cef_color_t row_text = theme::kText;
  SidebarRowViews& row_views = sidebar_rows_[row_index];
  bool text_changed = false;
  if (row_views.text != text) {
    row->SetText(text);
    row->SelectRange(CefRange(0, 0));
    row_views.text = text;
    text_changed = true;
  }
  if (text_changed || row_views.text_color != row_text ||
      row_views.background_color != row_bg) {
    StyleSidebarRow(row, index, active, tabs_[index].audible, row_bg);
    row_views.text_color = row_text;
    row_views.background_color = row_bg;
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
  suppress_next_devtools_char_.reset();
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
        website_mode_ = (event.modifiers & EVENTFLAG_SHIFT_DOWN)
                            ? vim::Mode::kWebsiteNormal
                            : vim::Mode::kNormal;
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

    if (website_mode_ == vim::Mode::kInsert &&
        ShouldForwardFocusedEditableKey(event, PageHasFocusedEditable(event))) {
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

      if (std::optional<bool> shortcut = HandlePageShortcut(event, true)) {
        return *shortcut;
      }

      if (StartNativeHints(event)) {
        return true;
      }

      if (IsPlain(event) && event.windows_key_code == 'O') {
        BeginCommand(event.modifiers & EVENTFLAG_SHIFT_DOWN ? Mode::kCommandOpenNext
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
        if (std::optional<bool> shortcut = HandlePageShortcut(event, true)) {
          return *shortcut;
        }
      }

      if (website_mode_ == vim::Mode::kNormal && StartNativeHints(event)) {
        return true;
      }

      if (website_mode_ == vim::Mode::kNormal && IsPlain(event) &&
          event.windows_key_code == 'O') {
        BeginCommand(event.modifiers & EVENTFLAG_SHIFT_DOWN ? Mode::kCommandOpenNext
                                                            : Mode::kCommandOpenCurrent);
        return true;
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

  if (tab->client->browser()->IsLoading()) {
    // Starting hints while a reload/navigation is replacing the document leaves
    // us with labels owned by a dying renderer frame. Consume the hint command
    // but keep the shell in normal/website mode; the user can press f again once
    // loading completes.
    ResetWebsitePendingKeys();
    return true;
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
    if (key == 'g') { ScrollActivePageToTop(); return true; }
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

  if (std::optional<bool> shortcut = HandlePageShortcut(event, false)) {
    return *shortcut;
  }

  switch (key) {
    case 'j': ScrollActivePageBy(kLineScrollPx); return true;
    case 'k': ScrollActivePageBy(-kLineScrollPx); return true;
    case 'G': ScrollActivePageToBottom(); return true;
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
    case 'J': ActivateRelative(1); return true;
    case 'K': ActivateRelative(-1); return true;
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
  } else if (id == kSidebarBorderPanelId) {
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
  }
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
  state.active_index = active_index_;
  state.show_mode_indicator = show_mode_indicator_;
  state.show_fps_indicator = show_fps_indicator_;
  state.show_statusline = show_statusline_;
  state.shader_enabled = shader_enabled_;
  state.open_history = open_history_;
  state.search_history = search_history_;
  for (const Tab& tab : tabs_) {
    if (!tab.url.empty()) {
      state.tabs.push_back(tab.url);
    }
  }
  if (!state.tabs.empty() && state.active_index >= state.tabs.size()) {
    state.active_index = state.tabs.size() - 1;
  }
  WriteAppState(state_path_, state);
}

std::string BrowserWindow::ModeIndicatorText() const {
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
