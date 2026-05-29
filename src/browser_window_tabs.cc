#include "browser_window.h"
#include "browser_window_internal.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "browser_client.h"
#include "config.h"
#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_closure_task.h"
#include "theme.h"

namespace vimbrowser {

void BrowserWindow::AddTab(std::string url, bool activate) {
  InsertTab(std::move(url), tabs_.size(), activate);
}

void BrowserWindow::AddTabAfterActive(std::string url, bool activate) {
  const size_t insert_index =
      active_index_ < tabs_.size() ? active_index_ + 1 : tabs_.size();
  InsertTab(std::move(url), insert_index, activate);
}

void BrowserWindow::InsertTab(std::string url,
                              size_t index,
                              bool activate,
                              bool defer_load) {
  last_tab_close_placeholder_ = false;
  const size_t insert_index = std::min(index, tabs_.size());
  const bool deferred_load = defer_load && !activate;

  Tab tab;
  SetTabId(tab, next_tab_id_++);
  SetTabUrl(tab, std::move(url));
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
    if (tabs_.size() > kSidebarMaxRenderedRows && sidebar_spacer_) {
      ScheduleSidebarRefresh();
    } else if (RefreshSidebar()) {
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
  ++tab_client_count_;
  tab.client = new BrowserClient(this);
  const std::string browser_url = load_deferred_now && tab.deferred_load
                                      ? tab.url
                                      : (tab.deferred_load ? "about:blank"
                                                           : tab.url);
  if (load_deferred_now) {
    tab.deferred_load = false;
  }
  tab.view = CefBrowserView::CreateBrowserView(tab.client, browser_url,
                                               browser_settings, nullptr,
                                               nullptr, this);
  tab.view->SetPreferAccelerators(true);
  tab.view->SetVisible(false);
  content_inner_panel_->AddChildView(tab.view);
  return true;
}

void BrowserWindow::InsertPopupTab(CefRefPtr<CefBrowserView> popup_browser_view,
                                   CefRefPtr<BrowserClient> popup_client,
                                   std::string url,
                                   size_t index,
                                   bool activate) {
  if (!popup_browser_view || !popup_client) {
    return;
  }

  last_tab_close_placeholder_ = false;

  Tab tab;
  SetTabId(tab, next_tab_id_++);
  SetTabUrl(tab, std::move(url));
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
    bool needs_sidebar_refresh = false;
    if (!bulk_tab_update_) {
      if (tabs_.size() <= kSidebarMaxRenderedRows &&
          sidebar_rows_.size() == tabs_.size() && sidebar_spacer_) {
        RefreshSidebarRow(index);
      } else {
        needs_sidebar_refresh = true;
      }
    }
    if (visible_tab_index_ != index || !tabs_[index].view) {
      ScheduleActiveBrowserSync();
    }
    if (needs_sidebar_refresh) {
      ScheduleSidebarRefresh();
    }
    UpdateStatusBar();
    return;
  }

  const size_t previous_active_index = active_index_;
  active_index_ = index;
  bool needs_sidebar_refresh = false;
  if (!bulk_tab_update_) {
    if (tabs_.size() <= kSidebarMaxRenderedRows &&
        sidebar_rows_.size() == tabs_.size() && sidebar_spacer_) {
      RefreshSidebarRow(previous_active_index);
      RefreshSidebarRow(active_index_);
    } else {
      needs_sidebar_refresh = true;
    }
  }
  if (!bulk_tab_update_) {
    ScheduleStateSave();
  }
  ScheduleActiveBrowserSync();
  if (needs_sidebar_refresh) {
    ScheduleSidebarRefresh();
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
  const auto [old_render_start, old_render_count] =
      SidebarRenderedRange(tabs_.size(), active_index_);
  const size_t old_render_end = old_render_start + old_render_count;

  Tab tab = tabs_[from];
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to), tab);

  if (old_active_index < tabs_.size()) {
    active_index_ = IndexAfterVectorMove(old_active_index, from, to);
  }
  if (old_visible_index < tabs_.size()) {
    visible_tab_index_ = IndexAfterVectorMove(old_visible_index, from, to);
  } else {
    visible_tab_index_ = kNoTabIndex;
  }

  SaveState();
  const bool can_keep_virtual_sidebar =
      tabs_.size() > kSidebarMaxRenderedRows &&
      active_index_ == old_active_index &&
      visible_tab_index_ == old_visible_index &&
      sidebar_rows_.size() == old_render_count &&
      ((from >= old_render_end && to >= old_render_end) ||
       (from < old_render_start && to < old_render_start));
  if (!can_keep_virtual_sidebar) {
    if (tabs_.size() > kSidebarMaxRenderedRows && sidebar_spacer_) {
      ScheduleSidebarRefresh();
    } else {
      if (RefreshSidebar()) {
        Layout();
      }
    }
  }
  UpdateStatusBar();
  return true;
}

void BrowserWindow::CloneActiveTab() {
  const std::string url = ActiveTabUrl();
  if (!url.empty()) {
    AddTab(url, true);
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
  const uint64_t active_id = active_index_ < tabs_.size() ? tabs_[active_index_].id : 0;
  const size_t old_active_index = active_index_;
  const size_t old_visible_index = visible_tab_index_;
  const auto [old_render_start, old_render_count] =
      SidebarRenderedRange(tabs_.size(), active_index_);
  const size_t old_render_end = old_render_start + old_render_count;
  const bool can_keep_virtual_sidebar_on_close =
      tabs_.size() > kSidebarMaxRenderedRows && !closing_active &&
      closing >= old_render_end && sidebar_rows_.size() == old_render_count;
  const std::string closing_url = tabs_[closing].url;
  std::cerr << "vimbrowser: close-tab id=" << tabs_[closing].id
            << " index=" << (closing + 1)
            << " count=" << tabs_.size() << " url=" << closing_url
            << std::endl;
  if (!closing_url.empty()) {
    closed_tabs_.push_back({closing_url, closing});
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
    ScheduleSidebarRefresh();
    if (content_inner_panel_ && content_inner_panel_->GetLayout()) {
      content_inner_panel_->Layout();
    }
    last_tab_close_placeholder_ = false;
    UpdateStatusBar();
    return;
  }
  if (can_keep_virtual_sidebar_on_close && active_index_ == old_active_index &&
      visible_tab_index_ == old_visible_index) {
    last_tab_close_placeholder_ = false;
    UpdateStatusBar();
    return;
  }
  if (!closing_active && tabs_.size() > kSidebarMaxRenderedRows &&
      sidebar_spacer_) {
    ScheduleSidebarRefresh();
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
    tabs_[0].client->browser()->GetMainFrame()->LoadURL(closed_tab.url);
    SaveState();
    RefreshSidebar();
    Layout();
    return;
  }
  InsertTab(closed_tab.url, closed_tab.index, true);
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
