#include "browser_window.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

#include "config.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
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
constexpr int kCommandFieldId = 105;
constexpr int kCommandSeparatorPanelId = 106;
constexpr int kCommandContentPanelId = 107;
constexpr int kSidebarContentPanelId = 108;
constexpr int kSidebarBorderPanelId = 109;

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
  UpdateStatusLine();
  RequestActiveScrollStatus();
}

void BrowserWindow::OnClientLoadEnd(BrowserClient* client, const std::string& url) {
  for (Tab& tab : tabs_) {
    if (tab.client.get() == client) {
      tab.url = url;
      RefreshSidebar();
      if (&tab == ActiveTab()) {
        UpdateStatusLine();
        RequestActiveScrollStatus();
      }
      return;
    }
  }
}

void BrowserWindow::OnClientScrollStatus(BrowserClient* client,
                                         const std::string& scroll) {
  if (Tab* tab = ActiveTab(); tab && tab->client.get() == client) {
    scroll_status_ = scroll.empty() ? "All" : scroll;
    UpdateStatusLine();
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
  content_panel_->SetToFillLayout();
  main_panel_->AddChildView(content_panel_);
  main_layout->SetFlexForView(content_panel_, 1);

  command_panel_ = CefPanel::CreatePanel(this);
  command_panel_->SetID(kCommandPanelId);
  command_panel_->SetBackgroundColor(theme::kAppBg);

  command_separator_panel_ = CefPanel::CreatePanel(nullptr);
  command_separator_panel_->SetID(kCommandSeparatorPanelId);
  command_separator_panel_->SetBackgroundColor(theme::kAccent);
  command_panel_->AddChildView(command_separator_panel_);

  command_content_panel_ = CefPanel::CreatePanel(this);
  command_content_panel_->SetID(kCommandContentPanelId);
  command_content_panel_->SetBackgroundColor(theme::kAppBg);
  command_content_panel_->SetToFillLayout();
  command_panel_->AddChildView(command_content_panel_);

  command_field_ = CefTextfield::CreateTextfield(this);
  command_field_->SetID(kCommandFieldId);
  command_field_->SetFontList("monospace, 13px");
  command_field_->SetReadOnly(true);
  command_field_->SetFocusable(false);
  command_field_->SetTextColor(theme::kText);
  command_field_->SetPlaceholderTextColor(theme::kMuted);
  command_field_->SetSelectionTextColor(theme::kText);
  command_field_->SetSelectionBackgroundColor(theme::kSelectionBg);
  command_field_->SetBackgroundColor(theme::kAppBg);
  command_field_->SetPlaceholderText("");
  command_content_panel_->AddChildView(command_field_);
  root_panel_->AddChildView(command_panel_);
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  tabs_.clear();
  command_field_ = nullptr;
  command_content_panel_ = nullptr;
  command_panel_ = nullptr;
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

  if (!IsRawKeyDown(event)) {
    return false;
  }

  return HandleNormalModeKey(event);
}

bool BrowserWindow::HandleBrowserKeyEvent(const CefKeyEvent& event) {
  if (mode_ != Mode::kNormal) {
    return HandleCommandModeKey(event);
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
    BeginCommand(shift ? Mode::kCommandOpenNext : Mode::kCommandOpenCurrent);
    return true;
  }

  if (shift && event.windows_key_code == 'J') {
    ActivateRelative(1);
    return true;
  }

  if (shift && event.windows_key_code == 'K') {
    ActivateRelative(-1);
    return true;
  }

  return false;
}

void BrowserWindow::OnAfterUserAction(CefRefPtr<CefTextfield> textfield) {
  if (textfield == command_field_) {
    RestyleCommandText();
  }
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
  if (id == kCommandFieldId) {
    return CefSize(1200, kCommandHeight);
  }
  if (id == kCommandSeparatorPanelId) {
    return CefSize(1200, 1);
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
  if (id == kCommandFieldId) {
    return CefSize(1, kCommandHeight);
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
  return CefSize();
}

CefSize BrowserWindow::GetMaximumSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  if (id == kCommandFieldId) {
    return CefSize(0, kCommandHeight);
  }
  if (id == kCommandPanelId) {
    return CefSize(0, kCommandHeight + 1);
  }
  if (id == kCommandContentPanelId) {
    return CefSize(0, kCommandHeight);
  }
  if (id == kCommandSeparatorPanelId) {
    return CefSize(0, 1);
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
  content_panel_->AddChildView(tab.view);

  tabs_.push_back(tab);
  RefreshSidebar();
  Layout();

  if (activate) {
    ActivateTab(tabs_.size() - 1);
  }
  UpdateStatusLine();
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
  tabs_[active_index_].view->RequestFocus();
  scroll_status_ = "All";
  RefreshSidebar();
  UpdateStatusLine();
  RequestActiveScrollStatus();
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
  mode_ = mode;
  SetCommandText(mode == Mode::kCommandOpenNext ? "open -t " : "open ");
  if (Tab* tab = ActiveTab(); tab) {
    tab->view->RequestFocus();
  }
  command_field_->SelectRange(CefRange(command_field_->GetText().ToString().size(),
                                       command_field_->GetText().ToString().size()));
  Layout();
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
  command_text_.clear();
  UpdateStatusLine();
  if (Tab* tab = ActiveTab(); tab) {
    tab->view->RequestFocus();
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
      CancelCommand();
      return true;
    }
    if (IsBackspaceKey(event)) {
      const std::string prefix = mode_ == Mode::kCommandOpenNext ? "open -t " : "open ";
      if (command_text_.size() > prefix.size()) {
        command_text_.pop_back();
        SetCommandText(command_text_);
      }
      return true;
    }
    const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
    const bool alt = event.modifiers & EVENTFLAG_ALT_DOWN;
    const bool command = event.modifiers & EVENTFLAG_COMMAND_DOWN;
    const char16_t c = event.character ? event.character : event.unmodified_character;
    if (!ctrl && !alt && !command && IsPrintableAscii(c)) {
      command_text_.push_back(static_cast<char>(c));
      SetCommandText(command_text_);
    }
    return true;
  }

  if (IsCharEvent(event)) {
    const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
    const bool alt = event.modifiers & EVENTFLAG_ALT_DOWN;
    const bool command = event.modifiers & EVENTFLAG_COMMAND_DOWN;
    const char16_t c = event.character ? event.character : event.unmodified_character;
    if (!ctrl && !alt && !command && IsPrintableAscii(c)) {
      command_text_.push_back(static_cast<char>(c));
      SetCommandText(command_text_);
    }
    return true;
  }

  return true;
}

void BrowserWindow::SetCommandText(std::string text) {
  command_text_ = std::move(text);
  command_field_->SetText(command_text_);
  command_field_->SelectRange(CefRange(command_text_.size(), command_text_.size()));
  RestyleCommandText();
}

void BrowserWindow::UpdateStatusLine() {
  if (!command_field_ || mode_ != Mode::kNormal) {
    return;
  }

  std::string url;
  if (Tab* tab = ActiveTab(); tab) {
    if (tab->client && tab->client->browser() &&
        tab->client->browser()->GetMainFrame()) {
      url = tab->client->browser()->GetMainFrame()->GetURL().ToString();
    }
    if (url.empty()) {
      url = tab->url;
    }
  }

  const std::string status = scroll_status_ + "  " + url;
  command_field_->SetText(status);
  // Keep normal-mode status anchored at the beginning. Long URLs should show
  // their scheme/start, not force-scroll the read-only field to the tail.
  command_field_->SelectRange(CefRange(0, 0));
  command_field_->SetTextColor(theme::kText);
  command_field_->ApplyTextColor(theme::kText, CefRange());
  command_field_->ApplyTextColor(theme::kVimNormal,
                                 CefRange(0, scroll_status_.size()));
}

void BrowserWindow::RequestActiveScrollStatus() {
  if (Tab* tab = ActiveTab(); tab && tab->client) {
    tab->client->RequestScrollStatus();
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
  const int main_height = std::max(1, height - command_total_height);

  root_panel_->SetBounds(CefRect(0, 0, width, height));
  RestyleView(root_panel_);
  RestyleView(main_panel_);
  RestyleView(sidebar_panel_);
  RestyleView(sidebar_content_panel_);
  RestyleView(sidebar_border_panel_);
  RestyleView(content_panel_);
  RestyleView(command_panel_);
  RestyleView(command_content_panel_);
  RestyleView(command_separator_panel_);
  RestyleView(command_field_);
  main_panel_->SetSize(CefSize(width, main_height));
  sidebar_panel_->SetSize(CefSize(kSidebarWidth, main_height));
  sidebar_content_panel_->SetSize(CefSize(kSidebarWidth - 1, main_height));
  sidebar_border_panel_->SetSize(CefSize(1, main_height));
  command_panel_->SetSize(CefSize(width, command_total_height));
  command_separator_panel_->SetSize(CefSize(width, 1));
  command_content_panel_->SetSize(CefSize(width, kCommandHeight));
  command_field_->SetSize(CefSize(width, kCommandHeight));
  command_separator_panel_->SetBounds(CefRect(0, 0, width, 1));
  command_content_panel_->SetBounds(CefRect(0, 1, width, kCommandHeight));
  command_field_->SetBounds(CefRect(0, 0, width, kCommandHeight));

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
  if (content_panel_->GetLayout()) {
    content_panel_->Layout();
  }
  if (command_content_panel_->GetLayout()) {
    command_content_panel_->Layout();
  }
}

void BrowserWindow::RefreshSidebar() {
  if (sidebar_client_ && sidebar_client_->browser()) {
    sidebar_client_->browser()->GetMainFrame()->LoadURL(DataUrl(SidebarHtml()));
  }
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
    view->SetBackgroundColor(theme::kAccent);
  } else if (id == kCommandPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandSeparatorPanelId) {
    view->SetBackgroundColor(theme::kAccent);
  } else if (id == kRootPanelId || id == kMainPanelId || id == kContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandFieldId) {
    command_field_->SetTextColor(theme::kText);
    command_field_->SetPlaceholderTextColor(theme::kMuted);
    command_field_->SetSelectionTextColor(theme::kText);
    command_field_->SetSelectionBackgroundColor(theme::kSelectionBg);
    command_field_->SetBackgroundColor(theme::kAppBg);
    if (mode_ == Mode::kNormal) {
      UpdateStatusLine();
    } else {
      RestyleCommandText();
    }
  }
}

std::string BrowserWindow::SidebarHtml() const {
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
         "background:#030814;color:#ffffff;font:12px monospace;}"
         ".row{height:24px;line-height:24px;white-space:nowrap;overflow:hidden;"
         "text-overflow:ellipsis;padding:0 6px;background:#030814;color:#ffffff;}"
         ".row.active{background:#0f193c;color:#ffffff;}"
         ".row.active .marker{color:#48cae4;}"
         ".marker{color:#48cae4;}"
         "::selection{background:#4f5258;color:#ffffff;}"
         "</style></head><body>" +
         rows + "</body></html>";
}

void BrowserWindow::RestyleCommandText() {
  if (!command_field_) {
    return;
  }

  const std::string text = command_field_->GetText().ToString();
  command_field_->SetTextColor(theme::kText);
  command_field_->ApplyTextColor(theme::kText, CefRange());

  const std::string prefix = mode_ == Mode::kCommandOpenNext ? "open -t " : "open ";
  if (mode_ != Mode::kNormal && text.rfind(prefix, 0) == 0) {
    command_field_->ApplyTextColor(theme::kCommand, CefRange(0, prefix.size()));
  }
}

Tab* BrowserWindow::ActiveTab() {
  if (tabs_.empty() || active_index_ >= tabs_.size()) {
    return nullptr;
  }
  return &tabs_[active_index_];
}

}  // namespace vimbrowser
