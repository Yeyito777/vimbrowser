#include "browser_window.h"
#include "browser_window_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "config.h"
#include "include/cef_browser.h"
#include "include/views/cef_textfield.h"
#include "theme.h"

namespace vimbrowser {

void BrowserWindow::BeginCommand(Mode mode) {
  BeginCommandText(mode == Mode::kCommandOpenNext ? ":open tab " : ":open ");
  mode_ = mode;
}

void BrowserWindow::BeginCommandText(std::string text) {
  previous_focus_area_ = focus_area_ == FocusArea::kCommandLine ? previous_focus_area_
                                                                : focus_area_;
  focus_area_ = FocusArea::kCommandLine;
  mode_ = Mode::kCommandOpenCurrent;
  command_text_ = std::move(text);
  vim::Reset(command_vim_, command_text_.size(), 0, vim::Mode::kInsert);
  ClearCommandAutocomplete();
  UpdateCommandAutocomplete();
  command_overlay_->SetVisible(true);
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
  std::string text = Trim(command_text_);
  bool open_in_new_tab = mode_ == Mode::kCommandOpenNext;

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

    if (command == ":showmode") {
      std::vector<std::string> argv = SplitArgs(args);
      for (std::string& arg : argv) {
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
      for (std::string& arg : argv) {
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
      for (std::string& arg : argv) {
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
      for (std::string& arg : argv) {
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

    if (command != ":open" && command != ":tab-focus") {
      if (!args.empty()) {
        CancelCommand();
        return;
      }

      if (command == ":back") {
        finish([&] {
          if (CefRefPtr<CefBrowser> browser = ActiveBrowser(); browser && browser->CanGoBack()) {
            browser->GoBack();
          }
        });
        return;
      }
      if (command == ":forward") {
        finish([&] {
          if (CefRefPtr<CefBrowser> browser = ActiveBrowser(); browser && browser->CanGoForward()) {
            browser->GoForward();
          }
        });
        return;
      }
      if (command == ":open-clipboard") { finish([&] { OpenClipboard(false); }); return; }
      if (command == ":open-clipboard-tab") { finish([&] { OpenClipboard(true); }); return; }
      if (command == ":reload") {
        finish([&] {
          if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) browser->Reload();
        });
        return;
      }
      if (command == ":reload-force") {
        finish([&] {
          if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) browser->ReloadIgnoreCache();
        });
        return;
      }
      if (command == ":q" || command == ":wq") { finish([&] { QuitBrowser(); }); return; }
      if (command == ":scroll-bottom") { finish([&] { ScrollActivePageToBottom(); }); return; }
      if (command == ":scroll-down") { finish([&] { ScrollActivePageBy(kLineScrollPx); }); return; }
      if (command == ":scroll-page-down") { finish([&] { ScrollActivePageBy(1120); }); return; }
      if (command == ":scroll-page-up") { finish([&] { ScrollActivePageBy(-1120); }); return; }
      if (command == ":scroll-top") { finish([&] { ScrollActivePageToTop(); }); return; }
      if (command == ":scroll-up") { finish([&] { ScrollActivePageBy(-kLineScrollPx); }); return; }
      if (command == ":tab-clone") { finish([&] { CloneActiveTab(); }); return; }
      if (command == ":tab-close") { finish([&] { CloseActiveTab(); }); return; }
      if (command == ":tab-first") { finish([&] { ActivateFirstTab(); }); return; }
      if (command == ":tab-last") { finish([&] { ActivateLastTab(); }); return; }
      if (command == ":tab-move-left") { finish([&] { MoveActiveTab(-1); }); return; }
      if (command == ":tab-move-right") { finish([&] { MoveActiveTab(1); }); return; }
      if (command == ":tab-next") { finish([&] { ActivateRelative(1); }); return; }
      if (command == ":tab-prev") { finish([&] { ActivateRelative(-1); }); return; }
      if (command == ":undo" || command == ":undo-close-tab") {
        finish([&] { UndoCloseTab(); });
        return;
      }
      if (command == ":yank") { finish([&] { YankActiveUrl(); }); return; }
      if (command == ":yank-dom") { finish([&] { YankActiveDom(); }); return; }
      if (command == ":yank-markdown") { finish([&] { YankActiveMarkdown(); }); return; }
      if (command == ":yank-title") { finish([&] { YankActiveTitle(); }); return; }
      if (command == ":zoom-in") { finish([&] { ZoomActivePage(CEF_ZOOM_COMMAND_IN); }); return; }
      if (command == ":zoom-out") { finish([&] { ZoomActivePage(CEF_ZOOM_COMMAND_OUT); }); return; }
      if (command == ":zoom-reset") { finish([&] { ZoomActivePage(CEF_ZOOM_COMMAND_RESET); }); return; }

      CancelCommand();
      return;
    }
  }

  if (StartsWithCaseInsensitive(text, ":tab-focus")) {
    const size_t after_command = 10;
    if (text.size() == after_command || std::isspace(static_cast<unsigned char>(text[after_command]))) {
      text.erase(0, after_command);
      text = Trim(text);
      CancelCommand();
      if (text.empty()) {
        return;
      }
      const bool all_digits = std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c);
      });
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
              tabs_[i].client->browser()->GetHost()->GetVisibleNavigationEntry();
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
    if (text.size() == after_command || std::isspace(static_cast<unsigned char>(text[after_command]))) {
      text.erase(0, after_command);
      text = Trim(text);
      if ((StartsWithCaseInsensitive(text, "tab") &&
           (text.size() == 3 || std::isspace(static_cast<unsigned char>(text[3])))) ||
          (StartsWithCaseInsensitive(text, "-t") &&
           (text.size() == 2 || std::isspace(static_cast<unsigned char>(text[2]))))) {
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
    if (text.size() == after_command || std::isspace(static_cast<unsigned char>(text[after_command]))) {
      text.erase(0, after_command);
      text = Trim(text);
      if ((StartsWithCaseInsensitive(text, "tab") &&
           (text.size() == 3 || std::isspace(static_cast<unsigned char>(text[3])))) ||
          (StartsWithCaseInsensitive(text, "-t") &&
           (text.size() == 2 || std::isspace(static_cast<unsigned char>(text[2]))))) {
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
  const std::string url = is_search_engine_invocation
                              ? ResolveSearchEngineUrl(search_engine, search_query)
                              : ResolveUrlOrSearch(text);
  if (open_in_new_tab) {
    if (is_search_engine_invocation) {
      RecordSearchHistory(search_engine, search_query);
    } else {
      RecordOpenHistory(text);
    }
    AddTabAfterActive(url, true);
  } else if (Tab* tab = ActiveTab(); tab && tab->client && tab->client->browser()) {
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
  mode_ = Mode::kNormal;
  ClearCommandAutocomplete();
  vim::Reset(command_vim_, 0, 0, vim::Mode::kInsert);
  SetCommandText("");
  command_overlay_->SetVisible(false);
  if (command_separator_overlay_) {
    command_separator_overlay_->SetVisible(false);
  }
  focus_area_ = previous_focus_area_ == FocusArea::kCommandLine ? FocusArea::kWebView
                                                                : previous_focus_area_;
  UpdateModeIndicator();
  if (Tab* tab = ActiveTab(); tab) {
    if (focus_area_ == FocusArea::kWebView) {
      tab->view->RequestFocus();
    }
  }
  Layout();
}

void BrowserWindow::ScrollActivePageBy(int dy) {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser) {
    return;
  }

  CefMouseEvent event;
  event.modifiers = 0;
  Tab* tab = ActiveTab();
  if (tab && tab->has_scroll_target) {
    event.x = tab->scroll_target_x;
    event.y = tab->scroll_target_y;
    if (!tab->scroll_target_is_page) {
      event.modifiers |= kVimbrowserScrollTargetElementCefModifier;
    }
  } else if (tab && tab->view) {
    const CefRect bounds = tab->view->GetBounds();
    event.x = std::max(1, bounds.width / 2);
    event.y = std::max(1, bounds.height / 2);
  } else if (window_) {
    const CefRect bounds = window_->GetBounds();
    event.x = std::max(1, bounds.width / 2);
    event.y = std::max(1, bounds.height / 2);
  }

  // CEF/Chromium wheel deltas use negative Y to scroll page content down.
  browser->GetHost()->SendMouseWheelEvent(event, 0, -dy);
}

void BrowserWindow::ScrollActivePageToTop() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser || !browser->GetMainFrame()) {
    return;
  }
  browser->GetMainFrame()->ExecuteJavaScript(
      "window.scrollTo({left:0,top:0,behavior:'auto'});",
      browser->GetMainFrame()->GetURL(), 0);
}

void BrowserWindow::ScrollActivePageToBottom() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser || !browser->GetMainFrame()) {
    return;
  }
  browser->GetMainFrame()->ExecuteJavaScript(
      "window.scrollTo({left:0,top:document.scrollingElement?"
      "document.scrollingElement.scrollHeight:document.body.scrollHeight,"
      "behavior:'auto'});",
      browser->GetMainFrame()->GetURL(), 0);
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

void BrowserWindow::RecordOpenHistory(const std::string& text) {
  std::string entry = Trim(text);
  if (entry.empty()) {
    return;
  }

  const std::string folded = ToLowerAscii(entry);
  open_history_.erase(
      std::remove_if(open_history_.begin(), open_history_.end(),
                     [&](const std::string& existing) {
                       return ToLowerAscii(existing) == folded;
                     }),
      open_history_.end());
  open_history_.push_back(std::move(entry));
  if (open_history_.size() > kMaxOpenHistoryEntries) {
    open_history_.erase(
        open_history_.begin(),
        open_history_.end() - static_cast<std::ptrdiff_t>(kMaxOpenHistoryEntries));
  }
}

void BrowserWindow::RecordSearchHistory(const std::string& engine,
                                        const std::string& query) {
  const std::string folded_engine = ToLowerAscii(engine);
  if (!FindSearchEngine(folded_engine)) {
    return;
  }

  std::string entry = Trim(query);
  if (entry.empty()) {
    return;
  }

  std::vector<std::string>& history = search_history_[folded_engine];
  const std::string folded_entry = ToLowerAscii(entry);
  history.erase(std::remove_if(history.begin(), history.end(),
                               [&](const std::string& existing) {
                                 return ToLowerAscii(existing) == folded_entry;
                               }),
                history.end());
  history.push_back(std::move(entry));
  if (history.size() > kMaxOpenHistoryEntries) {
    history.erase(history.begin(),
                  history.end() -
                      static_cast<std::ptrdiff_t>(kMaxOpenHistoryEntries));
  }
}

void BrowserWindow::ZoomActivePage(cef_zoom_command_t command) {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser && browser->GetHost()) {
    browser->GetHost()->Zoom(command);
  }
}

void BrowserWindow::YankActiveUrl() {
  WriteClipboardText(ActiveTabUrl());
}

void BrowserWindow::YankActiveTitle() {
  WriteClipboardText(ActiveTabTitle());
}

void BrowserWindow::YankActiveMarkdown() {
  WriteClipboardText("[" + ActiveTabTitle() + "](" + ActiveTabUrl() + ")");
}

void BrowserWindow::YankActiveDom() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser || !browser->GetMainFrame()) {
    return;
  }
  browser->GetMainFrame()->ExecuteJavaScript(
      "(()=>{const text=document.documentElement?document.documentElement.outerHTML:(document.body?document.body.outerHTML:'');"
      "if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(text).catch(()=>{});return;}"
      "const ta=document.createElement('textarea');ta.value=text;ta.style.position='fixed';ta.style.left='-10000px';document.body.appendChild(ta);ta.select();document.execCommand('copy');ta.remove();})()",
      browser->GetMainFrame()->GetURL(), 0);
}

}  // namespace vimbrowser
