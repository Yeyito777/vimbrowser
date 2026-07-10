#include "browser_window.h"

#include "browser_window_internal.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "config.h"
#include "include/base/cef_callback.h"
#include "include/cef_navigation_entry.h"
#include "include/wrapper/cef_closure_task.h"

namespace vimbrowser {
namespace {

uint32_t SidebarDecimalDigits(size_t value) {
  uint32_t digits = 1;
  while (value >= 10) {
    value /= 10;
    ++digits;
  }
  return digits;
}

bool ValidFolderName(const std::string& name) {
  if (name.empty() || name == "." || name == ".." ||
      name.find('/') != std::string::npos) {
    return false;
  }
  return std::none_of(name.begin(), name.end(),
                      [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

} // namespace

std::vector<BrowserWindow::SidebarDisplayRow>
BrowserWindow::BuildSidebarDisplayRows() const {
  std::vector<SidebarDisplayRow> rows;
  rows.reserve(tabs_.size() + sidebar_folders_.size() + 4);
  const std::string search_query = ActiveSidebarSearchQuery();
  const bool searching = !search_query.empty();

  // The root intentionally has no redundant "Tabs" heading. Nested folders keep
  // a compact breadcrumb so h/Backspace navigation still has visible context.
  if (!searching) {
    if (const SidebarFolder* folder =
            FindSidebarFolder(current_sidebar_folder_id_)) {
      rows.push_back({SidebarRowKind::kFolderHeader,
                      {},
                      kNoTabIndex,
                      " " + folder->name + "/"});
    }
  }

  struct FolderAggregate {
    size_t tab_count = 0;
    bool audible = false;
  };
  std::unordered_map<uint64_t, FolderAggregate> aggregates;
  aggregates.reserve(sidebar_folders_.size());
  for (const SidebarFolder& folder : sidebar_folders_) {
    aggregates.emplace(folder.id, FolderAggregate{});
  }
  for (const Tab& tab : tabs_) {
    uint64_t folder_id = tab.folder_id;
    std::unordered_set<uint64_t> seen;
    while (folder_id != 0 && seen.insert(folder_id).second) {
      auto aggregate = aggregates.find(folder_id);
      if (aggregate == aggregates.end()) {
        break;
      }
      ++aggregate->second.tab_count;
      aggregate->second.audible |= tab.audible;
      const SidebarFolder* folder = FindSidebarFolder(folder_id);
      folder_id = folder ? folder->parent_id : 0;
    }
  }

  if (!searching && current_sidebar_folder_id_ != 0) {
    const SidebarItemRef item{SidebarItemType::kParent, 0};
    const bool selected = sidebar_selected_item_ == item;
    rows.push_back({SidebarRowKind::kEntry, item, kNoTabIndex,
                    std::string(selected ? "▸ " : "  ") + "..", selected});
  }

  struct OrderedItem {
    bool pinned = false;
    uint64_t sort_order = 0;
    SidebarItemRef item;
    size_t tab_index = kNoTabIndex;
  };
  std::vector<OrderedItem> ordered;
  for (const SidebarFolder& folder : sidebar_folders_) {
    const bool visible =
        searching ? ContainsCaseInsensitive(folder.name, search_query)
                  : folder.parent_id == current_sidebar_folder_id_;
    if (visible) {
      ordered.push_back({folder.pinned,
                         folder.sort_order,
                         {SidebarItemType::kFolder, folder.id}});
    }
  }
  for (size_t i = 0; i < tabs_.size(); ++i) {
    std::string searchable = tabs_[i].url;
    if (tabs_[i].client && tabs_[i].client->browser() &&
        tabs_[i].client->browser()->GetHost()) {
      if (CefRefPtr<CefNavigationEntry> entry =
              tabs_[i]
                  .client->browser()
                  ->GetHost()
                  ->GetVisibleNavigationEntry()) {
        searchable += " ";
        searchable += entry->GetTitle().ToString();
      }
    }
    const bool visible = searching
                             ? ContainsCaseInsensitive(searchable, search_query)
                             : tabs_[i].folder_id == current_sidebar_folder_id_;
    if (visible) {
      ordered.push_back({tabs_[i].pinned,
                         tabs_[i].sidebar_sort_order,
                         {SidebarItemType::kTab, tabs_[i].id},
                         i});
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const OrderedItem& a, const OrderedItem& b) {
              if (a.pinned != b.pinned) {
                return a.pinned;
              }
              if (a.sort_order != b.sort_order) {
                return a.sort_order < b.sort_order;
              }
              if (a.item.type != b.item.type) {
                return a.item.type == SidebarItemType::kFolder;
              }
              return a.item.id < b.item.id;
            });

  auto append_entry = [&](const OrderedItem& entry) {
    const bool selected = sidebar_selected_item_ == entry.item;
    const char* prefix = selected ? "▸ " : "  ";
    if (entry.item.type == SidebarItemType::kFolder) {
      const SidebarFolder* folder = FindSidebarFolder(entry.item.id);
      if (!folder) {
        return;
      }
      const auto aggregate = aggregates.find(folder->id);
      const bool audible =
          aggregate != aggregates.end() && aggregate->second.audible;
      const size_t count =
          aggregate == aggregates.end() ? 0 : aggregate->second.tab_count;
      std::string text = prefix;
      if (audible) {
        text += "◉ ";
      }
      text += "📁 " + folder->name + "/ " + std::to_string(count);
      rows.push_back({SidebarRowKind::kEntry, entry.item, kNoTabIndex,
                      std::move(text), selected, false, audible, 2});
      return;
    }

    if (entry.tab_index >= tabs_.size()) {
      return;
    }
    const Tab& tab = tabs_[entry.tab_index];
    const bool active = entry.tab_index == active_index_;
    std::string text = selected ? "▸ " : active ? "• " : "  ";
    text += std::to_string(entry.tab_index + 1);
    text += ": ";
    const uint32_t audible_offset =
        2 + SidebarDecimalDigits(entry.tab_index + 1) + 2;
    if (tab.audible) {
      text += "◉ ";
    }
    text += DisplayUrl(tab.url);
    rows.push_back({SidebarRowKind::kEntry, entry.item, entry.tab_index,
                    std::move(text), selected, active, tab.audible,
                    audible_offset});
  };

  const auto first_unpinned =
      std::find_if(ordered.begin(), ordered.end(),
                   [](const OrderedItem& entry) { return !entry.pinned; });
  if (first_unpinned != ordered.begin()) {
    rows.push_back({SidebarRowKind::kSectionLabel, {}, kNoTabIndex, " Pinned"});
    for (auto entry = ordered.begin(); entry != first_unpinned; ++entry) {
      append_entry(*entry);
    }
    if (first_unpinned != ordered.end()) {
      rows.push_back({SidebarRowKind::kSeparator,
                      {},
                      kNoTabIndex,
                      " ────────────────────── "});
    }
  }
  for (auto entry = first_unpinned; entry != ordered.end(); ++entry) {
    append_entry(*entry);
  }
  return rows;
}

void BrowserWindow::EnsureSidebarSelection() {
  if (current_sidebar_folder_id_ != 0 &&
      !SidebarFolderExists(current_sidebar_folder_id_)) {
    current_sidebar_folder_id_ = 0;
  }

  auto visible = [&](const SidebarItemRef& item) {
    const bool searching = !ActiveSidebarSearchQuery().empty();
    if (item.type == SidebarItemType::kParent) {
      return !searching && current_sidebar_folder_id_ != 0;
    }
    if (item.type == SidebarItemType::kFolder) {
      const SidebarFolder* folder = FindSidebarFolder(item.id);
      return folder &&
             (searching || folder->parent_id == current_sidebar_folder_id_);
    }
    if (item.type == SidebarItemType::kTab) {
      const std::optional<size_t> index = FindTabIndexById(item.id);
      return index && (searching ||
                       tabs_[*index].folder_id == current_sidebar_folder_id_);
    }
    return false;
  };
  if (visible(sidebar_selected_item_)) {
    return;
  }

  if (current_sidebar_folder_id_ != 0) {
    sidebar_selected_item_ = {SidebarItemType::kParent, 0};
    return;
  }
  if (active_index_ < tabs_.size() && tabs_[active_index_].folder_id == 0) {
    sidebar_selected_item_ = {SidebarItemType::kTab, tabs_[active_index_].id};
    return;
  }

  sidebar_selected_item_ = {};
  const std::vector<SidebarDisplayRow> rows = BuildSidebarDisplayRows();
  for (const SidebarDisplayRow& row : rows) {
    if (row.kind == SidebarRowKind::kEntry) {
      sidebar_selected_item_ = row.item;
      break;
    }
  }
}

void BrowserWindow::RevealTabInSidebar(size_t index) {
  if (index >= tabs_.size()) {
    return;
  }
  current_sidebar_folder_id_ =
      SidebarFolderExists(tabs_[index].folder_id) ? tabs_[index].folder_id : 0;
  tabs_[index].folder_id = current_sidebar_folder_id_;
  sidebar_selected_item_ = {SidebarItemType::kTab, tabs_[index].id};
  sidebar_visual_anchor_ = {};
  sidebar_search_highlights_visible_ = false;
  sidebar_pending_keys_.clear();
}

void BrowserWindow::MoveSidebarSelection(int delta) {
  if (delta == 0) {
    return;
  }
  EnsureSidebarSelection();
  const std::vector<SidebarDisplayRow> rows = BuildSidebarDisplayRows();
  std::vector<SidebarItemRef> items;
  const bool visual_active =
      sidebar_visual_anchor_.type != SidebarItemType::kNone;
  for (const SidebarDisplayRow& row : rows) {
    if (row.kind == SidebarRowKind::kEntry &&
        (!visual_active || row.item.type == SidebarItemType::kFolder ||
         row.item.type == SidebarItemType::kTab)) {
      items.push_back(row.item);
    }
  }
  if (items.empty()) {
    sidebar_selected_item_ = {};
    RefreshSidebar();
    return;
  }
  auto selected = std::find(items.begin(), items.end(), sidebar_selected_item_);
  int index =
      selected == items.end() ? 0 : static_cast<int>(selected - items.begin());
  index = std::clamp(index + (delta > 0 ? 1 : -1), 0,
                     static_cast<int>(items.size()) - 1);
  sidebar_selected_item_ = items[static_cast<size_t>(index)];
  sidebar_pending_keys_.clear();
  RefreshSidebar();
}

void BrowserWindow::MoveSidebarSelectionToEdge(bool last) {
  EnsureSidebarSelection();
  const std::vector<SidebarDisplayRow> rows = BuildSidebarDisplayRows();
  SidebarItemRef target;
  for (const SidebarDisplayRow& row : rows) {
    if (row.kind != SidebarRowKind::kEntry) {
      continue;
    }
    target = row.item;
    if (!last) {
      break;
    }
  }
  if (target.type == SidebarItemType::kNone) {
    return;
  }
  sidebar_selected_item_ = target;
  sidebar_pending_keys_.clear();
  RefreshSidebar();
}

void BrowserWindow::ActivateSidebarItem(const SidebarItemRef& item) {
  sidebar_selected_item_ = item;
  if (item.type == SidebarItemType::kParent) {
    LeaveSidebarFolder();
    return;
  }
  if (item.type == SidebarItemType::kFolder) {
    if (sidebar_search_highlights_visible_) {
      sidebar_search_highlights_visible_ = false;
      if (const SidebarFolder* folder = FindSidebarFolder(item.id)) {
        current_sidebar_folder_id_ = folder->parent_id;
      }
    }
    EnterSidebarFolder(item.id);
    return;
  }
  if (item.type == SidebarItemType::kTab) {
    if (const std::optional<size_t> index = FindTabIndexById(item.id)) {
      sidebar_search_highlights_visible_ = false;
      ActivateTab(*index);
    }
  }
}

void BrowserWindow::EnterSidebarFolder(uint64_t folder_id) {
  const SidebarFolder* folder = FindSidebarFolder(folder_id);
  if (!folder || folder->parent_id != current_sidebar_folder_id_) {
    return;
  }
  current_sidebar_folder_id_ = folder_id;
  sidebar_selected_item_ = {SidebarItemType::kParent, 0};
  sidebar_visual_anchor_ = {};
  sidebar_pending_keys_.clear();
  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
}

void BrowserWindow::LeaveSidebarFolder() {
  const SidebarFolder* folder = FindSidebarFolder(current_sidebar_folder_id_);
  if (!folder) {
    current_sidebar_folder_id_ = 0;
    EnsureSidebarSelection();
  } else {
    const uint64_t leaving = folder->id;
    current_sidebar_folder_id_ = folder->parent_id;
    sidebar_selected_item_ = {SidebarItemType::kFolder, leaving};
  }
  sidebar_visual_anchor_ = {};
  sidebar_pending_keys_.clear();
  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
}

void BrowserWindow::ToggleSidebarVisualSelection() {
  if (sidebar_selected_item_.type != SidebarItemType::kFolder &&
      sidebar_selected_item_.type != SidebarItemType::kTab) {
    return;
  }
  sidebar_visual_anchor_ = sidebar_visual_anchor_.type == SidebarItemType::kNone
                               ? sidebar_selected_item_
                               : SidebarItemRef{};
  sidebar_pending_keys_.clear();
  RefreshSidebar();
}

void BrowserWindow::ToggleSelectedSidebarItemPinned() {
  if (sidebar_selected_item_.type == SidebarItemType::kFolder) {
    const SidebarFolder* folder = FindSidebarFolder(sidebar_selected_item_.id);
    if (folder) {
      SetFolderPinned(folder->id, !folder->pinned);
    }
    return;
  }
  if (sidebar_selected_item_.type == SidebarItemType::kTab) {
    const std::optional<size_t> selected_index =
        FindTabIndexById(sidebar_selected_item_.id);
    if (selected_index) {
      SetTabPinned(sidebar_selected_item_.id, !tabs_[*selected_index].pinned);
    }
  }
}

bool BrowserWindow::SetSidebarItemPinned(const SidebarItemRef& item,
                                         bool pinned) {
  uint64_t parent_id = 0;
  uint64_t* selected_order = nullptr;
  bool* selected_pinned = nullptr;
  if (item.type == SidebarItemType::kFolder) {
    SidebarFolder* folder = FindSidebarFolder(item.id);
    if (!folder) {
      return false;
    }
    parent_id = folder->parent_id;
    selected_order = &folder->sort_order;
    selected_pinned = &folder->pinned;
  } else if (item.type == SidebarItemType::kTab) {
    const std::optional<size_t> index = FindTabIndexById(item.id);
    if (!index) {
      return false;
    }
    parent_id = tabs_[*index].folder_id;
    selected_order = &tabs_[*index].sidebar_sort_order;
    selected_pinned = &tabs_[*index].pinned;
  } else {
    return false;
  }
  if (*selected_pinned == pinned) {
    return true;
  }

  struct Sibling {
    uint64_t order = 0;
    uint64_t* order_ref = nullptr;
    SidebarItemRef item;
  };
  if (pinned) {
    uint64_t bottom = 0;
    for (const SidebarFolder& folder : sidebar_folders_) {
      if (!(item.type == SidebarItemType::kFolder && folder.id == item.id) &&
          folder.parent_id == parent_id && folder.pinned) {
        bottom = std::max(bottom, folder.sort_order);
      }
    }
    for (const Tab& tab : tabs_) {
      if (!(item.type == SidebarItemType::kTab && tab.id == item.id) &&
          tab.folder_id == parent_id && tab.pinned) {
        bottom = std::max(bottom, tab.sidebar_sort_order);
      }
    }
    *selected_pinned = true;
    *selected_order = bottom <= std::numeric_limits<uint64_t>::max() - 1024
                          ? bottom + 1024
                          : bottom;
  } else {
    std::vector<Sibling> siblings;
    for (SidebarFolder& folder : sidebar_folders_) {
      if (!(item.type == SidebarItemType::kFolder && folder.id == item.id) &&
          folder.parent_id == parent_id && !folder.pinned) {
        siblings.push_back({folder.sort_order,
                            &folder.sort_order,
                            {SidebarItemType::kFolder, folder.id}});
      }
    }
    for (Tab& tab : tabs_) {
      if (!(item.type == SidebarItemType::kTab && tab.id == item.id) &&
          tab.folder_id == parent_id && !tab.pinned) {
        siblings.push_back({tab.sidebar_sort_order,
                            &tab.sidebar_sort_order,
                            {SidebarItemType::kTab, tab.id}});
      }
    }
    std::sort(siblings.begin(), siblings.end(),
              [](const Sibling& a, const Sibling& b) {
                if (a.order != b.order)
                  return a.order < b.order;
                if (a.item.type != b.item.type) {
                  return a.item.type == SidebarItemType::kFolder;
                }
                return a.item.id < b.item.id;
              });
    *selected_pinned = false;
    *selected_order = 1024;
    for (size_t i = 0; i < siblings.size(); ++i) {
      *siblings[i].order_ref = (i + 2) * 1024;
    }
  }
  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
  return true;
}

bool BrowserWindow::SetTabPinned(uint64_t tab_id, bool pinned) {
  return SetSidebarItemPinned({SidebarItemType::kTab, tab_id}, pinned);
}

bool BrowserWindow::SetFolderPinned(uint64_t folder_id, bool pinned) {
  return SetSidebarItemPinned({SidebarItemType::kFolder, folder_id}, pinned);
}

bool BrowserWindow::IsSidebarSearchMode() const {
  return mode_ == Mode::kSidebarSearchForward ||
         mode_ == Mode::kSidebarSearchBackward;
}

std::string BrowserWindow::ActiveSidebarSearchQuery() const {
  if (IsSidebarSearchMode()) {
    const std::string input =
        command_text_.size() > 1 ? command_text_.substr(1) : std::string();
    if (!input.empty()) {
      return input;
    }
  }
  return sidebar_search_highlights_visible_ ? sidebar_search_query_
                                            : std::string();
}

std::optional<BrowserWindow::SidebarItemRef>
BrowserWindow::FindSidebarSearchMatch(const std::string& query,
                                      const SidebarItemRef& from,
                                      bool forward) const {
  if (query.empty()) {
    return std::nullopt;
  }
  struct SearchEntry {
    bool pinned = false;
    uint64_t sort_order = 0;
    SidebarItemRef item;
    bool matches = false;
  };
  std::vector<SearchEntry> entries;
  entries.reserve(sidebar_folders_.size() + tabs_.size());
  for (const SidebarFolder& folder : sidebar_folders_) {
    entries.push_back({folder.pinned,
                       folder.sort_order,
                       {SidebarItemType::kFolder, folder.id},
                       ContainsCaseInsensitive(folder.name, query)});
  }
  for (const Tab& tab : tabs_) {
    std::string searchable = tab.url;
    if (tab.client && tab.client->browser() &&
        tab.client->browser()->GetHost()) {
      if (CefRefPtr<CefNavigationEntry> entry =
              tab.client->browser()->GetHost()->GetVisibleNavigationEntry()) {
        searchable += " ";
        searchable += entry->GetTitle().ToString();
      }
    }
    entries.push_back({tab.pinned,
                       tab.sidebar_sort_order,
                       {SidebarItemType::kTab, tab.id},
                       ContainsCaseInsensitive(searchable, query)});
  }
  std::sort(entries.begin(), entries.end(),
            [](const SearchEntry& a, const SearchEntry& b) {
              if (a.pinned != b.pinned)
                return a.pinned;
              if (a.sort_order != b.sort_order)
                return a.sort_order < b.sort_order;
              if (a.item.type != b.item.type) {
                return a.item.type == SidebarItemType::kFolder;
              }
              return a.item.id < b.item.id;
            });
  if (entries.empty()) {
    return std::nullopt;
  }
  const auto origin = std::find_if(
      entries.begin(), entries.end(),
      [&](const SearchEntry& entry) { return entry.item == from; });
  int index = origin == entries.end()
                  ? (forward ? -1 : static_cast<int>(entries.size()))
                  : static_cast<int>(origin - entries.begin());
  for (size_t step = 1; step <= entries.size(); ++step) {
    index = forward ? (index + 1) % static_cast<int>(entries.size())
                    : (index - 1 + static_cast<int>(entries.size())) %
                          static_cast<int>(entries.size());
    if (entries[static_cast<size_t>(index)].matches) {
      return entries[static_cast<size_t>(index)].item;
    }
  }
  return std::nullopt;
}

void BrowserWindow::BeginSidebarSearch(bool forward) {
  sidebar_search_saved_item_ = sidebar_selected_item_;
  sidebar_search_saved_folder_id_ = current_sidebar_folder_id_;
  sidebar_search_forward_ = forward;
  sidebar_visual_anchor_ = {};
  BeginCommandText(forward ? "/" : "?");
  mode_ = forward ? Mode::kSidebarSearchForward : Mode::kSidebarSearchBackward;
  ClearCommandAutocomplete();
  UpdateSidebarSearchLive();
  Layout();
}

void BrowserWindow::RestoreSidebarSearchOrigin() {
  current_sidebar_folder_id_ =
      SidebarFolderExists(sidebar_search_saved_folder_id_)
          ? sidebar_search_saved_folder_id_
          : 0;
  sidebar_selected_item_ = sidebar_search_saved_item_;
  EnsureSidebarSelection();
}

void BrowserWindow::UpdateSidebarSearchLive() {
  if (!IsSidebarSearchMode()) {
    return;
  }
  const std::string input =
      command_text_.size() > 1 ? command_text_.substr(1) : std::string();
  if (input.empty()) {
    RestoreSidebarSearchOrigin();
    RefreshSidebar();
    return;
  }
  if (const std::optional<SidebarItemRef> match = FindSidebarSearchMatch(
          input, sidebar_search_saved_item_, sidebar_search_forward_)) {
    sidebar_selected_item_ = *match;
  }
  RefreshSidebar();
}

void BrowserWindow::CommitSidebarSearch() {
  if (!IsSidebarSearchMode()) {
    return;
  }
  const std::string input =
      command_text_.size() > 1 ? command_text_.substr(1) : std::string();
  if (!input.empty()) {
    sidebar_search_query_ = input;
    sidebar_search_highlights_visible_ = true;
    UpdateSidebarSearchLive();
  }
  sidebar_search_committing_ = true;
  CancelCommand();
  sidebar_search_committing_ = false;
  RefreshSidebar();
}

bool BrowserWindow::JumpSidebarSearch(bool forward) {
  if (sidebar_search_query_.empty()) {
    return false;
  }
  const std::optional<SidebarItemRef> match = FindSidebarSearchMatch(
      sidebar_search_query_, sidebar_selected_item_, forward);
  if (!match) {
    return false;
  }
  sidebar_search_highlights_visible_ = true;
  sidebar_selected_item_ = *match;
  sidebar_visual_anchor_ = {};
  RefreshSidebar();
  return true;
}

void BrowserWindow::ClearSidebarSearchHighlights() {
  if (!sidebar_search_highlights_visible_) {
    return;
  }
  sidebar_search_highlights_visible_ = false;
  sidebar_visual_anchor_ = {};
  if (sidebar_selected_item_.type == SidebarItemType::kFolder) {
    if (const SidebarFolder* folder =
            FindSidebarFolder(sidebar_selected_item_.id)) {
      current_sidebar_folder_id_ = folder->parent_id;
    }
  } else if (sidebar_selected_item_.type == SidebarItemType::kTab) {
    if (const std::optional<size_t> index =
            FindTabIndexById(sidebar_selected_item_.id)) {
      current_sidebar_folder_id_ = tabs_[*index].folder_id;
    }
  }
  EnsureSidebarSelection();
  RefreshSidebar();
}

std::vector<BrowserWindow::SidebarItemRef>
BrowserWindow::SelectedSidebarItems() const {
  auto movable = [](const SidebarItemRef& item) {
    return item.type == SidebarItemType::kFolder ||
           item.type == SidebarItemType::kTab;
  };
  if (!movable(sidebar_selected_item_)) {
    return {};
  }
  if (!movable(sidebar_visual_anchor_)) {
    return {sidebar_selected_item_};
  }

  const std::vector<SidebarDisplayRow> rows = BuildSidebarDisplayRows();
  std::vector<SidebarItemRef> movable_rows;
  for (const SidebarDisplayRow& row : rows) {
    if (row.kind == SidebarRowKind::kEntry && movable(row.item)) {
      movable_rows.push_back(row.item);
    }
  }
  const auto anchor = std::find(movable_rows.begin(), movable_rows.end(),
                                sidebar_visual_anchor_);
  const auto selected = std::find(movable_rows.begin(), movable_rows.end(),
                                  sidebar_selected_item_);
  if (anchor == movable_rows.end() || selected == movable_rows.end()) {
    return {sidebar_selected_item_};
  }
  const size_t first = std::min<size_t>(anchor - movable_rows.begin(),
                                        selected - movable_rows.begin());
  const size_t last = std::max<size_t>(anchor - movable_rows.begin(),
                                       selected - movable_rows.begin());
  return std::vector<SidebarItemRef>(
      movable_rows.begin() + static_cast<std::ptrdiff_t>(first),
      movable_rows.begin() + static_cast<std::ptrdiff_t>(last + 1));
}

BrowserWindow::SidebarItemRef
BrowserWindow::SidebarFocusTargetAfterRemovingItems(
    const std::vector<SidebarItemRef>& items) const {
  if (items.empty()) {
    return {};
  }

  struct RemovalEntry {
    SidebarItemRef item;
    // Parent rows do not belong to either pinned section.
    std::optional<bool> pinned;
  };
  auto is_removed = [&](const SidebarItemRef& item) {
    return std::find(items.begin(), items.end(), item) != items.end();
  };
  auto pinned_for = [&](const SidebarItemRef& item) -> std::optional<bool> {
    if (item.type == SidebarItemType::kFolder) {
      if (const SidebarFolder* folder = FindSidebarFolder(item.id)) {
        return folder->pinned;
      }
    } else if (item.type == SidebarItemType::kTab) {
      if (const std::optional<size_t> index = FindTabIndexById(item.id)) {
        return tabs_[*index].pinned;
      }
    }
    return std::nullopt;
  };

  std::vector<RemovalEntry> before;
  for (const SidebarDisplayRow& row : BuildSidebarDisplayRows()) {
    if (row.kind == SidebarRowKind::kEntry) {
      before.push_back({row.item, pinned_for(row.item)});
    }
  }
  const auto first_removed = std::find_if(
      before.begin(), before.end(),
      [&](const RemovalEntry& entry) { return is_removed(entry.item); });
  if (first_removed == before.end()) {
    return before.empty() ? SidebarItemRef{} : before.front().item;
  }

  const size_t removed_index =
      static_cast<size_t>(first_removed - before.begin());
  const std::optional<bool> removed_section = first_removed->pinned;
  std::vector<RemovalEntry> after;
  for (const RemovalEntry& entry : before) {
    if (!is_removed(entry.item)) {
      after.push_back(entry);
    }
  }
  if (after.empty()) {
    return {};
  }

  const size_t split = std::min(removed_index, after.size());
  for (size_t i = split; i > 0; --i) {
    if (after[i - 1].pinned == removed_section) {
      return after[i - 1].item;
    }
  }
  for (size_t i = split; i < after.size(); ++i) {
    if (after[i].pinned == removed_section) {
      return after[i].item;
    }
  }
  if (split > 0) {
    return after[split - 1].item;
  }
  return after.front().item;
}

void BrowserWindow::BeginCreateFolderPrompt() {
  sidebar_prompt_ = {SidebarPromptPurpose::kCreateFolder, 0,
                     sidebar_visual_anchor_.type == SidebarItemType::kNone
                         ? std::vector<SidebarItemRef>{}
                         : SelectedSidebarItems()};
  sidebar_pending_keys_.clear();
  BeginCommandText(":folder-create ");
}

void BrowserWindow::BeginMoveSidebarItemsPrompt() {
  const std::vector<SidebarItemRef> items = SelectedSidebarItems();
  if (items.empty()) {
    return;
  }
  sidebar_prompt_ = {SidebarPromptPurpose::kMoveItems, 0, items};
  sidebar_pending_keys_.clear();
  BeginCommandText(":folder-move ");
}

void BrowserWindow::BeginRenameFolderPrompt() {
  if (sidebar_selected_item_.type != SidebarItemType::kFolder ||
      !SidebarFolderExists(sidebar_selected_item_.id)) {
    return;
  }
  sidebar_prompt_ = {
      SidebarPromptPurpose::kRenameFolder, sidebar_selected_item_.id, {}};
  sidebar_pending_keys_.clear();
  BeginCommandText(":folder-rename ");
}

void BrowserWindow::AppendFolderDestinationMatches(
    const std::string& prefix, const std::vector<SidebarItemRef>& moving_items,
    std::vector<CompletionItem>& matches) const {
  const std::string folded = ToLowerAscii(Trim(prefix));
  auto matches_prefix = [&](const std::string& value) {
    return folded.empty() || StartsWithCaseInsensitive(value, folded) ||
           ContainsCaseInsensitive(value, "/" + folded);
  };
  if (matches_prefix("/")) {
    matches.push_back({"/", "root folder"});
  }
  if (current_sidebar_folder_id_ != 0 && matches_prefix("..")) {
    matches.push_back({"..", "parent folder"});
  }

  std::vector<CompletionItem> folders;
  for (const SidebarFolder& folder : sidebar_folders_) {
    bool valid = true;
    for (const SidebarItemRef& item : moving_items) {
      if (item.type == SidebarItemType::kFolder &&
          (item.id == folder.id ||
           SidebarFolderIsDescendantOf(folder.id, item.id))) {
        valid = false;
        break;
      }
    }
    const std::string path = SidebarFolderPath(folder.id);
    if (!valid || !matches_prefix(path)) {
      continue;
    }
    const std::string parent_path = SidebarFolderPath(folder.parent_id);
    folders.push_back(
        {path, parent_path.empty() ? "top-level" : "in " + parent_path});
  }
  std::sort(folders.begin(), folders.end(),
            [](const CompletionItem& a, const CompletionItem& b) {
              return ToLowerAscii(a.name) < ToLowerAscii(b.name);
            });
  matches.insert(matches.end(), folders.begin(), folders.end());
}

bool BrowserWindow::CommitSidebarFolderCommand(const std::string& command,
                                               const std::string& args) {
  if (command != ":folder-create" && command != ":folder-move" &&
      command != ":folder-rename") {
    return false;
  }

  const SidebarPromptContext prompt = sidebar_prompt_;
  sidebar_prompt_ = {};
  const std::string value = Trim(args);
  if (command == ":folder-create") {
    const std::vector<SidebarItemRef> items =
        prompt.purpose == SidebarPromptPurpose::kCreateFolder
            ? prompt.items
            : std::vector<SidebarItemRef>{};
    CancelCommand();
    if (CreateSidebarFolder(value, current_sidebar_folder_id_, items) == 0) {
      SetStatusOutput("invalid or duplicate folder name");
    }
    return true;
  }
  if (command == ":folder-rename") {
    const uint64_t folder_id =
        prompt.purpose == SidebarPromptPurpose::kRenameFolder
            ? prompt.folder_id
            : (sidebar_selected_item_.type == SidebarItemType::kFolder
                   ? sidebar_selected_item_.id
                   : 0);
    CancelCommand();
    if (!RenameSidebarFolder(folder_id, value)) {
      SetStatusOutput("invalid or duplicate folder name");
    }
    return true;
  }

  const std::vector<SidebarItemRef> items =
      prompt.purpose == SidebarPromptPurpose::kMoveItems
          ? prompt.items
          : SelectedSidebarItems();
  const std::optional<uint64_t> destination =
      ResolveSidebarFolderDestination(value, items);
  CancelCommand();
  if (!destination || !MoveSidebarItems(items, *destination)) {
    SetStatusOutput("unknown or invalid folder destination");
  }
  return true;
}

uint64_t
BrowserWindow::CreateSidebarFolder(std::string name, uint64_t parent_id,
                                   const std::vector<SidebarItemRef>& items) {
  name = Trim(std::move(name));
  if (!ValidFolderName(name) ||
      (parent_id != 0 && !SidebarFolderExists(parent_id))) {
    return 0;
  }
  for (const SidebarFolder& folder : sidebar_folders_) {
    if (folder.parent_id == parent_id &&
        ToLowerAscii(folder.name) == ToLowerAscii(name)) {
      return 0;
    }
  }

  uint64_t shell_sort_order = NextSidebarSortOrder(parent_id);
  for (const SidebarItemRef& item : items) {
    if (item.type == SidebarItemType::kFolder) {
      const SidebarFolder* folder = FindSidebarFolder(item.id);
      if (folder && folder->parent_id == parent_id) {
        shell_sort_order = std::min(shell_sort_order, folder->sort_order);
      }
    } else if (item.type == SidebarItemType::kTab) {
      const std::optional<size_t> index = FindTabIndexById(item.id);
      if (index && tabs_[*index].folder_id == parent_id) {
        shell_sort_order =
            std::min(shell_sort_order, tabs_[*index].sidebar_sort_order);
      }
    }
  }
  const uint64_t id = next_folder_id_++;
  sidebar_folders_.push_back(
      {id, parent_id, shell_sort_order, std::move(name)});
  if (!items.empty()) {
    MoveSidebarItems(items, id);
  }
  current_sidebar_folder_id_ = parent_id;
  sidebar_selected_item_ = {SidebarItemType::kFolder, id};
  sidebar_visual_anchor_ = {};
  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
  return id;
}

bool BrowserWindow::RenameSidebarFolder(uint64_t folder_id, std::string name) {
  SidebarFolder* folder = FindSidebarFolder(folder_id);
  name = Trim(std::move(name));
  if (!folder || !ValidFolderName(name)) {
    return false;
  }
  for (const SidebarFolder& sibling : sidebar_folders_) {
    if (sibling.id != folder_id && sibling.parent_id == folder->parent_id &&
        ToLowerAscii(sibling.name) == ToLowerAscii(name)) {
      return false;
    }
  }
  folder->name = std::move(name);
  SaveState();
  RefreshSidebar();
  return true;
}

bool BrowserWindow::MoveSidebarItems(const std::vector<SidebarItemRef>& items,
                                     uint64_t destination_folder_id) {
  if (items.empty() || (destination_folder_id != 0 &&
                        !SidebarFolderExists(destination_folder_id))) {
    return false;
  }

  std::vector<SidebarItemRef> unique_items;
  for (const SidebarItemRef& item : items) {
    if ((item.type != SidebarItemType::kFolder &&
         item.type != SidebarItemType::kTab) ||
        std::find(unique_items.begin(), unique_items.end(), item) !=
            unique_items.end()) {
      continue;
    }
    if (item.type == SidebarItemType::kFolder) {
      const SidebarFolder* folder = FindSidebarFolder(item.id);
      if (!folder || item.id == destination_folder_id ||
          SidebarFolderIsDescendantOf(destination_folder_id, item.id)) {
        return false;
      }
    } else if (!FindTabIndexById(item.id)) {
      return false;
    }
    unique_items.push_back(item);
  }
  if (unique_items.empty()) {
    return false;
  }

  const bool selected_moves_out_of_view =
      ActiveSidebarSearchQuery().empty() &&
      destination_folder_id != current_sidebar_folder_id_ &&
      std::find(unique_items.begin(), unique_items.end(),
                sidebar_selected_item_) != unique_items.end();
  const SidebarItemRef focus_after_move =
      selected_moves_out_of_view
          ? SidebarFocusTargetAfterRemovingItems(unique_items)
          : SidebarItemRef{};

  uint64_t order = NextSidebarSortOrder(destination_folder_id);
  for (const SidebarItemRef& item : unique_items) {
    if (item.type == SidebarItemType::kFolder) {
      SidebarFolder* folder = FindSidebarFolder(item.id);
      folder->parent_id = destination_folder_id;
      folder->sort_order = order;
    } else {
      const std::optional<size_t> index = FindTabIndexById(item.id);
      tabs_[*index].folder_id = destination_folder_id;
      tabs_[*index].sidebar_sort_order = order;
    }
    order += 1024;
  }
  sidebar_visual_anchor_ = {};
  if (selected_moves_out_of_view) {
    sidebar_selected_item_ = focus_after_move;
  }
  EnsureSidebarSelection();
  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
  return true;
}

bool BrowserWindow::MoveSelectedSidebarItem(int delta) {
  if (delta == 0 ||
      (sidebar_selected_item_.type != SidebarItemType::kFolder &&
       sidebar_selected_item_.type != SidebarItemType::kTab) ||
      !ActiveSidebarSearchQuery().empty()) {
    return false;
  }
  const std::vector<SidebarDisplayRow> rows = BuildSidebarDisplayRows();
  std::vector<SidebarItemRef> items;
  for (const SidebarDisplayRow& row : rows) {
    if (row.kind == SidebarRowKind::kEntry &&
        (row.item.type == SidebarItemType::kFolder ||
         row.item.type == SidebarItemType::kTab)) {
      items.push_back(row.item);
    }
  }
  const std::vector<SidebarItemRef> selected_items = SelectedSidebarItems();
  if (selected_items.empty()) {
    return false;
  }

  std::vector<size_t> selected_indexes;
  for (const SidebarItemRef& selected_item : selected_items) {
    const auto selected = std::find(items.begin(), items.end(), selected_item);
    if (selected == items.end()) {
      return false;
    }
    selected_indexes.push_back(static_cast<size_t>(selected - items.begin()));
  }
  const size_t first =
      *std::min_element(selected_indexes.begin(), selected_indexes.end());
  const size_t last =
      *std::max_element(selected_indexes.begin(), selected_indexes.end());
  if (last - first + 1 != selected_items.size()) {
    return false;
  }

  auto pinned_for = [&](const SidebarItemRef& item) {
    if (item.type == SidebarItemType::kFolder) {
      const SidebarFolder* folder = FindSidebarFolder(item.id);
      return folder && folder->pinned;
    }
    if (item.type == SidebarItemType::kTab) {
      const std::optional<size_t> tab = FindTabIndexById(item.id);
      return tab && tabs_[*tab].pinned;
    }
    return false;
  };
  const bool selected_pinned = pinned_for(items[first]);
  if (std::any_of(selected_items.begin(), selected_items.end(),
                  [&](const SidebarItemRef& item) {
                    return pinned_for(item) != selected_pinned;
                  })) {
    return false;
  }

  const size_t target =
      delta < 0 ? (first == 0 ? items.size() : first - 1) : last + 1;
  if (target >= items.size() || pinned_for(items[target]) != selected_pinned) {
    return false;
  }

  if (delta < 0) {
    std::rotate(items.begin() + static_cast<std::ptrdiff_t>(target),
                items.begin() + static_cast<std::ptrdiff_t>(first),
                items.begin() + static_cast<std::ptrdiff_t>(last + 1));
  } else {
    std::rotate(items.begin() + static_cast<std::ptrdiff_t>(first),
                items.begin() + static_cast<std::ptrdiff_t>(last + 1),
                items.begin() + static_cast<std::ptrdiff_t>(target + 1));
  }

  auto order_for = [&](const SidebarItemRef& item) -> uint64_t* {
    if (item.type == SidebarItemType::kFolder) {
      SidebarFolder* folder = FindSidebarFolder(item.id);
      return folder ? &folder->sort_order : nullptr;
    }
    const std::optional<size_t> tab = FindTabIndexById(item.id);
    return tab ? &tabs_[*tab].sidebar_sort_order : nullptr;
  };
  for (size_t i = 0; i < items.size(); ++i) {
    if (uint64_t* item_order = order_for(items[i])) {
      *item_order = (i + 1) * 1024;
    }
  }
  SaveState();
  RefreshSidebar();
  return true;
}

bool BrowserWindow::DeleteSidebarFolder(uint64_t folder_id, bool unwrap) {
  const SidebarFolder* folder_ptr = FindSidebarFolder(folder_id);
  if (!folder_ptr) {
    return false;
  }
  const uint64_t parent_id = folder_ptr->parent_id;

  std::unordered_set<uint64_t> deleted_folders;
  if (!unwrap) {
    deleted_folders.insert(folder_id);
    bool changed = true;
    while (changed) {
      changed = false;
      for (const SidebarFolder& folder : sidebar_folders_) {
        if (deleted_folders.contains(folder.parent_id) &&
            deleted_folders.insert(folder.id).second) {
          changed = true;
        }
      }
    }
  }

  auto recursively_removed = [&](const SidebarItemRef& item) {
    if (unwrap) {
      return item.type == SidebarItemType::kFolder && item.id == folder_id;
    }
    if (item.type == SidebarItemType::kFolder) {
      return deleted_folders.contains(item.id);
    }
    if (item.type == SidebarItemType::kTab) {
      const std::optional<size_t> index = FindTabIndexById(item.id);
      return index && deleted_folders.contains(tabs_[*index].folder_id);
    }
    return false;
  };
  std::vector<SidebarItemRef> removed_items;
  for (const SidebarDisplayRow& row : BuildSidebarDisplayRows()) {
    if (row.kind == SidebarRowKind::kEntry && recursively_removed(row.item)) {
      removed_items.push_back(row.item);
    }
  }
  const bool selected_removed = recursively_removed(sidebar_selected_item_);
  SidebarItemRef focus_after_delete =
      selected_removed ? SidebarFocusTargetAfterRemovingItems(removed_items)
                       : SidebarItemRef{};

  if (unwrap && selected_removed) {
    struct ChildEntry {
      bool pinned = false;
      uint64_t order = 0;
      SidebarItemRef item;
    };
    std::vector<ChildEntry> children;
    for (const SidebarFolder& folder : sidebar_folders_) {
      if (folder.parent_id == folder_id) {
        children.push_back({folder.pinned,
                            folder.sort_order,
                            {SidebarItemType::kFolder, folder.id}});
      }
    }
    for (const Tab& tab : tabs_) {
      if (tab.folder_id == folder_id) {
        children.push_back({tab.pinned,
                            tab.sidebar_sort_order,
                            {SidebarItemType::kTab, tab.id}});
      }
    }
    std::sort(children.begin(), children.end(),
              [](const ChildEntry& a, const ChildEntry& b) {
                if (a.pinned != b.pinned)
                  return a.pinned;
                if (a.order != b.order)
                  return a.order < b.order;
                if (a.item.type != b.item.type) {
                  return a.item.type == SidebarItemType::kFolder;
                }
                return a.item.id < b.item.id;
              });
    if (!children.empty()) {
      focus_after_delete = children.front().item;
    }
  }

  if (unwrap) {
    struct OrderedItem {
      uint64_t order = 0;
      SidebarItemRef item;
    };
    auto ordered_less = [](const OrderedItem& a, const OrderedItem& b) {
      if (a.order != b.order) {
        return a.order < b.order;
      }
      if (a.item.type != b.item.type) {
        return a.item.type == SidebarItemType::kFolder;
      }
      return a.item.id < b.item.id;
    };
    std::vector<OrderedItem> parent_items;
    std::vector<OrderedItem> children;
    for (const SidebarFolder& folder : sidebar_folders_) {
      if (folder.parent_id == parent_id) {
        parent_items.push_back(
            {folder.sort_order, {SidebarItemType::kFolder, folder.id}});
      }
      if (folder.parent_id == folder_id) {
        children.push_back(
            {folder.sort_order, {SidebarItemType::kFolder, folder.id}});
      }
    }
    for (const Tab& tab : tabs_) {
      if (tab.folder_id == parent_id) {
        parent_items.push_back(
            {tab.sidebar_sort_order, {SidebarItemType::kTab, tab.id}});
      }
      if (tab.folder_id == folder_id) {
        children.push_back(
            {tab.sidebar_sort_order, {SidebarItemType::kTab, tab.id}});
      }
    }
    std::sort(parent_items.begin(), parent_items.end(), ordered_less);
    std::sort(children.begin(), children.end(), ordered_less);

    std::vector<SidebarItemRef> replacement_order;
    for (const OrderedItem& parent_item : parent_items) {
      if (parent_item.item.type == SidebarItemType::kFolder &&
          parent_item.item.id == folder_id) {
        for (const OrderedItem& child : children) {
          replacement_order.push_back(child.item);
        }
      } else {
        replacement_order.push_back(parent_item.item);
      }
    }

    for (const OrderedItem& child : children) {
      const SidebarItemRef& item = child.item;
      if (item.type == SidebarItemType::kFolder) {
        SidebarFolder* child_folder = FindSidebarFolder(item.id);
        child_folder->parent_id = parent_id;
      } else if (const std::optional<size_t> tab = FindTabIndexById(item.id)) {
        tabs_[*tab].folder_id = parent_id;
      }
    }
    sidebar_folders_.erase(std::remove_if(sidebar_folders_.begin(),
                                          sidebar_folders_.end(),
                                          [&](const SidebarFolder& folder) {
                                            return folder.id == folder_id;
                                          }),
                           sidebar_folders_.end());
    for (size_t i = 0; i < replacement_order.size(); ++i) {
      const SidebarItemRef& item = replacement_order[i];
      const uint64_t order = (i + 1) * 1024;
      if (item.type == SidebarItemType::kFolder) {
        if (SidebarFolder* sibling = FindSidebarFolder(item.id)) {
          sibling->sort_order = order;
        }
      } else if (const std::optional<size_t> tab = FindTabIndexById(item.id)) {
        tabs_[*tab].sidebar_sort_order = order;
      }
    }
  } else {
    sidebar_folders_.erase(
        std::remove_if(sidebar_folders_.begin(), sidebar_folders_.end(),
                       [&](const SidebarFolder& folder) {
                         return deleted_folders.contains(folder.id);
                       }),
        sidebar_folders_.end());
    if (deleted_folders.contains(current_sidebar_folder_id_)) {
      current_sidebar_folder_id_ =
          SidebarFolderExists(parent_id) ? parent_id : 0;
    }
    CloseTabsInDeletedSidebarFolders(deleted_folders);
  }

  if (current_sidebar_folder_id_ == folder_id) {
    current_sidebar_folder_id_ = SidebarFolderExists(parent_id) ? parent_id : 0;
  }
  if (selected_removed) {
    sidebar_selected_item_ = focus_after_delete;
  }
  sidebar_visual_anchor_ = {};
  EnsureSidebarSelection();
  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
  return true;
}

void BrowserWindow::DeleteSelectedSidebarItems() {
  const std::vector<SidebarItemRef> selected_items = SelectedSidebarItems();
  if (selected_items.empty()) {
    sidebar_pending_keys_.clear();
    return;
  }
  if (sidebar_pending_keys_ != "d") {
    sidebar_pending_keys_ = "d";
    const uint64_t generation = ++sidebar_delete_generation_;
    const bool includes_folder =
        std::any_of(selected_items.begin(), selected_items.end(),
                    [](const SidebarItemRef& item) {
                      return item.type == SidebarItemType::kFolder;
                    });
    SetStatusOutput(includes_folder
                        ? "press d again to recursively delete selection"
                        : "press d again to delete selection",
                    kSidebarDeleteConfirmationMs);
    CefRefPtr<BrowserWindow> self = this;
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&BrowserWindow::ClearSidebarDeleteConfirmation, self,
                       generation),
        kSidebarDeleteConfirmationMs);
    return;
  }

  const SidebarItemRef focus_after_delete =
      SidebarFocusTargetAfterRemovingItems(selected_items);
  const uint64_t original_folder_id = current_sidebar_folder_id_;
  const uint64_t original_active_tab_id = ActiveTabId();
  sidebar_pending_keys_.clear();
  ++sidebar_delete_generation_;

  // Delete folder shells first; recursive folder deletion also closes all tabs
  // below those shells. Keep IDs, not vector indexes, because each deletion can
  // reshape both collections.
  for (const SidebarItemRef& item : selected_items) {
    if (item.type == SidebarItemType::kFolder) {
      DeleteSidebarFolder(item.id, false);
    }
  }

  std::vector<size_t> tab_indexes;
  for (const SidebarItemRef& item : selected_items) {
    if (item.type != SidebarItemType::kTab) {
      continue;
    }
    if (const std::optional<size_t> index = FindTabIndexById(item.id)) {
      tab_indexes.push_back(*index);
    }
  }
  std::sort(tab_indexes.begin(), tab_indexes.end(), std::greater<size_t>());
  for (size_t index : tab_indexes) {
    if (index < tabs_.size()) {
      CloseTabAtIndex(index, CloseFocus::kPreviousTab);
    }
  }

  if (SidebarFolderExists(original_folder_id)) {
    current_sidebar_folder_id_ = original_folder_id;
  }
  if (!FindTabIndexById(original_active_tab_id) &&
      focus_after_delete.type == SidebarItemType::kTab) {
    if (const std::optional<size_t> index =
            FindTabIndexById(focus_after_delete.id)) {
      ActivateTab(*index);
    }
  }
  sidebar_selected_item_ = focus_after_delete;
  sidebar_visual_anchor_ = {};
  EnsureSidebarSelection();
  SaveState();
  if (RefreshSidebar()) {
    Layout();
  }
}

void BrowserWindow::ClearSidebarDeleteConfirmation(uint64_t generation) {
  if (generation == sidebar_delete_generation_ &&
      sidebar_pending_keys_ == "d") {
    sidebar_pending_keys_.clear();
  }
}

void BrowserWindow::UnwrapSelectedSidebarFolder() {
  if (sidebar_selected_item_.type == SidebarItemType::kFolder) {
    DeleteSidebarFolder(sidebar_selected_item_.id, true);
  }
  sidebar_pending_keys_.clear();
}

const BrowserWindow::SidebarFolder*
BrowserWindow::FindSidebarFolder(uint64_t folder_id) const {
  if (folder_id == 0) {
    return nullptr;
  }
  const auto folder =
      std::find_if(sidebar_folders_.begin(), sidebar_folders_.end(),
                   [&](const SidebarFolder& candidate) {
                     return candidate.id == folder_id;
                   });
  return folder == sidebar_folders_.end() ? nullptr : &*folder;
}

BrowserWindow::SidebarFolder*
BrowserWindow::FindSidebarFolder(uint64_t folder_id) {
  return const_cast<SidebarFolder*>(
      std::as_const(*this).FindSidebarFolder(folder_id));
}

bool BrowserWindow::SidebarFolderExists(uint64_t folder_id) const {
  return folder_id == 0 || FindSidebarFolder(folder_id) != nullptr;
}

bool BrowserWindow::SidebarFolderIsDescendantOf(uint64_t folder_id,
                                                uint64_t ancestor_id) const {
  if (folder_id == 0 || ancestor_id == 0) {
    return false;
  }
  std::unordered_set<uint64_t> seen;
  while (folder_id != 0 && seen.insert(folder_id).second) {
    if (folder_id == ancestor_id) {
      return true;
    }
    const SidebarFolder* folder = FindSidebarFolder(folder_id);
    folder_id = folder ? folder->parent_id : 0;
  }
  return false;
}

std::string BrowserWindow::SidebarFolderPath(uint64_t folder_id) const {
  std::vector<std::string> names;
  std::unordered_set<uint64_t> seen;
  while (folder_id != 0 && seen.insert(folder_id).second) {
    const SidebarFolder* folder = FindSidebarFolder(folder_id);
    if (!folder) {
      break;
    }
    names.push_back(folder->name);
    folder_id = folder->parent_id;
  }
  std::reverse(names.begin(), names.end());
  std::string path;
  for (const std::string& name : names) {
    if (!path.empty()) {
      path.push_back('/');
    }
    path += name;
  }
  return path;
}

std::optional<uint64_t> BrowserWindow::ResolveSidebarFolderDestination(
    const std::string& text,
    const std::vector<SidebarItemRef>& moving_items) const {
  std::string normalized = Trim(text);
  while (normalized.size() > 1 && normalized.back() == '/') {
    normalized.pop_back();
  }
  if (normalized.empty()) {
    return std::nullopt;
  }
  if (normalized == "/") {
    return uint64_t{0};
  }
  if (normalized == "..") {
    const SidebarFolder* current =
        FindSidebarFolder(current_sidebar_folder_id_);
    return current ? current->parent_id : uint64_t{0};
  }

  const std::string folded = ToLowerAscii(normalized);
  const SidebarFolder* destination = nullptr;
  for (const SidebarFolder& folder : sidebar_folders_) {
    if (ToLowerAscii(SidebarFolderPath(folder.id)) == folded) {
      destination = &folder;
      break;
    }
  }
  if (!destination) {
    for (const SidebarFolder& folder : sidebar_folders_) {
      if (folder.parent_id == current_sidebar_folder_id_ &&
          ToLowerAscii(folder.name) == folded) {
        destination = &folder;
        break;
      }
    }
  }
  if (!destination) {
    for (const SidebarFolder& folder : sidebar_folders_) {
      if (ToLowerAscii(folder.name) == folded) {
        if (destination) {
          return std::nullopt;
        }
        destination = &folder;
      }
    }
  }
  if (!destination) {
    return std::nullopt;
  }
  for (const SidebarItemRef& item : moving_items) {
    if (item.type == SidebarItemType::kFolder &&
        (item.id == destination->id ||
         SidebarFolderIsDescendantOf(destination->id, item.id))) {
      return std::nullopt;
    }
  }
  return destination->id;
}

uint64_t BrowserWindow::NextSidebarSortOrder(uint64_t parent_id) const {
  uint64_t largest = 0;
  for (const SidebarFolder& folder : sidebar_folders_) {
    if (folder.parent_id == parent_id) {
      largest = std::max(largest, folder.sort_order);
    }
  }
  for (const Tab& tab : tabs_) {
    if (tab.folder_id == parent_id) {
      largest = std::max(largest, tab.sidebar_sort_order);
    }
  }
  return largest > std::numeric_limits<uint64_t>::max() - 1024 ? largest
                                                               : largest + 1024;
}

uint64_t BrowserWindow::SidebarSortOrderAfterItem(const SidebarItemRef& item) {
  struct OrderedRef {
    SidebarItemRef item;
    uint64_t* order = nullptr;
  };
  uint64_t parent_id = 0;
  if (item.type == SidebarItemType::kFolder) {
    SidebarFolder* folder = FindSidebarFolder(item.id);
    if (!folder) {
      return 0;
    }
    parent_id = folder->parent_id;
  } else if (item.type == SidebarItemType::kTab) {
    const std::optional<size_t> index = FindTabIndexById(item.id);
    if (!index) {
      return 0;
    }
    parent_id = tabs_[*index].folder_id;
  } else {
    return 0;
  }

  std::vector<OrderedRef> siblings;
  for (SidebarFolder& folder : sidebar_folders_) {
    if (folder.parent_id == parent_id) {
      siblings.push_back(
          {{SidebarItemType::kFolder, folder.id}, &folder.sort_order});
    }
  }
  for (Tab& tab : tabs_) {
    if (tab.folder_id == parent_id) {
      siblings.push_back(
          {{SidebarItemType::kTab, tab.id}, &tab.sidebar_sort_order});
    }
  }
  std::sort(siblings.begin(), siblings.end(),
            [](const OrderedRef& a, const OrderedRef& b) {
              if (*a.order != *b.order) {
                return *a.order < *b.order;
              }
              if (a.item.type != b.item.type) {
                return a.item.type == SidebarItemType::kFolder;
              }
              return a.item.id < b.item.id;
            });
  auto current = std::find_if(
      siblings.begin(), siblings.end(),
      [&](const OrderedRef& sibling) { return sibling.item == item; });
  if (current == siblings.end()) {
    return 0;
  }
  const size_t index = static_cast<size_t>(current - siblings.begin());
  if (index + 1 == siblings.size()) {
    return *current->order <= std::numeric_limits<uint64_t>::max() - 1024
               ? *current->order + 1024
               : 0;
  }
  const uint64_t next_order = *siblings[index + 1].order;
  if (next_order > *current->order && next_order - *current->order > 1) {
    return *current->order + (next_order - *current->order) / 2;
  }

  for (size_t i = 0; i < siblings.size(); ++i) {
    *siblings[i].order = (i + 1) * 1024;
  }
  return *siblings[index].order + 512;
}

uint64_t BrowserWindow::NewTabFolderId() const {
  if (focus_area_ == FocusArea::kTabSidebar &&
      SidebarFolderExists(current_sidebar_folder_id_)) {
    return current_sidebar_folder_id_;
  }
  if (active_index_ < tabs_.size() &&
      SidebarFolderExists(tabs_[active_index_].folder_id)) {
    return tabs_[active_index_].folder_id;
  }
  return 0;
}

std::string BrowserWindow::SidebarJson() const {
  auto append_item_type = [](std::string& out, SidebarItemType type) {
    switch (type) {
      case SidebarItemType::kParent:
        AppendJsonString(out, "parent");
        break;
      case SidebarItemType::kFolder:
        AppendJsonString(out, "folder");
        break;
      case SidebarItemType::kTab:
        AppendJsonString(out, "tab");
        break;
      case SidebarItemType::kNone:
        AppendJsonString(out, "none");
        break;
    }
  };
  auto append_row_kind = [](std::string& out, SidebarRowKind kind) {
    switch (kind) {
      case SidebarRowKind::kFolderHeader:
        AppendJsonString(out, "folder_header");
        break;
      case SidebarRowKind::kSectionLabel:
        AppendJsonString(out, "section_label");
        break;
      case SidebarRowKind::kSeparator:
        AppendJsonString(out, "separator");
        break;
      case SidebarRowKind::kEntry:
        AppendJsonString(out, "entry");
        break;
    }
  };

  const std::string active_query = ActiveSidebarSearchQuery();
  const std::vector<SidebarDisplayRow> rows = BuildSidebarDisplayRows();
  std::string out;
  out.reserve(256 + rows.size() * 128);
  out += "{\"visible\":";
  AppendJsonBool(out, sidebar_visible_);
  out += ",\"focused\":";
  AppendJsonBool(out, focus_area_ == FocusArea::kTabSidebar);
  out += ",\"current_folder_id\":";
  AppendJsonNumber(out, current_sidebar_folder_id_);
  out += ",\"selected_type\":";
  append_item_type(out, sidebar_selected_item_.type);
  out += ",\"selected_id\":";
  AppendJsonNumber(out, sidebar_selected_item_.id);
  out += ",\"search_bar_open\":";
  AppendJsonBool(out, IsSidebarSearchMode());
  out += ",\"search_filter_active\":";
  AppendJsonBool(out, !active_query.empty());
  out += ",\"search_direction\":";
  AppendJsonString(out, sidebar_search_forward_ ? "forward" : "backward");
  out += ",\"search_query\":";
  AppendJsonString(out, active_query);
  out += ",\"rows\":[";
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i > 0) {
      out.push_back(',');
    }
    const SidebarDisplayRow& row = rows[i];
    out += "{\"kind\":";
    append_row_kind(out, row.kind);
    out += ",\"item_type\":";
    append_item_type(out, row.item.type);
    out += ",\"item_id\":";
    AppendJsonNumber(out, row.item.id);
    out += ",\"selected\":";
    AppendJsonBool(out, row.selected);
    out += ",\"active\":";
    AppendJsonBool(out, row.active);
    out += ",\"text\":";
    AppendJsonString(out, row.text);
    if (row.item.type == SidebarItemType::kFolder) {
      if (const SidebarFolder* folder = FindSidebarFolder(row.item.id)) {
        out += ",\"pinned\":";
        AppendJsonBool(out, folder->pinned);
      }
    } else if (row.item.type == SidebarItemType::kTab &&
               row.tab_index < tabs_.size()) {
      out += ",\"tab_index\":";
      AppendJsonNumber(out, row.tab_index);
      out += ",\"pinned\":";
      AppendJsonBool(out, tabs_[row.tab_index].pinned);
    }
    out.push_back('}');
  }
  out += "]}";
  return out;
}

std::string BrowserWindow::FoldersJson() const {
  std::string out = "{\"current_folder_id\":";
  AppendJsonNumber(out, current_sidebar_folder_id_);
  out += ",\"folders\":[";
  for (size_t i = 0; i < sidebar_folders_.size(); ++i) {
    if (i > 0) {
      out.push_back(',');
    }
    const SidebarFolder& folder = sidebar_folders_[i];
    out += "{\"id\":";
    AppendJsonNumber(out, folder.id);
    out += ",\"parent_id\":";
    AppendJsonNumber(out, folder.parent_id);
    out += ",\"sort_order\":";
    AppendJsonNumber(out, folder.sort_order);
    out += ",\"pinned\":";
    AppendJsonBool(out, folder.pinned);
    out += ",\"name\":";
    AppendJsonString(out, folder.name);
    out += ",\"path\":";
    AppendJsonString(out, SidebarFolderPath(folder.id));
    out.push_back('}');
  }
  out += "]}";
  return out;
}

} // namespace vimbrowser
