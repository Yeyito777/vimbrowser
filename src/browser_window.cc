#include "browser_window.h"

#include <algorithm>
#include <utility>

#include "config.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
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
constexpr int kCommandFieldId = 105;
constexpr int kCommandSeparatorPanelId = 106;
constexpr int kTabLabelBaseId = 1000;
constexpr int kTabRowBaseId = 2000;

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
  sidebar_settings.horizontal = false;
  sidebar_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  sidebar_panel_->SetToBoxLayout(sidebar_settings);
  main_panel_->AddChildView(sidebar_panel_);

  content_panel_ = CefPanel::CreatePanel(nullptr);
  content_panel_->SetID(kContentPanelId);
  content_panel_->SetBackgroundColor(theme::kAppBg);
  content_panel_->SetToFillLayout();
  main_panel_->AddChildView(content_panel_);
  main_layout->SetFlexForView(content_panel_, 1);

  command_panel_ = CefPanel::CreatePanel(this);
  command_panel_->SetID(kCommandPanelId);
  command_panel_->SetBackgroundColor(theme::kAppBg);
  CefBoxLayoutSettings command_settings = {};
  command_settings.size = sizeof(command_settings);
  command_settings.horizontal = false;
  command_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> command_layout =
      command_panel_->SetToBoxLayout(command_settings);

  command_separator_panel_ = CefPanel::CreatePanel(nullptr);
  command_separator_panel_->SetID(kCommandSeparatorPanelId);
  command_separator_panel_->SetBackgroundColor(theme::kBorderFocused);
  command_panel_->AddChildView(command_separator_panel_);

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
  command_panel_->AddChildView(command_field_);
  command_layout->SetFlexForView(command_field_, 1);

  // Command mode is a visual overlay, not part of the document/content layout.
  // This keeps the web view at a stable size and avoids reordering/reflowing
  // pages when entering or leaving command mode.
  command_overlay_ = window_->AddOverlayView(command_panel_, CEF_DOCKING_MODE_CUSTOM,
                                            false);
  command_overlay_->SetVisible(false);
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  tabs_.clear();
  command_overlay_ = nullptr;
  command_field_ = nullptr;
  command_panel_ = nullptr;
  content_panel_ = nullptr;
  command_separator_panel_ = nullptr;
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

  if (event.windows_key_code == 0x0D) {
    CommitCommand();
    return true;
  }

  if (event.windows_key_code == 0x1B) {
    CancelCommand();
    return true;
  }

  return false;
}

void BrowserWindow::OnButtonPressed(CefRefPtr<CefButton> button) {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].label.get() == button.get()) {
      ActivateTab(i);
      return;
    }
  }
}

CefSize BrowserWindow::GetPreferredSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  if (id == kSidebarPanelId) {
    return CefSize(kSidebarWidth, 1);
  }
  if (id == kMainPanelId || id == kRootPanelId) {
    return CefSize(1200, 800);
  }
  if (id == kCommandPanelId) {
    return CefSize(1200, kCommandHeight + 1);
  }
  if (id == kCommandFieldId) {
    return CefSize(1200, kCommandHeight);
  }
  if (id == kCommandSeparatorPanelId) {
    return CefSize(1200, 1);
  }
  if (id >= kTabRowBaseId || id >= kTabLabelBaseId) {
    return CefSize(kSidebarWidth, 24);
  }
  return CefSize(1200, 800);
}

CefSize BrowserWindow::GetMinimumSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  if (id == kSidebarPanelId) {
    return CefSize(kSidebarWidth, 1);
  }
  if (id == kCommandFieldId) {
    return CefSize(1, mode_ == Mode::kNormal ? 0 : kCommandHeight);
  }
  if (id == kCommandPanelId) {
    return CefSize(1, mode_ == Mode::kNormal ? 0 : kCommandHeight + 1);
  }
  if (id == kCommandSeparatorPanelId) {
    return CefSize(1, mode_ == Mode::kNormal ? 0 : 1);
  }
  if (id >= kTabRowBaseId || id >= kTabLabelBaseId) {
    return CefSize(kSidebarWidth, 24);
  }
  return CefSize();
}

CefSize BrowserWindow::GetMaximumSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  if (id == kCommandFieldId) {
    return CefSize(0, mode_ == Mode::kNormal ? 0 : kCommandHeight);
  }
  if (id == kCommandPanelId) {
    return CefSize(0, mode_ == Mode::kNormal ? 0 : kCommandHeight + 1);
  }
  if (id == kCommandSeparatorPanelId) {
    return CefSize(0, mode_ == Mode::kNormal ? 0 : 1);
  }
  if (id >= kTabRowBaseId || id >= kTabLabelBaseId) {
    return CefSize(kSidebarWidth, 24);
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

  tab.row = CefPanel::CreatePanel(this);
  tab.row->SetID(kTabRowBaseId + static_cast<int>(tabs_.size()));
  tab.row->SetBackgroundColor(theme::kSidebarBg);
  tab.row->SetToFillLayout();
  sidebar_panel_->AddChildView(tab.row);

  tab.label = CefLabelButton::CreateLabelButton(this, "");
  tab.label->SetID(kTabLabelBaseId + static_cast<int>(tabs_.size()));
  tab.label->SetFontList("monospace, 12px");
  tab.label->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
  tab.label->SetMinimumSize(CefSize(kSidebarWidth, 24));
  tab.label->SetMaximumSize(CefSize(kSidebarWidth, 24));
  tab.label->SetFocusable(false);
  tab.label->SetInkDropEnabled(false);
  tab.label->SetEnabledTextColors(theme::kText);
  tab.label->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
  tab.label->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
  tab.label->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
  tab.label->SetBackgroundColor(theme::kTransparent);
  tab.row->AddChildView(tab.label);

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
  tabs_[active_index_].view->RequestFocus();
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
  mode_ = mode;
  SetCommandText(mode == Mode::kCommandOpenNext ? "open -t " : "open ");
  command_overlay_->SetVisible(true);
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
  SetCommandText("");
  command_overlay_->SetVisible(false);
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
    if (event.windows_key_code == 0x0D) {
      CommitCommand();
      return true;
    }
    if (event.windows_key_code == 0x1B) {
      CancelCommand();
      return true;
    }
    if (event.windows_key_code == 0x08) {
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

void BrowserWindow::Layout() {
  if (!window_ || !root_panel_) {
    return;
  }

  const CefRect bounds = window_->GetBounds();
  const int width = std::max(1, bounds.width);
  const int height = std::max(1, bounds.height);
  const int overlay_height = kCommandHeight + 1;
  if (command_overlay_) {
    command_overlay_->SetVisible(mode_ != Mode::kNormal);
  }

  root_panel_->SetBounds(CefRect(0, 0, width, height));
  RestyleView(root_panel_);
  RestyleView(main_panel_);
  RestyleView(sidebar_panel_);
  RestyleView(content_panel_);
  RestyleView(command_panel_);
  RestyleView(command_separator_panel_);
  RestyleView(command_field_);
  main_panel_->SetSize(CefSize(width, height));
  sidebar_panel_->SetSize(CefSize(kSidebarWidth, height));
  command_panel_->SetSize(CefSize(width, overlay_height));
  command_separator_panel_->SetSize(CefSize(width, 1));
  command_field_->SetSize(CefSize(width, kCommandHeight));
  if (command_overlay_) {
    command_overlay_->SetBounds(CefRect(0, std::max(0, height - overlay_height),
                                        width, overlay_height));
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
  if (content_panel_->GetLayout()) {
    content_panel_->Layout();
  }
  if (command_panel_->GetLayout()) {
    command_panel_->Layout();
  }
}

void BrowserWindow::RefreshSidebar() {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool active = i == active_index_;
    std::string text = (active ? "> " : "  ") + std::to_string(i + 1) + ": " +
                       DisplayUrl(tabs_[i].url);
    tabs_[i].label->SetText(text);
    tabs_[i].row->SetBackgroundColor(active ? theme::kSidebarSelBg
                                            : theme::kSidebarBg);
    tabs_[i].label->SetFocusable(false);
    tabs_[i].label->SetInkDropEnabled(false);
    tabs_[i].label->SetEnabledTextColors(theme::kText);
    tabs_[i].label->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
    tabs_[i].label->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
    tabs_[i].label->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
    tabs_[i].label->SetBackgroundColor(theme::kTransparent);
    tabs_[i].label->SetState(CEF_BUTTON_STATE_NORMAL);
  }
}

void BrowserWindow::RestyleView(CefRefPtr<CefView> view) {
  if (!view) {
    return;
  }

  const int id = view->GetID();
  if (id == kSidebarPanelId) {
    view->SetBackgroundColor(theme::kSidebarBg);
  } else if (id == kCommandPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandSeparatorPanelId) {
    view->SetBackgroundColor(theme::kBorderFocused);
  } else if (id == kRootPanelId || id == kMainPanelId || id == kContentPanelId) {
    view->SetBackgroundColor(theme::kAppBg);
  } else if (id == kCommandFieldId) {
    command_field_->SetTextColor(theme::kText);
    command_field_->SetPlaceholderTextColor(theme::kMuted);
    command_field_->SetSelectionTextColor(theme::kText);
    command_field_->SetSelectionBackgroundColor(theme::kSelectionBg);
    command_field_->SetBackgroundColor(theme::kAppBg);
    RestyleCommandText();
  } else if (id >= kTabRowBaseId) {
    for (size_t i = 0; i < tabs_.size(); ++i) {
      if (tabs_[i].row.get() == view.get()) {
        const bool active = i == active_index_;
        tabs_[i].row->SetBackgroundColor(active ? theme::kSidebarSelBg
                                                : theme::kSidebarBg);
        return;
      }
    }
  } else if (id >= kTabLabelBaseId) {
    for (size_t i = 0; i < tabs_.size(); ++i) {
      if (tabs_[i].label.get() == view.get()) {
        tabs_[i].label->SetFocusable(false);
        tabs_[i].label->SetInkDropEnabled(false);
        tabs_[i].label->SetEnabledTextColors(theme::kText);
        tabs_[i].label->SetTextColor(CEF_BUTTON_STATE_NORMAL, theme::kText);
        tabs_[i].label->SetTextColor(CEF_BUTTON_STATE_HOVERED, theme::kText);
        tabs_[i].label->SetTextColor(CEF_BUTTON_STATE_PRESSED, theme::kText);
        tabs_[i].label->SetBackgroundColor(theme::kTransparent);
        tabs_[i].label->SetState(CEF_BUTTON_STATE_NORMAL);
        return;
      }
    }
  }
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
