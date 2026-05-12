#include "browser_window.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

#include "config.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/views/cef_button.h"
#include "theme.h"

namespace vimbrowser {
namespace {

constexpr int kSidebarWidth = 175;
constexpr int kCommandHeight = 28;
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
// Experimental chrome-level mode indicator. Flip to false to disable without
// touching the mode/focus state machines.
constexpr bool kModeIndicatorEnabled = true;
constexpr int kModeIndicatorWidth = 96;
constexpr int kModeIndicatorHeight = 24;
constexpr int kCommandTextInsetX = 10;
constexpr int kCommandCharWidth = 8;
constexpr int kCommandCursorTop = 5;
constexpr int kCommandCursorHeight = 18;
constexpr int kCommandCursorBarWidth = 2;
constexpr int kCommandCursorBlockWidth = 8;

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

bool IsCtrlKey(const CefKeyEvent& event, char key) {
  if (!(event.modifiers & EVENTFLAG_CONTROL_DOWN)) {
    return false;
  }
  return event.windows_key_code == key || event.windows_key_code == key + ('a' - 'A') ||
         event.character == key || event.character == key + ('a' - 'A') ||
         event.unmodified_character == key ||
         event.unmodified_character == key + ('a' - 'A');
}

std::string HtmlEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      default:
        escaped.push_back(c);
        break;
    }
  }
  return escaped;
}

std::string DataUrl(const std::string& html) {
  std::ostringstream out;
  out << "data:text/html;charset=utf-8,";
  out << std::uppercase << std::hex << std::setfill('0');
  for (unsigned char c : html) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out << c;
    } else {
      out << '%' << std::setw(2) << static_cast<int>(c);
    }
  }
  return out.str();
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

  CefBrowserSettings sidebar_browser_settings;
  sidebar_browser_settings.background_color = theme::kSidebarBg;
  sidebar_client_ = new BrowserClient(this);
  sidebar_view_ = CefBrowserView::CreateBrowserView(
      sidebar_client_, DataUrl(SidebarHtml()), sidebar_browser_settings, nullptr,
      nullptr, this);
  sidebar_view_->SetPreferAccelerators(true);
  sidebar_view_->SetVisible(true);
  sidebar_content_panel_->AddChildView(sidebar_view_);

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
  sidebar_content_panel_->SetToFillLayout();
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
  command_content_panel_->SetToFillLayout();
  command_panel_->AddChildView(command_content_panel_);

  CefBrowserSettings command_browser_settings;
  command_browser_settings.background_color = theme::kAppBg;
  command_client_ = new BrowserClient(this);
  command_view_ = CefBrowserView::CreateBrowserView(
      command_client_, DataUrl(CommandHtml()), command_browser_settings, nullptr,
      nullptr, this);
  command_view_->SetPreferAccelerators(true);
  command_content_panel_->AddChildView(command_view_);
  command_overlay_ = window_->AddOverlayView(command_panel_, CEF_DOCKING_MODE_CUSTOM,
                                            false);
  command_overlay_->SetVisible(false);

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
  command_overlay_ = nullptr;
  mode_indicator_label_ = nullptr;
  mode_indicator_panel_ = nullptr;
  command_view_ = nullptr;
  command_client_ = nullptr;
  command_content_panel_ = nullptr;
  command_panel_ = nullptr;
  content_inner_panel_ = nullptr;
  content_panel_ = nullptr;
  command_separator_panel_ = nullptr;
  sidebar_border_panel_ = nullptr;
  sidebar_content_panel_ = nullptr;
  sidebar_view_ = nullptr;
  sidebar_client_ = nullptr;
  sidebar_panel_ = nullptr;
  main_panel_ = nullptr;
  root_panel_ = nullptr;
  window_ = nullptr;
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

void BrowserWindow::BeginCommand(Mode mode) {
  previous_focus_area_ = focus_area_ == FocusArea::kCommandLine ? previous_focus_area_
                                                                : focus_area_;
  focus_area_ = FocusArea::kCommandLine;
  mode_ = mode;
  command_text_ = mode == Mode::kCommandOpenNext ? "open -t " : "open ";
  vim::Reset(command_vim_, command_text_.size(), command_text_.size(),
             vim::Mode::kInsert);
  command_overlay_->SetVisible(true);
  Layout();
  SetCommandText(command_text_);
  UpdateModeIndicator();
  if (Tab* tab = ActiveTab(); tab) {
    tab->view->RequestFocus();
  }
}

void BrowserWindow::CommitCommand() {
  std::string text = command_text_;
  const Mode command_mode = mode_;
  const std::string prefix = mode_ == Mode::kCommandOpenNext ? "open -t " : "open ";
  if (text.rfind(prefix, 0) == 0) {
    text.erase(0, prefix.size());
  }

  CancelCommand();
  if (text.empty()) {
    return;
  }

  const std::string url = ResolveUrlOrSearch(text);
  if (command_mode == Mode::kCommandOpenNext) {
    AddTab(url, true);
  } else if (Tab* tab = ActiveTab(); tab && tab->client && tab->client->browser()) {
    tab->url = url;
    tab->client->browser()->GetMainFrame()->LoadURL(url);
    RefreshSidebar();
  }
}

void BrowserWindow::CancelCommand() {
  mode_ = Mode::kNormal;
  vim::Reset(command_vim_, 0, 0, vim::Mode::kInsert);
  SetCommandText("");
  command_overlay_->SetVisible(false);
  focus_area_ = previous_focus_area_ == FocusArea::kCommandLine ? FocusArea::kWebView
                                                                : previous_focus_area_;
  UpdateModeIndicator();
  if (Tab* tab = ActiveTab(); tab) {
    if (focus_area_ == FocusArea::kWebView) {
      tab->view->RequestFocus();
    } else if (focus_area_ == FocusArea::kTabSidebar && sidebar_view_) {
      sidebar_view_->RequestFocus();
    }
  }
  Layout();
}

bool BrowserWindow::HandleCommandModeKey(const CefKeyEvent& event) {
  if (mode_ == Mode::kNormal) {
    return false;
  }

  if (IsRawKeyDown(event)) {
    if (IsEnterKey(event)) {
      CommitCommand();
      return true;
    }
    if (IsEscapeKey(event)) {
      if (command_vim_.mode == vim::Mode::kInsert) {
        vim::LeaveInsert(command_vim_, command_text_);
        SetCommandText(command_text_);
        UpdateModeIndicator();
        return true;
      }
      CancelCommand();
      return true;
    }
    if (IsBackspaceKey(event)) {
      if (command_vim_.mode == vim::Mode::kInsert) {
        vim::Backspace(command_vim_, command_text_);
        SetCommandText(command_text_);
      }
      return true;
    }

    if (command_vim_.mode == vim::Mode::kNormal) {
      if (IsPlainLetterKey(event, 'i')) {
        vim::EnterInsert(command_vim_, command_text_);
        SetCommandText(command_text_);
        UpdateModeIndicator();
        suppress_next_char_event_ = true;
        return true;
      }
      if (IsPlainLetterKey(event, 'a')) {
        vim::EnterInsertAfter(command_vim_, command_text_);
        SetCommandText(command_text_);
        UpdateModeIndicator();
        suppress_next_char_event_ = true;
        return true;
      }
      if (IsPlainLetterKey(event, 'h')) {
        vim::MoveLeft(command_vim_, command_text_);
        SetCommandText(command_text_);
        return true;
      }
      if (IsPlainLetterKey(event, 'l')) {
        vim::MoveRight(command_vim_, command_text_);
        SetCommandText(command_text_);
        return true;
      }
      if (IsPlainLetterKey(event, 'x')) {
        vim::DeleteAtCursor(command_vim_, command_text_);
        SetCommandText(command_text_);
        return true;
      }
    }

    if (command_vim_.mode == vim::Mode::kInsert) {
      const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
      const bool alt = event.modifiers & EVENTFLAG_ALT_DOWN;
      const bool command = event.modifiers & EVENTFLAG_COMMAND_DOWN;
      const char16_t c = event.character ? event.character : event.unmodified_character;
      if (!ctrl && !alt && !command && IsPrintableAscii(c)) {
        vim::InsertChar(command_vim_, command_text_, static_cast<char>(c));
        SetCommandText(command_text_);
        suppress_next_char_event_ = true;
      }
      return true;
    }

    return true;
  }

  if (IsCharEvent(event)) {
    if (suppress_next_char_event_) {
      suppress_next_char_event_ = false;
      return true;
    }

    if (command_vim_.mode == vim::Mode::kInsert) {
      const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
      const bool alt = event.modifiers & EVENTFLAG_ALT_DOWN;
      const bool command = event.modifiers & EVENTFLAG_COMMAND_DOWN;
      const char16_t c = event.character ? event.character : event.unmodified_character;
      if (!ctrl && !alt && !command && IsPrintableAscii(c)) {
        vim::InsertChar(command_vim_, command_text_, static_cast<char>(c));
        SetCommandText(command_text_);
      }
      return true;
    }

    if (command_vim_.mode == vim::Mode::kNormal) {
      if (IsPlainLetterKey(event, 'i')) {
        vim::EnterInsert(command_vim_, command_text_);
        SetCommandText(command_text_);
        UpdateModeIndicator();
      } else if (IsPlainLetterKey(event, 'a')) {
        vim::EnterInsertAfter(command_vim_, command_text_);
        SetCommandText(command_text_);
        UpdateModeIndicator();
      } else if (IsPlainLetterKey(event, 'h')) {
        vim::MoveLeft(command_vim_, command_text_);
        SetCommandText(command_text_);
      } else if (IsPlainLetterKey(event, 'l')) {
        vim::MoveRight(command_vim_, command_text_);
        SetCommandText(command_text_);
      } else if (IsPlainLetterKey(event, 'x')) {
        vim::DeleteAtCursor(command_vim_, command_text_);
        SetCommandText(command_text_);
      }
    }
    return true;
  }

  return true;
}

void BrowserWindow::SetCommandText(std::string text) {
  command_text_ = std::move(text);
  vim::Clamp(command_vim_, command_text_);
  UpdateCommandView();
}

void BrowserWindow::UpdateCommandView() {
  if (command_client_ && command_client_->browser()) {
    CefRefPtr<CefFrame> frame = command_client_->browser()->GetMainFrame();
    CefRefPtr<CefDictionaryValue> dict = CefDictionaryValue::Create();
    dict->SetString("text", command_text_);
    dict->SetString("prefix", mode_ == Mode::kCommandOpenNext ? "open -t " : "open ");
    dict->SetInt("cursor", static_cast<int>(vim::CursorDisplayOffset(command_vim_, command_text_)));
    dict->SetString("mode", command_vim_.mode == vim::Mode::kNormal ? "normal" : "insert");
    CefRefPtr<CefValue> value = CefValue::Create();
    value->SetDictionary(dict);
    const std::string json = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
    frame->ExecuteJavaScript("window.vimbrowserSetCommand && window.vimbrowserSetCommand(" + json + ");",
                             frame->GetURL(), 0);
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
  const int main_height = height;
  if (command_overlay_) {
    command_overlay_->SetVisible(mode_ != Mode::kNormal);
  }
  sidebar_panel_->SetVisible(sidebar_visible_);

  root_panel_->SetBounds(CefRect(0, 0, width, height));
  RestyleView(root_panel_);
  RestyleView(main_panel_);
  RestyleView(sidebar_panel_);
  RestyleView(sidebar_content_panel_);
  RestyleView(sidebar_border_panel_);
  RestyleView(content_panel_);
  RestyleView(content_inner_panel_);
  RestyleView(command_panel_);
  RestyleView(command_content_panel_);
  RestyleView(command_separator_panel_);
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
  if (sidebar_view_) {
    sidebar_view_->SetSize(CefSize(kSidebarWidth - 1, main_height));
  }
  if (command_view_) {
    command_view_->SetSize(CefSize(width, kCommandHeight));
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
}

void BrowserWindow::RefreshSidebar() {
  if (sidebar_client_ && sidebar_client_->browser()) {
    sidebar_client_->browser()->GetMainFrame()->LoadURL(DataUrl(SidebarHtml()));
  }
}

void BrowserWindow::SetFocusArea(FocusArea area) {
  if (area == FocusArea::kTabSidebar && !sidebar_visible_) {
    sidebar_visible_ = true;
  }
  focus_area_ = area;
  if (focus_area_ == FocusArea::kWebView) {
    if (Tab* tab = ActiveTab(); tab) {
      tab->view->RequestFocus();
    }
  } else if (focus_area_ == FocusArea::kTabSidebar) {
    if (sidebar_view_) {
      sidebar_view_->RequestFocus();
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

void BrowserWindow::RestyleView(CefRefPtr<CefView> view) {
  if (!view) {
    return;
  }

  const int id = view->GetID();
  if (id == kSidebarPanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (id == kSidebarContentPanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (id == kSidebarBorderPanelId) {
    view->SetBackgroundColor(focus_area_ == FocusArea::kTabSidebar
                                 ? theme::kBorderFocused
                                 : theme::kBorderUnfocused);
  } else if (id == kCommandPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandSeparatorPanelId) {
    view->SetBackgroundColor(focus_area_ == FocusArea::kCommandLine
                                 ? theme::kBorderFocused
                                 : theme::kBorderUnfocused);
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

std::string BrowserWindow::SidebarHtml() const {
  const std::string text_color = "#ffffff";
  const std::string marker_color = "#48cae4";
  std::string rows;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool active = i == active_index_;
    rows += "<div class=\"row";
    if (active) {
      rows += " active";
    }
    rows += "\">";
    rows += "<span class=\"marker\">";
    rows += active ? "&gt;" : "&nbsp;";
    rows += "</span> ";
    rows += std::to_string(i + 1);
    rows += ": ";
    rows += HtmlEscape(DisplayUrl(tabs_[i].url));
    rows += "</div>";
  }

  return "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
         "*{box-sizing:border-box;border-radius:0!important}"
         "html,body{margin:0;width:100%;height:100%;overflow:hidden;"
         "background:#030814;color:" + text_color + ";font:12px monospace;}"
         ".row{height:24px;line-height:24px;white-space:nowrap;overflow:hidden;"
         "text-overflow:ellipsis;padding:0 6px;background:#030814;color:" + text_color + ";}"
         ".row.active{background:#0f193c;color:" + text_color + ";}"
         ".row.active .marker{color:" + marker_color + ";}"
         ".marker{color:" + marker_color + ";}"
         "::selection{background:#4f5258;color:#ffffff;}"
         "</style></head><body>" +
         rows + "</body></html>";
}

std::string BrowserWindow::CommandHtml() const {
  return "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
         "*{box-sizing:border-box;border-radius:0!important}"
         "html,body{margin:0;width:100%;height:100%;overflow:hidden;"
         "background:#00050f;color:#ffffff;font:13px monospace;}"
         ".line{position:relative;height:28px;line-height:28px;padding-left:10px;"
         "white-space:pre;overflow:hidden;background:#00050f;}"
         ".cells{display:inline-grid;grid-auto-flow:column;grid-auto-columns:8px;"
         "justify-content:start;align-items:center;position:relative;z-index:2;}"
         ".cell{width:8px;height:28px;line-height:28px;text-align:center;"
         "position:relative;}"
         ".prefix{color:#aed6fe}.text{color:#ffffff}.eof{color:#ffffff}"
         ".under-cursor{color:#00050f}"
         ".cursor{position:absolute;top:5px;height:18px;background:#48cae4;"
         "pointer-events:none;}"
         ".cursor.bar{width:2px;z-index:3}.cursor.block{width:8px;z-index:1}"
         "::selection{background:#4f5258;color:#ffffff}"
         "</style></head><body><div class=\"line\"><div id=\"cursor\" "
         "class=\"cursor bar\"></div><span id=\"cells\" class=\"cells\"></span>"
         "</div><script>"
         "function esc(s){return String(s).replace(/[&<>\"']/g,function(c){return "
         "{'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]})}"
         "window.vimbrowserSetCommand=function(s){"
         "var cells=document.getElementById('cells'),cur=document.getElementById('cursor');"
         "var html='',text=s.text||'',prefix=s.prefix||'',cursor=s.cursor||0,normal=s.mode==='normal';"
         "for(var i=0;i<text.length;i++){var cls='cell '+(i<prefix.length?'prefix':'text');"
         "if(normal&&i===cursor)cls+=' under-cursor';html+='<span class=\"'+cls+'\">'+esc(text[i])+'</span>'}"
         "if(cursor>=text.length)html+='<span class=\"cell eof\"></span>';"
         "cells.innerHTML=html;cur.className='cursor '+(normal?'block':'bar');"
         "cur.style.left=(10+cursor*8)+'px';};"
         "window.vimbrowserSetCommand({text:'',prefix:'open ',cursor:0,mode:'insert'});"
         "</script></body></html>";
}

Tab* BrowserWindow::ActiveTab() {
  if (tabs_.empty() || active_index_ >= tabs_.size()) {
    return nullptr;
  }
  return &tabs_[active_index_];
}

}  // namespace vimbrowser
