#include "browser_window.h"
#include "browser_window_internal.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <string_view>
#include <utility>

#include "include/cef_browser.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_frame.h"
#include "include/cef_image.h"
#include "include/cef_values.h"
#include "include/views/cef_button.h"
#include "theme.h"

namespace vimbrowser {
namespace {

constexpr int kContextCommandOpenLinkCurrent = MENU_ID_USER_FIRST + 1;
constexpr int kContextCommandOpenLinkNewTab = MENU_ID_USER_FIRST + 2;
constexpr int kContextCommandCopyLinkAddress = MENU_ID_USER_FIRST + 3;
constexpr int kContextCommandOpenSourceCurrent = MENU_ID_USER_FIRST + 4;
constexpr int kContextCommandOpenSourceNewTab = MENU_ID_USER_FIRST + 5;
constexpr int kContextCommandCopySourceAddress = MENU_ID_USER_FIRST + 6;
constexpr int kContextCommandCopyImage = MENU_ID_USER_FIRST + 7;
constexpr int kContextCommandCopyPageUrl = MENU_ID_USER_FIRST + 8;
constexpr int kContextCommandCopyFrameUrl = MENU_ID_USER_FIRST + 9;
constexpr int kContextCommandInspectElement = MENU_ID_USER_FIRST + 10;
constexpr int kContextCommandViewFrameSource = MENU_ID_USER_FIRST + 11;

std::string FirstNonEmpty(std::string first, const std::string& second) {
  if (!first.empty()) {
    return first;
  }
  return second;
}

std::string UrlDetail(const std::string& url) {
  return Ellipsize(url, 72);
}

std::string QuoteEllipsized(const std::string& value, size_t max_size) {
  return "\"" + Ellipsize(value, max_size) + "\"";
}

bool IsImageContext(const BrowserWindow::NativeContextMenu& menu) {
  return menu.media_type == CM_MEDIATYPE_IMAGE || menu.has_image_contents;
}

bool IsMediaContext(const BrowserWindow::NativeContextMenu& menu) {
  return menu.media_type == CM_MEDIATYPE_IMAGE ||
         menu.media_type == CM_MEDIATYPE_VIDEO ||
         menu.media_type == CM_MEDIATYPE_AUDIO ||
         menu.media_type == CM_MEDIATYPE_CANVAS ||
         menu.media_type == CM_MEDIATYPE_FILE ||
         menu.media_type == CM_MEDIATYPE_PLUGIN;
}

std::string MediaNoun(cef_context_menu_media_type_t media_type) {
  switch (media_type) {
    case CM_MEDIATYPE_IMAGE:
      return "image";
    case CM_MEDIATYPE_VIDEO:
      return "video";
    case CM_MEDIATYPE_AUDIO:
      return "audio";
    case CM_MEDIATYPE_CANVAS:
      return "canvas";
    case CM_MEDIATYPE_FILE:
      return "file";
    case CM_MEDIATYPE_PLUGIN:
      return "plugin";
    default:
      return "media";
  }
}

void AddSeparator(std::vector<BrowserWindow::ContextMenuItem>* items) {
  // vimbrowser context menus should stay compact and command-like. Do not add
  // visible separator rows between groups; ordering and labels are enough.
}

void AddItem(std::vector<BrowserWindow::ContextMenuItem>* items,
             int command_id,
             std::string label,
             std::string detail = {},
             bool enabled = true) {
  BrowserWindow::ContextMenuItem item;
  item.command_id = command_id;
  item.label = std::move(label);
  item.detail = std::move(detail);
  item.enabled = enabled;
  items->push_back(std::move(item));
}

bool IsSelectable(const BrowserWindow::ContextMenuItem& item) {
  return !item.separator && item.enabled && item.command_id != 0;
}

int FirstSelectableIndex(const std::vector<BrowserWindow::ContextMenuItem>& items) {
  for (size_t i = 0; i < items.size(); ++i) {
    if (IsSelectable(items[i])) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int SelectableIndexRelative(const std::vector<BrowserWindow::ContextMenuItem>& items,
                            int current,
                            int delta) {
  if (items.empty()) {
    return -1;
  }
  if (current < 0 || current >= static_cast<int>(items.size()) ||
      !IsSelectable(items[static_cast<size_t>(current)])) {
    return FirstSelectableIndex(items);
  }

  const int count = static_cast<int>(items.size());
  int index = current;
  for (int tries = 0; tries < count; ++tries) {
    index = (index + delta + count) % count;
    if (IsSelectable(items[static_cast<size_t>(index)])) {
      return index;
    }
  }
  return current;
}

void AssignMenuKeys(std::vector<BrowserWindow::ContextMenuItem>* items) {
  if (!items) {
    return;
  }
  constexpr std::string_view keys = "1234567890abcdefghijklmnopqrstuvwxyz";
  size_t key_index = 0;
  for (BrowserWindow::ContextMenuItem& item : *items) {
    if (!IsSelectable(item) || key_index >= keys.size()) {
      item.key = 0;
      continue;
    }
    item.key = keys[key_index++];
  }
}

std::string MenuRowText(const BrowserWindow::ContextMenuItem& item) {
  if (item.separator) {
    return "  " + item.label;
  }

  std::string out = " ";
  if (item.key) {
    out.push_back('[');
    out.push_back(item.key);
    out += "] ";
  } else {
    out += "    ";
  }
  out += item.label;
  if (!item.detail.empty()) {
    out += "  ";
    out += item.detail;
  }
  return out;
}

void StyleContextMenuButton(CefRefPtr<CefLabelButton> button,
                            const BrowserWindow::ContextMenuItem& item,
                            bool selected) {
  if (!button) {
    return;
  }

  const cef_color_t background =
      item.separator ? theme::kSidebarBg
                     : (selected ? theme::kSidebarSelBg : theme::kAppBg);
  const cef_color_t text =
      item.separator || !item.enabled
          ? theme::kMuted
          : (selected ? theme::kCommand : theme::kText);

  button->SetFontList("monospace, 12px");
  button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
  button->SetFocusable(false);
  button->SetInkDropEnabled(false);
  button->SetBackgroundColor(background);
  button->SetEnabled(!item.separator && item.enabled);
  button->SetEnabledTextColors(text);
  button->SetTextColor(CEF_BUTTON_STATE_NORMAL, text);
  button->SetTextColor(CEF_BUTTON_STATE_HOVERED, text);
  button->SetTextColor(CEF_BUTTON_STATE_PRESSED, text);
  button->SetTextColor(CEF_BUTTON_STATE_DISABLED, text);
  button->SetState(selected ? CEF_BUTTON_STATE_HOVERED : CEF_BUTTON_STATE_NORMAL);
}

std::string BinaryValueToString(CefRefPtr<CefBinaryValue> value) {
  std::string out;
  if (!value) {
    return out;
  }
  const size_t size = value->GetSize();
  if (size == 0) {
    return out;
  }
  out.resize(size);
  const size_t copied = value->GetData(out.data(), out.size(), 0);
  if (copied < out.size()) {
    out.resize(copied);
  }
  return out;
}

class DownloadImageCallback final : public CefDownloadImageCallback {
 public:
  using DoneCallback = std::function<void(const CefString&, int, CefRefPtr<CefImage>)>;

  explicit DownloadImageCallback(DoneCallback done) : done_(std::move(done)) {}

  void OnDownloadImageFinished(const CefString& image_url,
                               int http_status_code,
                               CefRefPtr<CefImage> image) override {
    if (done_) {
      done_(image_url, http_status_code, image);
    }
  }

 private:
  DoneCallback done_;

  IMPLEMENT_REFCOUNTING(DownloadImageCallback);
  DISALLOW_COPY_AND_ASSIGN(DownloadImageCallback);
};

}  // namespace

bool BrowserWindow::RunNativeContextMenu(
    BrowserClient* client,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefRunContextMenuCallback> callback) {
  if (!callback || !params || !window_) {
    if (callback) {
      callback->Cancel();
    }
    return true;
  }

  CancelNativeContextMenu();

  NativeContextMenu menu;
  menu.client = client;
  menu.browser = browser;
  menu.frame = frame;
  menu.callback = callback;
  menu.x = params->GetXCoord();
  menu.y = params->GetYCoord();
  menu.type_flags = params->GetTypeFlags();
  menu.media_type = params->GetMediaType();
  menu.media_state_flags = params->GetMediaStateFlags();
  menu.edit_state_flags = params->GetEditStateFlags();
  menu.has_image_contents = params->HasImageContents();
  menu.editable = params->IsEditable();
  menu.spellcheck_enabled = params->IsSpellCheckEnabled();
  menu.page_url = params->GetPageUrl().ToString();
  menu.frame_url = params->GetFrameUrl().ToString();
  menu.link_url = params->GetLinkUrl().ToString();
  menu.unfiltered_link_url = params->GetUnfilteredLinkUrl().ToString();
  menu.source_url = params->GetSourceUrl().ToString();
  menu.title_text = params->GetTitleText().ToString();
  menu.selection_text = params->GetSelectionText().ToString();
  menu.misspelled_word = params->GetMisspelledWord().ToString();

  std::vector<CefString> suggestions;
  if (params->GetDictionarySuggestions(suggestions)) {
    for (const CefString& suggestion : suggestions) {
      if (menu.dictionary_suggestions.size() >= 5) {
        break;
      }
      menu.dictionary_suggestions.push_back(suggestion.ToString());
    }
  }

  BuildNativeContextMenuItems(&menu);
  AssignMenuKeys(&menu.items);
  menu.selected_index = FirstSelectableIndex(menu.items);
  native_context_menu_ = std::move(menu);

  EnsureContextMenuViews();
  RebuildNativeContextMenuRows();
  Layout();
  UpdateNativeContextMenuSelection();
  return true;
}

void BrowserWindow::BuildNativeContextMenuItems(NativeContextMenu* menu) {
  if (!menu) {
    return;
  }

  std::vector<ContextMenuItem>& items = menu->items;
  items.clear();

  if (menu->editable) {
    if (!menu->misspelled_word.empty()) {
      if (!menu->dictionary_suggestions.empty()) {
        for (size_t i = 0; i < menu->dictionary_suggestions.size() &&
                           MENU_ID_SPELLCHECK_SUGGESTION_0 + static_cast<int>(i) <=
                               MENU_ID_SPELLCHECK_SUGGESTION_LAST;
             ++i) {
          AddItem(&items,
                  MENU_ID_SPELLCHECK_SUGGESTION_0 + static_cast<int>(i),
                  menu->dictionary_suggestions[i],
                  "replace misspelling");
        }
      } else {
        AddItem(&items, MENU_ID_NO_SPELLING_SUGGESTIONS,
                "No spelling suggestions", {}, false);
      }
      AddItem(&items, MENU_ID_ADD_TO_DICTIONARY,
              "Add " + QuoteEllipsized(menu->misspelled_word, 28) + " to dictionary");
      AddSeparator(&items);
    }

    const auto edit = menu->edit_state_flags;
    AddItem(&items, MENU_ID_UNDO, "Undo", {}, edit & CM_EDITFLAG_CAN_UNDO);
    AddItem(&items, MENU_ID_REDO, "Redo", {}, edit & CM_EDITFLAG_CAN_REDO);
    AddSeparator(&items);
    AddItem(&items, MENU_ID_CUT, "Cut", {}, edit & CM_EDITFLAG_CAN_CUT);
    AddItem(&items, MENU_ID_COPY, "Copy", {}, edit & CM_EDITFLAG_CAN_COPY);
    AddItem(&items, MENU_ID_PASTE, "Paste", {}, edit & CM_EDITFLAG_CAN_PASTE);
    AddItem(&items, MENU_ID_PASTE_MATCH_STYLE, "Paste without style", {},
            edit & CM_EDITFLAG_CAN_PASTE);
    AddItem(&items, MENU_ID_DELETE, "Delete", {}, edit & CM_EDITFLAG_CAN_DELETE);
    AddSeparator(&items);
    AddItem(&items, MENU_ID_SELECT_ALL, "Select all", {},
            edit & CM_EDITFLAG_CAN_SELECT_ALL);
  } else if (!menu->selection_text.empty()) {
    AddItem(&items, MENU_ID_COPY, "Copy selection",
            QuoteEllipsized(menu->selection_text, 48));
  }

  const std::string link_url =
      FirstNonEmpty(menu->unfiltered_link_url, menu->link_url);
  if (!link_url.empty()) {
    AddSeparator(&items);
    AddItem(&items, kContextCommandOpenLinkCurrent, "Open link here",
            UrlDetail(link_url));
    AddItem(&items, kContextCommandOpenLinkNewTab, "Open link in new tab",
            UrlDetail(link_url));
    AddItem(&items, kContextCommandCopyLinkAddress, "Copy link address",
            UrlDetail(link_url));
  }

  if (IsMediaContext(*menu)) {
    const std::string noun = MediaNoun(menu->media_type);
    AddSeparator(&items);
    if (IsImageContext(*menu)) {
      AddItem(&items, kContextCommandCopyImage, "Copy image",
              menu->source_url.empty() ? "no source URL" : UrlDetail(menu->source_url),
              !menu->source_url.empty());
      AddItem(&items, kContextCommandCopySourceAddress, "Copy image address",
              UrlDetail(menu->source_url), !menu->source_url.empty());
      AddItem(&items, kContextCommandOpenSourceNewTab, "Open image in new tab",
              UrlDetail(menu->source_url), !menu->source_url.empty());
      AddItem(&items, kContextCommandOpenSourceCurrent, "Open image here",
              UrlDetail(menu->source_url), !menu->source_url.empty());
    } else {
      AddItem(&items, kContextCommandCopySourceAddress,
              "Copy " + noun + " address", UrlDetail(menu->source_url),
              !menu->source_url.empty());
      AddItem(&items, kContextCommandOpenSourceNewTab,
              "Open " + noun + " in new tab", UrlDetail(menu->source_url),
              !menu->source_url.empty());
      AddItem(&items, kContextCommandOpenSourceCurrent,
              "Open " + noun + " here", UrlDetail(menu->source_url),
              !menu->source_url.empty());
    }
  }

  AddSeparator(&items);
  const bool is_loading = menu->browser && menu->browser->IsLoading();
  AddItem(&items, is_loading ? MENU_ID_STOPLOAD : MENU_ID_RELOAD,
          is_loading ? "Stop loading" : "Reload");
  AddItem(&items, kContextCommandCopyPageUrl, "Copy page URL",
          UrlDetail(FirstNonEmpty(menu->page_url, ActiveTabUrl())));
  AddItem(&items, MENU_ID_VIEW_SOURCE, "View page source");

  if (!menu->frame_url.empty() && menu->frame_url != menu->page_url) {
    AddSeparator(&items);
    AddItem(&items, kContextCommandCopyFrameUrl, "Copy frame URL",
            UrlDetail(menu->frame_url));
    AddItem(&items, kContextCommandViewFrameSource, "View frame source");
  }

  AddSeparator(&items);
  AddItem(&items, kContextCommandInspectElement, "Inspect element",
          "DevTools at clicked node");

  while (!items.empty() && items.back().separator) {
    items.pop_back();
  }
}

void BrowserWindow::EnsureContextMenuViews() {
  if (!window_ || context_menu_overlay_) {
    return;
  }

  context_menu_backdrop_button_ = CefLabelButton::CreateLabelButton(this, "");
  context_menu_backdrop_button_->SetID(kContextMenuBackdropButtonId);
  context_menu_backdrop_button_->SetFocusable(false);
  context_menu_backdrop_button_->SetInkDropEnabled(false);
  context_menu_backdrop_button_->SetBackgroundColor(theme::kTransparent);
  context_menu_backdrop_button_->SetEnabledTextColors(theme::kTransparent);
  context_menu_backdrop_button_->SetTextColor(CEF_BUTTON_STATE_NORMAL,
                                              theme::kTransparent);
  context_menu_backdrop_button_->SetTextColor(CEF_BUTTON_STATE_HOVERED,
                                              theme::kTransparent);
  context_menu_backdrop_button_->SetTextColor(CEF_BUTTON_STATE_PRESSED,
                                              theme::kTransparent);
  context_menu_backdrop_overlay_ = window_->AddOverlayView(
      context_menu_backdrop_button_, CEF_DOCKING_MODE_CUSTOM, true);
  context_menu_backdrop_overlay_->SetVisible(false);

  context_menu_panel_ = CefPanel::CreatePanel(this);
  context_menu_panel_->SetID(kContextMenuPanelId);
  context_menu_panel_->SetBackgroundColor(theme::kAccent);
  CefBoxLayoutSettings menu_settings = {};
  menu_settings.size = sizeof(menu_settings);
  menu_settings.horizontal = false;
  menu_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  menu_settings.inside_border_insets =
      CefInsets(kContextMenuBorderWidth, kContextMenuBorderWidth,
                kContextMenuBorderWidth, kContextMenuBorderWidth);
  context_menu_panel_->SetToBoxLayout(menu_settings);

  context_menu_overlay_ = window_->AddOverlayView(
      context_menu_panel_, CEF_DOCKING_MODE_CUSTOM, true);
  context_menu_overlay_->SetVisible(false);
}

void BrowserWindow::RebuildNativeContextMenuRows() {
  if (!context_menu_panel_) {
    return;
  }

  for (auto& row : context_menu_rows_) {
    context_menu_panel_->RemoveChildView(row);
  }
  context_menu_rows_.clear();

  if (!native_context_menu_) {
    return;
  }

  for (size_t i = 0; i < native_context_menu_->items.size(); ++i) {
    const ContextMenuItem& item = native_context_menu_->items[i];
    CefRefPtr<CefLabelButton> row =
        CefLabelButton::CreateLabelButton(this, MenuRowText(item));
    row->SetID(kContextMenuRowBaseId + static_cast<int>(i));
    row->SetAccessibleName("vimbrowser context menu " + item.label);
    StyleContextMenuButton(row, item,
                           static_cast<int>(i) == native_context_menu_->selected_index);
    context_menu_panel_->AddChildView(row);
    context_menu_rows_.push_back(row);
  }
}

int BrowserWindow::NativeContextMenuWidth() const {
  if (!native_context_menu_) {
    return kContextMenuMinWidth;
  }

  int max_columns = 0;
  for (const ContextMenuItem& item : native_context_menu_->items) {
    max_columns = std::max(max_columns, TextColumns(MenuRowText(item)));
  }
  const int desired = max_columns * kCommandCharWidth +
                      2 * kContextMenuHPadding +
                      2 * kContextMenuBorderWidth;
  return std::clamp(desired, kContextMenuMinWidth, kContextMenuMaxWidth);
}

int BrowserWindow::NativeContextMenuHeight() const {
  if (!native_context_menu_) {
    return 1;
  }
  return 2 * kContextMenuBorderWidth +
         static_cast<int>(native_context_menu_->items.size()) *
             kContextMenuRowHeight;
}

void BrowserWindow::LayoutNativeContextMenu(int window_width, int window_height) {
  if (!context_menu_overlay_ || !context_menu_panel_ ||
      !context_menu_backdrop_overlay_ || !context_menu_backdrop_button_) {
    return;
  }

  const bool visible = native_context_menu_.has_value();
  context_menu_backdrop_overlay_->SetVisible(visible);
  context_menu_backdrop_button_->SetVisible(visible);
  context_menu_overlay_->SetVisible(visible);
  context_menu_panel_->SetVisible(visible);
  if (!visible) {
    ClearContextMenuMouseBounds();
    return;
  }

  context_menu_backdrop_button_->SetBackgroundColor(theme::kTransparent);
  context_menu_backdrop_button_->SetSize(CefSize(window_width, window_height));
  context_menu_backdrop_overlay_->SetBounds(
      CefRect(0, 0, window_width, window_height));

  const int menu_width = std::max(1, NativeContextMenuWidth());
  const int menu_height = std::max(1, NativeContextMenuHeight());
  const int page_origin_x = sidebar_visible_ ? kSidebarWidth : 0;
  int x = page_origin_x + native_context_menu_->x;
  int y = native_context_menu_->y;
  if (x + menu_width > window_width) {
    x = std::max(0, window_width - menu_width);
  }
  if (y + menu_height > window_height) {
    y = std::max(0, window_height - menu_height);
  }
  x = std::max(0, x);
  y = std::max(0, y);

  context_menu_panel_->SetBackgroundColor(theme::kAccent);
  context_menu_panel_->SetSize(CefSize(menu_width, menu_height));
  context_menu_overlay_->SetBounds(CefRect(x, y, menu_width, menu_height));
  UpdateContextMenuMouseBounds(x, y, menu_width, menu_height);

  const int row_width = std::max(1, menu_width - 2 * kContextMenuBorderWidth);
  for (size_t i = 0; i < context_menu_rows_.size(); ++i) {
    CefRefPtr<CefLabelButton> row = context_menu_rows_[i];
    if (!row) {
      continue;
    }
    const int row_y = kContextMenuBorderWidth +
                      static_cast<int>(i) * kContextMenuRowHeight;
    row->SetBounds(CefRect(kContextMenuBorderWidth, row_y,
                           row_width, kContextMenuRowHeight));
    row->SetSize(CefSize(row_width, kContextMenuRowHeight));
  }
  context_menu_panel_->InvalidateLayout();
  if (context_menu_panel_->GetLayout()) {
    context_menu_panel_->Layout();
  }
}

void BrowserWindow::UpdateNativeContextMenuSelection() {
  if (!native_context_menu_) {
    return;
  }
  for (size_t i = 0; i < context_menu_rows_.size() &&
                     i < native_context_menu_->items.size();
       ++i) {
    StyleContextMenuButton(context_menu_rows_[i], native_context_menu_->items[i],
                           static_cast<int>(i) == native_context_menu_->selected_index);
  }
}

void BrowserWindow::SelectNativeContextMenuRelative(int delta) {
  if (!native_context_menu_) {
    return;
  }
  native_context_menu_->selected_index = SelectableIndexRelative(
      native_context_menu_->items, native_context_menu_->selected_index, delta);
  UpdateNativeContextMenuSelection();
}

bool BrowserWindow::HandleNativeContextMenuKey(const CefKeyEvent& event) {
  if (!native_context_menu_) {
    return false;
  }

  if (!IsRawKeyDown(event)) {
    return IsCharEvent(event);
  }

  if (IsEscapeKey(event)) {
    CancelNativeContextMenu();
    return true;
  }

  if (IsEnterKey(event) || IsSpaceKey(event)) {
    if (native_context_menu_->selected_index >= 0) {
      ActivateNativeContextMenuRow(
          static_cast<size_t>(native_context_menu_->selected_index));
    }
    return true;
  }

  if (event.windows_key_code == 0x28) {  // VKEY_DOWN.
    SelectNativeContextMenuRelative(1);
    return true;
  }
  if (event.windows_key_code == 0x26) {  // VKEY_UP.
    SelectNativeContextMenuRelative(-1);
    return true;
  }

  if (IsPlain(event)) {
    const char key = LowerAsciiChar(PlainKeyChar(event));
    if (key == 'j') {
      SelectNativeContextMenuRelative(1);
      return true;
    }
    if (key == 'k') {
      SelectNativeContextMenuRelative(-1);
      return true;
    }
    if (key) {
      for (size_t i = 0; i < native_context_menu_->items.size(); ++i) {
        if (native_context_menu_->items[i].key == key) {
          ActivateNativeContextMenuRow(i);
          return true;
        }
      }
    }
  }

  return true;
}

void BrowserWindow::ActivateNativeContextMenuRow(size_t row_index) {
  if (!native_context_menu_ || row_index >= native_context_menu_->items.size()) {
    return;
  }

  const ContextMenuItem& item = native_context_menu_->items[row_index];
  if (!IsSelectable(item)) {
    return;
  }

  native_context_menu_->selected_index = static_cast<int>(row_index);
  UpdateNativeContextMenuSelection();
  CompleteNativeContextMenu(item.command_id);
}

void BrowserWindow::HoverNativeContextMenuRow(size_t row_index) {
  if (!native_context_menu_ || row_index >= native_context_menu_->items.size()) {
    return;
  }

  const ContextMenuItem& item = native_context_menu_->items[row_index];
  if (!IsSelectable(item)) {
    return;
  }

  const int index = static_cast<int>(row_index);
  if (native_context_menu_->selected_index == index) {
    return;
  }

  native_context_menu_->selected_index = index;
  UpdateNativeContextMenuSelection();
}

void BrowserWindow::CompleteNativeContextMenu(int command_id) {
  if (!native_context_menu_) {
    return;
  }

  CefRefPtr<CefRunContextMenuCallback> callback = native_context_menu_->callback;
  native_context_menu_->callback = nullptr;
  HideNativeContextMenuViews();
  if (callback) {
    callback->Continue(command_id, static_cast<cef_event_flags_t>(0));
  }
}

void BrowserWindow::CancelNativeContextMenu() {
  if (!native_context_menu_ || native_context_menu_->closing) {
    return;
  }

  CefRefPtr<CefRunContextMenuCallback> callback = native_context_menu_->callback;
  native_context_menu_->callback = nullptr;
  HideNativeContextMenuViews();
  native_context_menu_->closing = true;
  if (callback) {
    callback->Cancel();
  }
  native_context_menu_.reset();
}

void BrowserWindow::HideNativeContextMenuViews() {
  ClearContextMenuMouseBounds();
  if (context_menu_overlay_) {
    context_menu_overlay_->SetVisible(false);
  }
  if (context_menu_panel_) {
    context_menu_panel_->SetVisible(false);
  }
  if (context_menu_backdrop_overlay_) {
    context_menu_backdrop_overlay_->SetVisible(false);
  }
  if (context_menu_backdrop_button_) {
    context_menu_backdrop_button_->SetVisible(false);
  }
}

void BrowserWindow::UpdateContextMenuMouseBounds(int x,
                                                 int y,
                                                 int width,
                                                 int height) {
  if (!window_ || !native_context_menu_) {
    ClearContextMenuMouseBounds();
    return;
  }

  const CefRect screen_bounds = window_->GetClientAreaBoundsInScreen();
  context_menu_mouse_screen_x_.store(screen_bounds.x + x);
  context_menu_mouse_screen_y_.store(screen_bounds.y + y);
  context_menu_mouse_width_.store(std::max(0, width));
  context_menu_mouse_height_.store(std::max(0, height));
  context_menu_mouse_row_count_.store(
      static_cast<int>(native_context_menu_->items.size()));
#if defined(__linux__)
  // Only the X11 context-menu mouse watcher consumes this window id.
  context_menu_mouse_window_.store(
      static_cast<unsigned long>(window_->GetWindowHandle()));
#else
  context_menu_mouse_window_.store(0);
#endif
}

void BrowserWindow::ClearContextMenuMouseBounds() {
  context_menu_mouse_screen_x_.store(0);
  context_menu_mouse_screen_y_.store(0);
  context_menu_mouse_width_.store(0);
  context_menu_mouse_height_.store(0);
  context_menu_mouse_row_count_.store(0);
  context_menu_mouse_window_.store(0);
}

void BrowserWindow::OnNativeContextMenuDismissed(BrowserClient* client) {
  if (!native_context_menu_) {
    return;
  }
  if (client && native_context_menu_->client && client != native_context_menu_->client) {
    return;
  }
  HideNativeContextMenuViews();
  native_context_menu_.reset();
}

bool BrowserWindow::OnNativeContextMenuCommand(
    BrowserClient* client,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    int command_id,
    cef_event_flags_t event_flags) {
  if (!native_context_menu_) {
    return false;
  }
  if (client && native_context_menu_->client && client != native_context_menu_->client) {
    return false;
  }

  NativeContextMenu& menu = *native_context_menu_;
  CefRefPtr<CefBrowser> target_browser = menu.browser ? menu.browser : browser;
  CefRefPtr<CefFrame> target_frame = menu.frame ? menu.frame : frame;
  if (!target_frame && target_browser) {
    target_frame = target_browser->GetFocusedFrame();
  }
  if (!target_frame && target_browser) {
    target_frame = target_browser->GetMainFrame();
  }

  const std::string link_url = FirstNonEmpty(menu.unfiltered_link_url, menu.link_url);
  const std::string page_url = FirstNonEmpty(menu.page_url, ActiveTabUrl());

  if (command_id >= MENU_ID_SPELLCHECK_SUGGESTION_0 &&
      command_id <= MENU_ID_SPELLCHECK_SUGGESTION_LAST) {
    const size_t suggestion_index =
        static_cast<size_t>(command_id - MENU_ID_SPELLCHECK_SUGGESTION_0);
    if (target_browser && target_browser->GetHost() &&
        suggestion_index < menu.dictionary_suggestions.size()) {
      target_browser->GetHost()->ReplaceMisspelling(
          menu.dictionary_suggestions[suggestion_index]);
    }
    return true;
  }

  switch (command_id) {
    case MENU_ID_BACK:
      if (target_browser) target_browser->GoBack();
      return true;
    case MENU_ID_FORWARD:
      if (target_browser) target_browser->GoForward();
      return true;
    case MENU_ID_RELOAD:
      if (target_browser) target_browser->Reload();
      return true;
    case MENU_ID_RELOAD_NOCACHE:
      if (target_browser) target_browser->ReloadIgnoreCache();
      return true;
    case MENU_ID_STOPLOAD:
      if (target_browser) target_browser->StopLoad();
      return true;
    case MENU_ID_UNDO:
      if (target_frame) target_frame->Undo();
      return true;
    case MENU_ID_REDO:
      if (target_frame) target_frame->Redo();
      return true;
    case MENU_ID_CUT:
      if (target_frame) target_frame->Cut();
      return true;
    case MENU_ID_COPY:
      if (!menu.selection_text.empty() && !menu.editable) {
        WriteClipboardText(menu.selection_text);
      } else if (target_frame) {
        target_frame->Copy();
      }
      return true;
    case MENU_ID_PASTE:
      if (target_frame) target_frame->Paste();
      return true;
    case MENU_ID_PASTE_MATCH_STYLE:
      if (target_frame) target_frame->PasteAndMatchStyle();
      return true;
    case MENU_ID_DELETE:
      if (target_frame) target_frame->Delete();
      return true;
    case MENU_ID_SELECT_ALL:
      if (target_frame) target_frame->SelectAll();
      return true;
    case MENU_ID_VIEW_SOURCE:
      if (target_browser && target_browser->GetMainFrame()) {
        target_browser->GetMainFrame()->ViewSource();
      } else if (target_frame) {
        target_frame->ViewSource();
      }
      return true;
    case MENU_ID_ADD_TO_DICTIONARY:
      if (target_browser && target_browser->GetHost() &&
          !menu.misspelled_word.empty()) {
        target_browser->GetHost()->AddWordToDictionary(menu.misspelled_word);
      }
      return true;
    case MENU_ID_NO_SPELLING_SUGGESTIONS:
      return true;
    case kContextCommandOpenLinkCurrent:
      if (target_browser && target_browser->GetMainFrame() && !link_url.empty()) {
        target_browser->GetMainFrame()->LoadURL(link_url);
      }
      return true;
    case kContextCommandOpenLinkNewTab:
      if (!link_url.empty()) {
        AddTabAfterSelection(link_url, true);
      }
      return true;
    case kContextCommandCopyLinkAddress:
      if (!link_url.empty()) {
        WriteClipboardText(link_url);
        SetStatusOutput("copied link address");
      }
      return true;
    case kContextCommandOpenSourceCurrent:
      if (target_browser && target_browser->GetMainFrame() &&
          !menu.source_url.empty()) {
        target_browser->GetMainFrame()->LoadURL(menu.source_url);
      }
      return true;
    case kContextCommandOpenSourceNewTab:
      if (!menu.source_url.empty()) {
        AddTabAfterSelection(menu.source_url, true);
      }
      return true;
    case kContextCommandCopySourceAddress:
      if (!menu.source_url.empty()) {
        WriteClipboardText(menu.source_url);
        SetStatusOutput("copied " + MediaNoun(menu.media_type) + " address");
      }
      return true;
    case kContextCommandCopyImage:
      if (!menu.source_url.empty()) {
        CopyContextImageToClipboard(target_browser, menu.source_url);
      }
      return true;
    case kContextCommandCopyPageUrl:
      if (!page_url.empty()) {
        WriteClipboardText(page_url);
        SetStatusOutput("copied page URL");
      }
      return true;
    case kContextCommandCopyFrameUrl:
      if (!menu.frame_url.empty()) {
        WriteClipboardText(menu.frame_url);
        SetStatusOutput("copied frame URL");
      }
      return true;
    case kContextCommandViewFrameSource:
      if (target_frame) {
        target_frame->ViewSource();
      }
      return true;
    case kContextCommandInspectElement:
      if (menu.client) {
        ShowDevToolsForClient(menu.client, CefPoint(menu.x, menu.y));
      }
      return true;
    default:
      return false;
  }
}

void BrowserWindow::CopyContextImageToClipboard(CefRefPtr<CefBrowser> browser,
                                                const std::string& image_url) {
  if (!browser || !browser->GetHost() || image_url.empty()) {
    SetStatusOutput("copy image failed: no image URL");
    return;
  }

  SetStatusOutput("copying image...");
  CefRefPtr<BrowserWindow> self(this);
  CefRefPtr<CefDownloadImageCallback> callback(new DownloadImageCallback(
      [self](const CefString& returned_url,
             int http_status_code,
             CefRefPtr<CefImage> image) {
        if (!self) {
          return;
        }
        if (!image || image->IsEmpty()) {
          std::ostringstream message;
          message << "copy image failed";
          if (http_status_code > 0) {
            message << " (HTTP " << http_status_code << ")";
          }
          self->SetStatusOutput(message.str());
          return;
        }

        int pixel_width = 0;
        int pixel_height = 0;
        CefRefPtr<CefBinaryValue> png =
            image->GetAsPNG(1.0f, true, pixel_width, pixel_height);
        const std::string png_bytes = BinaryValueToString(png);
        if (png_bytes.empty()) {
          self->SetStatusOutput("copy image failed: could not encode PNG");
          return;
        }

        if (ShellWrite("xclip -selection clipboard -t image/png -i 2>/dev/null",
                       png_bytes) ||
            ShellWrite("wl-copy --type image/png 2>/dev/null", png_bytes)) {
          std::ostringstream message;
          message << "copied image";
          if (pixel_width > 0 && pixel_height > 0) {
            message << " " << pixel_width << "x" << pixel_height;
          }
          self->SetStatusOutput(message.str());
          return;
        }

        self->SetStatusOutput("copy image failed: no image clipboard helper");
      }));
  browser->GetHost()->DownloadImage(image_url, false, 0, false, callback);
}

}  // namespace vimbrowser
