#include "browser_window.h"
#include "browser_font_settings.h"
#include "browser_window_internal.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "browser_client.h"
#include "config.h"
#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_request_context_handler.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_closure_task.h"
#include "theme.h"

namespace vimbrowser {
namespace {

class NamedRequestContextHandler final : public CefRequestContextHandler {
 public:
  NamedRequestContextHandler(BrowserWindow* owner, std::string context_name)
      : owner_(owner), context_name_(std::move(context_name)) {}

  void OnRequestContextInitialized(
      CefRefPtr<CefRequestContext> request_context) override {
    if (owner_) {
      owner_->OnNamedRequestContextInitialized(context_name_, request_context);
    }
  }

 private:
  BrowserWindow* owner_;
  const std::string context_name_;

  IMPLEMENT_REFCOUNTING(NamedRequestContextHandler);
  DISALLOW_COPY_AND_ASSIGN(NamedRequestContextHandler);
};

}  // namespace

void BrowserWindow::AddTab(std::string url,
                           bool activate,
                           std::string context_name) {
  InsertTab(std::move(url), tabs_.size(), activate, false, NewTabFolderId(), 0,
            false, std::move(context_name));
}

void BrowserWindow::AddTabAfterActive(std::string url, bool activate) {
  const size_t insert_index =
      active_index_ < tabs_.size() ? active_index_ + 1 : tabs_.size();
  const uint64_t folder_id = NewTabFolderId();
  uint64_t sort_order = 0;
  std::string context_name;
  if (active_index_ < tabs_.size() &&
      tabs_[active_index_].folder_id == folder_id) {
    sort_order = SidebarSortOrderAfterItem(
        {SidebarItemType::kTab, tabs_[active_index_].id});
  }
  if (active_index_ < tabs_.size()) {
    context_name = tabs_[active_index_].context;
  }
  InsertTab(std::move(url), insert_index, activate, false, folder_id,
            sort_order, false, std::move(context_name));
}

void BrowserWindow::InsertTab(std::string url,
                              size_t index,
                              bool activate,
                              bool defer_load,
                              uint64_t folder_id,
                              uint64_t sidebar_sort_order,
                              bool pinned,
                              std::string context_name) {
  last_tab_close_placeholder_ = false;
  const size_t insert_index = std::min(index, tabs_.size());
  const bool deferred_load = defer_load && !activate;

  Tab tab;
  SetTabId(tab, next_tab_id_++);
  SetTabUrl(tab, std::move(url));
  tab.context = std::move(context_name);
  tab.folder_id = SidebarFolderExists(folder_id) ? folder_id : 0;
  tab.sidebar_sort_order = sidebar_sort_order != 0
                               ? sidebar_sort_order
                               : NextSidebarSortOrder(tab.folder_id);
  tab.pinned = pinned;
  tab.deferred_load = deferred_load;

  if (!tabs_.empty() && insert_index <= active_index_) {
    ++active_index_;
  }
  if (visible_tab_index_ != kNoTabIndex && insert_index <= visible_tab_index_) {
    ++visible_tab_index_;
  }
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(insert_index), tab);
  if (!deferred_load) {
    EnsureTabBrowser(insert_index, false);
  }

  if (activate && !bulk_tab_update_) {
    active_index_ = insert_index;
    RevealTabInSidebar(active_index_);
    if (RefreshSidebar()) {
      Layout();
    }
    ScheduleStateSave();
    ScheduleActiveBrowserSync();
    UpdateStatusBar();
    return;
  }

  if (!bulk_tab_update_) {
    RefreshSidebar();
    Layout();
  }

  if (activate) {
    ActivateTab(insert_index);
  } else if (!bulk_tab_update_) {
    SaveState();
  }
}

bool BrowserWindow::AddContextTab(std::string context_name,
                                  std::string url,
                                  std::string* error) {
  if (!RequestContextForName(context_name, error)) {
    return false;
  }
  AddTab(std::move(url), true, std::move(context_name));
  return true;
}

CefRefPtr<CefRequestContext> BrowserWindow::RequestContextForName(
    const std::string& context_name,
    std::string* error) {
  if (context_name.empty()) {
    return CefRequestContext::GetGlobalContext();
  }
  if (!IsValidRequestContextName(context_name)) {
    if (error) {
      *error = "ERR invalid context name (use 1-48 lowercase letters, digits, '-' or '_'; first character must be alphanumeric)\n";
    }
    return nullptr;
  }
  if (const auto existing = request_contexts_.find(context_name);
      existing != request_contexts_.end()) {
    return existing->second;
  }
  if (root_cache_path_.empty()) {
    if (error) {
      *error = "ERR CEF root cache path is not configured\n";
    }
    return nullptr;
  }

  // Chrome-runtime CEF requires every disk-backed request context to be an
  // immediate child of root_cache_path (ChromeBrowserContext rejects deeper
  // profile paths). Keep the deterministic name grouped with a contexts- prefix.
  const std::filesystem::path context_path =
      std::filesystem::path(root_cache_path_) / ("contexts-" + context_name);
  std::error_code ec;
  std::filesystem::create_directories(context_path, ec);
  if (ec) {
    if (error) {
      *error = "ERR failed to create context cache directory\n";
    }
    return nullptr;
  }

  CefRequestContextSettings settings;
  CefString(&settings.cache_path) = context_path.string();
  settings.persist_session_cookies = true;
  CefRefPtr<CefRequestContext> context =
      CefRequestContext::CreateContext(
          settings, new NamedRequestContextHandler(this, context_name));
  if (!context) {
    if (error) {
      *error = "ERR failed to create request context\n";
    }
    return nullptr;
  }
  request_contexts_.emplace(context_name, context);
  return context;
}

void BrowserWindow::OnNamedRequestContextInitialized(
    std::string context_name,
    CefRefPtr<CefRequestContext> request_context) {
  if (!request_context || !IsValidRequestContextName(context_name)) {
    return;
  }
  request_contexts_.insert_or_assign(context_name, request_context);
  initialized_request_contexts_.insert(context_name);

  // Disk-backed contexts initialize asynchronously. Materialize every tab that
  // was queued while the profile loaded, then rerun active-view selection so an
  // active context tab becomes visible and focused.
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].context == context_name && !tabs_[i].view) {
      EnsureTabBrowser(i, false);
    }
  }
  ScheduleActiveBrowserSync();
}

bool BrowserWindow::EnsureTabBrowser(size_t index, bool load_deferred_now) {
  if (!content_inner_panel_ || index >= tabs_.size()) {
    return false;
  }
  Tab& tab = tabs_[index];
  if (tab.view) {
    if (load_deferred_now && tab.deferred_load && tab.client &&
        tab.client->browser() && tab.client->browser()->GetMainFrame()) {
      tab.deferred_load = false;
      tab.client->browser()->GetMainFrame()->LoadURL(tab.url);
    }
    return true;
  }

  CefBrowserSettings browser_settings;
  browser_settings.background_color = theme::kAppBg;
  browser_settings.tab_to_links = STATE_ENABLED;
  ApplyBrowserFontSettings(browser_settings);
  ++tab_client_count_;
  tab.client = new BrowserClient(this);
  std::string context_error;
  CefRefPtr<CefRequestContext> request_context =
      tab.context.empty() ? nullptr
                          : RequestContextForName(tab.context, &context_error);
  if (!tab.context.empty() && !request_context) {
    std::cerr << "vimbrowser: " << context_error;
    tab.client = nullptr;
    --tab_client_count_;
    return false;
  }
  if (!tab.context.empty() &&
      !initialized_request_contexts_.contains(tab.context)) {
    tab.client = nullptr;
    --tab_client_count_;
    return false;
  }
  const std::string browser_url = load_deferred_now && tab.deferred_load
                                      ? tab.url
                                      : (tab.deferred_load ? "about:blank"
                                                           : tab.url);
  if (load_deferred_now) {
    tab.deferred_load = false;
  }
  tab.view = CefBrowserView::CreateBrowserView(tab.client, browser_url,
                                               browser_settings, nullptr,
                                               request_context, this);
  if (!tab.view) {
    tab.client = nullptr;
    --tab_client_count_;
    return false;
  }
  tab.view->SetPreferAccelerators(true);
  tab.view->SetVisible(false);
  content_inner_panel_->AddChildView(tab.view);
  return true;
}

void BrowserWindow::InsertPopupTab(CefRefPtr<CefBrowserView> popup_browser_view,
                                   CefRefPtr<BrowserClient> popup_client,
                                   std::string url,
                                   size_t index,
                                   bool activate,
                                   uint64_t folder_id,
                                   uint64_t sidebar_sort_order,
                                   std::string context_name) {
  if (!popup_browser_view || !popup_client) {
    return;
  }

  last_tab_close_placeholder_ = false;

  Tab tab;
  SetTabId(tab, next_tab_id_++);
  SetTabUrl(tab, std::move(url));
  tab.context = std::move(context_name);
  tab.folder_id = SidebarFolderExists(folder_id) ? folder_id : 0;
  tab.sidebar_sort_order = sidebar_sort_order != 0
                               ? sidebar_sort_order
                               : NextSidebarSortOrder(tab.folder_id);
  tab.client = popup_client;
  ++tab_client_count_;
  tab.view = popup_browser_view;
  tab.view->SetPreferAccelerators(true);
  tab.view->SetVisible(false);
  content_inner_panel_->AddChildView(tab.view);

  const size_t insert_index = std::min(index, tabs_.size());
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
    if (!bulk_tab_update_) {
      RevealTabInSidebar(index);
    }
    if (visible_tab_index_ != index || !tabs_[index].view) {
      ScheduleActiveBrowserSync();
    }
    if (!bulk_tab_update_ && RefreshSidebar()) {
      Layout();
    }
    UpdateStatusBar();
    return;
  }

  active_index_ = index;
  if (!bulk_tab_update_) {
    RevealTabInSidebar(active_index_);
  }
  if (!bulk_tab_update_) {
    ScheduleStateSave();
  }
  ScheduleActiveBrowserSync();
  if (!bulk_tab_update_ && RefreshSidebar()) {
    Layout();
  }
  UpdateStatusBar();
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
  EnsureTabBrowser(active_index_, true);
  Tab& tab = tabs_[active_index_];
  if (tab.view) {
    tab.view->SetVisible(true);
    if (tab.deferred_load && tab.client && tab.client->browser() &&
        tab.client->browser()->GetMainFrame()) {
      tab.deferred_load = false;
      tab.client->browser()->GetMainFrame()->LoadURL(tab.url);
    }
    if (content_inner_panel_ && content_inner_panel_->GetLayout()) {
      content_inner_panel_->Layout();
    }
    // Showing a different BrowserView remaps Chromium's native page surface
    // after the previous chrome layout. Refresh the top-level layout now so the
    // sidebar separator overlay is repainted above the newly-active surface.
    Layout();
    if (focus_area_ == FocusArea::kWebView) {
      tab.view->RequestFocus();
    }
  }
  UpdateFpsIndicator();
  UpdateStatusBar();
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

bool BrowserWindow::ActivateRelativeAudible(int delta) {
  if (tabs_.empty() || delta == 0) {
    return false;
  }
  RefreshAudibleTabs();

  const size_t count = tabs_.size();
  const int direction = delta > 0 ? 1 : -1;
  for (size_t step = 1; step <= count; ++step) {
    const size_t offset = step % count;
    const size_t index = direction > 0
                             ? (active_index_ + offset) % count
                             : (active_index_ + count - offset) % count;
    if (tabs_[index].audible) {
      ActivateTab(index);
      return true;
    }
  }
  return false;
}

void BrowserWindow::ActivateFirstTab() {
  ActivateTab(0);
}

void BrowserWindow::ActivateLastTab() {
  if (!tabs_.empty()) {
    ActivateTab(tabs_.size() - 1);
  }
}

void BrowserWindow::ScheduleActivePageBlur() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser) {
    return;
  }
  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(&BrowserWindow::BlurPageFocus, self,
                                    browser),
                     25);
}

void BrowserWindow::BlurPageFocus(CefRefPtr<CefBrowser> browser) {
  if (!browser || !browser->GetMainFrame()) {
    return;
  }
  browser->GetMainFrame()->ExecuteJavaScript(
      kBlurActiveElementScript, browser->GetMainFrame()->GetURL(), 0);
}

void BrowserWindow::ForwardKeyToActivePage(const CefKeyEvent& event) {
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    forwarding_key_to_page_ = true;
    browser->GetHost()->SendKeyEvent(event);
    if (IsEscapeKey(event)) {
      ScheduleActivePageBlur();
    }
    CefRefPtr<BrowserWindow> self = this;
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&BrowserWindow::ClearForwardingKeyGuard,
                                      self),
                       50);
  }
}

void BrowserWindow::ClearForwardingKeyGuard() {
  forwarding_key_to_page_ = false;
}

void BrowserWindow::ClearForwardingDevToolsKeyGuard() {
  forwarding_key_to_devtools_ = false;
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
  if (tabs_[old_active_index].folder_id == tabs_[new_active_index].folder_id) {
    std::swap(tabs_[old_active_index].sidebar_sort_order,
              tabs_[new_active_index].sidebar_sort_order);
  }
  std::swap(tabs_[active_index_], tabs_[static_cast<size_t>(next)]);
  if (visible_tab_index_ == old_active_index) {
    visible_tab_index_ = new_active_index;
  } else if (visible_tab_index_ == new_active_index) {
    visible_tab_index_ = old_active_index;
  }
  active_index_ = new_active_index;
  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
  UpdateStatusBar();
}

bool BrowserWindow::MoveTabToIndex(size_t from, size_t to) {
  if (tabs_.empty() || from >= tabs_.size()) {
    return false;
  }
  to = std::min(to, tabs_.size() - 1);
  if (from == to) {
    return true;
  }

  const size_t old_active_index = active_index_;
  const size_t old_visible_index = visible_tab_index_;
  const uint64_t moved_tab_id = tabs_[from].id;
  const uint64_t moved_folder_id = tabs_[from].folder_id;
  std::unordered_set<uint64_t> crossed_sibling_ids{moved_tab_id};
  const size_t crossed_start = std::min(from, to);
  const size_t crossed_end = std::max(from, to);
  for (size_t i = crossed_start; i <= crossed_end; ++i) {
    if (tabs_[i].folder_id == moved_folder_id) {
      crossed_sibling_ids.insert(tabs_[i].id);
    }
  }

  Tab tab = tabs_[from];
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to), tab);

  if (crossed_sibling_ids.size() > 1) {
    std::vector<uint64_t> sibling_sort_orders;
    for (const Tab& sibling : tabs_) {
      if (crossed_sibling_ids.contains(sibling.id)) {
        sibling_sort_orders.push_back(sibling.sidebar_sort_order);
      }
    }
    std::sort(sibling_sort_orders.begin(), sibling_sort_orders.end());
    size_t sibling_index = 0;
    for (Tab& sibling : tabs_) {
      if (crossed_sibling_ids.contains(sibling.id)) {
        sibling.sidebar_sort_order = sibling_sort_orders[sibling_index++];
      }
    }
  }

  if (old_active_index < tabs_.size()) {
    active_index_ = IndexAfterVectorMove(old_active_index, from, to);
  }
  if (old_visible_index < tabs_.size()) {
    visible_tab_index_ = IndexAfterVectorMove(old_visible_index, from, to);
  } else {
    visible_tab_index_ = kNoTabIndex;
  }

  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
  UpdateStatusBar();
  return true;
}

void BrowserWindow::CloneActiveTab() {
  const std::string url = ActiveTabUrl();
  if (!url.empty()) {
    const std::string context = ActiveTab() ? ActiveTab()->context : std::string();
    AddTab(url, true, context);
  }
}

void BrowserWindow::CloseActiveTab(CloseFocus focus_after_close) {
  CloseTabAtIndex(active_index_, focus_after_close);
}

void BrowserWindow::CloseTabAtIndex(size_t closing, CloseFocus focus_after_close) {
  if (tabs_.empty() || closing >= tabs_.size()) {
    return;
  }

  const bool closing_active = closing == active_index_;
  const bool closing_sidebar_selection =
      sidebar_selected_item_.type == SidebarItemType::kTab &&
      sidebar_selected_item_.id == tabs_[closing].id;
  if ((sidebar_visual_anchor_.type == SidebarItemType::kTab &&
       sidebar_visual_anchor_.id == tabs_[closing].id) ||
      closing_sidebar_selection) {
    sidebar_visual_anchor_ = {};
  }
  size_t closing_sidebar_entry = 0;
  if (closing_sidebar_selection) {
    size_t entry = 0;
    for (const SidebarDisplayRow& row : BuildSidebarDisplayRows()) {
      if (row.kind != SidebarRowKind::kEntry) {
        continue;
      }
      if (row.item == sidebar_selected_item_) {
        closing_sidebar_entry = entry;
        break;
      }
      ++entry;
    }
  }
  const uint64_t active_id = active_index_ < tabs_.size() ? tabs_[active_index_].id : 0;
  const std::string closing_url = tabs_[closing].url;
  std::cerr << "vimbrowser: close-tab id=" << tabs_[closing].id
            << " index=" << (closing + 1)
            << " count=" << tabs_.size() << " url=" << closing_url
            << std::endl;
  if (!closing_url.empty() && tabs_[closing].context.empty()) {
    closed_tabs_.push_back({closing_url, closing, tabs_[closing].folder_id,
                            tabs_[closing].sidebar_sort_order,
                            tabs_[closing].pinned});
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

  if (closing_active) {
    active_index_ = std::min(next_index, tabs_.size() - 1);
    RevealTabInSidebar(active_index_);
    if (tabs_.size() > kSidebarMaxRenderedRows && sidebar_spacer_) {
      visible_tab_index_ = kNoTabIndex;
      ScheduleActiveBrowserSync();
    } else {
      EnsureTabBrowser(active_index_, true);
      if (tabs_[active_index_].view) {
        tabs_[active_index_].view->SetVisible(true);
      }
      visible_tab_index_ = active_index_;
    }
  } else if (active_id != 0) {
    if (std::optional<size_t> index = FindTabIndexById(active_id)) {
      active_index_ = *index;
    } else {
      active_index_ = std::min(active_index_, tabs_.size() - 1);
    }
    if (closing_sidebar_selection) {
      std::vector<SidebarItemRef> remaining_items;
      sidebar_selected_item_ = {};
      for (const SidebarDisplayRow& row : BuildSidebarDisplayRows()) {
        if (row.kind == SidebarRowKind::kEntry) {
          remaining_items.push_back(row.item);
        }
      }
      if (!remaining_items.empty()) {
        const size_t target = closing_sidebar_entry > 0
                                  ? closing_sidebar_entry - 1
                                  : 0;
        sidebar_selected_item_ =
            remaining_items[std::min(target, remaining_items.size() - 1)];
      }
    }
  }
  UpdateFpsIndicator();
  UpdateStatusBar();
  if (closing_active && focus_area_ == FocusArea::kWebView &&
      tabs_[active_index_].view) {
    tabs_[active_index_].view->RequestFocus();
  }
  SaveState();
  if (closing_active && tabs_.size() > kSidebarMaxRenderedRows &&
      sidebar_spacer_) {
    const bool sidebar_hierarchy_changed = RefreshSidebar();
    if (sidebar_hierarchy_changed) {
      Layout();
    }
    if (content_inner_panel_ && content_inner_panel_->GetLayout()) {
      content_inner_panel_->Layout();
    }
    last_tab_close_placeholder_ = false;
    UpdateStatusBar();
    return;
  }
  if (!closing_active && tabs_.size() > kSidebarMaxRenderedRows &&
      sidebar_spacer_) {
    const bool sidebar_hierarchy_changed = RefreshSidebar();
    if (sidebar_hierarchy_changed) {
      Layout();
    }
    last_tab_close_placeholder_ = false;
    UpdateStatusBar();
    return;
  }
  const bool sidebar_hierarchy_changed = RefreshSidebar();
  if (closing_active || sidebar_hierarchy_changed) {
    Layout();
  }
  last_tab_close_placeholder_ = false;
  UpdateStatusBar();
}

void BrowserWindow::CloseTabBackend(Tab& tab) {
  if (tab.id != 0 && tab.id == devtools_opener_tab_id_) {
    CloseDevTools();
  }
  if (tab.view) {
    tab.view->SetVisible(false);
    if (content_inner_panel_) {
      content_inner_panel_->RemoveChildView(tab.view);
    }
  }
  if (tab.client) {
    tab.client->DetachOwner();
    if (tab_client_count_ > 0) {
      --tab_client_count_;
    }
  }
  tab.view = nullptr;
  tab.client = nullptr;
}

void BrowserWindow::CloseTabsInDeletedSidebarFolders(
    const std::unordered_set<uint64_t>& folder_ids) {
  if (folder_ids.empty() || tabs_.empty()) {
    return;
  }

  const uint64_t old_active_id = ActiveTabId();
  const uint64_t old_visible_id =
      visible_tab_index_ < tabs_.size() ? tabs_[visible_tab_index_].id : 0;
  const size_t old_active_index = active_index_;
  size_t surviving_before_active = 0;
  for (size_t i = 0; i < std::min(old_active_index, tabs_.size()); ++i) {
    if (!folder_ids.contains(tabs_[i].folder_id)) {
      ++surviving_before_active;
    }
  }
  const bool active_deleted =
      active_index_ < tabs_.size() &&
      folder_ids.contains(tabs_[active_index_].folder_id);

  ++active_browser_sync_generation_;
  bulk_tab_update_ = true;
  for (size_t i = tabs_.size(); i > 0; --i) {
    const size_t index = i - 1;
    if (!folder_ids.contains(tabs_[index].folder_id)) {
      continue;
    }
    if (!tabs_[index].url.empty() && tabs_[index].context.empty()) {
      closed_tabs_.push_back({tabs_[index].url, index, tabs_[index].folder_id,
                              tabs_[index].sidebar_sort_order,
                              tabs_[index].pinned});
    }
    Tab closing_tab = tabs_[index];
    CloseTabBackend(closing_tab);
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
  }

  if (tabs_.empty()) {
    active_index_ = 0;
    visible_tab_index_ = kNoTabIndex;
    InsertTab("about:blank", 0, true, false, current_sidebar_folder_id_);
    last_tab_close_placeholder_ = true;
  } else {
    if (!active_deleted) {
      active_index_ = FindTabIndexById(old_active_id).value_or(0);
    } else {
      active_index_ = surviving_before_active > 0
                          ? std::min(surviving_before_active - 1,
                                     tabs_.size() - 1)
                          : 0;
    }
    visible_tab_index_ = FindTabIndexById(old_visible_id).value_or(kNoTabIndex);
    if (active_deleted || visible_tab_index_ != active_index_) {
      ScheduleActiveBrowserSync();
    }
  }
  bulk_tab_update_ = false;
  UpdateFpsIndicator();
  UpdateStatusBar();
}

void BrowserWindow::QuitBrowser() {
  if (window_close_pending_) {
    return;
  }
  if (window_) {
    window_->Close();
  } else {
    CefQuitMessageLoop();
  }
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
    SetTabUrl(tabs_[0], closed_tab.url);
    tabs_[0].folder_id = SidebarFolderExists(closed_tab.folder_id)
                             ? closed_tab.folder_id
                             : 0;
    tabs_[0].sidebar_sort_order = closed_tab.sidebar_sort_order != 0
                                      ? closed_tab.sidebar_sort_order
                                      : NextSidebarSortOrder(tabs_[0].folder_id);
    tabs_[0].pinned = closed_tab.pinned;
    RevealTabInSidebar(0);
    tabs_[0].client->browser()->GetMainFrame()->LoadURL(closed_tab.url);
    SaveState();
    RefreshSidebar();
    Layout();
    return;
  }
  InsertTab(closed_tab.url, closed_tab.index, true, false,
            closed_tab.folder_id, closed_tab.sidebar_sort_order,
            closed_tab.pinned);
}

std::optional<size_t> BrowserWindow::FindTabIndexById(uint64_t tab_id) const {
  if (tab_id == 0) {
    return std::nullopt;
  }
  auto cache_lookup = [&]() -> std::optional<size_t> {
    for (size_t i = 0; i < cached_tab_lookup_ids_.size(); ++i) {
      if (cached_tab_lookup_ids_[i] == tab_id &&
          cached_tab_lookup_indexes_[i] < tabs_.size() &&
          tabs_[cached_tab_lookup_indexes_[i]].id == tab_id) {
        return cached_tab_lookup_indexes_[i];
      }
    }
    return std::nullopt;
  };
  auto cache_store = [&](size_t index) {
    for (size_t i = cached_tab_lookup_ids_.size() - 1; i > 0; --i) {
      cached_tab_lookup_ids_[i] = cached_tab_lookup_ids_[i - 1];
      cached_tab_lookup_indexes_[i] = cached_tab_lookup_indexes_[i - 1];
    }
    cached_tab_lookup_ids_[0] = tab_id;
    cached_tab_lookup_indexes_[0] = index;
  };

  const size_t likely_index = static_cast<size_t>(tab_id - 1);
  if (likely_index < tabs_.size() && tabs_[likely_index].id == tab_id) {
    cache_store(likely_index);
    return likely_index;
  }
  if (std::optional<size_t> cached_index = cache_lookup()) {
    return *cached_index;
  }
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].id == tab_id) {
      cache_store(i);
      return i;
    }
  }
  return std::nullopt;
}

uint64_t BrowserWindow::ActiveTabId() const {
  if (tabs_.empty() || active_index_ >= tabs_.size()) {
    return 0;
  }
  return tabs_[active_index_].id;
}

CefRefPtr<CefBrowser> BrowserWindow::BrowserForTabId(uint64_t tab_id,
                                                     std::string* error,
                                                     size_t* index_out) const {
  std::optional<size_t> index = FindTabIndexById(tab_id);
  if (!index) {
    if (error) {
      *error = "ERR no such tabid\n";
    }
    return nullptr;
  }
  if (index_out) {
    *index_out = *index;
  }
  const Tab& tab = tabs_[*index];
  CefRefPtr<CefBrowser> browser = tab.client ? tab.client->browser() : nullptr;
  if (!browser) {
    if (error) {
      *error = "ERR tab has no browser\n";
    }
    return nullptr;
  }
  return browser;
}


}  // namespace vimbrowser
