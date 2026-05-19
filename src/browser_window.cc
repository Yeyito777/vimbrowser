#include "browser_window.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "config.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
#include "include/cef_navigation_entry.h"
#include "include/views/cef_button.h"
#include "include/wrapper/cef_closure_task.h"
#include "ipc_server.h"
#include "shortcuts.h"
#include "theme.h"

namespace vimbrowser {
namespace {

constexpr const char kIpcProtocolName[] = "vimbrowser-ipc";
constexpr int kIpcProtocolVersion = 1;

// This is only a style-invalidation pulse after :shader changes. The color
// transform itself remains native Blink code in StyleResolver::ResolveStyle().
constexpr const char kShaderRefreshScript[] = R"JS(
(() => {
  const refresh = () => {
    const root = document.documentElement;
    if (!root) return;
    const oldDisplay = root.style.display;
    root.style.display = 'none';
    void root.offsetHeight;
    root.style.display = oldDisplay;
    void root.offsetHeight;
  };
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', refresh, {once: true});
  } else {
    refresh();
    setTimeout(refresh, 300);
  }
})();
)JS";

constexpr int kSidebarWidth = 175;
constexpr int kCommandHeight = 28;
constexpr int kCommandAutocompleteRowHeight = 24;
constexpr int kCommandAutocompleteMaxVisible = 10;
constexpr int kCommandAutocompleteBorder = 0;
constexpr int kCommandAutocompleteHPadding = 8;
constexpr int kRootPanelId = 100;
constexpr int kMainPanelId = 101;
constexpr int kSidebarPanelId = 102;
constexpr int kContentPanelId = 103;
constexpr int kCommandPanelId = 104;
constexpr int kCommandSeparatorPanelId = 106;
constexpr int kCommandContentPanelId = 107;
constexpr int kSidebarContentPanelId = 108;
constexpr int kSidebarBorderPanelId = 109;
constexpr int kContentInnerPanelId = 110;
constexpr int kModeIndicatorPanelId = 111;
constexpr int kModeIndicatorFieldId = 112;
constexpr int kCommandAutocompletePanelId = 113;
constexpr int kCommandFieldId = 114;
constexpr int kSidebarSpacerId = 115;
constexpr int kFpsIndicatorPanelId = 116;
constexpr int kFpsIndicatorFieldId = 117;
constexpr int kAcceleratorCommandTab = 5000;
constexpr int kAcceleratorCommandBacktab = 5001;
constexpr int kAcceleratorTabNext = 5002;
constexpr int kAcceleratorTabPrevious = 5003;
constexpr int kSidebarRowBaseId = 2000;
constexpr int kAutocompleteRowBaseId = 6000;
constexpr int kSidebarRowHeight = 24;
// Experimental chrome-level mode indicator. Flip to false to disable without
// touching the mode/focus state machines.
constexpr bool kModeIndicatorEnabled = true;
constexpr int kModeIndicatorWidth = 96;
constexpr int kModeIndicatorHeight = 24;
constexpr int kCommandTextInsetX = 0;
constexpr int kCommandCharWidth = 8;
constexpr int kLineScrollPx = 280;
constexpr int kSmallScrollPx = 140;
constexpr int kTabContentActivationDelayMs = 75;
constexpr int kTabStateSaveDelayMs = 250;
constexpr size_t kNoTabIndex = std::numeric_limits<size_t>::max();

bool InIdRange(int id, int base, int count) {
  return id >= base && id < base + count;
}

void StyleTextfield(CefRefPtr<CefTextfield> field,
                    cef_color_t text,
                    cef_color_t background,
                    const CefString& font = "monospace, 13px") {
  if (!field) {
    return;
  }
  field->SetReadOnly(true);
  field->SetFocusable(false);
  field->SetFontList(font);
  field->SetBackgroundColor(background);
  field->SetTextColor(text);
  field->SetSelectionTextColor(theme::kText);
  field->SetSelectionBackgroundColor(theme::kSelectionBg);
}

void StyleCommandField(CefRefPtr<CefTextfield> field) {
  if (!field) {
    return;
  }
  // The command line is a real focused native textfield. We intercept editing
  // keys in BrowserWindow and drive vim::LineEditState ourselves, but the
  // textfield owns all text/caret/selection painting. This keeps normal-mode
  // block cursors and insert-mode bar cursors in Chromium's renderer instead of
  // using overlay views that can drift, move text, or fail to erase glyphs.
  field->SetReadOnly(false);
  field->SetFocusable(true);
  field->SetFontList("monospace, 13px");
  field->SetBackgroundColor(theme::kTransparent);
  // Chromium colors the insertion caret from the default text color. Keep that
  // cyan, then apply per-range colors for the actual glyphs below.
  field->SetTextColor(theme::kVimNormal);
  field->SetSelectionTextColor(theme::kAppBg);
  field->SetSelectionBackgroundColor(theme::kVimNormal);
}

const std::vector<CompletionItem>& CommandList() {
  static const std::vector<CompletionItem> commands = {
      {":open", "open URL/search in current tab"},
      {":tab-focus", "focus tab by number/title/url"},
      {":shader", "toggle native page color shader"},
      {":showmode", "toggle top-right vim mode display"},
      {":showfps", "toggle current page fps display"},
  };
  return commands;
}

const std::vector<CompletionItem>& OnOffArgList() {
  static const std::vector<CompletionItem> args = {
      {"off", "turn off"},
      {"on", "turn on"},
  };
  return args;
}

const std::vector<CompletionItem>& OpenArgList() {
  static const std::vector<CompletionItem> args = {
      {"-t", "open in a new tab"},
      {"tab", "open in a new tab"},
  };
  return args;
}

bool CommandTakesArguments(const std::string& command) {
  return command == ":open" || command == ":tab-focus" ||
         command == ":shader" || command == ":showmode" ||
         command == ":showfps";
}

bool IsRawKeyDown(const CefKeyEvent& event) {
  return event.type == KEYEVENT_RAWKEYDOWN;
}

bool IsCharEvent(const CefKeyEvent& event) {
  return event.type == KEYEVENT_CHAR;
}

bool IsPrintableAscii(char16_t c) {
  return c >= 0x20 && c <= 0x7e;
}

bool IsPlain(const CefKeyEvent& event) {
  return !(event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
         !(event.modifiers & EVENTFLAG_ALT_DOWN) &&
         !(event.modifiers & EVENTFLAG_COMMAND_DOWN);
}

bool IsPlainPrintableKey(const CefKeyEvent& event) {
  const char16_t c = event.character ? event.character : event.unmodified_character;
  return IsPlain(event) && IsPrintableAscii(c);
}

bool IsPlainLetterKey(const CefKeyEvent& event, char key) {
  if (!IsPlain(event)) {
    return false;
  }
  const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
  const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(key)));
  const char16_t c = event.character ? event.character : event.unmodified_character;
  return event.windows_key_code == upper || event.windows_key_code == lower ||
         c == upper || c == lower;
}

char PlainKeyChar(const CefKeyEvent& event) {
  if (!IsPlain(event)) {
    return 0;
  }
  const char16_t c = event.character ? event.character : event.unmodified_character;
  if (IsPrintableAscii(c)) {
    return static_cast<char>(c);
  }
  if (event.windows_key_code >= 'A' && event.windows_key_code <= 'Z') {
    const bool shift = event.modifiers & EVENTFLAG_SHIFT_DOWN;
    return static_cast<char>(shift ? event.windows_key_code
                                   : std::tolower(event.windows_key_code));
  }
  if (event.modifiers & EVENTFLAG_SHIFT_DOWN) {
    switch (event.windows_key_code) {
      case '1': return '!';
      case '2': return '@';
      case '3': return '#';
      case '4': return '$';
      case '5': return '%';
      case '6': return '^';
      case '7': return '&';
      case '8': return '*';
      case '9': return '(';
      case '0': return ')';
      case '-': return '_';
      case '=': return '+';
      case '[': return '{';
      case ']': return '}';
      case '\\': return '|';
      case ';': return ':';
      case '\'': return '"';
      case ',': return '<';
      case '.': return '>';
      case '/': return '?';
      case '`': return '~';
    }
  }
  if (event.windows_key_code >= 0x20 && event.windows_key_code <= 0x7e) {
    return static_cast<char>(event.windows_key_code);
  }
  return 0;
}

bool IsEnterKey(const CefKeyEvent& event) {
  return event.windows_key_code == 0x0D || event.native_key_code == 36;
}

bool IsEscapeKey(const CefKeyEvent& event) {
  return event.windows_key_code == 0x1B || event.native_key_code == 9 ||
         event.character == 0x1B || event.unmodified_character == 0x1B;
}

bool IsBackspaceKey(const CefKeyEvent& event) {
  return event.windows_key_code == 0x08 || event.windows_key_code == 0xFF08 ||
         event.native_key_code == 22 || event.character == 0x08 ||
         event.unmodified_character == 0x08;
}

bool IsTabKey(const CefKeyEvent& event) {
  return event.windows_key_code == 0x09 || event.native_key_code == 23;
}

bool IsCtrlKey(const CefKeyEvent& event, char key) {
  if (!(event.modifiers & EVENTFLAG_CONTROL_DOWN)) {
    return false;
  }
  return event.windows_key_code == key || event.windows_key_code == key + ('a' - 'A') ||
         event.character == key || event.character == key + ('a' - 'A') ||
         event.unmodified_character == key ||
         event.unmodified_character == key + ('a' - 'A');
}

std::string Trim(std::string value) {
  auto is_space = [](unsigned char c) { return std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                          [&](char c) { return !is_space(c); }));
  value.erase(std::find_if(value.rbegin(), value.rend(),
                           [&](char c) { return !is_space(c); })
                  .base(),
              value.end());
  return value;
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::vector<std::string> SplitArgs(const std::string& value) {
  std::vector<std::string> args;
  size_t pos = 0;
  while (pos < value.size()) {
    while (pos < value.size() &&
           std::isspace(static_cast<unsigned char>(value[pos]))) {
      ++pos;
    }
    if (pos >= value.size()) {
      break;
    }
    const size_t start = pos;
    while (pos < value.size() &&
           !std::isspace(static_cast<unsigned char>(value[pos]))) {
      ++pos;
    }
    args.push_back(value.substr(start, pos - start));
  }
  return args;
}

bool StartsWithCaseInsensitive(const std::string& value, const std::string& prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(value[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool ContainsCaseInsensitive(const std::string& value, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  return ToLowerAscii(value).find(ToLowerAscii(needle)) != std::string::npos;
}

std::string Ellipsize(std::string value, size_t max_size) {
  if (value.size() <= max_size) {
    return value;
  }
  if (max_size <= 3) {
    value.resize(max_size);
    return value;
  }
  value.resize(max_size - 3);
  value += "...";
  return value;
}

bool IsWhitespaceOnly(const std::string& value) {
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isspace(c);
  });
}

bool IsOpenTabArg(const std::string& value) {
  const std::string lower = ToLowerAscii(value);
  return lower == "tab" || lower == "-t";
}

bool ArgsContainOpenTabArg(const std::string& value) {
  for (std::string arg : SplitArgs(value)) {
    if (IsOpenTabArg(arg)) {
      return true;
    }
  }
  return false;
}

bool IsTokenBoundary(const std::string& value, size_t pos) {
  return pos >= value.size() || std::isspace(static_cast<unsigned char>(value[pos]));
}

int TextColumns(const std::string& value) {
  return static_cast<int>(value.size());
}

std::string SidebarTextForTab(size_t index,
                              const std::string& url,
                              bool active) {
  std::string text = active ? "▸ " : "  ";
  text += std::to_string(index + 1);
  text += ": ";
  text += DisplayUrl(url);
  if (text.size() > 160) {
    text.resize(157);
    text += "...";
  }
  return text;
}

std::string ShellRead(const char* command) {
  std::string output;
  FILE* pipe = popen(command, "r");
  if (!pipe) {
    return output;
  }
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe)) {
    output += buffer;
  }
  pclose(pipe);
  return output;
}

bool ShellWrite(const char* command, const std::string& text) {
  FILE* pipe = popen(command, "w");
  if (!pipe) {
    return false;
  }
  if (!text.empty()) {
    fwrite(text.data(), 1, text.size(), pipe);
  }
  return pclose(pipe) == 0;
}

std::string ReadClipboardText() {
  return ShellRead("(xclip -selection clipboard -o 2>/dev/null || "
                   "xsel -b -o 2>/dev/null || "
                   "wl-paste -n 2>/dev/null) | head -c 1048576");
}

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0xf]);
          out.push_back(kHex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::string IpcSocketPathForStatePath(const std::string& state_path) {
  std::filesystem::path dir = std::filesystem::path(state_path).parent_path();
  if (dir.empty()) {
    dir = "/tmp/vimbrowser";
  }
  return (dir / "ipc.sock").string();
}

std::string IpcVersionJson() {
  std::ostringstream out;
  out << "{"
      << "\"protocol\":\"" << kIpcProtocolName << "\","
      << "\"version\":" << kIpcProtocolVersion
      << "}";
  return out.str();
}

void WriteClipboardText(const std::string& text) {
  if (ShellWrite("xclip -selection clipboard -i 2>/dev/null", text)) {
    return;
  }
  if (ShellWrite("xsel -b -i 2>/dev/null", text)) {
    return;
  }
  ShellWrite("wl-copy 2>/dev/null", text);
}

}  // namespace

BrowserWindow::BrowserWindow(std::vector<std::string> initial_urls,
                             size_t active_index,
                             bool show_mode_indicator,
                             bool show_fps_indicator,
                             bool shader_enabled,
                             std::string state_path)
    : initial_urls_(std::move(initial_urls)),
      state_path_(std::move(state_path)),
      initial_active_index_(active_index),
      show_mode_indicator_(show_mode_indicator),
      show_fps_indicator_(show_fps_indicator),
      shader_enabled_(shader_enabled) {
  open_history_ = ReadAppState(state_path_).open_history;
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

void BrowserWindow::OnClientLoadStart(BrowserClient* client, const std::string& url) {
  for (Tab& tab : tabs_) {
    if (tab.client.get() == client) {
      tab.url = url;
      if (url != "about:blank") {
        last_tab_close_placeholder_ = false;
      }
      SaveState();
      RefreshSidebar();
      return;
    }
  }
}

bool BrowserWindow::OnClientBeforePopup(BrowserClient* client,
                                        const std::string& target_url,
                                        bool activate) {
  if (target_url.empty()) {
    return false;
  }

  Tab* tab = ActiveTab();
  if (!tab || tab->client.get() != client) {
    return false;
  }

  native_hints_active_ = false;
  AddTab(target_url, activate);
  UpdateModeIndicator();
  return true;
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
  AddTab(url, true);
  UpdateModeIndicator();
}

void BrowserWindow::OnNativeHintsStopped(BrowserClient* client) {
  if (Tab* tab = ActiveTab(); !tab || tab->client.get() != client) {
    return;
  }
  native_hints_active_ = false;
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
  ipc_server_ = std::make_unique<IpcServer>(this, IpcSocketPathForStatePath(state_path_));
  ipc_server_->Start();
  BuildChrome();
  for (size_t i = 0; i < initial_urls_.size(); ++i) {
    AddTab(initial_urls_[i], i == initial_active_index_);
  }
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
  CefBoxLayoutSettings sidebar_settings = {};
  sidebar_settings.size = sizeof(sidebar_settings);
  sidebar_settings.horizontal = true;
  sidebar_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> sidebar_layout =
      sidebar_panel_->SetToBoxLayout(sidebar_settings);
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
  sidebar_layout->SetFlexForView(sidebar_content_panel_, 1);

  sidebar_border_panel_ = CefPanel::CreatePanel(nullptr);
  sidebar_border_panel_->SetID(kSidebarBorderPanelId);
  sidebar_border_panel_->SetBackgroundColor(theme::kAccent);
  sidebar_panel_->AddChildView(sidebar_border_panel_);

  content_panel_ = CefPanel::CreatePanel(nullptr);
  content_panel_->SetID(kContentPanelId);
  content_panel_->SetBackgroundColor(theme::kAppBg);
  main_panel_->AddChildView(content_panel_);
  main_layout->SetFlexForView(content_panel_, 1);

  content_inner_panel_ = CefPanel::CreatePanel(nullptr);
  content_inner_panel_->SetID(kContentInnerPanelId);
  content_inner_panel_->SetBackgroundColor(theme::kAppBg);
  content_inner_panel_->SetToFillLayout();
  content_panel_->AddChildView(content_inner_panel_);

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
  autocomplete_rows_.clear();
  autocomplete_panel_ = nullptr;
  command_field_ = nullptr;
  command_content_panel_ = nullptr;
  command_panel_ = nullptr;
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
  if (mode_ != Mode::kNormal && IsCharEvent(event) && PlainKeyChar(event) == ':') {
    return true;
  }
  if (mode_ != Mode::kNormal) {
    return HandleCommandModeKey(event);
  }

  if (HandleGlobalFocusKey(event)) {
    return true;
  }

  if (focus_area_ == FocusArea::kWebView) {
    return HandleWebsiteModeKey(event);
  }

  if (!IsRawKeyDown(event)) {
    return false;
  }

  return HandleNormalModeKey(event);
}

bool BrowserWindow::OnAccelerator(CefRefPtr<CefWindow> window, int command_id) {
  if (mode_ != Mode::kNormal && command_vim_.mode == vim::Mode::kInsert) {
    if (command_id == kAcceleratorCommandTab ||
        command_id == kAcceleratorCommandBacktab) {
      return CycleCommandAutocomplete(command_id == kAcceleratorCommandBacktab ? -1 : 1);
    }
  }
  if (mode_ == Mode::kNormal && !native_hints_active_) {
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
  if (mode_ != Mode::kNormal) {
    return HandleCommandModeKey(event);
  }

  if (HandleGlobalFocusKey(event)) {
    return true;
  }

  if (focus_area_ == FocusArea::kWebView) {
    return HandleWebsiteModeKey(event);
  }

  if (!IsRawKeyDown(event)) {
    return false;
  }

  if (event.focus_on_editable_field) {
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
      case 'e':
        MoveActiveTab(-1);
        return true;
      case 'E':
        MoveActiveTab(1);
        return true;
    }
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
    const size_t index = static_cast<size_t>(id - kSidebarRowBaseId);
    if (index < tabs_.size()) {
      ActivateTab(index);
      SetFocusArea(FocusArea::kTabSidebar);
    }
    return;
  }
  if (InIdRange(id, kAutocompleteRowBaseId, 1000)) {
    const int index = id - kAutocompleteRowBaseId;
    if (index >= 0 && index < static_cast<int>(command_autocomplete_.matches.size())) {
      command_autocomplete_.selection = index;
      FillCommandAutocomplete(command_autocomplete_.matches[index].name);
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
    return CefSize(kSidebarWidth, 1);
  }
  if (id == kSidebarContentPanelId) {
    return CefSize(kSidebarWidth - 1, 1);
  }
  if (id == kSidebarBorderPanelId) {
    return CefSize(1, 1);
  }
  if (id == kMainPanelId || id == kRootPanelId) {
    return CefSize(1200, 800);
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
  if (InIdRange(id, kSidebarRowBaseId, 1000)) {
    return CefSize(kSidebarWidth - 1, kSidebarRowHeight);
  }
  if (id == kSidebarSpacerId) {
    return CefSize(kSidebarWidth - 1, 1);
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
    return CefSize(kSidebarWidth, 1);
  }
  if (id == kSidebarContentPanelId) {
    return CefSize(kSidebarWidth - 1, 1);
  }
  if (id == kSidebarBorderPanelId) {
    return CefSize(1, 1);
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
  return CEF_RUNTIME_STYLE_ALLOY;
}

cef_runtime_style_t BrowserWindow::GetBrowserRuntimeStyle() {
  return CEF_RUNTIME_STYLE_ALLOY;
}

void BrowserWindow::AddTab(std::string url, bool activate) {
  InsertTab(std::move(url), tabs_.size(), activate);
}

void BrowserWindow::InsertTab(std::string url, size_t index, bool activate) {
  last_tab_close_placeholder_ = false;
  const size_t insert_index = std::min(index, tabs_.size());
  CefBrowserSettings browser_settings;
  browser_settings.background_color = theme::kAppBg;

  Tab tab;
  tab.url = std::move(url);
  tab.client = new BrowserClient(this);
  tab.view = CefBrowserView::CreateBrowserView(tab.client, tab.url, browser_settings,
                                               nullptr, nullptr, this);
  tab.view->SetPreferAccelerators(true);
  tab.view->SetVisible(false);
  content_inner_panel_->AddChildView(tab.view);

  if (!tabs_.empty() && insert_index <= active_index_) {
    ++active_index_;
  }
  if (visible_tab_index_ != kNoTabIndex && insert_index <= visible_tab_index_) {
    ++visible_tab_index_;
  }
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(insert_index), tab);
  RefreshSidebar();
  Layout();

  if (activate) {
    ActivateTab(insert_index);
  } else {
    SaveState();
  }
}

void BrowserWindow::ActivateTab(size_t index) {
  if (tabs_.empty() || index >= tabs_.size()) {
    return;
  }

  if (active_index_ == index) {
    RefreshSidebar();
    if (visible_tab_index_ != index) {
      ScheduleActiveBrowserSync();
    }
    return;
  }

  active_index_ = index;
  RefreshSidebar();
  ScheduleStateSave();
  ScheduleActiveBrowserSync();
}

void BrowserWindow::ScheduleActiveBrowserSync() {
  if (!window_) {
    return;
  }

  const uint64_t generation = ++active_browser_sync_generation_;
  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&BrowserWindow::ApplyActiveBrowserSelection, self,
                     generation),
      kTabContentActivationDelayMs);
}

void BrowserWindow::ApplyActiveBrowserSelection(uint64_t generation) {
  if (!window_ || generation != active_browser_sync_generation_ || tabs_.empty() ||
      active_index_ >= tabs_.size()) {
    return;
  }

  if (visible_tab_index_ < tabs_.size() && visible_tab_index_ != active_index_ &&
      tabs_[visible_tab_index_].view) {
    tabs_[visible_tab_index_].view->SetVisible(false);
  }

  visible_tab_index_ = active_index_;
  Tab& tab = tabs_[active_index_];
  if (tab.view) {
    tab.view->SetVisible(true);
    if (content_inner_panel_ && content_inner_panel_->GetLayout()) {
      content_inner_panel_->Layout();
    }
    if (focus_area_ == FocusArea::kWebView) {
      tab.view->RequestFocus();
    }
  }
  UpdateFpsIndicator();
}

void BrowserWindow::ScheduleStateSave() {
  if (!window_) {
    return;
  }

  const uint64_t generation = ++state_save_generation_;
  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(&BrowserWindow::SaveStateForGeneration,
                                    self, generation),
                     kTabStateSaveDelayMs);
}

void BrowserWindow::SaveStateForGeneration(uint64_t generation) {
  if (window_ && generation == state_save_generation_) {
    SaveState();
  }
}

void BrowserWindow::ActivateRelative(int delta) {
  if (tabs_.empty()) {
    return;
  }
  const int count = static_cast<int>(tabs_.size());
  int next = static_cast<int>(active_index_) + delta;
  next = (next % count + count) % count;
  ActivateTab(static_cast<size_t>(next));
}

void BrowserWindow::ActivateFirstTab() {
  ActivateTab(0);
}

void BrowserWindow::ActivateLastTab() {
  if (!tabs_.empty()) {
    ActivateTab(tabs_.size() - 1);
  }
}

void BrowserWindow::MoveActiveTab(int delta) {
  if (tabs_.size() < 2) {
    return;
  }
  const int count = static_cast<int>(tabs_.size());
  const int current = static_cast<int>(active_index_);
  const int next = (current + delta + count) % count;
  const size_t old_active_index = active_index_;
  const size_t new_active_index = static_cast<size_t>(next);
  std::swap(tabs_[active_index_], tabs_[static_cast<size_t>(next)]);
  if (visible_tab_index_ == old_active_index) {
    visible_tab_index_ = new_active_index;
  } else if (visible_tab_index_ == new_active_index) {
    visible_tab_index_ = old_active_index;
  }
  active_index_ = new_active_index;
  SaveState();
  RefreshSidebar();
  Layout();
}

void BrowserWindow::CloneActiveTab() {
  const std::string url = ActiveTabUrl();
  if (!url.empty()) {
    AddTab(url, true);
  }
}

void BrowserWindow::CloseActiveTab(CloseFocus focus_after_close) {
  if (tabs_.empty()) {
    return;
  }

  const size_t closing = active_index_;
  const std::string url = ActiveTabUrl();
  std::cerr << "vimbrowser: close-tab index=" << (closing + 1)
            << " count=" << tabs_.size() << " url=" << url << std::endl;
  if (!url.empty()) {
    closed_tabs_.push_back({url, closing});
  }

  ++active_browser_sync_generation_;

  if (tabs_.size() == 1) {
    Tab closing_tab = tabs_[0];
    tabs_.clear();
    active_index_ = 0;
    visible_tab_index_ = kNoTabIndex;
    CloseTabBackend(closing_tab);
    AddTab("about:blank", true);
    last_tab_close_placeholder_ = true;
    UpdateFpsIndicator();
    SaveState();
    RefreshSidebar();
    Layout();
    return;
  }

  if (visible_tab_index_ == closing) {
    visible_tab_index_ = kNoTabIndex;
  } else if (visible_tab_index_ > closing && visible_tab_index_ < tabs_.size()) {
    --visible_tab_index_;
  }
  Tab closing_tab = tabs_[closing];
  CloseTabBackend(closing_tab);

  const size_t next_index = focus_after_close == CloseFocus::kNextTab
                                ? closing
                                : (closing == 0 ? 0 : closing - 1);
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(closing));
  active_index_ = std::min(next_index, tabs_.size() - 1);
  tabs_[active_index_].view->SetVisible(true);
  visible_tab_index_ = active_index_;
  UpdateFpsIndicator();
  if (focus_area_ == FocusArea::kWebView) {
    tabs_[active_index_].view->RequestFocus();
  }
  SaveState();
  RefreshSidebar();
  Layout();
  last_tab_close_placeholder_ = false;
}

void BrowserWindow::CloseTabBackend(Tab& tab) {
  if (tab.view) {
    tab.view->SetVisible(false);
    if (content_inner_panel_) {
      content_inner_panel_->RemoveChildView(tab.view);
    }
  }
  if (tab.client) {
    tab.client->DetachOwner();
  }
  tab.view = nullptr;
  tab.client = nullptr;
}

void BrowserWindow::UndoCloseTab() {
  if (closed_tabs_.empty()) {
    std::cerr << "vimbrowser: undo-close-tab ignored; stack empty" << std::endl;
    return;
  }
  const ClosedTab closed_tab = closed_tabs_.back();
  closed_tabs_.pop_back();
  std::cerr << "vimbrowser: undo-close-tab index=" << (closed_tab.index + 1)
            << " url=" << closed_tab.url
            << " placeholder=" << last_tab_close_placeholder_
            << " count=" << tabs_.size() << std::endl;
  if (last_tab_close_placeholder_ && tabs_.size() == 1 &&
      active_index_ == 0 && tabs_[0].client && tabs_[0].client->browser()) {
    last_tab_close_placeholder_ = false;
    tabs_[0].url = closed_tab.url;
    tabs_[0].client->browser()->GetMainFrame()->LoadURL(closed_tab.url);
    SaveState();
    RefreshSidebar();
    Layout();
    return;
  }
  InsertTab(closed_tab.url, closed_tab.index, true);
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

  const std::string url = ResolveUrlOrSearch(text);
  if (open_in_new_tab) {
    RecordOpenHistory(text);
    AddTab(url, true);
  } else if (Tab* tab = ActiveTab(); tab && tab->client && tab->client->browser()) {
    RecordOpenHistory(text);
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
  if (Tab* tab = ActiveTab(); tab && tab->view) {
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
    AddTab(url, true);
  } else if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    last_tab_close_placeholder_ = false;
    if (active_index_ < tabs_.size()) {
      tabs_[active_index_].url = url;
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

void BrowserWindow::ClearCommandAutocomplete() {
  command_autocomplete_ = CommandAutocompleteState{};
  UpdateAutocompleteView();
  if (autocomplete_overlay_) {
    autocomplete_overlay_->SetVisible(false);
  }
}

void BrowserWindow::AppendOpenHistoryMatches(
    const std::string& prefix,
    std::vector<CompletionItem>& matches) const {
  struct RankedHistoryMatch {
    CompletionItem item;
    size_t recency_rank = 0;
  };

  std::vector<RankedHistoryMatch> ranked;
  std::unordered_set<std::string> seen;
  for (const CompletionItem& item : matches) {
    seen.insert(ToLowerAscii(item.name));
  }
  for (size_t i = open_history_.size(); i > 0; --i) {
    const size_t index = i - 1;
    const std::string& entry = open_history_[index];
    if (entry.empty() ||
        (!prefix.empty() && !StartsWithCaseInsensitive(entry, prefix))) {
      continue;
    }

    const std::string folded = ToLowerAscii(entry);
    if (!seen.insert(folded).second) {
      continue;
    }

    ranked.push_back({CompletionItem{entry, "open history"},
                      open_history_.size() - 1 - index});
  }

  std::sort(ranked.begin(), ranked.end(), [](const RankedHistoryMatch& a,
                                             const RankedHistoryMatch& b) {
    if (a.item.name.size() != b.item.name.size()) {
      return a.item.name.size() < b.item.name.size();
    }
    if (a.recency_rank != b.recency_rank) {
      return a.recency_rank < b.recency_rank;
    }
    return ToLowerAscii(a.item.name) < ToLowerAscii(b.item.name);
  });

  for (const RankedHistoryMatch& match : ranked) {
    matches.push_back(match.item);
  }
}

void BrowserWindow::AppendTabFocusMatches(
    const std::string& prefix,
    std::vector<CompletionItem>& matches) const {
  std::unordered_set<std::string> seen;
  for (const CompletionItem& item : matches) {
    seen.insert(ToLowerAscii(item.name));
  }

  for (size_t i = 0; i < tabs_.size(); ++i) {
    const Tab& tab = tabs_[i];
    const std::string number = std::to_string(i + 1);
    std::string title;
    if (tab.client && tab.client->browser() && tab.client->browser()->GetHost()) {
      CefRefPtr<CefNavigationEntry> entry =
          tab.client->browser()->GetHost()->GetVisibleNavigationEntry();
      if (entry) {
        title = entry->GetTitle().ToString();
      }
    }

    const bool matches_prefix =
        prefix.empty() || StartsWithCaseInsensitive(number, prefix) ||
        ContainsCaseInsensitive(title, prefix) ||
        ContainsCaseInsensitive(tab.url, prefix);
    if (!matches_prefix || !seen.insert(ToLowerAscii(number)).second) {
      continue;
    }

    std::string description = "tab " + number;
    if (i == active_index_) {
      description += " (active)";
    }
    if (!title.empty()) {
      description += "  ";
      description += title;
      if (!tab.url.empty()) {
        description += " — ";
        description += DisplayUrl(tab.url);
      }
    } else if (!tab.url.empty()) {
      description += "  ";
      description += DisplayUrl(tab.url);
    }

    matches.push_back({number, Ellipsize(std::move(description), 140)});
  }
}

void BrowserWindow::UpdateCommandAutocomplete() {
  ClearCommandAutocomplete();
  if (command_text_.find('\n') != std::string::npos) {
    return;
  }
  const size_t cursor = command_vim_.mode == vim::Mode::kNormal
                            ? std::min(command_vim_.cursor + 1,
                                       command_text_.size())
                            : command_vim_.cursor;
  if (cursor != command_text_.size()) {
    return;
  }

  const size_t first_non_space = command_text_.find_first_not_of(" \t");
  if (first_non_space == std::string::npos || command_text_[first_non_space] != ':') {
    return;
  }

  const std::string raw = command_text_.substr(first_non_space);
  std::vector<CompletionItem> matches;

  const size_t first_space = raw.find_first_of(" \t");
  const std::string typed_command = first_space == std::string::npos ? raw : raw.substr(0, first_space);
  const std::string after_command = first_space == std::string::npos ? "" : raw.substr(first_space + 1);

  if (first_space == std::string::npos) {
    for (const CompletionItem& item : CommandList()) {
      if (StartsWithCaseInsensitive(item.name, typed_command)) {
        matches.push_back(item);
      }
    }
  } else if (StartsWithCaseInsensitive(typed_command, ":open") && IsTokenBoundary(typed_command, 5)) {
    const size_t arg_start = after_command.find_last_of(" \t");
    const std::string arg_prefix = arg_start == std::string::npos ? after_command : after_command.substr(arg_start + 1);
    const std::string completed_args = arg_start == std::string::npos ? "" : after_command.substr(0, arg_start + 1);
    const bool already_has_tab_arg = ArgsContainOpenTabArg(completed_args);
    const bool completing_new_arg = IsWhitespaceOnly(after_command) ||
                                    (!after_command.empty() && std::isspace(static_cast<unsigned char>(after_command.back())));
    if (!already_has_tab_arg && (completing_new_arg || !arg_prefix.empty())) {
      for (const CompletionItem& item : OpenArgList()) {
        if (completing_new_arg || StartsWithCaseInsensitive(item.name, arg_prefix)) {
          matches.push_back(item);
        }
      }
    }
    if (completing_new_arg || !arg_prefix.empty()) {
      AppendOpenHistoryMatches(arg_prefix, matches);
    }
  } else if (StartsWithCaseInsensitive(typed_command, ":tab-focus") &&
             IsTokenBoundary(typed_command, 10)) {
    const size_t arg_start = after_command.find_last_of(" \t");
    const std::string arg_prefix = arg_start == std::string::npos
                                       ? after_command
                                       : after_command.substr(arg_start + 1);
    const bool completing_new_arg = IsWhitespaceOnly(after_command) ||
                                    (!after_command.empty() &&
                                     std::isspace(static_cast<unsigned char>(
                                         after_command.back())));
    if (completing_new_arg || !arg_prefix.empty()) {
      AppendTabFocusMatches(arg_prefix, matches);
    }
  } else if ((StartsWithCaseInsensitive(typed_command, ":showmode") &&
              IsTokenBoundary(typed_command, 9)) ||
             (StartsWithCaseInsensitive(typed_command, ":showfps") &&
              IsTokenBoundary(typed_command, 8)) ||
             (StartsWithCaseInsensitive(typed_command, ":shader") &&
              IsTokenBoundary(typed_command, 7))) {
    const size_t arg_start = after_command.find_last_of(" \t");
    const std::string arg_prefix = arg_start == std::string::npos
                                       ? after_command
                                       : after_command.substr(arg_start + 1);
    const bool completing_new_arg = IsWhitespaceOnly(after_command) ||
                                    (!after_command.empty() &&
                                     std::isspace(static_cast<unsigned char>(
                                         after_command.back())));
    if (completing_new_arg || !arg_prefix.empty()) {
      for (const CompletionItem& item : OnOffArgList()) {
        if (completing_new_arg || StartsWithCaseInsensitive(item.name, arg_prefix)) {
          matches.push_back(item);
        }
      }
    }
  }

  if (matches.empty()) {
    return;
  }

  command_autocomplete_.active = true;
  command_autocomplete_.selection = -1;
  command_autocomplete_.prefix = command_text_;
  command_autocomplete_.token_start = first_non_space;
  command_autocomplete_.matches = std::move(matches);
}

void BrowserWindow::FillCommandAutocomplete(const std::string& name) {
  if (!command_autocomplete_.active) {
    return;
  }

  std::string completed;
  if (!name.empty() && name[0] == ':') {
    const size_t first_non_space = command_autocomplete_.prefix.find_first_not_of(" \t");
    const std::string leading = first_non_space == std::string::npos
                                    ? ""
                                    : command_autocomplete_.prefix.substr(0, first_non_space);
    completed = leading + name;
    if (CommandTakesArguments(name)) {
      completed += " ";
    }
  } else {
    const size_t last_space = command_autocomplete_.prefix.find_last_of(" \t");
    if (last_space != std::string::npos) {
      completed = command_autocomplete_.prefix.substr(0, last_space + 1) + name;
    } else {
      completed = name;
    }
  }

  command_text_ = completed;
  command_vim_.cursor = command_text_.size();
  vim::Clamp(command_vim_, command_text_);
}

int BrowserWindow::CommandAutocompleteVisibleRows() const {
  if (!command_autocomplete_.active || command_autocomplete_.matches.empty()) {
    return 0;
  }
  return std::min(kCommandAutocompleteMaxVisible,
                  static_cast<int>(command_autocomplete_.matches.size()));
}

int BrowserWindow::CommandAutocompleteHeight() const {
  const int visible = CommandAutocompleteVisibleRows();
  if (visible == 0) {
    return 0;
  }
  return visible * kCommandAutocompleteRowHeight + kCommandAutocompleteBorder * 2;
}

int BrowserWindow::CommandAutocompleteWidth() const {
  if (!command_autocomplete_.active || command_autocomplete_.matches.empty()) {
    return 0;
  }
  int max_name = 0;
  int max_desc = 0;
  for (const CompletionItem& item : command_autocomplete_.matches) {
    max_name = std::max(max_name, TextColumns(item.name));
    max_desc = std::max(max_desc, TextColumns(item.description));
  }
  return (max_name + max_desc + 7) * kCommandCharWidth +
         kCommandAutocompleteHPadding * 2 + kCommandAutocompleteBorder * 2;
}

bool BrowserWindow::CycleCommandAutocomplete(int direction) {
  if (!command_autocomplete_.active) {
    UpdateCommandAutocomplete();
  }
  if (!command_autocomplete_.active || command_autocomplete_.matches.empty()) {
    return false;
  }

  const int size = static_cast<int>(command_autocomplete_.matches.size());
  if (direction > 0) {
    command_autocomplete_.selection = command_autocomplete_.selection < 0
                                          ? 0
                                          : (command_autocomplete_.selection + 1) % size;
  } else {
    command_autocomplete_.selection = command_autocomplete_.selection <= 0
                                          ? size - 1
                                          : command_autocomplete_.selection - 1;
  }
  FillCommandAutocomplete(
      command_autocomplete_.matches[static_cast<size_t>(command_autocomplete_.selection)].name);
  SetCommandText(command_text_);
  Layout();
  return true;
}

bool BrowserWindow::HandleCommandModeKey(const CefKeyEvent& event) {
  if (mode_ == Mode::kNormal) {
    return false;
  }

  // Some platform textfield edit commands are applied natively without reaching
  // our key model. Synchronize those insert-mode edits before handling the next
  // modeled key. In normal mode the vim model is authoritative: the textfield is
  // only a renderer for text/cursor state, and syncing it can resurrect stale
  // native contents after commands like dd/D/cw just rewrote command_text_.
  if (command_vim_.mode == vim::Mode::kInsert && !suppress_next_char_event_) {
    SyncCommandTextFromField();
  }

  if (IsTabKey(event)) {
    if ((IsRawKeyDown(event) || event.type == KEYEVENT_KEYDOWN) &&
        command_vim_.mode == vim::Mode::kInsert) {
      CycleCommandAutocomplete((event.modifiers & EVENTFLAG_SHIFT_DOWN) ? -1 : 1);
    }
    return true;
  }

  auto apply_result = [&](const vim::LineEditResult& result) {
    if (result.submit) {
      CommitCommand();
      return;
    }
    if (result.cancel) {
      CancelCommand();
      return;
    }
    if (result.text_changed || result.cursor_changed) {
      if (command_vim_.mode == vim::Mode::kInsert) {
        UpdateCommandAutocomplete();
      } else {
        ClearCommandAutocomplete();
      }
      Layout();
    }
    if (result.mode_changed) {
      ClearCommandAutocomplete();
      Layout();
      UpdateModeIndicator();
    }
    if (result.text_changed || result.cursor_changed || result.mode_changed || result.pending) {
      SetCommandText(command_text_);
    }
  };

  auto process_key = [&](vim::KeyInput key, bool suppress_char) {
    const vim::Mode old_mode = command_vim_.mode;
    const size_t old_cursor = command_vim_.cursor;
    const std::string old_text = command_text_;
    vim::LineEditResult result = vim::HandleLineEditKey(command_vim_, command_text_, key);
    if (!result.handled) {
      return false;
    }
    if (command_text_ != old_text) result.text_changed = true;
    if (command_vim_.cursor != old_cursor) result.cursor_changed = true;
    if (command_vim_.mode != old_mode) result.mode_changed = true;
    apply_result(result);
    if (suppress_char) suppress_next_char_event_ = true;
    return true;
  };

  const bool key_down = IsRawKeyDown(event) || event.type == KEYEVENT_KEYDOWN;
  if (key_down) {
    if (IsEnterKey(event)) {
      return process_key({vim::KeyType::kEnter}, false);
    }
    if (IsEscapeKey(event)) {
      const bool shifted = event.modifiers & EVENTFLAG_SHIFT_DOWN;
      return process_key({vim::KeyType::kEscape, 0, shifted}, false);
    }
    if (IsBackspaceKey(event)) {
      return process_key({vim::KeyType::kBackspace}, true);
    }
  }

  if (IsRawKeyDown(event)) {
    const char key = PlainKeyChar(event);
    if (key) {
      return process_key({vim::KeyType::kChar, key,
                          static_cast<bool>(event.modifiers & EVENTFLAG_SHIFT_DOWN)},
                         true);
    }
    return true;
  }

  if (key_down) {
    return true;
  }

  if (IsCharEvent(event) || event.type == KEYEVENT_KEYUP) {
    if (suppress_next_char_event_) {
      suppress_next_char_event_ = false;
      SetCommandText(command_text_);
      return true;
    }
    if (event.type == KEYEVENT_KEYUP) {
      return true;
    }
    const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
    const bool alt = event.modifiers & EVENTFLAG_ALT_DOWN;
    const bool command = event.modifiers & EVENTFLAG_COMMAND_DOWN;
    const char16_t c = event.character ? event.character : event.unmodified_character;
    if (IsBackspaceKey(event)) {
      return process_key({vim::KeyType::kBackspace}, false);
    }
    if (!ctrl && !alt && !command && IsPrintableAscii(c)) {
      return process_key({vim::KeyType::kChar, static_cast<char>(c),
                          static_cast<bool>(event.modifiers & EVENTFLAG_SHIFT_DOWN)},
                         false);
    }
    return true;
  }

  return true;
}

void BrowserWindow::SetCommandText(std::string text) {
  command_text_ = std::move(text);
  vim::Clamp(command_vim_, command_text_);
  UpdateCommandView();
  UpdateAutocompleteView();
}

bool BrowserWindow::SyncCommandTextFromField() {
  if (!command_field_) {
    return false;
  }

  std::string text = command_field_->GetText().ToString();
  size_t cursor = std::min(command_field_->GetCursorPosition(), text.size());
  if (command_text_.empty() && text == " ") {
    // Empty command-normal mode renders one harmless space as the block cursor
    // target. If CEF still reports that rendered placeholder after returning to
    // insert mode, do not sync it into the real command model; otherwise typing
    // ':' after dd creates a hidden trailing space and autocomplete refuses to
    // open because the model cursor is no longer at end-of-line.
    text.clear();
    cursor = 0;
  }
  if (text == command_text_ && cursor == command_vim_.cursor) {
    return false;
  }

  command_text_ = text;
  command_vim_.cursor = cursor;
  vim::Clamp(command_vim_, command_text_);
  if (command_vim_.mode == vim::Mode::kInsert) {
    UpdateCommandAutocomplete();
  } else {
    ClearCommandAutocomplete();
  }
  return true;
}

void BrowserWindow::UpdateCommandView() {
  RebuildCommandCells();
}

void BrowserWindow::UpdateAutocompleteView() {
  if (autocomplete_overlay_) {
    autocomplete_overlay_->SetVisible(mode_ != Mode::kNormal &&
                                      command_autocomplete_.active &&
                                      !command_autocomplete_.matches.empty());
  }
  RebuildAutocompleteRows();
}

void BrowserWindow::RebuildCommandCells() {
  if (!command_field_) {
    return;
  }

  const size_t cursor = vim::CursorDisplayOffset(command_vim_, command_text_);
  const bool normal = command_vim_.mode == vim::Mode::kNormal;
  size_t command_end = 0;
  size_t open_arg_start = 0;
  size_t open_arg_end = 0;
  const size_t first_non_space = command_text_.find_first_not_of(" \t");
  if (first_non_space != std::string::npos && command_text_[first_non_space] == ':') {
    const size_t command_start = first_non_space;
    size_t command_stop = command_text_.find_first_of(" \t", command_start);
    if (command_stop == std::string::npos) {
      command_stop = command_text_.size();
    }
    const std::string typed_command = ToLowerAscii(
        command_text_.substr(command_start, command_stop - command_start));
    for (const CompletionItem& item : CommandList()) {
      if (item.name == typed_command) {
        command_end = command_stop;
        break;
      }
    }

    if (typed_command == ":open") {
      size_t arg_start = command_stop;
      while (arg_start < command_text_.size() &&
             std::isspace(static_cast<unsigned char>(command_text_[arg_start]))) {
        ++arg_start;
      }
      if (arg_start < command_text_.size()) {
        size_t arg_stop = command_text_.find_first_of(" \t", arg_start);
        if (arg_stop == std::string::npos) {
          arg_stop = command_text_.size();
        }
        const std::string first_arg = ToLowerAscii(
            command_text_.substr(arg_start, arg_stop - arg_start));
        if (first_arg == "tab" || first_arg == "-t") {
          open_arg_start = arg_start;
          open_arg_end = arg_stop;
        }
      }
    }
  }

  const std::string rendered_text =
      normal && command_text_.empty() ? std::string(" ") : command_text_;
  const size_t previous_rendered_length =
      command_field_->GetText().ToString().size();
  if (previous_rendered_length > rendered_text.size()) {
    // CEF textfields can leave stale glyphs behind when their contents shrink
    // after a programmatic vim edit (dd/D/cw/etc). Paint over the old contents
    // with spaces before installing the real model text so deletions visibly
    // erase instead of only moving the native caret/selection.
    command_field_->SetText(std::string(previous_rendered_length, ' '));
  }
  command_field_->SetText(rendered_text);
  StyleCommandField(command_field_);
  if (!command_text_.empty()) {
    command_field_->ApplyTextColor(theme::kText,
                                   CefRange(0, static_cast<uint32_t>(command_text_.size())));
  }
  if (command_end > first_non_space) {
    command_field_->ApplyTextColor(theme::kCommand,
                                   CefRange(static_cast<uint32_t>(first_non_space),
                                            static_cast<uint32_t>(command_end)));
  }
  if (open_arg_end > open_arg_start) {
    command_field_->ApplyTextColor(theme::kCommand,
                                   CefRange(static_cast<uint32_t>(open_arg_start),
                                            static_cast<uint32_t>(open_arg_end)));
  }
  if (normal) {
    const size_t selection_end = std::min(cursor + 1, rendered_text.size());
    command_field_->SelectRange(
        CefRange(static_cast<uint32_t>(cursor), static_cast<uint32_t>(selection_end)));
  } else {
    command_field_->SelectRange(
        CefRange(static_cast<uint32_t>(cursor), static_cast<uint32_t>(cursor)));
  }
  if (mode_ != Mode::kNormal && !command_field_->HasFocus()) {
    command_field_->RequestFocus();
  }
}

void BrowserWindow::RebuildAutocompleteRows() {
  if (!autocomplete_panel_) {
    return;
  }
  autocomplete_panel_->SetBackgroundColor(theme::kSidebarBg);

  for (auto& row : autocomplete_rows_) {
    autocomplete_panel_->RemoveChildView(row);
  }
  autocomplete_rows_.clear();

  if (!command_autocomplete_.active) {
    return;
  }

  int visible = CommandAutocompleteVisibleRows();
  int start = 0;
  if (static_cast<int>(command_autocomplete_.matches.size()) > visible &&
      command_autocomplete_.selection >= 0) {
    start = std::max(0, std::min(command_autocomplete_.selection - visible / 2,
                                 static_cast<int>(command_autocomplete_.matches.size()) - visible));
  }

  int max_name = 0;
  for (const CompletionItem& item : command_autocomplete_.matches) {
    max_name = std::max(max_name, static_cast<int>(item.name.size()));
  }

  const int width = std::max(1, CommandAutocompleteWidth());
  const int row_width = std::max(1, width - kCommandAutocompleteBorder * 2);
  for (int r = 0; r < visible; ++r) {
    const int index = start + r;
    if (index < 0 || index >= static_cast<int>(command_autocomplete_.matches.size())) {
      break;
    }
    const CompletionItem& item = command_autocomplete_.matches[index];
    const bool selected = index == command_autocomplete_.selection;
    std::string text = "  ";
    const size_t name_start = text.size();
    text += item.name;
    const size_t name_end = text.size();
    if (static_cast<int>(item.name.size()) < max_name) {
      text.append(static_cast<size_t>(max_name - item.name.size()), ' ');
    }
    text += "  ";
    const size_t description_start = text.size();
    text += item.description;

    CefRefPtr<CefTextfield> row = CefTextfield::CreateTextfield(this);
    row->SetText(text);
    row->SetID(kAutocompleteRowBaseId + index);
    StyleTextfield(row, theme::kText,
                   selected ? theme::kSidebarSelBg : theme::kSidebarBg);
    row->ApplyTextColor(theme::kText,
                        CefRange(static_cast<uint32_t>(name_start),
                                 static_cast<uint32_t>(name_end)));
    if (description_start < text.size()) {
      row->ApplyTextColor(theme::kDim,
                          CefRange(static_cast<uint32_t>(description_start),
                                   static_cast<uint32_t>(text.size())));
    }
    autocomplete_panel_->AddChildView(row);
    row->SetBounds(CefRect(kCommandAutocompleteBorder,
                           kCommandAutocompleteBorder + r * kCommandAutocompleteRowHeight,
                           row_width, kCommandAutocompleteRowHeight));
    autocomplete_rows_.push_back(row);
  }
  autocomplete_panel_->InvalidateLayout();
  if (autocomplete_panel_->GetLayout()) {
    autocomplete_panel_->Layout();
  }
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
  const int main_height = height;
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
  RestyleView(command_panel_);
  RestyleView(command_content_panel_);
  RestyleView(command_separator_panel_);
  RestyleView(autocomplete_panel_);
  RestyleView(mode_indicator_panel_);
  RestyleView(mode_indicator_label_);
  RestyleView(fps_indicator_panel_);
  RestyleView(fps_indicator_label_);
  main_panel_->SetSize(CefSize(width, main_height));
  sidebar_panel_->SetSize(CefSize(sidebar_visible_ ? kSidebarWidth : 0, main_height));
  sidebar_content_panel_->SetSize(CefSize(kSidebarWidth - 1, main_height));
  sidebar_border_panel_->SetSize(CefSize(1, main_height));
  const int content_x = sidebar_visible_ ? kSidebarWidth : 0;
  const int content_width = std::max(1, width - content_x);
  const bool content_size_changed = content_width != laid_out_content_width_ ||
                                    main_height != laid_out_content_height_;
  content_inner_panel_->SetBounds(CefRect(0, 0, content_width, main_height));
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
  sidebar_border_panel_->SetSize(CefSize(1, main_height));
  sidebar_border_panel_->SetBounds(CefRect(kSidebarWidth - 1, 0, 1, main_height));
  sidebar_border_panel_->SetBackgroundColor(focus_area_ == FocusArea::kTabSidebar
                                                ? theme::kAccent
                                                : theme::kBorderUnfocused);
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
  if (content_size_changed && content_panel_->GetLayout()) {
    content_panel_->Layout();
  }
  if (content_size_changed && content_inner_panel_->GetLayout()) {
    content_inner_panel_->Layout();
  }
  laid_out_content_width_ = content_width;
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

void BrowserWindow::RefreshSidebar() {
  if (!sidebar_content_panel_) {
    return;
  }

  if (sidebar_rows_.size() == tabs_.size() && sidebar_spacer_) {
    for (size_t i = 0; i < tabs_.size(); ++i) {
      CefRefPtr<CefTextfield> row = sidebar_rows_[i].row;
      if (!row) {
        continue;
      }
      const bool active = i == active_index_;
      row->SetText(SidebarTextForTab(i, tabs_[i].url, active));
      row->SelectRange(CefRange(0, 0));
      StyleTextfield(row, theme::kText,
                     active ? theme::kSidebarSelBg : theme::kSidebarBg,
                     "monospace, 12px");
    }
    return;
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

  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool active = i == active_index_;
    const std::string text = SidebarTextForTab(i, tabs_[i].url, active);

    const cef_color_t row_bg = active ? theme::kSidebarSelBg : theme::kSidebarBg;
    CefRefPtr<CefTextfield> row = CefTextfield::CreateTextfield(this);
    row->SetText(text);
    row->SelectRange(CefRange(0, 0));
    row->SetID(kSidebarRowBaseId + static_cast<int>(i));
    StyleTextfield(row, theme::kText, row_bg, "monospace, 12px");
    sidebar_content_panel_->AddChildView(row);
    sidebar_rows_.push_back({row});
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
}

void BrowserWindow::SetFocusArea(FocusArea area) {
  ResetWebsitePendingKeys();
  if (area == FocusArea::kTabSidebar && !sidebar_visible_) {
    sidebar_visible_ = true;
  }
  focus_area_ = area;
  if (focus_area_ == FocusArea::kWebView) {
    if (Tab* tab = ActiveTab(); tab) {
      tab->view->RequestFocus();
    }
  }
  RefreshSidebar();
  UpdateModeIndicator();
  Layout();
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

  if (IsCtrlKey(event, 'M')) {
    ToggleSidebar();
    return true;
  }

  if (IsCtrlKey(event, 'J') || IsCtrlKey(event, 'K')) {
    if (!sidebar_visible_) {
      SetFocusArea(FocusArea::kWebView);
      return true;
    }
    SetFocusArea(focus_area_ == FocusArea::kTabSidebar ? FocusArea::kWebView
                                                       : FocusArea::kTabSidebar);
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
    if (IsEscapeKey(event)) {
      ResetWebsitePendingKeys();
      if ((event.modifiers & EVENTFLAG_SHIFT_DOWN) &&
          website_mode_ == vim::Mode::kInsert) {
        website_mode_ = vim::Mode::kWebsiteNormal;
        UpdateModeIndicator();
        return true;
      }
      if (website_mode_ == vim::Mode::kInsert) {
        website_mode_ = vim::Mode::kNormal;
      } else if (website_mode_ == vim::Mode::kNormal ||
                 website_mode_ == vim::Mode::kVisual) {
        website_mode_ = vim::Mode::kWebsiteNormal;
      }
      UpdateModeIndicator();
      return true;
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
        UpdateModeIndicator();
        return true;
      }

      // Website-normal is the future home for hinting, page scrolling, and
      // qutebrowser-like page commands. Until those bindings exist, keep plain
      // printable keys out of the page. Use insert mode for page text input.
      if (IsPlainPrintableKey(event)) {
        return true;
      }
      return false;
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
        UpdateModeIndicator();
        return true;
      }

      // Regular Vim normal/visual modes are skeleton states for future operators,
      // text objects, and selections. For now they intentionally swallow plain
      // printable keys and perform no page action.
      if (IsPlainPrintableKey(event)) {
        return true;
      }
      return false;
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
    return IsPlainPrintableKey(event);
  }

  return false;
}

std::optional<bool> BrowserWindow::HandlePageShortcut(
    const CefKeyEvent& event,
    bool allow_forward_to_page) {
  if (focus_area_ != FocusArea::kWebView || native_hints_active_) {
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

  if (website_mode_ == vim::Mode::kInsert && event.focus_on_editable_field) {
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
      IsCharEvent(event), plain_without_shift, shortcut_mode);

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

bool BrowserWindow::StartNativeHints(const CefKeyEvent& event) {
  if (!IsRawKeyDown(event) || !IsPlain(event) || event.windows_key_code != 'F') {
    return false;
  }

  Tab* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) {
    return false;
  }

  ResetWebsitePendingKeys();
  native_hints_active_ = true;
  UpdateModeIndicator();
  tab->client->SendBrowserCommandKeyEvent(event);
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
    view->SetBackgroundColor(focus_area_ == FocusArea::kTabSidebar
                                 ? theme::kAccent
                                 : theme::kBorderUnfocused);
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
  UpdateFpsIndicator();
  ScheduleFpsIndicatorUpdate();
}

void BrowserWindow::SetShowFpsIndicator(bool visible) {
  show_fps_indicator_ = visible;
  SaveState();
  UpdateFpsIndicator();
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

std::string BrowserWindow::HandleIpcCommand(const std::string& command_line) {
  const std::vector<std::string> argv = SplitArgs(command_line);
  if (argv.empty()) {
    return "ERR empty command\n";
  }

  const std::string command = ToLowerAscii(argv[0]);
  if (command == "version" || command == "protocol") {
    return IpcVersionJson();
  }
  if (command == "status" || command == "json") {
    return IpcStatusJson();
  }
  if (command == "fps") {
    if (Tab* tab = ActiveTab(); tab && tab->client && tab->client->fps_has_sample()) {
      return std::to_string(static_cast<int>(std::round(tab->client->current_fps())));
    }
    return "--";
  }
  if (command == "refresh") {
    if (Tab* tab = ActiveTab(); tab && tab->client) {
      return std::to_string(tab->client->compositor_refresh_rate()) + "\n";
    }
    return "0\n";
  }
  if (command == "url") {
    return ActiveTabUrl();
  }
  if (command == "showfps") {
    if (argv.size() == 1) {
      SetShowFpsIndicator(!show_fps_indicator_);
      return IpcStatusJson();
    }
    const std::string arg = ToLowerAscii(argv[1]);
    if (arg == "on" || arg == "1" || arg == "true") {
      SetShowFpsIndicator(true);
      return IpcStatusJson();
    }
    if (arg == "off" || arg == "0" || arg == "false") {
      SetShowFpsIndicator(false);
      return IpcStatusJson();
    }
    return "ERR usage: showfps [on|off]\n";
  }
  if (command == "shader") {
    if (argv.size() == 1) {
      SetShaderEnabled(!shader_enabled_);
      return IpcStatusJson();
    }
    const std::string arg = ToLowerAscii(argv[1]);
    if (arg == "on" || arg == "1" || arg == "true") {
      SetShaderEnabled(true);
      return IpcStatusJson();
    }
    if (arg == "off" || arg == "0" || arg == "false") {
      SetShaderEnabled(false);
      return IpcStatusJson();
    }
    return "ERR usage: shader [on|off]\n";
  }
  if (command == "scroll") {
    if (argv.size() < 2) {
      return "ERR usage: scroll <dy> [count]\n";
    }
    char* end = nullptr;
    const long dy = std::strtol(argv[1].c_str(), &end, 10);
    if (end == argv[1].c_str()) {
      return "ERR invalid dy\n";
    }
    int count = 1;
    if (argv.size() >= 3) {
      char* count_end = nullptr;
      count = static_cast<int>(std::strtol(argv[2].c_str(), &count_end, 10));
      if (count_end == argv[2].c_str()) {
        return "ERR invalid count\n";
      }
    }
    count = std::clamp(count, 1, 100);
    for (int i = 0; i < count; ++i) {
      ScrollActivePageBy(static_cast<int>(dy));
    }
    return IpcStatusJson();
  }
  if (command == "tab") {
    if (argv.size() != 2) {
      return "ERR usage: tab <1-based-index>\n";
    }
    char* end = nullptr;
    const long index = std::strtol(argv[1].c_str(), &end, 10);
    if (end == argv[1].c_str() || index <= 0) {
      return "ERR invalid tab index\n";
    }
    ActivateTab(static_cast<size_t>(index - 1));
    return IpcStatusJson();
  }
  if (command == "tab-close") {
    if (argv.size() != 1) {
      return "ERR usage: tab-close\n";
    }
    CloseActiveTab();
    return IpcStatusJson();
  }
  if (command == "help") {
    return "commands: version, status, fps, refresh, url, showfps [on|off], shader [on|off], scroll <dy> [count], tab <index>, tab-close\n";
  }
  return "ERR unknown command\n";
}

std::string BrowserWindow::IpcStatusJson() const {
  bool fps_has_sample = false;
  double fps = 0.0;
  double refresh_rate = 0.0;
  std::string url;
  std::string title;
  if (!tabs_.empty() && active_index_ < tabs_.size()) {
    const Tab& tab = tabs_[active_index_];
    url = tab.url;
    if (tab.client) {
      fps_has_sample = tab.client->fps_has_sample();
      fps = tab.client->current_fps();
      refresh_rate = tab.client->compositor_refresh_rate();
    }
    if (CefRefPtr<CefBrowser> browser = tab.client ? tab.client->browser() : nullptr;
        browser && browser->GetHost()) {
      CefRefPtr<CefNavigationEntry> entry = browser->GetHost()->GetVisibleNavigationEntry();
      if (entry) {
        title = entry->GetTitle().ToString();
      }
    }
  }

  std::ostringstream out;
  out << "{"
      << "\"ipc_protocol\":\"" << kIpcProtocolName << "\","
      << "\"ipc_version\":" << kIpcProtocolVersion << ","
      << "\"active_index\":" << active_index_ << ","
      << "\"active_tab\":" << (active_index_ + 1) << ","
      << "\"tabs\":" << tabs_.size() << ","
      << "\"url\":\"" << JsonEscape(url) << "\","
      << "\"title\":\"" << JsonEscape(title) << "\","
      << "\"showfps\":" << (show_fps_indicator_ ? "true" : "false") << ","
      << "\"shader\":" << (shader_enabled_ ? "true" : "false") << ","
      << "\"fps_has_sample\":" << (fps_has_sample ? "true" : "false") << ","
      << "\"fps\":" << (fps_has_sample ? std::to_string(static_cast<int>(std::round(fps))) : "null")
      << ",\"refresh_rate\":" << refresh_rate
      << "}";
  return out.str();
}

void BrowserWindow::SaveState() const {
  AppState state;
  state.active_index = active_index_;
  state.show_mode_indicator = show_mode_indicator_;
  state.show_fps_indicator = show_fps_indicator_;
  state.shader_enabled = shader_enabled_;
  state.open_history = open_history_;
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

Tab* BrowserWindow::ActiveTab() {
  if (tabs_.empty() || active_index_ >= tabs_.size()) {
    return nullptr;
  }
  return &tabs_[active_index_];
}

}  // namespace vimbrowser
