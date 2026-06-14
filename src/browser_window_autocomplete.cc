#include "browser_window.h"
#include "browser_window_internal.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <utility>

#include "config.h"
#include "include/views/cef_textfield.h"
#include "theme.h"

namespace vimbrowser {

namespace {

bool EraseCaseInsensitive(std::vector<std::string>& values,
                          const std::string& value) {
  if (value.empty()) {
    return false;
  }

  const std::string folded = ToLowerAscii(value);
  const size_t old_size = values.size();
  values.erase(std::remove_if(values.begin(), values.end(),
                              [&](const std::string& existing) {
                                return ToLowerAscii(existing) == folded;
                              }),
               values.end());
  return values.size() != old_size;
}

bool SameHistoryCompletionSource(const CompletionItem& a,
                                 const CompletionItem& b) {
  return a.source == b.source && a.source_key == b.source_key;
}

}  // namespace

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
    seen.insert(ToLowerAscii(CompletionInsertText(item)));
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

    ranked.push_back(
        {CompletionItem{Ellipsize(entry, kOpenHistoryCompletionNameMax),
                        "open history", entry,
                        CompletionSource::kOpenHistory},
         open_history_.size() - 1 - index});
  }

  std::sort(ranked.begin(), ranked.end(), [](const RankedHistoryMatch& a,
                                             const RankedHistoryMatch& b) {
    const std::string& a_insert = CompletionInsertText(a.item);
    const std::string& b_insert = CompletionInsertText(b.item);
    if (a_insert.size() != b_insert.size()) {
      return a_insert.size() < b_insert.size();
    }
    if (a.recency_rank != b.recency_rank) {
      return a.recency_rank < b.recency_rank;
    }
    return ToLowerAscii(a_insert) < ToLowerAscii(b_insert);
  });

  for (const RankedHistoryMatch& match : ranked) {
    matches.push_back(match.item);
  }
}

void BrowserWindow::AppendSearchHistoryMatches(
    const std::string& engine,
    const std::string& prefix,
    std::vector<CompletionItem>& matches) const {
  struct RankedHistoryMatch {
    CompletionItem item;
    size_t recency_rank = 0;
  };

  const std::string folded_engine = ToLowerAscii(engine);
  const auto history_it = search_history_.find(folded_engine);
  if (history_it == search_history_.end()) {
    return;
  }

  std::vector<RankedHistoryMatch> ranked;
  std::unordered_set<std::string> seen;
  for (size_t i = history_it->second.size(); i > 0; --i) {
    const size_t index = i - 1;
    const std::string& entry = history_it->second[index];
    if (entry.empty() ||
        (!prefix.empty() && !StartsWithCaseInsensitive(entry, prefix))) {
      continue;
    }

    const std::string folded = ToLowerAscii(entry);
    if (!seen.insert(folded).second) {
      continue;
    }

    ranked.push_back(
        {CompletionItem{Ellipsize(entry, kOpenHistoryCompletionNameMax),
                        folded_engine + " search history", entry,
                        CompletionSource::kSearchHistory, folded_engine},
         history_it->second.size() - 1 - index});
  }

  std::sort(ranked.begin(), ranked.end(), [](const RankedHistoryMatch& a,
                                             const RankedHistoryMatch& b) {
    const std::string& a_insert = CompletionInsertText(a.item);
    const std::string& b_insert = CompletionInsertText(b.item);
    if (a_insert.size() != b_insert.size()) {
      return a_insert.size() < b_insert.size();
    }
    if (a.recency_rank != b.recency_rank) {
      return a.recency_rank < b.recency_rank;
    }
    return ToLowerAscii(a_insert) < ToLowerAscii(b_insert);
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

    matches.push_back({number, Ellipsize(std::move(description),
                                         kTabFocusCompletionDescriptionMax)});
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
    const OpenAutocompleteContext context = AnalyzeOpenAutocompleteArgs(after_command);
    if (!context.search_engine.empty() &&
        !context.search_engine_token_is_current) {
      AppendSearchHistoryMatches(context.search_engine, context.search_prefix,
                                 matches);
      command_autocomplete_.completion_start = first_non_space + first_space + 1 +
                                               context.search_prefix_start;
    } else if (!context.already_has_tab_arg &&
               (context.completing_new_arg || !context.arg_prefix.empty())) {
      for (const CompletionItem& item : OpenArgList()) {
        if (context.completing_new_arg ||
            StartsWithCaseInsensitive(item.name, context.arg_prefix)) {
          matches.push_back(item);
        }
      }
      AppendOpenHistoryMatches(context.arg_prefix, matches);
    } else if (context.completing_new_arg || !context.arg_prefix.empty()) {
      AppendOpenHistoryMatches(context.arg_prefix, matches);
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
  } else if (StartsWithCaseInsensitive(typed_command, ":test") &&
             IsTokenBoundary(typed_command, 5)) {
    const size_t arg_start = after_command.find_last_of(" \t");
    const std::string arg_prefix = arg_start == std::string::npos
                                       ? after_command
                                       : after_command.substr(arg_start + 1);
    const bool completing_new_arg = IsWhitespaceOnly(after_command) ||
                                    (!after_command.empty() &&
                                     std::isspace(static_cast<unsigned char>(
                                         after_command.back())));
    if (completing_new_arg || !arg_prefix.empty()) {
      for (const CompletionItem& item : TestArgList()) {
        if (StartsWithCaseInsensitive(item.name, arg_prefix)) {
          matches.push_back(item);
        }
      }
    }
  } else if ((StartsWithCaseInsensitive(typed_command, ":showmode") &&
              IsTokenBoundary(typed_command, 9)) ||
             (StartsWithCaseInsensitive(typed_command, ":showfps") &&
              IsTokenBoundary(typed_command, 8)) ||
             (StartsWithCaseInsensitive(typed_command, ":showstatusline") &&
              IsTokenBoundary(typed_command, 15)) ||
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
  if (command_autocomplete_.completion_start == std::string::npos) {
    command_autocomplete_.completion_start = command_text_.find_last_of(" \t");
    if (command_autocomplete_.completion_start == std::string::npos) {
      command_autocomplete_.completion_start = 0;
    } else {
      ++command_autocomplete_.completion_start;
    }
  }
  command_autocomplete_.matches = std::move(matches);
}

void BrowserWindow::FillCommandAutocomplete(const CompletionItem& item) {
  if (!command_autocomplete_.active) {
    return;
  }

  const std::string& name = CompletionInsertText(item);
  std::string completed;
  if (!name.empty() && name[0] == ':') {
    const size_t first_non_space = command_autocomplete_.prefix.find_first_not_of(" \t");
    const std::string leading = first_non_space == std::string::npos
                                    ? ""
                                    : command_autocomplete_.prefix.substr(0, first_non_space);
    completed = leading + name;
  } else {
    const size_t last_space = command_autocomplete_.prefix.find_last_of(" \t");
    if (command_autocomplete_.completion_start != std::string::npos &&
        command_autocomplete_.completion_start <= command_autocomplete_.prefix.size()) {
      completed = command_autocomplete_.prefix.substr(
                      0, command_autocomplete_.completion_start) +
                  name;
    } else if (last_space != std::string::npos) {
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
      command_autocomplete_.matches[static_cast<size_t>(command_autocomplete_.selection)]);
  SetCommandText(command_text_);
  Layout();
  return true;
}

bool BrowserWindow::DeleteSelectedCommandAutocomplete() {
  if (!command_autocomplete_.active ||
      command_autocomplete_.selection < 0 ||
      command_autocomplete_.selection >=
          static_cast<int>(command_autocomplete_.matches.size())) {
    return false;
  }

  const CompletionItem item =
      command_autocomplete_.matches[static_cast<size_t>(command_autocomplete_.selection)];
  const std::string entry = CompletionInsertText(item);
  bool deleted = false;

  if (item.source == CompletionSource::kOpenHistory) {
    deleted = EraseCaseInsensitive(open_history_, entry);
  } else if (item.source == CompletionSource::kSearchHistory) {
    auto history_it = search_history_.find(ToLowerAscii(item.source_key));
    if (history_it != search_history_.end()) {
      deleted = EraseCaseInsensitive(history_it->second, entry);
      if (history_it->second.empty()) {
        search_history_.erase(history_it);
      }
    }
  } else {
    return true;
  }

  if (!deleted) {
    return true;
  }

  const std::string prefix = command_autocomplete_.prefix;
  const int old_selection = command_autocomplete_.selection;

  // Cycling through completions keeps |prefix| as the original typed text while
  // rendering the selected completion into the command field. After removing a
  // history row, rebuild from that original prefix so the deleted text does not
  // remain in the command line, then keep the highlight on the next/previous
  // remaining history row instead of falling back to static rows like "tab".
  command_text_ = prefix;
  command_vim_.cursor = command_text_.size();
  vim::Clamp(command_vim_, command_text_);
  UpdateCommandAutocomplete();

  int next_selection = -1;
  if (command_autocomplete_.active &&
      !command_autocomplete_.matches.empty()) {
    const int size = static_cast<int>(command_autocomplete_.matches.size());
    const int start = std::min(old_selection, size - 1);
    for (int i = start; i < size; ++i) {
      if (SameHistoryCompletionSource(command_autocomplete_.matches[static_cast<size_t>(i)],
                                      item)) {
        next_selection = i;
        break;
      }
    }
    for (int i = start; next_selection < 0 && i >= 0; --i) {
      if (SameHistoryCompletionSource(command_autocomplete_.matches[static_cast<size_t>(i)],
                                      item)) {
        next_selection = i;
        break;
      }
    }
  }

  if (next_selection >= 0) {
    command_autocomplete_.selection = next_selection;
    FillCommandAutocomplete(
        command_autocomplete_.matches[static_cast<size_t>(next_selection)]);
  }

  SaveState();
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
    if (HasOnlyControlModifier(event) && IsCtrlKey(event, 'X')) {
      DeleteSelectedCommandAutocomplete();
      return true;
    }
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
  size_t search_engine_arg_start = 0;
  size_t search_engine_arg_end = 0;
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
          arg_start = arg_stop;
          while (arg_start < command_text_.size() &&
                 std::isspace(static_cast<unsigned char>(command_text_[arg_start]))) {
            ++arg_start;
          }
          if (arg_start < command_text_.size()) {
            arg_stop = command_text_.find_first_of(" \t", arg_start);
            if (arg_stop == std::string::npos) {
              arg_stop = command_text_.size();
            }
            const std::string search_engine = ToLowerAscii(
                command_text_.substr(arg_start, arg_stop - arg_start));
            if (FindSearchEngine(search_engine)) {
              search_engine_arg_start = arg_start;
              search_engine_arg_end = arg_stop;
            }
          }
        } else if (FindSearchEngine(first_arg)) {
          search_engine_arg_start = arg_start;
          search_engine_arg_end = arg_stop;
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
  if (search_engine_arg_end > search_engine_arg_start) {
    command_field_->ApplyTextColor(
        theme::kCommand,
        CefRange(static_cast<uint32_t>(search_engine_arg_start),
                 static_cast<uint32_t>(search_engine_arg_end)));
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


}  // namespace vimbrowser
