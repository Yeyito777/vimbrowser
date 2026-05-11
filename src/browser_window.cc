#include "browser_window.h"

#include <algorithm>
#include <utility>

#include "config.h"
#include "include/cef_browser.h"
#include "include/views/cef_button.h"

namespace vimbrowser {
namespace {

constexpr int kSidebarWidth = 175;
constexpr int kCommandHeight = 28;
constexpr cef_color_t kChromeBackground = CefColorSetARGB(255, 0, 5, 15);
constexpr cef_color_t kSidebarBackground = CefColorSetARGB(255, 0, 10, 24);
constexpr cef_color_t kActiveTabBackground = CefColorSetARGB(255, 20, 45, 80);
constexpr cef_color_t kInactiveTabBackground = CefColorSetARGB(255, 0, 10, 24);
constexpr cef_color_t kTextColor = CefColorSetARGB(255, 220, 235, 255);
constexpr cef_color_t kAccentColor = CefColorSetARGB(255, 80, 170, 255);
constexpr int kRootPanelId = 100;
constexpr int kMainPanelId = 101;
constexpr int kSidebarPanelId = 102;
constexpr int kContentPanelId = 103;
constexpr int kCommandFieldId = 104;
constexpr int kTabLabelBaseId = 1000;

bool IsRawKeyDown(const CefKeyEvent& event) {
  return event.type == KEYEVENT_RAWKEYDOWN;
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
  root_panel_->SetBackgroundColor(kChromeBackground);
  CefBoxLayoutSettings root_settings = {};
  root_settings.size = sizeof(root_settings);
  root_settings.horizontal = false;
  root_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> root_layout = root_panel_->SetToBoxLayout(root_settings);
  window_->AddChildView(root_panel_);

  main_panel_ = CefPanel::CreatePanel(this);
  main_panel_->SetID(kMainPanelId);
  main_panel_->SetBackgroundColor(kChromeBackground);
  CefBoxLayoutSettings main_settings = {};
  main_settings.size = sizeof(main_settings);
  main_settings.horizontal = true;
  main_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> main_layout = main_panel_->SetToBoxLayout(main_settings);
  root_panel_->AddChildView(main_panel_);
  root_layout->SetFlexForView(main_panel_, 1);

  sidebar_panel_ = CefPanel::CreatePanel(this);
  sidebar_panel_->SetID(kSidebarPanelId);
  sidebar_panel_->SetBackgroundColor(kSidebarBackground);
  CefBoxLayoutSettings sidebar_settings = {};
  sidebar_settings.size = sizeof(sidebar_settings);
  sidebar_settings.horizontal = false;
  sidebar_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  sidebar_panel_->SetToBoxLayout(sidebar_settings);
  main_panel_->AddChildView(sidebar_panel_);

  content_panel_ = CefPanel::CreatePanel(nullptr);
  content_panel_->SetID(kContentPanelId);
  content_panel_->SetBackgroundColor(kChromeBackground);
  content_panel_->SetToFillLayout();
  main_panel_->AddChildView(content_panel_);
  main_layout->SetFlexForView(content_panel_, 1);

  command_field_ = CefTextfield::CreateTextfield(this);
  command_field_->SetID(kCommandFieldId);
  command_field_->SetFontList("monospace, 13px");
  command_field_->SetTextColor(kTextColor);
  command_field_->SetSelectionTextColor(CefColorSetARGB(255, 0, 0, 0));
  command_field_->SetSelectionBackgroundColor(kAccentColor);
  command_field_->SetBackgroundColor(CefColorSetARGB(255, 0, 16, 32));
  command_field_->SetVisible(false);
  root_panel_->AddChildView(command_field_);
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  tabs_.clear();
  command_field_ = nullptr;
  content_panel_ = nullptr;
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
  if (!IsRawKeyDown(event)) {
    return false;
  }

  if (mode_ != Mode::kNormal) {
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
  if (id == kCommandFieldId) {
    return CefSize(1200, kCommandHeight);
  }
  if (id >= kTabLabelBaseId) {
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
  if (id >= kTabLabelBaseId) {
    return CefSize(kSidebarWidth, 24);
  }
  return CefSize();
}

CefSize BrowserWindow::GetMaximumSize(CefRefPtr<CefView> view) {
  const int id = view->GetID();
  if (id == kSidebarPanelId) {
    return CefSize(kSidebarWidth, 0);
  }
  if (id == kCommandFieldId) {
    return CefSize(0, mode_ == Mode::kNormal ? 0 : kCommandHeight);
  }
  if (id >= kTabLabelBaseId) {
    return CefSize(kSidebarWidth, 24);
  }
  return CefSize();
}

cef_runtime_style_t BrowserWindow::GetWindowRuntimeStyle() {
  return CEF_RUNTIME_STYLE_ALLOY;
}

cef_runtime_style_t BrowserWindow::GetBrowserRuntimeStyle() {
  return CEF_RUNTIME_STYLE_ALLOY;
}

void BrowserWindow::AddTab(std::string url, bool activate) {
  CefBrowserSettings browser_settings;
  browser_settings.background_color = CefColorSetARGB(255, 5, 8, 18);

  Tab tab;
  tab.url = std::move(url);
  tab.client = new BrowserClient(this);
  tab.view = CefBrowserView::CreateBrowserView(tab.client, tab.url, browser_settings,
                                               nullptr, nullptr, this);
  tab.view->SetPreferAccelerators(true);
  tab.view->SetVisible(false);
  content_panel_->AddChildView(tab.view);

  tab.label = CefLabelButton::CreateLabelButton(this, "");
  tab.label->SetID(kTabLabelBaseId + static_cast<int>(tabs_.size()));
  tab.label->SetFontList("monospace, 12px");
  tab.label->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
  tab.label->SetMinimumSize(CefSize(kSidebarWidth, 24));
  sidebar_panel_->AddChildView(tab.label);

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
  command_field_->SetText(mode == Mode::kCommandOpenNext ? "open -t " : "open ");
  command_field_->SetVisible(true);
  command_field_->RequestFocus();
  command_field_->SelectRange(CefRange(command_field_->GetText().ToString().size(),
                                       command_field_->GetText().ToString().size()));
  Layout();
}

void BrowserWindow::CommitCommand() {
  std::string text = command_field_->GetText().ToString();
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
  command_field_->SetText("");
  command_field_->SetVisible(false);
  if (Tab* tab = ActiveTab(); tab) {
    tab->view->RequestFocus();
  }
  Layout();
}

void BrowserWindow::Layout() {
  if (!window_ || !root_panel_) {
    return;
  }

  const CefRect bounds = window_->GetBounds();
  const int width = std::max(1, bounds.width);
  const int height = std::max(1, bounds.height);
  const int command_height = mode_ == Mode::kNormal ? 0 : kCommandHeight;
  command_field_->SetVisible(mode_ != Mode::kNormal);

  root_panel_->SetBounds(CefRect(0, 0, width, height));
  main_panel_->SetSize(CefSize(width, std::max(1, height - command_height)));
  sidebar_panel_->SetSize(CefSize(kSidebarWidth, std::max(1, height - command_height)));
  command_field_->SetSize(CefSize(width, command_height));

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
}

void BrowserWindow::RefreshSidebar() {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool active = i == active_index_;
    std::string text = (active ? "▸ " : "  ") + std::to_string(i + 1) + ": " +
                       DisplayUrl(tabs_[i].url);
    tabs_[i].label->SetText(text);
    tabs_[i].label->SetEnabledTextColors(active ? kAccentColor : kTextColor);
    tabs_[i].label->SetBackgroundColor(active ? kActiveTabBackground
                                              : kInactiveTabBackground);
  }
}

Tab* BrowserWindow::ActiveTab() {
  if (tabs_.empty() || active_index_ >= tabs_.size()) {
    return nullptr;
  }
  return &tabs_[active_index_];
}

}  // namespace vimbrowser
