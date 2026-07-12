#include "browser_window.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "config.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
#include "include/cef_cookie.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_navigation_entry.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_string_visitor.h"
#include "include/cef_urlrequest.h"
#include "include/cef_values.h"
#include "include/views/cef_button.h"
#include "include/wrapper/cef_closure_task.h"
#include "ipc_server.h"
#include "shortcuts.h"
#include "theme.h"

namespace vimbrowser {

constexpr const char kIpcProtocolName[] = "vimbrowser-ipc";
constexpr int kIpcProtocolVersion = 1;

// This is only a style-invalidation pulse after :shader changes. The color
// transform itself remains native Blink code in StyleResolver::ResolveStyle().
constexpr const char kShaderRefreshScript[] = R"JS(
(() => {
  const refresh = () => {
    const root = document.documentElement;
    if (!root) return;
    const oldDisplay = root.style.display;
    root.style.display = 'none';
    void root.offsetHeight;
    root.style.display = oldDisplay;
    void root.offsetHeight;
  };
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', refresh, {once: true});
  } else {
    refresh();
    setTimeout(refresh, 300);
  }
})();
)JS";

constexpr const char kBlurActiveElementScript[] = R"JS(
(() => {
  const element = document.activeElement;
  if (element && element !== document.body &&
      element !== document.documentElement &&
      typeof element.blur === 'function') {
    element.blur();
  }
})();
)JS";

constexpr int kSidebarWidth = 175;
constexpr int kCommandHeight = 28;
constexpr int kCommandAutocompleteRowHeight = 24;
constexpr int kCommandAutocompleteMaxVisible = 10;
constexpr int kCommandAutocompleteBorder = 0;
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
constexpr int kFpsIndicatorPanelId = 116;
constexpr int kFpsIndicatorFieldId = 117;
constexpr int kStatusBarPanelId = 118;
constexpr int kStatusModeFieldId = 119;
constexpr int kStatusUrlFieldId = 120;
constexpr int kStatusContentPanelId = 121;
constexpr int kStatusOutputFieldId = 122;
constexpr int kStatusBorderPanelId = 123;
constexpr int kStatusSidebarSpacerPanelId = 124;
constexpr int kSidebarBorderOverlayPanelId = 125;
constexpr int kAcceleratorCommandTab = 5000;
constexpr int kAcceleratorCommandBacktab = 5001;
constexpr int kAcceleratorTabNext = 5002;
constexpr int kAcceleratorTabPrevious = 5003;
constexpr int kAcceleratorSidebarSpace = 5004;
constexpr int kAcceleratorHintRightClick = 5005;
constexpr int kAcceleratorHintHover = 5006;
constexpr int kSidebarRowBaseId = 2000;
constexpr int kAutocompleteRowBaseId = 6000;
constexpr int kSidebarRowHeight = 24;
// The sidebar is fixed-height and does not expose a scroll container. Rendering
// hundreds of off-screen textfields dominates extreme session restore startup,
// while 96 rows already covers unusually tall windows at 24px per row.
constexpr size_t kSidebarMaxRenderedRows = 96;
// Experimental chrome-level mode indicator. Flip to false to disable without
// touching the mode/focus state machines.
constexpr bool kModeIndicatorEnabled = true;
constexpr int kModeIndicatorWidth = 96;
constexpr int kModeIndicatorHeight = 24;
constexpr int kStatusBarHeight = 16;
constexpr int kStatusModeWidth = 64;
constexpr int kCommandTextInsetX = 0;
constexpr int kCommandCharWidth = 8;
constexpr int kLineScrollPx = 280;
constexpr int kSmallScrollPx = 140;
constexpr size_t kLazyRestoreBackgroundTabThreshold = 8;
constexpr int kVirtualSidebarRefreshDelayMs = 250;
// Keep tab content selection asynchronous so rapid tab-switch bursts still
// coalesce by generation, but do not add an artificial human-visible delay.
constexpr int kTabContentActivationDelayMs = 0;
constexpr int kTabStateSaveDelayMs = 250;
constexpr size_t kOpenHistoryCompletionNameMax = 140;
constexpr size_t kTabFocusCompletionDescriptionMax = 140;
constexpr size_t kNoTabIndex = std::numeric_limits<size_t>::max();

size_t IndexAfterVectorMove(size_t index, size_t from, size_t to) {
  if (index == from) {
    return to;
  }
  if (from < to) {
    return (index > from && index <= to) ? index - 1 : index;
  }
  return (index >= to && index < from) ? index + 1 : index;
}

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

void StyleCommandField(CefRefPtr<CefTextfield> field) {
  if (!field) {
    return;
  }
  // The command line is a real focused native textfield. We intercept editing
  // keys in BrowserWindow and drive vim::LineEditState ourselves, but the
  // textfield owns all text/caret/selection painting. This keeps normal-mode
  // block cursors and insert-mode bar cursors in Chromium's renderer instead of
  // using overlay views that can drift, move text, or fail to erase glyphs.
  field->SetReadOnly(false);
  field->SetFocusable(true);
  field->SetFontList("monospace, 13px");
  field->SetBackgroundColor(theme::kTransparent);
  // Chromium colors the insertion caret from the default text color. Keep that
  // cyan, then apply per-range colors for the actual glyphs below.
  field->SetTextColor(theme::kVimNormal);
  field->SetSelectionTextColor(theme::kAppBg);
  field->SetSelectionBackgroundColor(theme::kVimNormal);
}

const std::vector<CompletionItem>& CommandList() {
  static const std::vector<CompletionItem> commands = {
      {":open", "open URL/search in current tab"},
      {":tab-focus", "focus tab by number/title/url"},
      {":folder-create", "create a folder in the current sidebar folder"},
      {":folder-move", "move selected sidebar items to a folder"},
      {":folder-rename", "rename the selected sidebar folder"},
      {":mspdf", "download the current MuseScore score as a PDF"},
      {":test", "open deterministic internal test fixtures"},
      {":shader", "toggle native page color shader"},
      {":showmode", "toggle top-right vim mode display"},
      {":showfps", "toggle current page fps display"},
      {":showstatusline", "toggle bottom statusline display"},
      {":noh", "clear page search highlights"},
  };
  return commands;
}

const std::vector<CompletionItem>& OnOffArgList() {
  static const std::vector<CompletionItem> args = {
      {"off", "turn off"},
      {"on", "turn on"},
  };
  return args;
}

const std::vector<CompletionItem>& OpenArgList() {
  static const std::vector<CompletionItem> args = {
      {"-t", "open in a new tab"},
      {"tab", "open in a new tab"},
  };
  return args;
}

const std::vector<CompletionItem>& TestArgList() {
  static const std::vector<CompletionItem> args = {
      {"permission-modal", "show a mock media permission prompt"},
  };
  return args;
}

bool CommandTakesArguments(const std::string& command) {
  return command == ":open" || command == ":tab-focus" ||
         command == ":folder-create" || command == ":folder-move" ||
         command == ":folder-rename" ||
         command == ":shader" || command == ":showmode" ||
         command == ":showfps" || command == ":showstatusline" ||
         command == ":test";
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

bool HasOnlyControlModifier(const CefKeyEvent& event) {
  return (event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
         !(event.modifiers & EVENTFLAG_SHIFT_DOWN) &&
         !(event.modifiers & EVENTFLAG_ALT_DOWN) &&
         !(event.modifiers & EVENTFLAG_COMMAND_DOWN);
}

bool IsSpaceKey(const CefKeyEvent& event) {
  return event.windows_key_code == 0x20 || event.character == 0x20 ||
         event.unmodified_character == 0x20;
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

char LowerAsciiChar(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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
  return event.windows_key_code == 0x08 || event.windows_key_code == 0xFF08 ||
         event.native_key_code == 22 || event.character == 0x08 ||
         event.unmodified_character == 0x08;
}

bool IsTabKey(const CefKeyEvent& event) {
  return event.windows_key_code == 0x09 || event.native_key_code == 23;
}

bool IsDeleteKey(const CefKeyEvent& event) {
  return event.windows_key_code == 0x2E || event.windows_key_code == 0xFFFF ||
         event.native_key_code == 119;
}

bool IsNavigationEditingKey(const CefKeyEvent& event) {
  switch (event.windows_key_code) {
    case 0x23:  // End
    case 0x24:  // Home
    case 0x25:  // Left
    case 0x26:  // Up
    case 0x27:  // Right
    case 0x28:  // Down
      return true;
    default:
      return false;
  }
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

bool IsCtrlSemicolonKey(const CefKeyEvent& event) {
  if (!HasOnlyControlModifier(event)) {
    return false;
  }
  // Chromium reports ';' as VKEY_OEM_1 (0xBA) on common XKB layouts, while
  // CEF character fields preserve the literal ASCII semicolon on some paths.
  return event.windows_key_code == ';' || event.windows_key_code == 0xBA ||
         event.character == ';' || event.unmodified_character == ';' ||
         event.native_key_code == 47;
}

bool IsCommonCtrlEditingKey(const CefKeyEvent& event) {
  return IsCtrlKey(event, 'a') || IsCtrlKey(event, 'c') ||
         IsCtrlKey(event, 'v') || IsCtrlKey(event, 'x') ||
         IsCtrlKey(event, 'y') || IsCtrlKey(event, 'z');
}

bool ShouldForwardFocusedEditableKey(const CefKeyEvent& event,
                                     bool focus_on_editable_field) {
  if (!focus_on_editable_field) {
    return false;
  }
  if (event.modifiers & (EVENTFLAG_ALT_DOWN | EVENTFLAG_COMMAND_DOWN)) {
    return false;
  }
  return IsPlainPrintableKey(event) || IsBackspaceKey(event) ||
         IsDeleteKey(event) || IsEnterKey(event) || IsTabKey(event) ||
         IsNavigationEditingKey(event) || IsCommonCtrlEditingKey(event);
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

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::vector<std::string> SplitArgs(const std::string& value) {
  std::vector<std::string> args;
  size_t pos = 0;
  while (pos < value.size()) {
    while (pos < value.size() &&
           std::isspace(static_cast<unsigned char>(value[pos]))) {
      ++pos;
    }
    if (pos >= value.size()) {
      break;
    }
    const size_t start = pos;
    while (pos < value.size() &&
           !std::isspace(static_cast<unsigned char>(value[pos]))) {
      ++pos;
    }
    args.push_back(value.substr(start, pos - start));
  }
  return args;
}

std::string JoinArgs(const std::vector<std::string>& args, size_t start) {
  std::string result;
  for (size_t i = start; i < args.size(); ++i) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += args[i];
  }
  return result;
}

bool ParseUint64Arg(const std::string& text, uint64_t* out) {
  if (!out || text.empty()) {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || errno != 0) {
    return false;
  }
  *out = static_cast<uint64_t>(value);
  return true;
}

bool ParseLongArg(const std::string& text, long* out) {
  if (!out || text.empty()) {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || errno != 0) {
    return false;
  }
  *out = value;
  return true;
}

bool ParseDoubleArg(const std::string& text, double* out) {
  if (!out || text.empty()) {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || errno != 0) {
    return false;
  }
  *out = value;
  return true;
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

bool ContainsCaseInsensitive(const std::string& value, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  return ToLowerAscii(value).find(ToLowerAscii(needle)) != std::string::npos;
}

std::string Ellipsize(std::string value, size_t max_size) {
  if (value.size() <= max_size) {
    return value;
  }
  if (max_size <= 3) {
    value.resize(max_size);
    return value;
  }
  value.resize(max_size - 3);
  value += "...";
  return value;
}

const std::string& CompletionInsertText(const CompletionItem& item) {
  return item.insert_text.empty() ? item.name : item.insert_text;
}

bool IsWhitespaceOnly(const std::string& value) {
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isspace(c);
  });
}

bool IsOpenTabArg(const std::string& value) {
  const std::string lower = ToLowerAscii(value);
  return lower == "tab" || lower == "-t";
}

bool ArgsContainOpenTabArg(const std::string& value) {
  for (std::string arg : SplitArgs(value)) {
    if (IsOpenTabArg(arg)) {
      return true;
    }
  }
  return false;
}

bool ParseSearchEngineInvocation(const std::string& text,
                                 std::string* engine_out,
                                 std::string* query_out) {
  size_t engine_start = 0;
  while (engine_start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[engine_start]))) {
    ++engine_start;
  }
  if (engine_start >= text.size()) {
    return false;
  }

  size_t engine_end = engine_start;
  while (engine_end < text.size() &&
         !std::isspace(static_cast<unsigned char>(text[engine_end]))) {
    ++engine_end;
  }

  const std::string engine =
      ToLowerAscii(text.substr(engine_start, engine_end - engine_start));
  if (!FindSearchEngine(engine)) {
    return false;
  }

  if (engine_out) {
    *engine_out = engine;
  }
  if (query_out) {
    *query_out = Trim(text.substr(engine_end));
  }
  return true;
}

struct OpenAutocompleteContext {
  bool completing_new_arg = false;
  std::string arg_prefix;
  size_t arg_prefix_start = 0;
  bool already_has_tab_arg = false;
  std::string search_engine;
  std::string search_prefix;
  size_t search_prefix_start = 0;
  bool search_engine_token_is_current = false;
};

OpenAutocompleteContext AnalyzeOpenAutocompleteArgs(
    const std::string& after_command) {
  OpenAutocompleteContext context;
  const size_t arg_start = after_command.find_last_of(" \t");
  context.arg_prefix = arg_start == std::string::npos
                           ? after_command
                           : after_command.substr(arg_start + 1);
  context.arg_prefix_start = arg_start == std::string::npos ? 0 : arg_start + 1;
  const std::string completed_args = arg_start == std::string::npos
                                         ? ""
                                         : after_command.substr(0, arg_start + 1);
  context.already_has_tab_arg = ArgsContainOpenTabArg(completed_args);
  context.completing_new_arg = IsWhitespaceOnly(after_command) ||
                               (!after_command.empty() &&
                                std::isspace(static_cast<unsigned char>(
                                    after_command.back())));

  size_t token_start = 0;
  while (token_start < after_command.size() &&
         std::isspace(static_cast<unsigned char>(after_command[token_start]))) {
    ++token_start;
  }
  if (token_start >= after_command.size()) {
    return context;
  }
  size_t token_end = token_start;
  while (token_end < after_command.size() &&
         !std::isspace(static_cast<unsigned char>(after_command[token_end]))) {
    ++token_end;
  }

  const std::string first_token = after_command.substr(token_start,
                                                       token_end - token_start);
  if (IsOpenTabArg(first_token)) {
    context.already_has_tab_arg = true;
    token_start = token_end;
    while (token_start < after_command.size() &&
           std::isspace(static_cast<unsigned char>(after_command[token_start]))) {
      ++token_start;
    }
    if (token_start >= after_command.size()) {
      return context;
    }
    token_end = token_start;
    while (token_end < after_command.size() &&
           !std::isspace(static_cast<unsigned char>(after_command[token_end]))) {
      ++token_end;
    }
  }

  const std::string engine = ToLowerAscii(
      after_command.substr(token_start, token_end - token_start));
  if (!FindSearchEngine(engine)) {
    return context;
  }

  context.search_engine = engine;
  if (context.arg_prefix_start == token_start && !context.completing_new_arg) {
    context.search_engine_token_is_current = true;
    return context;
  }

  size_t query_start = token_end;
  while (query_start < after_command.size() &&
         std::isspace(static_cast<unsigned char>(after_command[query_start]))) {
    ++query_start;
  }
  context.search_prefix_start = query_start;
  context.search_prefix = after_command.substr(query_start);
  return context;
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

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0xf]);
          out.push_back(kHex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

void AppendJsonEscaped(std::string& out, std::string_view text) {
  const char* chunk_start = text.data();
  const char* const end = text.data() + text.size();
  for (const char* current = chunk_start; current != end; ++current) {
    const unsigned char c = static_cast<unsigned char>(*current);
    if (c != '\\' && c != '"' && c >= 0x20) {
      continue;
    }

    out.append(chunk_start, static_cast<size_t>(current - chunk_start));
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          static constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0xf]);
          out.push_back(kHex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
    chunk_start = current + 1;
  }
  out.append(chunk_start, static_cast<size_t>(end - chunk_start));
}

void AppendJsonString(std::string& out, std::string_view text) {
  out.push_back('"');
  AppendJsonEscaped(out, text);
  out.push_back('"');
}

template <typename Integer>
void AppendJsonNumber(std::string& out, Integer value) {
  char buffer[32];
  auto [ptr, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value);
  if (ec == std::errc()) {
    out.append(buffer, ptr);
  }
}

void AppendJsonNumber(std::string& out, double value) {
  char buffer[64];
  auto [ptr, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value);
  if (ec == std::errc()) {
    out.append(buffer, ptr);
  } else {
    out += "0";
  }
}

void AppendJsonBool(std::string& out, bool value) {
  if (value) {
    out.append("true", 4);
  } else {
    out.append("false", 5);
  }
}

void SetTabUrl(Tab& tab, std::string url) {
  tab.url = std::move(url);
  tab.url_json.clear();
  AppendJsonString(tab.url_json, tab.url);
}

void SetTabId(Tab& tab, uint64_t id) {
  tab.id = id;
  tab.id_json.clear();
  AppendJsonNumber(tab.id_json, tab.id);
}

bool IsValidRequestContextName(std::string_view name) {
  // Context names are also suffixes of profile directories below the CEF cache
  // root. Keep the accepted grammar deliberately smaller than a generic path.
  const auto is_lower = [](char c) { return c >= 'a' && c <= 'z'; };
  const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  if (name.empty() || name.size() > 48 ||
      (!is_lower(name.front()) && !is_digit(name.front()))) {
    return false;
  }
  return std::all_of(name.begin(), name.end(), [&](char c) {
    return is_lower(c) || is_digit(c) || c == '-' || c == '_';
  });
}

std::string IpcSocketPathForStatePath(const std::string& state_path) {
  std::filesystem::path dir = std::filesystem::path(state_path).parent_path();
  if (dir.empty()) {
    dir = "/tmp/vimbrowser";
  }
  return (dir / "ipc.sock").string();
}

std::string IpcVersionJson() {
  static const std::string kJson = [] {
    std::string out;
    out.reserve(48);
    out += "{\"protocol\":";
    AppendJsonString(out, kIpcProtocolName);
    out += ",\"version\":";
    AppendJsonNumber(out, kIpcProtocolVersion);
    out.push_back('}');
    return out;
  }();
  return kJson;
}

struct IpcCommandInfo {
  const char* name;
  const char* usage;
  const char* description;
  const char* response;
};

const std::vector<IpcCommandInfo>& IpcCommandList() {
  static const std::vector<IpcCommandInfo> commands = {
      {"version", "version", "protocol metadata", "json"},
      {"protocol", "protocol", "protocol metadata alias", "json"},
      {"status", "status", "active tab and app state", "json"},
      {"json", "json", "status alias", "json"},
      {"tabs", "tabs", "list all tabs with stable tab ids", "json"},
      {"commands", "commands", "machine-readable command metadata", "json"},
      {"folders", "folders", "list durable nested sidebar folders", "json"},
      {"folder-create", "folder-create <parent-folderid|0> <name>", "create a sidebar folder", "json"},
      {"folder-rename", "folder-rename <folderid> <name>", "rename a sidebar folder", "json"},
      {"folder-delete", "folder-delete <folderid> <recursive|unwrap>", "delete or unwrap a sidebar folder", "json"},
      {"folder-move", "folder-move <folderid> <parent-folderid|0>", "move a folder without reloading contained tabs", "json"},
      {"folder-pin", "folder-pin <folderid> [on|off]", "toggle or set a folder's pinned sidebar state", "json"},
      {"tab-folder", "tab-folder <tabid> <folderid|0>", "move a tab into a sidebar folder", "json"},
      {"tab-pin", "tab-pin <tabid> [on|off]", "toggle or set a tab's pinned sidebar state", "json"},
      {"sidebar", "sidebar", "inspect sidebar visibility, focus, selection, search, and displayed rows", "json"},
      {"sidebar-folder", "sidebar-folder <folderid|0>", "open a folder in the sidebar", "json"},
      {"sidebar-visibility", "sidebar-visibility <on|off|toggle>", "set or toggle sidebar visibility", "json"},
      {"sidebar-focus", "sidebar-focus [sidebar|web]", "focus the sidebar or web view", "json"},
      {"sidebar-select", "sidebar-select <tab|folder|parent> [id]", "select a sidebar item without activating it", "json"},
      {"sidebar-activate", "sidebar-activate", "activate the selected tab or enter the selected folder", "json"},
      {"sidebar-search", "sidebar-search <forward|backward> <query> | next [same|opposite|forward|backward] | clear", "set, navigate, or clear the global sidebar filter", "json"},
      {"tab-focus", "tab-focus <tabid>", "focus a tab by stable id", "json"},
      {"tab-delete", "tab-delete <tabid>", "delete a tab by stable id and destroy its backend", "json"},
      {"tab-order", "tab-order <tabid> <index>", "move tab to zero-based index", "json"},
      {"open-tab", "open-tab <url-or-query>", "open url/query in a new active tab", "json"},
      {"open-context-tab", "open-context-tab <context-name> <url-or-query>", "open a transient tab in a named persistent isolated request context", "json"},
      {"open", "open <tabid> <url-or-query>", "load url/query in an existing tab", "json"},
      {"reload", "reload [tabid]", "reload a tab", "json"},
      {"reload-ignore-cache", "reload-ignore-cache [tabid]", "hard reload a tab", "json"},
      {"back", "back [tabid]", "navigate tab back", "json"},
      {"forward", "forward [tabid]", "navigate tab forward", "json"},
      {"stop", "stop [tabid]", "stop tab loading", "json"},
      {"zoom", "zoom [tabid] <in|out|reset|level>", "run native tab zoom", "json"},
      {"scroll", "scroll <dy> [count]", "scroll active page", "json"},
      {"scroll-tab", "scroll-tab <tabid> <dy> [count]", "scroll a tab by stable id", "json"},
      {"html", "html <tabid>", "return current document HTML via native CEF frame source", "text/html"},
      {"text", "text <tabid>", "return current document text via native CEF frame text", "text/plain"},
      {"screenshot", "screenshot <tabid>", "capture a tab as a PNG without changing focus", "image/png;base64"},
      {"js", "js <tabid> <javascript>", "evaluate JavaScript in the tab renderer", "json"},
      {"js-file", "js-file <tabid> <path>", "evaluate JavaScript loaded from a file", "json"},
      {"cookies", "cookies <tabid> [url]", "list cookies visible to the tab URL or an explicit URL using the backend cookie manager", "json"},
      {"cookies-url", "cookies-url <url>", "list cookies visible to an explicit URL using the global backend cookie manager", "json"},
      {"cookie-delete", "cookie-delete <tabid> <name>", "delete a cookie visible to the tab URL", "json"},
      {"cookie-set", "cookie-set <tabid> <name> <value> [domain] [path]", "set a cookie for the tab URL", "json"},
      {"network", "network <tabid> list|detail|body|replay|clear [requestid]", "inspect, replay, or clear native captured network requests", "json/body"},
      {"fps", "fps", "active tab fps sample", "text/plain"},
      {"refresh", "refresh", "active tab compositor refresh rate", "text/plain"},
      {"url", "url", "active tab url", "text/plain"},
      {"showfps", "showfps [on|off]", "toggle/set fps overlay", "json"},
      {"showstatusline", "showstatusline [on|off]", "toggle/set bottom statusline", "json"},
      {"shader", "shader [on|off]", "toggle/set shader", "json"},
      {"tab", "tab <1-based-index>", "legacy focus by index", "json"},
      {"tab-close", "tab-close [tabid]", "legacy close active tab, or close tabid when provided", "json"},
      {"help", "help", "text command summary", "text/plain"},
  };
  return commands;
}

std::string IpcCommandsJson() {
  static const std::string kJson = [] {
    std::string out;
    out.reserve(4096);
    out += "{\"commands\":[";
    const auto& commands = IpcCommandList();
    for (size_t i = 0; i < commands.size(); ++i) {
      if (i) {
        out.push_back(',');
      }
      out += "{\"name\":";
      AppendJsonString(out, commands[i].name);
      out += ",\"usage\":";
      AppendJsonString(out, commands[i].usage);
      out += ",\"description\":";
      AppendJsonString(out, commands[i].description);
      out += ",\"response\":";
      AppendJsonString(out, commands[i].response);
      out.push_back('}');
    }
    out += "]}";
    return out;
  }();
  return kJson;
}

constexpr const char kJsEvalMessage[] = "__vimbrowser_ipc_js_eval__";
constexpr const char kJsResultMessage[] = "__vimbrowser_ipc_js_result__";
constexpr const char kFocusedEditableMessage[] =
    "__vimbrowser_focused_editable_changed__";
// Private CEF mouse-event modifier: the wheel point came from a native hint
// activation. The CEF Aura delegate uses this before event translation to route
// synthetic scroll gestures to the currently-focused renderer frame/guest
// instead of always to the root WebContents widget.
constexpr uint32_t kVimbrowserHintScrollTargetCefModifier = 1u << 29;
// Private CEF mouse-event modifier: Chromium masks this off as an unknown UI
// flag, but our CEF Aura delegate reads it before translation to decide whether
// synthetic smooth-scroll gestures should target the viewport or the element
// under the hinted coordinates.
constexpr uint32_t kVimbrowserScrollTargetElementCefModifier = 1u << 30;
// Private CEF mouse-event modifier: use the same synthetic gesture path as
// normal vimbrowser scrolling, but dispatch the full delta immediately instead
// of feeding it through the smooth-scroll animation accumulator/timer.
constexpr uint32_t kVimbrowserInstantScrollCefModifier = 1u << 31;

std::string ReadFileToString(const std::string& path, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) {
      *error = "ERR failed to open file\n";
    }
    return {};
  }
  std::ostringstream out;
  out << file.rdbuf();
  if (!file.good() && !file.eof()) {
    if (error) {
      *error = "ERR failed to read file\n";
    }
    return {};
  }
  return out.str();
}

std::string HeadersJson(const CefResponse::HeaderMap& headers) {
  std::ostringstream out;
  out << "[";
  size_t index = 0;
  for (const auto& [name, value] : headers) {
    if (index++) {
      out << ",";
    }
    out << "{\"name\":\"" << JsonEscape(name.ToString())
        << "\",\"value\":\"" << JsonEscape(value.ToString()) << "\"}";
  }
  out << "]";
  return out.str();
}

std::string SameSiteName(cef_cookie_same_site_t same_site) {
  switch (same_site) {
    case CEF_COOKIE_SAME_SITE_UNSPECIFIED: return "unspecified";
    case CEF_COOKIE_SAME_SITE_NO_RESTRICTION: return "none";
    case CEF_COOKIE_SAME_SITE_LAX_MODE: return "lax";
    case CEF_COOKIE_SAME_SITE_STRICT_MODE: return "strict";
    default: return "unknown";
  }
}

std::string CookieJson(const CefCookie& cookie) {
  std::ostringstream out;
  out << "{"
      << "\"name\":\"" << JsonEscape(CefString(&cookie.name).ToString()) << "\","
      << "\"value\":\"" << JsonEscape(CefString(&cookie.value).ToString()) << "\","
      << "\"domain\":\"" << JsonEscape(CefString(&cookie.domain).ToString()) << "\","
      << "\"path\":\"" << JsonEscape(CefString(&cookie.path).ToString()) << "\","
      << "\"secure\":" << (cookie.secure ? "true" : "false") << ","
      << "\"httponly\":" << (cookie.httponly ? "true" : "false") << ","
      << "\"same_site\":\"" << SameSiteName(cookie.same_site) << "\","
      << "\"creation\":" << cookie.creation.val << ","
      << "\"last_access\":" << cookie.last_access.val << ","
      << "\"has_expires\":" << (cookie.has_expires ? "true" : "false") << ","
      << "\"expires\":" << cookie.expires.val
      << "}";
  return out.str();
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

}  // namespace vimbrowser
