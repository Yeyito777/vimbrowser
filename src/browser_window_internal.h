#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "browser_window.h"
#include "include/cef_cookie.h"
#include "include/cef_response.h"
#include "include/views/cef_textfield.h"

namespace vimbrowser {

inline constexpr const char kIpcProtocolName[] = "vimbrowser-ipc";
inline constexpr int kIpcProtocolVersion = 1;
inline constexpr const char kShaderRefreshScript[] = R"JS(
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
inline constexpr const char kBlurActiveElementScript[] = R"JS(
(() => {
  const element = document.activeElement;
  if (element && element !== document.body &&
      element !== document.documentElement &&
      typeof element.blur === 'function') {
    element.blur();
  }
})();
)JS";
inline constexpr int kSidebarWidth = 175;
inline constexpr int kSidebarBorderWidth = 1;
inline constexpr int kSidebarContentWidth = kSidebarWidth - kSidebarBorderWidth;
inline constexpr int kCommandHeight = 28;
inline constexpr int kCommandAutocompleteRowHeight = 24;
inline constexpr int kCommandAutocompleteMaxVisible = 10;
inline constexpr int kCommandAutocompleteBorder = 0;
inline constexpr int kCommandAutocompleteHPadding = 8;
inline constexpr int kRootPanelId = 100;
inline constexpr int kMainPanelId = 101;
inline constexpr int kSidebarPanelId = 102;
inline constexpr int kContentPanelId = 103;
inline constexpr int kCommandPanelId = 104;
inline constexpr int kCommandSeparatorPanelId = 106;
inline constexpr int kCommandContentPanelId = 107;
inline constexpr int kSidebarContentPanelId = 108;
inline constexpr int kSidebarBorderPanelId = 109;
inline constexpr int kContentInnerPanelId = 110;
inline constexpr int kModeIndicatorPanelId = 111;
inline constexpr int kModeIndicatorFieldId = 112;
inline constexpr int kCommandAutocompletePanelId = 113;
inline constexpr int kCommandFieldId = 114;
inline constexpr int kSidebarSpacerId = 115;
inline constexpr int kFpsIndicatorPanelId = 116;
inline constexpr int kFpsIndicatorFieldId = 117;
inline constexpr int kStatusBarPanelId = 118;
inline constexpr int kStatusModeFieldId = 119;
inline constexpr int kStatusUrlFieldId = 120;
inline constexpr int kStatusContentPanelId = 121;
inline constexpr int kStatusOutputFieldId = 122;
inline constexpr int kStatusBorderPanelId = 123;
inline constexpr int kStatusSidebarSpacerPanelId = 124;
inline constexpr int kSidebarBorderOverlayPanelId = 125;
inline constexpr int kDevToolsBrowserViewId = 126;
inline constexpr int kDevToolsPanelId = 127;
inline constexpr int kDevToolsContentPanelId = 128;
inline constexpr int kMediaPermissionPromptPanelId = 129;
inline constexpr int kMediaPermissionPromptContentPanelId = 130;
inline constexpr int kMediaPermissionTitleFieldId = 131;
inline constexpr int kMediaPermissionOriginFieldId = 132;
inline constexpr int kMediaPermissionBodyFieldId = 133;
inline constexpr int kMediaPermissionHintFieldId = 134;
inline constexpr int kMediaPermissionAllowButtonId = 135;
inline constexpr int kMediaPermissionDenyButtonId = 136;
inline constexpr int kMediaPermissionButtonPanelId = 137;
inline constexpr int kMediaPermissionBorderTopPanelId = 138;
inline constexpr int kMediaPermissionBorderBottomPanelId = 139;
inline constexpr int kMediaPermissionBorderLeftPanelId = 140;
inline constexpr int kMediaPermissionBorderRightPanelId = 141;
inline constexpr int kContextMenuBackdropButtonId = 142;
inline constexpr int kContextMenuPanelId = 143;
inline constexpr int kA26ChromePanelId = 144;
inline constexpr int kA26NavigationPanelId = 145;
inline constexpr int kA26BottomReservePanelId = 146;
inline constexpr int kA26BackButtonId = 147;
inline constexpr int kA26ForwardButtonId = 148;
inline constexpr int kA26UrlFieldId = 149;
inline constexpr int kA26ReloadButtonId = 150;
inline constexpr int kA26TabsButtonId = 151;
inline constexpr int kAcceleratorCommandTab = 5000;
inline constexpr int kAcceleratorCommandBacktab = 5001;
inline constexpr int kAcceleratorTabNext = 5002;
inline constexpr int kAcceleratorTabPrevious = 5003;
inline constexpr int kAcceleratorSidebarSpace = 5004;
inline constexpr int kAcceleratorHintRightClick = 5005;
inline constexpr int kAcceleratorHintHover = 5006;
inline constexpr int kAcceleratorFocusNext = 5007;
inline constexpr int kAcceleratorFocusPrevious = 5008;
inline constexpr int kAcceleratorToggleDevToolsSemicolon = 5009;
inline constexpr int kAcceleratorToggleDevToolsOem1 = 5010;
inline constexpr int kAcceleratorCommandDeleteCompletion = 5011;
inline constexpr int kSidebarRowBaseId = 2000;
inline constexpr int kAutocompleteRowBaseId = 6000;
inline constexpr int kContextMenuRowBaseId = 7000;
inline constexpr int kSidebarRowHeight = 24;
inline constexpr size_t kSidebarMaxRenderedRows = 96;
inline constexpr bool kModeIndicatorEnabled = true;
inline constexpr int kModeIndicatorWidth = 96;
inline constexpr int kModeIndicatorHeight = 24;
inline constexpr int kStatusBarHeight = 16;
inline constexpr int kStatusModeWidth = 64;
inline constexpr int kCommandTextInsetX = 0;
inline constexpr int kCommandCharWidth = 8;
inline constexpr int kLineScrollPx = 280;
inline constexpr int kSmallScrollPx = 140;
inline constexpr int kDevToolsBorderWidth = 1;
inline constexpr int kDevToolsDefaultWidthPercent = 40;
inline constexpr int kDevToolsMinWidth = 420;
inline constexpr int kDevToolsMinPageWidth = 240;
inline constexpr int kMediaPermissionPromptWidth = 640;
inline constexpr int kMediaPermissionPromptHeight = 124;
inline constexpr int kMediaPermissionPromptBorderWidth = 1;
inline constexpr int kContextMenuMinWidth = 260;
inline constexpr int kContextMenuMaxWidth = 520;
inline constexpr int kContextMenuRowHeight = 24;
inline constexpr int kContextMenuBorderWidth = 1;
inline constexpr int kContextMenuHPadding = 10;
inline constexpr int kContextMenuMaxVisibleRows = 18;
// CEF Views sizes are device-independent logical coordinates. At the A26
// launcher's 2.5 device scale these provide >=120px touch targets while keeping
// Moon's final 180 physical pixels free for its bottom-edge close gesture.
inline constexpr int kA26NavigationHeight = 64;
inline constexpr int kA26BottomReserveHeight = 72;
inline constexpr int kA26ChromeHeight =
    kA26NavigationHeight + kA26BottomReserveHeight;
inline constexpr int kA26TouchControlHeight = 52;
inline constexpr int kA26HistoryButtonWidth = 64;
inline constexpr int kA26ReloadButtonWidth = 60;
inline constexpr int kA26TabsButtonWidth = 76;
inline constexpr size_t kLazyRestoreBackgroundTabThreshold = 8;
inline constexpr int kVirtualSidebarRefreshDelayMs = 250;
inline constexpr int kTabContentActivationDelayMs = 0;
inline constexpr int kTabStateSaveDelayMs = 250;
inline constexpr int kSidebarDeleteConfirmationMs = 3000;
inline constexpr size_t kOpenHistoryCompletionNameMax = 140;
inline constexpr size_t kTabFocusCompletionDescriptionMax = 140;
inline constexpr size_t kNoTabIndex = std::numeric_limits<size_t>::max();
inline constexpr const char kJsEvalMessage[] = "__vimbrowser_ipc_js_eval__";
inline constexpr const char kJsResultMessage[] = "__vimbrowser_ipc_js_result__";
inline constexpr const char kFocusedEditableMessage[] =
    "__vimbrowser_focused_editable_changed__";
inline constexpr uint32_t kVimbrowserHintScrollTargetCefModifier = 1u << 29;
inline constexpr uint32_t kVimbrowserScrollTargetElementCefModifier = 1u << 30;
inline constexpr uint32_t kVimbrowserInstantScrollCefModifier = 1u << 31;

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

size_t IndexAfterVectorMove(size_t index, size_t from, size_t to);
bool InIdRange(int id, int base, int count);
void StyleTextfield(CefRefPtr<CefTextfield> field,
                    cef_color_t text,
                    cef_color_t background,
                    const CefString& font = "monospace, 13px");
void StyleCommandField(CefRefPtr<CefTextfield> field);
const std::vector<CompletionItem>& CommandList();
const std::vector<CompletionItem>& OnOffArgList();
const std::vector<CompletionItem>& OpenArgList();
const std::vector<CompletionItem>& TestArgList();
bool CommandTakesArguments(const std::string& command);
bool IsRawKeyDown(const CefKeyEvent& event);
bool IsCharEvent(const CefKeyEvent& event);
bool IsPrintableAscii(char16_t c);
bool IsPlain(const CefKeyEvent& event);
bool HasOnlyControlModifier(const CefKeyEvent& event);
bool IsSpaceKey(const CefKeyEvent& event);
bool IsPlainPrintableKey(const CefKeyEvent& event);
bool IsPlainLetterKey(const CefKeyEvent& event, char key);
char LowerAsciiChar(char c);
char PlainKeyChar(const CefKeyEvent& event);
bool IsEnterKey(const CefKeyEvent& event);
bool IsEscapeKey(const CefKeyEvent& event);
bool IsBackspaceKey(const CefKeyEvent& event);
bool IsTabKey(const CefKeyEvent& event);
bool IsDeleteKey(const CefKeyEvent& event);
bool IsNavigationEditingKey(const CefKeyEvent& event);
bool IsCtrlKey(const CefKeyEvent& event, char key);
bool IsCtrlSemicolonKey(const CefKeyEvent& event);
bool IsCommonCtrlEditingKey(const CefKeyEvent& event);
bool ShouldForwardFocusedEditableKey(const CefKeyEvent& event,
                                     bool focus_on_editable_field);
std::string Trim(std::string value);
std::string ToLowerAscii(std::string value);
bool IsValidRequestContextName(std::string_view name);
std::vector<std::string> SplitArgs(const std::string& value);
std::string JoinArgs(const std::vector<std::string>& args, size_t start);
bool ParseUint64Arg(const std::string& text, uint64_t* out);
bool ParseLongArg(const std::string& text, long* out);
bool ParseDoubleArg(const std::string& text, double* out);
bool StartsWithCaseInsensitive(const std::string& value,
                               const std::string& prefix);
bool ContainsCaseInsensitive(const std::string& value,
                             const std::string& needle);
std::string Ellipsize(std::string value, size_t max_size);
const std::string& CompletionInsertText(const CompletionItem& item);
bool IsWhitespaceOnly(const std::string& value);
bool IsOpenTabArg(const std::string& value);
bool ArgsContainOpenTabArg(const std::string& value);
bool ParseSearchEngineInvocation(const std::string& text,
                                 std::string* engine_out,
                                 std::string* query_out);
OpenAutocompleteContext AnalyzeOpenAutocompleteArgs(
    const std::string& after_command);
bool IsTokenBoundary(const std::string& value, size_t pos);
int TextColumns(const std::string& value);
std::string ShellRead(const char* command);
bool ShellWrite(const char* command, const std::string& text);
std::string ReadClipboardText();
std::string JsonEscape(std::string_view text);
void AppendJsonEscaped(std::string& out, std::string_view text);
void AppendJsonString(std::string& out, std::string_view text);

template <typename Integer>
void AppendJsonNumber(std::string& out, Integer value) {
  char buffer[32];
  auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (ec == std::errc()) {
    out.append(buffer, ptr);
  }
}

void AppendJsonNumber(std::string& out, double value);
void AppendJsonBool(std::string& out, bool value);
void SetTabUrl(Tab& tab, std::string url);
void SetTabId(Tab& tab, uint64_t id);
std::string IpcSocketPathForStatePath(const std::string& state_path);
std::string IpcVersionJson();
std::string IpcCommandsJson();
std::string ReadRegularFileToString(const std::string& path,
                                    size_t max_bytes,
                                    std::string* error);
std::string HeadersJson(const CefResponse::HeaderMap& headers);
std::string SameSiteName(cef_cookie_same_site_t same_site);
std::string CookieJson(const CefCookie& cookie);
void WriteClipboardText(const std::string& text);

}  // namespace vimbrowser
