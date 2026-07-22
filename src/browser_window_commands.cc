#include "browser_window.h"
#include "browser_window_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.h"
#include "include/cef_browser.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/views/cef_textfield.h"
#include "include/wrapper/cef_closure_task.h"
#include "musescore_downloader.h"
#include "theme.h"

namespace vimbrowser {

namespace {

// gg/G enters through the same CEF wheel/compositor path as j/k and the Ctrl
// scroll keys, targeting the same current scroll container. The only extra bit
// asks the existing CEF gesture bridge to dispatch this edge jump immediately
// instead of feeding it into the smooth-scroll accumulator/timer.
constexpr int kScrollToEdgePx = 10'000'000;

CefMouseEvent ScrollTargetMouseEvent(Tab *tab, CefRefPtr<CefWindow> window) {
  CefMouseEvent event;
  event.modifiers = 0;
  if (tab && tab->has_scroll_target) {
    event.x = tab->scroll_target_x;
    event.y = tab->scroll_target_y;
    event.modifiers |= kVimbrowserHintScrollTargetCefModifier;
    if (!tab->scroll_target_is_page) {
      event.modifiers |= kVimbrowserScrollTargetElementCefModifier;
    }
  } else if (tab && tab->view) {
    const CefRect bounds = tab->view->GetBounds();
    event.x = std::max(1, bounds.width / 2);
    event.y = std::max(1, bounds.height / 2);
  } else if (window) {
    const CefRect bounds = window->GetBounds();
    event.x = std::max(1, bounds.width / 2);
    event.y = std::max(1, bounds.height / 2);
  }
  return event;
}

void SendScrollWheel(CefRefPtr<CefBrowser> browser,
                     const CefMouseEvent& event,
                     int dx,
                     int dy) {
  // CEF/Chromium wheel deltas use the opposite sign from content-space motion.
  browser->GetHost()->SendMouseWheelEvent(event, -dx, -dy);
}

void SendScrollWheel(CefRefPtr<CefBrowser> browser,
                     const CefMouseEvent& event,
                     int dy) {
  SendScrollWheel(browser, event, 0, dy);
}

void ExecuteJavaScriptInAllFrames(CefRefPtr<CefBrowser> browser,
                                  const std::string &script) {
  if (!browser) {
    return;
  }

  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);
  if (frame_ids.empty() && browser->GetMainFrame()) {
    frame_ids.push_back(browser->GetMainFrame()->GetIdentifier());
  }

  for (const CefString &frame_id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
    if (!frame || !frame->IsValid()) {
      continue;
    }
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
  }
}

struct MuseScoreMetadata {
  std::string title;
  std::vector<std::string> urls;
};

bool IsMuseScoreUrl(const std::string &url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return false;
  }
  const std::string host = ToLowerAscii(CefString(&parts.host).ToString());
  return host == "musescore.com" ||
         (host.size() > 14 && host.ends_with(".musescore.com"));
}

bool ParseMuseScoreMetadata(const std::string &response,
                            MuseScoreMetadata *metadata, std::string *error) {
  if (!metadata || !error) {
    return false;
  }
  if (response.starts_with("ERR ")) {
    *error = Trim(response.substr(4));
    return false;
  }

  CefRefPtr<CefValue> outer =
      CefParseJSON(response.data(), response.size(), JSON_PARSER_RFC);
  if (!outer || outer->GetType() != VTYPE_DICTIONARY) {
    *error = "invalid renderer response";
    return false;
  }
  CefRefPtr<CefDictionaryValue> outer_dict = outer->GetDictionary();
  if (!outer_dict || outer_dict->GetType("ok") != VTYPE_BOOL ||
      !outer_dict->GetBool("ok")) {
    *error = outer_dict && outer_dict->GetType("error") == VTYPE_STRING
                 ? outer_dict->GetString("error").ToString()
                 : "score-page extraction failed";
    return false;
  }
  if (outer_dict->GetType("result") != VTYPE_STRING) {
    *error = "score-page extraction returned no metadata";
    return false;
  }

  const std::string payload = outer_dict->GetString("result").ToString();
  CefRefPtr<CefValue> inner =
      CefParseJSON(payload.data(), payload.size(), JSON_PARSER_RFC);
  if (!inner || inner->GetType() != VTYPE_DICTIONARY) {
    *error = "invalid score metadata";
    return false;
  }
  CefRefPtr<CefDictionaryValue> dict = inner->GetDictionary();
  if (!dict || dict->GetType("urls") != VTYPE_LIST) {
    *error = "score metadata did not contain page URLs";
    return false;
  }

  metadata->title = dict->GetType("title") == VTYPE_STRING
                        ? dict->GetString("title").ToString()
                        : "musescore-score";
  CefRefPtr<CefListValue> urls = dict->GetList("urls");
  if (!urls || urls->GetSize() == 0 || urls->GetSize() > 1000) {
    *error = "invalid MuseScore page count";
    return false;
  }
  metadata->urls.reserve(urls->GetSize());
  for (size_t i = 0; i < urls->GetSize(); ++i) {
    if (urls->GetType(i) != VTYPE_STRING) {
      *error = "invalid MuseScore page URL";
      return false;
    }
    std::string page_url = urls->GetString(i).ToString();
    CefURLParts page_parts;
    if (!CefParseURL(page_url, page_parts) ||
        ToLowerAscii(CefString(&page_parts.scheme).ToString()) != "https") {
      *error = "MuseScore returned an unsafe page URL";
      return false;
    }
    metadata->urls.push_back(std::move(page_url));
  }
  return true;
}

void SendPdfViewerScrollHook(CefRefPtr<CefBrowser> browser, int dy,
                             bool instant = false) {
  std::ostringstream script;
  script << "(()=>{"
            "if(location.href.indexOf('chrome-extension://"
            "mhjfbmdgcfjbbpaeojofohoefgiehjai/')!==0&&"
            "!document.querySelector('pdf-viewer'))return;"
            "const f=window.__vimbrowserPdfScrollBy;"
            "if(typeof f==='function'){try{f("
         << dy << "," << (instant ? "true" : "false") << ");}catch(e){}}})();";
  ExecuteJavaScriptInAllFrames(browser, script.str());
}

} // namespace

void BrowserWindow::BeginCommand(Mode mode) {
  BeginCommandText(mode == Mode::kCommandOpenNext ? ":open tab " : ":open ");
  mode_ = mode;
}

void BrowserWindow::BeginCommandText(std::string text) {
  sidebar_pending_keys_.clear();
  ++sidebar_delete_generation_;
  previous_focus_area_ = focus_area_ == FocusArea::kCommandLine
                             ? previous_focus_area_
                             : focus_area_;
  focus_area_ = FocusArea::kCommandLine;
  mode_ = Mode::kCommandOpenCurrent;
  command_text_ = std::move(text);
  vim::Reset(command_vim_, command_text_.size(), 0, vim::Mode::kInsert);
  ClearCommandAutocomplete();
  UpdateCommandAutocomplete();
  if (command_overlay_) {
    command_overlay_->SetVisible(true);
  }
  if (command_separator_overlay_) {
    command_separator_overlay_->SetVisible(true);
  }
  Layout();
  SetCommandText(command_text_);
  if (command_field_) {
    command_field_->RequestFocus();
  }
  UpdateModeIndicator();
}

void BrowserWindow::CommitCommand() {
  if (IsSidebarSearchMode()) {
    CommitSidebarSearch();
    return;
  }
  const std::string raw_text = command_text_;
  std::string text = Trim(command_text_);
  bool open_in_new_tab = mode_ == Mode::kCommandOpenNext;

  if (!raw_text.empty() && (raw_text[0] == '/' || raw_text[0] == '?')) {
    const bool forward = raw_text[0] == '/';
    text = raw_text.substr(1);
    CancelCommand();
    StartPageSearch(std::move(text), forward);
    return;
  }

  if (!text.empty() && text[0] == ':') {
    const size_t first_space = text.find_first_of(" \t");
    const std::string command = ToLowerAscii(
        first_space == std::string::npos ? text : text.substr(0, first_space));
    const std::string args = first_space == std::string::npos
                                 ? ""
                                 : Trim(text.substr(first_space + 1));

    auto finish = [&](auto action) {
      CancelCommand();
      action();
      return;
    };

    if (CommitSidebarFolderCommand(command, args)) {
      return;
    }

    if (command == ":noh" || command == ":nohlsearch") {
      if (!args.empty()) {
        CancelCommand();
        return;
      }
      const bool clear_sidebar = previous_focus_area_ == FocusArea::kTabSidebar;
      finish([&] {
        ClearPageSearchHighlights();
        if (clear_sidebar)
          ClearSidebarSearchHighlights();
      });
      return;
    }

    if (command == ":showmode") {
      std::vector<std::string> argv = SplitArgs(args);
      for (std::string &arg : argv) {
        arg = ToLowerAscii(arg);
      }

      if (argv.empty()) {
        const bool visible = !show_mode_indicator_;
        CancelCommand();
        SetShowModeIndicator(visible);
        return;
      }
      if (argv.size() == 1 && (argv[0] == "on" || argv[0] == "off")) {
        const bool visible = argv[0] == "on";
        CancelCommand();
        SetShowModeIndicator(visible);
        return;
      }

      CancelCommand();
      return;
    }

    if (command == ":showfps") {
      std::vector<std::string> argv = SplitArgs(args);
      for (std::string &arg : argv) {
        arg = ToLowerAscii(arg);
      }

      if (argv.empty()) {
        const bool visible = !show_fps_indicator_;
        CancelCommand();
        SetShowFpsIndicator(visible);
        return;
      }
      if (argv.size() == 1 && (argv[0] == "on" || argv[0] == "off")) {
        const bool visible = argv[0] == "on";
        CancelCommand();
        SetShowFpsIndicator(visible);
        return;
      }

      CancelCommand();
      return;
    }

    if (command == ":showstatusline") {
      std::vector<std::string> argv = SplitArgs(args);
      for (std::string &arg : argv) {
        arg = ToLowerAscii(arg);
      }

      if (argv.empty()) {
        const bool visible = !show_statusline_;
        CancelCommand();
        SetShowStatusLine(visible);
        return;
      }
      if (argv.size() == 1 && (argv[0] == "on" || argv[0] == "off")) {
        const bool visible = argv[0] == "on";
        CancelCommand();
        SetShowStatusLine(visible);
        return;
      }

      CancelCommand();
      return;
    }

    if (command == ":shader") {
      std::vector<std::string> argv = SplitArgs(args);
      for (std::string &arg : argv) {
        arg = ToLowerAscii(arg);
      }

      if (argv.empty()) {
        const bool enabled = !shader_enabled_;
        CancelCommand();
        SetShaderEnabled(enabled);
        return;
      }
      if (argv.size() == 1 && (argv[0] == "on" || argv[0] == "off")) {
        const bool enabled = argv[0] == "on";
        CancelCommand();
        SetShaderEnabled(enabled);
        return;
      }

      CancelCommand();
      return;
    }

    if (command == ":test") {
      std::vector<std::string> argv = SplitArgs(args);
      for (std::string &arg : argv) {
        arg = ToLowerAscii(arg);
      }

      if (argv.size() == 1 && argv[0] == "permission-modal") {
        CancelCommand();
        ShowMockMediaPermissionPrompt();
        return;
      }

      CancelCommand();
      SetStatusOutput("usage: :test permission-modal");
      return;
    }

    if (command != ":open" && command != ":tab-focus") {
      if (!args.empty()) {
        CancelCommand();
        return;
      }

      if (command == ":back") {
        finish([&] {
          if (CefRefPtr<CefBrowser> browser = ActiveBrowser();
              browser && browser->CanGoBack()) {
            browser->GoBack();
          }
        });
        return;
      }
      if (command == ":forward") {
        finish([&] {
          if (CefRefPtr<CefBrowser> browser = ActiveBrowser();
              browser && browser->CanGoForward()) {
            browser->GoForward();
          }
        });
        return;
      }
      if (command == ":open-clipboard") {
        finish([&] { OpenClipboard(false); });
        return;
      }
      if (command == ":open-clipboard-tab") {
        finish([&] { OpenClipboard(true); });
        return;
      }
      if (command == ":reload") {
        finish([&] {
          if (CefRefPtr<CefBrowser> browser = ActiveBrowser())
            browser->Reload();
        });
        return;
      }
      if (command == ":reload-force") {
        finish([&] {
          if (CefRefPtr<CefBrowser> browser = ActiveBrowser())
            browser->ReloadIgnoreCache();
        });
        return;
      }
      if (command == ":q" || command == ":wq") {
        finish([&] { QuitBrowser(); });
        return;
      }
      if (command == ":scroll-bottom") {
        finish([&] { ScrollActivePageToBottom(); });
        return;
      }
      if (command == ":scroll-down") {
        finish([&] { ScrollActivePageBy(kLineScrollPx); });
        return;
      }
      if (command == ":scroll-page-down") {
        finish([&] { ScrollActivePageBy(1120); });
        return;
      }
      if (command == ":scroll-page-up") {
        finish([&] { ScrollActivePageBy(-1120); });
        return;
      }
      if (command == ":scroll-top") {
        finish([&] { ScrollActivePageToTop(); });
        return;
      }
      if (command == ":scroll-up") {
        finish([&] { ScrollActivePageBy(-kLineScrollPx); });
        return;
      }
      if (command == ":tab-clone") {
        finish([&] { CloneActiveTab(); });
        return;
      }
      if (command == ":tab-close") {
        finish([&] { CloseActiveTab(); });
        return;
      }
      if (command == ":tab-first") {
        finish([&] { ActivateFirstTab(); });
        return;
      }
      if (command == ":tab-last") {
        finish([&] { ActivateLastTab(); });
        return;
      }
      if (command == ":tab-move-left") {
        finish([&] { MoveActiveTab(-1); });
        return;
      }
      if (command == ":tab-move-right") {
        finish([&] { MoveActiveTab(1); });
        return;
      }
      if (command == ":tab-next") {
        finish([&] { ActivateRelative(1); });
        return;
      }
      if (command == ":tab-prev") {
        finish([&] { ActivateRelative(-1); });
        return;
      }
      if (command == ":undo" || command == ":undo-close-tab") {
        finish([&] { UndoCloseTab(); });
        return;
      }
      if (command == ":mspdf") {
        finish([&] { StartMuseScorePdfDownload(); });
        return;
      }
      if (command == ":yank") {
        finish([&] { YankActiveUrl(); });
        return;
      }
      if (command == ":yank-dom") {
        finish([&] { YankActiveDom(); });
        return;
      }
      if (command == ":yank-markdown") {
        finish([&] { YankActiveMarkdown(); });
        return;
      }
      if (command == ":yank-title") {
        finish([&] { YankActiveTitle(); });
        return;
      }
      if (command == ":zoom-in") {
        finish([&] { ZoomActivePage(CEF_ZOOM_COMMAND_IN); });
        return;
      }
      if (command == ":zoom-out") {
        finish([&] { ZoomActivePage(CEF_ZOOM_COMMAND_OUT); });
        return;
      }
      if (command == ":zoom-reset") {
        finish([&] { ZoomActivePage(CEF_ZOOM_COMMAND_RESET); });
        return;
      }

      CancelCommand();
      return;
    }
  }

  if (StartsWithCaseInsensitive(text, ":tab-focus")) {
    const size_t after_command = 10;
    if (text.size() == after_command ||
        std::isspace(static_cast<unsigned char>(text[after_command]))) {
      text.erase(0, after_command);
      text = Trim(text);
      CancelCommand();
      if (text.empty()) {
        return;
      }
      const bool all_digits =
          std::all_of(text.begin(), text.end(),
                      [](unsigned char c) { return std::isdigit(c); });
      if (all_digits) {
        const int index = std::stoi(text);
        if (index > 0) {
          ActivateTab(static_cast<size_t>(index - 1));
        }
        return;
      }
      const std::string needle = text;
      for (size_t i = 0; i < tabs_.size(); ++i) {
        std::string title;
        if (tabs_[i].client && tabs_[i].client->browser() &&
            tabs_[i].client->browser()->GetHost()) {
          CefRefPtr<CefNavigationEntry> entry =
              tabs_[i]
                  .client->browser()
                  ->GetHost()
                  ->GetVisibleNavigationEntry();
          if (entry) {
            title = entry->GetTitle().ToString();
          }
        }
        if (ContainsCaseInsensitive(tabs_[i].url, needle) ||
            ContainsCaseInsensitive(title, needle)) {
          ActivateTab(i);
          return;
        }
      }
      return;
    }
  }

  if (StartsWithCaseInsensitive(text, ":open")) {
    const size_t after_command = 5;
    if (text.size() == after_command ||
        std::isspace(static_cast<unsigned char>(text[after_command]))) {
      text.erase(0, after_command);
      text = Trim(text);
      if ((StartsWithCaseInsensitive(text, "tab") &&
           (text.size() == 3 ||
            std::isspace(static_cast<unsigned char>(text[3])))) ||
          (StartsWithCaseInsensitive(text, "-t") &&
           (text.size() == 2 ||
            std::isspace(static_cast<unsigned char>(text[2]))))) {
        open_in_new_tab = true;
        text.erase(0, StartsWithCaseInsensitive(text, "tab") ? 3 : 2);
        text = Trim(text);
      } else {
        open_in_new_tab = false;
      }
    }
  } else if (StartsWithCaseInsensitive(text, "open")) {
    // Backward compatibility for command lines created before colon commands.
    const size_t after_command = 4;
    if (text.size() == after_command ||
        std::isspace(static_cast<unsigned char>(text[after_command]))) {
      text.erase(0, after_command);
      text = Trim(text);
      if ((StartsWithCaseInsensitive(text, "tab") &&
           (text.size() == 3 ||
            std::isspace(static_cast<unsigned char>(text[3])))) ||
          (StartsWithCaseInsensitive(text, "-t") &&
           (text.size() == 2 ||
            std::isspace(static_cast<unsigned char>(text[2]))))) {
        open_in_new_tab = true;
        text.erase(0, StartsWithCaseInsensitive(text, "tab") ? 3 : 2);
        text = Trim(text);
      } else {
        open_in_new_tab = false;
      }
    }
  }

  CancelCommand();
  if (text.empty()) {
    return;
  }

  std::string search_engine;
  std::string search_query;
  const bool is_search_engine_invocation =
      ParseSearchEngineInvocation(text, &search_engine, &search_query);
  const std::string url =
      is_search_engine_invocation
          ? ResolveSearchEngineUrl(search_engine, search_query)
          : ResolveUrlOrSearch(text);
  if (open_in_new_tab) {
    if (is_search_engine_invocation) {
      RecordSearchHistory(search_engine, search_query);
    } else {
      RecordOpenHistory(text);
    }
    AddTabAfterActive(url, true);
  } else if (Tab *tab = ActiveTab();
             tab && tab->client && tab->client->browser()) {
    if (is_search_engine_invocation) {
      RecordSearchHistory(search_engine, search_query);
    } else {
      RecordOpenHistory(text);
    }
    last_tab_close_placeholder_ = false;
    tab->url = url;
    tab->client->browser()->GetMainFrame()->LoadURL(url);
    SaveState();
    RefreshSidebar();
  }
}

void BrowserWindow::CancelCommand() {
  const bool sidebar_search = IsSidebarSearchMode();
  if (sidebar_search && !sidebar_search_committing_) {
    RestoreSidebarSearchOrigin();
  }
  mode_ = Mode::kNormal;
  sidebar_prompt_ = {};
  ClearCommandAutocomplete();
  vim::Reset(command_vim_, 0, 0, vim::Mode::kInsert);
  SetCommandText("");
  if (command_overlay_) {
    command_overlay_->SetVisible(false);
  }
  if (command_separator_overlay_) {
    command_separator_overlay_->SetVisible(false);
  }
  focus_area_ = previous_focus_area_ == FocusArea::kCommandLine
                    ? FocusArea::kWebView
                    : previous_focus_area_;
  UpdateModeIndicator();
  if (Tab *tab = ActiveTab(); tab) {
    if (focus_area_ == FocusArea::kWebView) {
      tab->view->RequestFocus();
    }
  }
  if (focus_area_ == FocusArea::kDevTools && devtools_browser_view_) {
    devtools_browser_view_->RequestFocus();
  }
  if (sidebar_search) {
    RefreshSidebar();
  }
  Layout();
}

void BrowserWindow::HandleA26TouchScroll(int x, int y, int dx, int dy) {
  if (!a26_shell_ || (dx == 0 && dy == 0)) {
    return;
  }
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser || !browser->GetHost()) {
    return;
  }

  CefMouseEvent event;
  event.x = std::max(0, x);
  event.y = std::max(0, y);
  event.modifiers = 0;
  SendPdfViewerScrollHook(browser, dy);
  SendScrollWheel(browser, event, dx, dy);
}

void BrowserWindow::HandleA26PinchZoom(bool zoom_in) {
  if (!a26_shell_) {
    return;
  }
  ZoomActivePage(zoom_in ? CEF_ZOOM_COMMAND_IN : CEF_ZOOM_COMMAND_OUT);
}

void BrowserWindow::ScrollActivePageBy(int dy) {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser) {
    return;
  }

  Tab *tab = ActiveTab();
  SendPdfViewerScrollHook(browser, dy);
  if (tab && tab->has_scroll_target && tab->scroll_target_is_pdf_viewport) {
    return;
  }
  SendScrollWheel(browser, ScrollTargetMouseEvent(tab, window_), dy);
}

void BrowserWindow::ScrollDevToolsBy(int dy) {
  if (!devtools_browser_view_ || !devtools_browser_view_->GetBrowser()) {
    return;
  }

  CefMouseEvent event;
  event.modifiers = 0;
  if (devtools_has_scroll_target_) {
    event.x = devtools_scroll_target_x_;
    event.y = devtools_scroll_target_y_;
    event.modifiers |= kVimbrowserHintScrollTargetCefModifier;
    if (!devtools_scroll_target_is_page_) {
      event.modifiers |= kVimbrowserScrollTargetElementCefModifier;
    }
  } else {
    const CefRect bounds = devtools_browser_view_->GetBounds();
    event.x = std::max(1, bounds.width / 2);
    event.y = std::max(1, bounds.height / 2);
  }
  SendScrollWheel(devtools_browser_view_->GetBrowser(), event, dy);
}

void BrowserWindow::CycleDevToolsPanel(int delta) {
  if (!devtools_browser_view_ || !devtools_browser_view_->GetBrowser()) {
    return;
  }

  CefRefPtr<CefBrowserHost> host =
      devtools_browser_view_->GetBrowser()->GetHost();
  if (!host) {
    return;
  }

  // Chrome DevTools already exposes first-class global actions for switching
  // main panels (Elements, Console, Sources, Network, ...): Ctrl+[ and Ctrl+].
  // DevTools normal mode maps h/l to those actions by sending the same key
  // events directly to the DevTools renderer. This keeps panel
  // ordering/customization in DevTools itself instead of duplicating frontend
  // state in vimbrowser chrome.
  CefKeyEvent event;
  event.type = KEYEVENT_RAWKEYDOWN;
  event.windows_key_code = delta > 0 ? 0xDD : 0xDB; // VKEY_OEM_6 / VKEY_OEM_4.
  event.native_key_code = NativeKeyCodeForSyntheticKey(
      event.windows_key_code, delta > 0 ? ']' : '[');
  event.character = 0;
  event.unmodified_character = delta > 0 ? ']' : '[';
  event.modifiers = EVENTFLAG_CONTROL_DOWN;
  forwarding_key_to_devtools_ = true;
  host->SendKeyEvent(event);
  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&BrowserWindow::ClearForwardingDevToolsKeyGuard, self),
      50);
}

void BrowserWindow::ScrollActivePageToTop() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser) {
    return;
  }
  Tab *tab = ActiveTab();
  CefMouseEvent event = ScrollTargetMouseEvent(tab, window_);
  event.modifiers |= kVimbrowserInstantScrollCefModifier;
  SendPdfViewerScrollHook(browser, -kScrollToEdgePx, true);
  if (tab && tab->has_scroll_target && tab->scroll_target_is_pdf_viewport) {
    return;
  }
  SendScrollWheel(browser, event, -kScrollToEdgePx);
}

void BrowserWindow::ScrollActivePageToBottom() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser) {
    return;
  }
  Tab *tab = ActiveTab();
  CefMouseEvent event = ScrollTargetMouseEvent(tab, window_);
  event.modifiers |= kVimbrowserInstantScrollCefModifier;
  SendPdfViewerScrollHook(browser, kScrollToEdgePx, true);
  if (tab && tab->has_scroll_target && tab->scroll_target_is_pdf_viewport) {
    return;
  }
  SendScrollWheel(browser, event, kScrollToEdgePx);
}

void BrowserWindow::StartPageSearch(std::string text, bool forward) {
  if (text.empty()) {
    if (page_search_text_.empty()) {
      SetStatusOutput("no previous search", 1500);
      return;
    }
    page_search_forward_ = forward;
    FindNextPageSearch(false);
    return;
  }

  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser || !browser->GetHost()) {
    return;
  }

  page_search_text_ = std::move(text);
  page_search_forward_ = forward;
  page_search_highlights_visible_ = true;
  page_search_browser_id_ = browser->GetIdentifier();
  browser->GetHost()->Find(page_search_text_, forward, false, false);
}

void BrowserWindow::FindNextPageSearch(bool reverse_direction) {
  if (page_search_text_.empty()) {
    SetStatusOutput("no previous search", 1500);
    return;
  }

  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser || !browser->GetHost()) {
    return;
  }

  const bool forward =
      reverse_direction ? !page_search_forward_ : page_search_forward_;
  const bool same_browser = page_search_browser_id_ == browser->GetIdentifier();
  const bool find_next = same_browser && page_search_highlights_visible_;
  page_search_highlights_visible_ = true;
  page_search_browser_id_ = browser->GetIdentifier();
  browser->GetHost()->Find(page_search_text_, forward, false, find_next);
}

void BrowserWindow::ClearPageSearchHighlights() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser && browser->GetHost()) {
    browser->GetHost()->StopFinding(true);
    page_search_browser_id_ = browser->GetIdentifier();
  }
  page_search_highlights_visible_ = false;
  SetStatusOutput("search highlights cleared", 1500);
}

void BrowserWindow::OpenClipboard(bool new_tab) {
  std::string text = Trim(ReadClipboardText());
  if (text.empty()) {
    return;
  }
  const std::string url = ResolveUrlOrSearch(text);
  RecordOpenHistory(text);
  if (new_tab) {
    AddTabAfterActive(url, true);
  } else if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    last_tab_close_placeholder_ = false;
    if (active_index_ < tabs_.size()) {
      SetTabUrl(tabs_[active_index_], url);
    }
    browser->GetMainFrame()->LoadURL(url);
    SaveState();
    RefreshSidebar();
  }
}

void BrowserWindow::RecordOpenHistory(const std::string &text) {
  std::string entry = Trim(text);
  if (entry.empty()) {
    return;
  }

  const std::string folded = ToLowerAscii(entry);
  open_history_.erase(std::remove_if(open_history_.begin(), open_history_.end(),
                                     [&](const std::string &existing) {
                                       return ToLowerAscii(existing) == folded;
                                     }),
                      open_history_.end());
  open_history_.push_back(std::move(entry));
  if (open_history_.size() > kMaxOpenHistoryEntries) {
    open_history_.erase(open_history_.begin(),
                        open_history_.end() - static_cast<std::ptrdiff_t>(
                                                  kMaxOpenHistoryEntries));
  }
}

void BrowserWindow::RecordSearchHistory(const std::string &engine,
                                        const std::string &query) {
  const std::string folded_engine = ToLowerAscii(engine);
  if (!FindSearchEngine(folded_engine)) {
    return;
  }

  std::string entry = Trim(query);
  if (entry.empty()) {
    return;
  }

  std::vector<std::string> &history = search_history_[folded_engine];
  const std::string folded_entry = ToLowerAscii(entry);
  history.erase(std::remove_if(history.begin(), history.end(),
                               [&](const std::string &existing) {
                                 return ToLowerAscii(existing) == folded_entry;
                               }),
                history.end());
  history.push_back(std::move(entry));
  if (history.size() > kMaxOpenHistoryEntries) {
    history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(
                                                       kMaxOpenHistoryEntries));
  }
}

void BrowserWindow::ZoomActivePage(cef_zoom_command_t command) {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser && browser->GetHost()) {
    browser->GetHost()->Zoom(command);
  }
}

void BrowserWindow::YankActiveUrl() {
  const std::string url = ActiveTabUrl();
  WriteClipboardText(url);
  SetStatusOutput("url copied: " + url);
}

void BrowserWindow::YankActiveTitle() { WriteClipboardText(ActiveTabTitle()); }

void BrowserWindow::YankActiveMarkdown() {
  WriteClipboardText("[" + ActiveTabTitle() + "](" + ActiveTabUrl() + ")");
}

void BrowserWindow::YankActiveDom() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser || !browser->GetMainFrame()) {
    return;
  }
  browser->GetMainFrame()->ExecuteJavaScript(
      "(()=>{const "
      "text=document.documentElement?document.documentElement.outerHTML:("
      "document.body?document.body.outerHTML:'');"
      "if(navigator.clipboard&&navigator.clipboard.writeText){navigator."
      "clipboard.writeText(text).catch(()=>{});return;}"
      "const "
      "ta=document.createElement('textarea');ta.value=text;ta.style.position='"
      "fixed';ta.style.left='-10000px';document.body.appendChild(ta);ta.select("
      ");document.execCommand('copy');ta.remove();})()",
      browser->GetMainFrame()->GetURL(), 0);
}

void BrowserWindow::StartMuseScorePdfDownload() {
  if (musescore_download_in_progress_) {
    SetStatusOutput("MuseScore PDF download is already in progress", 3000);
    return;
  }

  Tab *tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) {
    SetStatusOutput("MuseScore PDF download failed: current tab has no browser",
                    6000);
    return;
  }
  const std::string url =
      tab->client->browser()->GetMainFrame()
          ? tab->client->browser()->GetMainFrame()->GetURL().ToString()
          : tab->url;
  if (!IsMuseScoreUrl(url)) {
    SetStatusOutput(
        "MuseScore PDF download failed: current tab is not a MuseScore page",
        6000);
    return;
  }

  musescore_download_in_progress_ = true;
  SetStatusOutput("MuseScore: extracting score pages...", 0);
  CefRefPtr<BrowserWindow> self = this;
  HandleJsIpcCommand(
      tab->id, {}, MuseScoreMetadataScript(),
      [self](std::string response) {
        self->OnMuseScoreMetadata(std::move(response));
      },
      120000);
}

void BrowserWindow::OnMuseScoreMetadata(std::string response) {
  if (!musescore_download_in_progress_) {
    return;
  }

  MuseScoreMetadata metadata;
  std::string error;
  if (!ParseMuseScoreMetadata(response, &metadata, &error)) {
    FinishMuseScorePdfDownload({}, std::move(error));
    return;
  }

  SetStatusOutput("MuseScore: downloading " +
                      std::to_string(metadata.urls.size()) + " page(s)...",
                  0);
  const std::string directory = DefaultMuseScoreDownloadDirectory().string();
  CefRefPtr<BrowserWindow> self = this;
  if (!CefPostTask(TID_FILE_USER_BLOCKING,
                   base::BindOnce(&BrowserWindow::RunMuseScorePdfDownload, self,
                                  std::move(metadata.title),
                                  std::move(metadata.urls), directory))) {
    FinishMuseScorePdfDownload({}, "failed to start the native download task");
  }
}

void BrowserWindow::RunMuseScorePdfDownload(std::string title,
                                            std::vector<std::string> urls,
                                            std::string download_directory) {
  CefRefPtr<BrowserWindow> self = this;
  MuseScorePdfResult result = DownloadMuseScorePdf(
      title, urls, download_directory, [self](std::string message) {
        CefPostTask(TID_UI,
                    base::BindOnce(&BrowserWindow::UpdateMuseScorePdfStatus,
                                   self, std::move(message)));
      });
  CefPostTask(TID_UI, base::BindOnce(&BrowserWindow::FinishMuseScorePdfDownload,
                                     self, std::move(result.output_path),
                                     std::move(result.error)));
}

void BrowserWindow::UpdateMuseScorePdfStatus(std::string message) {
  if (musescore_download_in_progress_ && window_) {
    SetStatusOutput(std::move(message), 0);
  }
}

void BrowserWindow::FinishMuseScorePdfDownload(std::string output_path,
                                               std::string error) {
  musescore_download_in_progress_ = false;
  if (!window_) {
    return;
  }
  if (!error.empty()) {
    SetStatusOutput("MuseScore PDF download failed: " + error, 10000);
    return;
  }
  SetStatusOutput("MuseScore PDF saved to " + output_path, 10000);
}

} // namespace vimbrowser
