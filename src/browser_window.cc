#include "browser_window.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <iostream>
#include <utility>

#include "config.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
#include "include/cef_navigation_entry.h"
#include "include/views/cef_button.h"
#include "theme.h"

namespace vimbrowser {
namespace {

constexpr int kSidebarWidth = 175;
constexpr int kCommandHeight = 28;
constexpr int kCommandAutocompleteRowHeight = 28;
constexpr int kCommandAutocompleteMaxVisible = 8;
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
constexpr int kCommandCursorPanelId = 116;
constexpr int kSidebarRowBaseId = 2000;
constexpr int kSidebarMarkerBaseId = 3000;
constexpr int kSidebarLabelBaseId = 4000;
constexpr int kAutocompleteRowBaseId = 6000;
constexpr int kSidebarRowHeight = 24;
constexpr int kSidebarMarkerWidth = 26;
constexpr int kCommandCursorTop = 5;
constexpr int kCommandCursorHeight = 18;
constexpr int kCommandCursorBarWidth = 2;
constexpr int kCommandCursorBlockWidth = 8;
// Experimental chrome-level mode indicator. Flip to false to disable without
// touching the mode/focus state machines.
constexpr bool kModeIndicatorEnabled = true;
constexpr int kModeIndicatorWidth = 96;
constexpr int kModeIndicatorHeight = 24;
constexpr int kCommandTextInsetX = 10;
constexpr int kCommandCharWidth = 8;

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

const std::vector<CompletionItem>& CommandList() {
  static const std::vector<CompletionItem> commands = {
      {":open", "open URL/search in current tab"},
      {":tab-focus", "focus tab by number or title"},
  };
  return commands;
}

const std::vector<CompletionItem>& OpenArgList() {
  static const std::vector<CompletionItem> args = {
      {"tab", "open in a new tab"},
  };
  return args;
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
  return event.windows_key_code == 0x08 || event.native_key_code == 22;
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

bool IsWhitespaceOnly(const std::string& value) {
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isspace(c);
  });
}

bool IsTokenBoundary(const std::string& value, size_t pos) {
  return pos >= value.size() || std::isspace(static_cast<unsigned char>(value[pos]));
}

int TextColumns(const std::string& value) {
  return static_cast<int>(value.size());
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

BrowserWindow::BrowserWindow(std::string initial_url)
    : initial_url_(std::move(initial_url)) {}

void BrowserWindow::Create() {
  CefWindow::CreateTopLevelWindow(this);
}

void BrowserWindow::OnClientBrowserCreated(BrowserClient* client) {
  RefreshSidebar();
}

void BrowserWindow::OnClientLoadStart(BrowserClient* client, const std::string& url) {
  for (Tab& tab : tabs_) {
    if (tab.client.get() == client) {
      tab.url = url;
      if (url != "about:blank") {
        last_tab_close_placeholder_ = false;
      }
      RefreshSidebar();
      return;
    }
  }
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
  BuildChrome();
  AddTab(initial_url_, true);
  RefreshSidebar();

  window_->CenterWindow(CefSize(1200, 800));
  window_->Show();
  Layout();
  SetFocusArea(FocusArea::kWebView);
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
  command_separator_panel_->SetBackgroundColor(theme::kBorderFocused);
  command_panel_->AddChildView(command_separator_panel_);

  command_content_panel_ = CefPanel::CreatePanel(this);
  command_content_panel_->SetID(kCommandContentPanelId);
  command_content_panel_->SetBackgroundColor(theme::kAppBg);
  command_panel_->AddChildView(command_content_panel_);

  command_cursor_panel_ = CefPanel::CreatePanel(nullptr);
  command_cursor_panel_->SetID(kCommandCursorPanelId);
  command_cursor_panel_->SetBackgroundColor(theme::kVimNormal);
  command_content_panel_->AddChildView(command_cursor_panel_);

  command_field_ = CefTextfield::CreateTextfield(this);
  command_field_->SetID(kCommandFieldId);
  StyleTextfield(command_field_, theme::kText, theme::kTransparent);
  command_field_->SetAccessibleName("vimbrowser command line");
  command_content_panel_->AddChildView(command_field_);

  command_overlay_ = window_->AddOverlayView(command_panel_, CEF_DOCKING_MODE_CUSTOM,
                                            false);
  command_overlay_->SetVisible(false);

  autocomplete_panel_ = CefPanel::CreatePanel(this);
  autocomplete_panel_->SetID(kCommandAutocompletePanelId);
  autocomplete_panel_->SetBackgroundColor(theme::kSidebarBg);
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
  }
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  tabs_.clear();
  mode_indicator_overlay_ = nullptr;
  autocomplete_overlay_ = nullptr;
  command_overlay_ = nullptr;
  mode_indicator_label_ = nullptr;
  mode_indicator_panel_ = nullptr;
  autocomplete_rows_.clear();
  autocomplete_panel_ = nullptr;
  command_cursor_panel_ = nullptr;
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
  for (Tab& tab : tabs_) {
    if (tab.client && tab.client->browser()) {
      tab.client->browser()->GetHost()->CloseBrowser(false);
    }
  }
  return true;
}

bool BrowserWindow::OnKeyEvent(CefRefPtr<CefWindow> window,
                               const CefKeyEvent& event) {
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

  return false;
}

void BrowserWindow::OnAfterUserAction(CefRefPtr<CefTextfield> textfield) {
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
  if (!IsRawKeyDown(event) || mode_ == Mode::kNormal) {
    return false;
  }

  if (IsEnterKey(event)) {
    CommitCommand();
    return true;
  }

  if (IsEscapeKey(event)) {
    CancelCommand();
    return true;
  }

  return false;
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
  if (InIdRange(id, kSidebarMarkerBaseId, 1000)) {
    return CefSize(kSidebarMarkerWidth, kSidebarRowHeight);
  }
  if (InIdRange(id, kSidebarLabelBaseId, 1000)) {
    return CefSize(kSidebarWidth - 1 - kSidebarMarkerWidth, kSidebarRowHeight);
  }
  if (id == kSidebarSpacerId) {
    return CefSize(kSidebarWidth - 1, 1);
  }
  if (id == kCommandCursorPanelId) {
    return CefSize(kCommandCursorBlockWidth, kCommandCursorHeight);
  }
  if (id == kCommandFieldId) {
    return CefSize(1200, kCommandHeight);
  }
  if (InIdRange(id, kAutocompleteRowBaseId, 1000)) {
    return CefSize(std::max(1, CommandAutocompleteWidth()),
                   kCommandAutocompleteRowHeight);
  }
  if (id == kModeIndicatorPanelId) {
    return CefSize(kModeIndicatorWidth, kModeIndicatorHeight);
  }
  if (id == kModeIndicatorFieldId) {
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
      InIdRange(id, kSidebarMarkerBaseId, 1000) ||
      InIdRange(id, kSidebarLabelBaseId, 1000) ||
      id == kSidebarSpacerId ||
      id == kCommandCursorPanelId ||
      id == kCommandFieldId ||
      InIdRange(id, kAutocompleteRowBaseId, 1000)) {
    return CefSize(1, 1);
  }
  if (id == kModeIndicatorPanelId || id == kModeIndicatorFieldId) {
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
  if (id == kModeIndicatorPanelId || id == kModeIndicatorFieldId) {
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
  last_tab_close_placeholder_ = false;
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

  tabs_.push_back(tab);
  RefreshSidebar();
  Layout();

  if (activate) {
    ActivateTab(tabs_.size() - 1);
  }
}

void BrowserWindow::ActivateTab(size_t index) {
  if (tabs_.empty() || index >= tabs_.size()) {
    return;
  }

  if (active_index_ < tabs_.size()) {
    tabs_[active_index_].view->SetVisible(false);
  }

  active_index_ = index;
  tabs_[active_index_].view->SetVisible(true);
  if (focus_area_ == FocusArea::kWebView) {
    tabs_[active_index_].view->RequestFocus();
  }
  RefreshSidebar();
  Layout();
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
  std::swap(tabs_[active_index_], tabs_[static_cast<size_t>(next)]);
  active_index_ = static_cast<size_t>(next);
  RefreshSidebar();
  Layout();
}

void BrowserWindow::CloneActiveTab() {
  const std::string url = ActiveTabUrl();
  if (!url.empty()) {
    AddTab(url, true);
  }
}

void BrowserWindow::CloseActiveTab() {
  if (tabs_.empty()) {
    return;
  }

  const size_t closing = active_index_;
  const std::string url = ActiveTabUrl();
  std::cerr << "vimbrowser: close-tab index=" << (closing + 1)
            << " count=" << tabs_.size() << " url=" << url << std::endl;
  if (!url.empty()) {
    closed_tab_urls_.push_back(url);
  }

  if (tabs_.size() == 1) {
    if (Tab* tab = ActiveTab(); tab && tab->client && tab->client->browser()) {
      tab->url = "about:blank";
      last_tab_close_placeholder_ = true;
      tab->client->browser()->GetMainFrame()->LoadURL(tab->url);
      RefreshSidebar();
    }
    return;
  }

  if (tabs_[closing].view) {
    tabs_[closing].view->SetVisible(false);
  }

  Tab closed_tab = tabs_[closing];

  const size_t next_index = closing == 0 ? 0 : closing - 1;
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(closing));
  active_index_ = std::min(next_index, tabs_.size() - 1);
  tabs_[active_index_].view->SetVisible(true);
  if (focus_area_ == FocusArea::kWebView) {
    tabs_[active_index_].view->RequestFocus();
  }
  RefreshSidebar();
  Layout();
  closed_tabs_.push_back(closed_tab);
  last_tab_close_placeholder_ = false;
}

void BrowserWindow::UndoCloseTab() {
  if (!closed_tabs_.empty()) {
    Tab tab = closed_tabs_.back();
    closed_tabs_.pop_back();
    std::cerr << "vimbrowser: undo-close-tab view url=" << tab.url
              << " count=" << tabs_.size() << std::endl;
    tab.view->SetVisible(false);
    tabs_.push_back(tab);
    RefreshSidebar();
    Layout();
    ActivateTab(tabs_.size() - 1);
    return;
  }

  if (closed_tab_urls_.empty()) {
    std::cerr << "vimbrowser: undo-close-tab ignored; stack empty" << std::endl;
    return;
  }
  const std::string url = closed_tab_urls_.back();
  closed_tab_urls_.pop_back();
  std::cerr << "vimbrowser: undo-close-tab url=" << url
            << " placeholder=" << last_tab_close_placeholder_
            << " count=" << tabs_.size() << std::endl;
  if (last_tab_close_placeholder_ && tabs_.size() == 1 &&
      active_index_ == 0 && tabs_[0].client && tabs_[0].client->browser()) {
    last_tab_close_placeholder_ = false;
    tabs_[0].url = url;
    tabs_[0].client->browser()->GetMainFrame()->LoadURL(url);
    RefreshSidebar();
    Layout();
    return;
  }
  AddTab(url, true);
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
  Layout();
  SetCommandText(command_text_);
  UpdateModeIndicator();
  if (Tab* tab = ActiveTab(); tab) {
    tab->view->RequestFocus();
  }
}

void BrowserWindow::CommitCommand() {
  std::string text = Trim(command_text_);
  bool open_in_new_tab = mode_ == Mode::kCommandOpenNext;

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
        const std::string haystack = tabs_[i].url;
        if (haystack.find(needle) != std::string::npos) {
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
    AddTab(url, true);
  } else if (Tab* tab = ActiveTab(); tab && tab->client && tab->client->browser()) {
    last_tab_close_placeholder_ = false;
    tab->url = url;
    tab->client->browser()->GetMainFrame()->LoadURL(url);
    RefreshSidebar();
  }
}

void BrowserWindow::CancelCommand() {
  mode_ = Mode::kNormal;
  ClearCommandAutocomplete();
  vim::Reset(command_vim_, 0, 0, vim::Mode::kInsert);
  SetCommandText("");
  command_overlay_->SetVisible(false);
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
  if (!browser || !browser->GetMainFrame()) {
    return;
  }
  browser->GetMainFrame()->ExecuteJavaScript(
      "window.scrollBy({left:0,top:" + std::to_string(dy) + ",behavior:'auto'});",
      browser->GetMainFrame()->GetURL(), 0);
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
      "window.scrollTo({left:0,top:document.scrollingElement?document.scrollingElement.scrollHeight:document.body.scrollHeight,behavior:'auto'});",
      browser->GetMainFrame()->GetURL(), 0);
}

void BrowserWindow::OpenClipboard(bool new_tab) {
  std::string text = Trim(ReadClipboardText());
  if (text.empty()) {
    return;
  }
  const std::string url = ResolveUrlOrSearch(text);
  if (new_tab) {
    AddTab(url, true);
  } else if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    last_tab_close_placeholder_ = false;
    if (active_index_ < tabs_.size()) {
      tabs_[active_index_].url = url;
    }
    browser->GetMainFrame()->LoadURL(url);
    RefreshSidebar();
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

void BrowserWindow::UpdateCommandAutocomplete() {
  ClearCommandAutocomplete();
  if (command_text_.find('\n') != std::string::npos) {
    return;
  }
  if (command_vim_.cursor != command_text_.size()) {
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
    const bool already_has_tab_arg = completed_args.find("tab") != std::string::npos ||
                                     (arg_prefix == "tab" && !after_command.empty() &&
                                      std::isspace(static_cast<unsigned char>(after_command.back())));
    const bool completing_new_arg = IsWhitespaceOnly(after_command) ||
                                    (!after_command.empty() && std::isspace(static_cast<unsigned char>(after_command.back())));
    if (!already_has_tab_arg && (completing_new_arg || !arg_prefix.empty())) {
      for (const CompletionItem& item : OpenArgList()) {
        if (completing_new_arg || StartsWithCaseInsensitive(item.name, arg_prefix)) {
          matches.push_back(item);
        }
      }
    }
  } else if (StartsWithCaseInsensitive(typed_command, ":tab-focus") &&
             IsTokenBoundary(typed_command, 10)) {
    const size_t arg_start = after_command.find_last_of(" \t");
    const std::string arg_prefix = arg_start == std::string::npos ? after_command : after_command.substr(arg_start + 1);
    const bool completing_new_arg = IsWhitespaceOnly(after_command) ||
                                    (!after_command.empty() && std::isspace(static_cast<unsigned char>(after_command.back())));
    if (completing_new_arg || !arg_prefix.empty()) {
      for (size_t i = 0; i < tabs_.size(); ++i) {
        const std::string name = std::to_string(i + 1);
        const std::string description = DisplayUrl(tabs_[i].url);
        if (completing_new_arg || StartsWithCaseInsensitive(name, arg_prefix) ||
            StartsWithCaseInsensitive(description, arg_prefix)) {
          matches.push_back({name, description});
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
  return CommandAutocompleteVisibleRows() * kCommandAutocompleteRowHeight;
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
  return (max_name + max_desc + 6) * kCommandCharWidth +
         kCommandAutocompleteHPadding * 2;
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

  if (IsRawKeyDown(event)) {
    if (IsEnterKey(event)) {
      return process_key({vim::KeyType::kEnter}, false);
    }
    if (IsEscapeKey(event)) {
      return process_key({vim::KeyType::kEscape, 0,
                          static_cast<bool>(event.modifiers & EVENTFLAG_SHIFT_DOWN)},
                         false);
    }
    if (IsBackspaceKey(event)) {
      return process_key({vim::KeyType::kBackspace}, false);
    }
    if (IsTabKey(event)) {
      if (command_vim_.mode == vim::Mode::kInsert && CycleCommandAutocomplete(
              (event.modifiers & EVENTFLAG_SHIFT_DOWN) ? -1 : 1)) {
        return true;
      }
      return true;
    }

    const char key = PlainKeyChar(event);
    if (key) {
      return process_key({vim::KeyType::kChar, key,
                          static_cast<bool>(event.modifiers & EVENTFLAG_SHIFT_DOWN)},
                         true);
    }
    return true;
  }

  if (IsCharEvent(event)) {
    if (suppress_next_char_event_) {
      suppress_next_char_event_ = false;
      return true;
    }
    const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
    const bool alt = event.modifiers & EVENTFLAG_ALT_DOWN;
    const bool command = event.modifiers & EVENTFLAG_COMMAND_DOWN;
    const char16_t c = event.character ? event.character : event.unmodified_character;
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
  if (!command_field_ || !command_cursor_panel_) {
    return;
  }

  const size_t cursor = vim::CursorDisplayOffset(command_vim_, command_text_);
  const bool normal = command_vim_.mode == vim::Mode::kNormal;
  size_t command_end = 0;
  if (StartsWithCaseInsensitive(command_text_, ":open")) {
    command_end = std::min<size_t>(command_text_.size(), 5);
    if (StartsWithCaseInsensitive(command_text_, ":open tab")) {
      command_end = std::min<size_t>(command_text_.size(), 9);
    }
  } else if (StartsWithCaseInsensitive(command_text_, ":tab-focus")) {
    command_end = std::min<size_t>(command_text_.size(), 10);
  }

  command_field_->SetText(command_text_);
  command_field_->SetBackgroundColor(theme::kTransparent);
  command_field_->SetTextColor(theme::kText);
  command_field_->SetSelectionTextColor(theme::kText);
  command_field_->SetSelectionBackgroundColor(theme::kTransparent);
  command_field_->ClearSelection();
  if (command_end > 0) {
    command_field_->ApplyTextColor(theme::kCommand,
                                   CefRange(0, static_cast<uint32_t>(command_end)));
  }
  if (normal && cursor < command_text_.size()) {
    command_field_->ApplyTextColor(theme::kAppBg,
                                   CefRange(static_cast<uint32_t>(cursor),
                                            static_cast<uint32_t>(cursor + 1)));
  }

  const int cursor_x = kCommandTextInsetX + static_cast<int>(cursor) * kCommandCharWidth;
  if (normal) {
    command_cursor_panel_->SetBounds(CefRect(cursor_x, kCommandCursorTop,
                                             kCommandCursorBlockWidth,
                                             kCommandCursorHeight));
  } else {
    command_cursor_panel_->SetBounds(CefRect(cursor_x, kCommandCursorTop,
                                             kCommandCursorBarWidth,
                                             kCommandCursorHeight));
  }
  command_cursor_panel_->SetBackgroundColor(theme::kVimNormal);
  command_cursor_panel_->SetVisible(true);
  command_content_panel_->ReorderChildView(command_cursor_panel_, 0);
  command_content_panel_->ReorderChildView(command_field_, -1);
}

void BrowserWindow::RebuildAutocompleteRows() {
  if (!autocomplete_panel_) {
    return;
  }

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
  for (int r = 0; r < visible; ++r) {
    const int index = start + r;
    if (index < 0 || index >= static_cast<int>(command_autocomplete_.matches.size())) {
      break;
    }
    const CompletionItem& item = command_autocomplete_.matches[index];
    const bool selected = index == command_autocomplete_.selection;
    std::string text = selected ? "▸ " : "  ";
    text += item.name;
    if (static_cast<int>(item.name.size()) < max_name + 1) {
      text.append(static_cast<size_t>(max_name + 1 - item.name.size()), ' ');
    }
    text += item.description;

    CefRefPtr<CefTextfield> row = CefTextfield::CreateTextfield(this);
    row->SetText(text);
    row->SetID(kAutocompleteRowBaseId + index);
    StyleTextfield(row, theme::kText,
                   selected ? theme::kSidebarSelBg : theme::kSidebarBg);
    autocomplete_panel_->AddChildView(row);
    row->SetBounds(CefRect(0, r * kCommandAutocompleteRowHeight,
                           width, kCommandAutocompleteRowHeight));
    autocomplete_rows_.push_back(row);
  }
  autocomplete_panel_->InvalidateLayout();
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
  main_panel_->SetSize(CefSize(width, main_height));
  sidebar_panel_->SetSize(CefSize(sidebar_visible_ ? kSidebarWidth : 0, main_height));
  sidebar_content_panel_->SetSize(CefSize(kSidebarWidth - 1, main_height));
  sidebar_border_panel_->SetSize(CefSize(1, main_height));
  const int content_x = sidebar_visible_ ? kSidebarWidth : 0;
  const int content_width = std::max(1, width - content_x);
  content_inner_panel_->SetBounds(CefRect(0, 0, content_width, main_height));
  command_panel_->SetSize(CefSize(width, command_total_height));
  command_separator_panel_->SetSize(CefSize(width, 1));
  command_content_panel_->SetSize(CefSize(width, kCommandHeight));
  command_separator_panel_->SetBounds(CefRect(0, 0, width, 1));
  command_content_panel_->SetBounds(CefRect(0, 1, width, kCommandHeight));
  if (command_overlay_) {
    command_overlay_->SetBounds(CefRect(0, std::max(0, height - command_total_height),
                                        width, command_total_height));
  }
  if (autocomplete_panel_ && autocomplete_overlay_) {
    autocomplete_panel_->SetSize(CefSize(autocomplete_width, std::max(1, autocomplete_height)));
    autocomplete_overlay_->SetBounds(
        CefRect(0, std::max(0, height - command_total_height - autocomplete_height),
                autocomplete_width, std::max(1, autocomplete_height)));
  }
  if (mode_indicator_overlay_ && mode_indicator_panel_ && mode_indicator_label_) {
    mode_indicator_overlay_->SetVisible(true);
    mode_indicator_overlay_->SetBounds(
        CefRect(std::max(0, width - kModeIndicatorWidth), 0,
                kModeIndicatorWidth, kModeIndicatorHeight));
    mode_indicator_panel_->SetSize(CefSize(kModeIndicatorWidth, kModeIndicatorHeight));
    mode_indicator_label_->SetSize(CefSize(kModeIndicatorWidth, kModeIndicatorHeight));
    mode_indicator_label_->SetBounds(CefRect(0, 0, kModeIndicatorWidth,
                                             kModeIndicatorHeight));
    UpdateModeIndicator();
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
  if (command_field_) {
    command_field_->SetBounds(CefRect(kCommandTextInsetX, 0,
                                      std::max(1, width - kCommandTextInsetX),
                                      kCommandHeight));
  }
  if (command_cursor_panel_) {
    const size_t cursor = vim::CursorDisplayOffset(command_vim_, command_text_);
    const bool normal = command_vim_.mode == vim::Mode::kNormal;
    command_cursor_panel_->SetBounds(
        CefRect(kCommandTextInsetX + static_cast<int>(cursor) * kCommandCharWidth,
                kCommandCursorTop,
                normal ? kCommandCursorBlockWidth : kCommandCursorBarWidth,
                kCommandCursorHeight));
  }
  for (size_t i = 0; i < autocomplete_rows_.size(); ++i) {
    autocomplete_rows_[i]->SetBounds(CefRect(0, static_cast<int>(i) * kCommandAutocompleteRowHeight,
                                            autocomplete_width, kCommandAutocompleteRowHeight));
  }
  if (content_panel_->GetLayout()) {
    content_panel_->Layout();
  }
  if (content_inner_panel_->GetLayout()) {
    content_inner_panel_->Layout();
  }
  if (mode_indicator_panel_ && mode_indicator_panel_->GetLayout()) {
    mode_indicator_panel_->Layout();
  }
  if (autocomplete_panel_ && autocomplete_panel_->GetLayout()) {
    autocomplete_panel_->Layout();
  }
}

void BrowserWindow::RefreshSidebar() {
  if (!sidebar_content_panel_) {
    return;
  }

  for (auto& row : sidebar_rows_) {
    if (row.panel) {
      sidebar_content_panel_->RemoveChildView(row.panel);
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
    std::string text = std::to_string(i + 1);
    text += ": ";
    text += DisplayUrl(tabs_[i].url);
    if (text.size() > 160) {
      text.resize(157);
      text += "...";
    }

    const cef_color_t row_bg = active ? theme::kSidebarSelBg : theme::kSidebarBg;
    CefRefPtr<CefPanel> panel = CefPanel::CreatePanel(nullptr);
    panel->SetID(kSidebarRowBaseId + static_cast<int>(i));
    panel->SetBackgroundColor(row_bg);
    CefBoxLayoutSettings row_settings = {};
    row_settings.size = sizeof(row_settings);
    row_settings.horizontal = true;
    row_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
    CefRefPtr<CefBoxLayout> row_layout = panel->SetToBoxLayout(row_settings);

    CefRefPtr<CefTextfield> marker = CefTextfield::CreateTextfield(this);
    marker->SetText(active ? ">" : " ");
    marker->SetID(kSidebarMarkerBaseId + static_cast<int>(i));
    StyleTextfield(marker, active ? theme::kVimNormal : theme::kSidebarBg,
                   row_bg, "monospace, 12px");

    CefRefPtr<CefTextfield> label = CefTextfield::CreateTextfield(this);
    label->SetText(text);
    label->SetID(kSidebarLabelBaseId + static_cast<int>(i));
    StyleTextfield(label, theme::kText, row_bg, "monospace, 12px");

    panel->AddChildView(marker);
    panel->AddChildView(label);
    row_layout->SetFlexForView(label, 1);
    sidebar_content_panel_->AddChildView(panel);
    sidebar_rows_.push_back({panel, marker, label});
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

      if (website_mode_ == vim::Mode::kNormal && IsPlain(event) &&
          event.windows_key_code == 'O') {
        BeginCommand(event.modifiers & EVENTFLAG_SHIFT_DOWN ? Mode::kCommandOpenNext
                                                            : Mode::kCommandOpenCurrent);
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

    // Insert mode lets the page handle normal input. Escape was handled above.
    return false;
  }

  if (IsCharEvent(event)) {
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

void BrowserWindow::ResetWebsitePendingKeys() {
  website_pending_keys_.clear();
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
    if (IsCtrlKey(event, 'E')) { ScrollActivePageBy(140); return true; }
    if (IsCtrlKey(event, 'Y')) { ScrollActivePageBy(-140); return true; }
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

  switch (key) {
    case 'j': ScrollActivePageBy(280); return true;
    case 'k': ScrollActivePageBy(-280); return true;
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
    case 'd': CloseActiveTab(); return true;
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
    // RefreshSidebar() applies per-row textfield colors so the active marker can
    // have its own accent color.
  } else if (id == kSidebarSpacerId) {
    if (sidebar_spacer_) {
      StyleTextfield(sidebar_spacer_, theme::kText, theme::kSidebarBg,
                     "monospace, 12px");
    }
  } else if (id == kSidebarBorderPanelId) {
    view->SetBackgroundColor(focus_area_ == FocusArea::kTabSidebar
                                 ? theme::kBorderFocused
                                 : theme::kBorderUnfocused);
  } else if (id == kCommandPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandCursorPanelId) {
    view->SetBackgroundColor(theme::kVimNormal);
  } else if (id == kCommandSeparatorPanelId) {
    view->SetBackgroundColor(focus_area_ == FocusArea::kCommandLine
                                 ? theme::kBorderFocused
                                 : theme::kBorderUnfocused);
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
  }
}

void BrowserWindow::UpdateModeIndicator() {
  if (!kModeIndicatorEnabled || !mode_indicator_label_) {
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

std::string BrowserWindow::ModeIndicatorText() const {
  if (focus_area_ == FocusArea::kCommandLine || mode_ != Mode::kNormal) {
    return command_vim_.mode == vim::Mode::kNormal ? "CMD-N" : "CMD-I";
  }
  if (focus_area_ == FocusArea::kTabSidebar) {
    return "SIDEBAR";
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
