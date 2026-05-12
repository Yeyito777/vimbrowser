# Experimental focus model

Status: experimental/uncommitted. This is a working note for the next
vimbrowser focus pass.

## Exocortex TUI focus model observed

Relevant files in `~/Workspace/exocortex/tui/src`:

- `focus.ts`
- `state.ts`
- `render.ts`
- `sidebar/render.ts`
- `themes/whale.ts`

The TUI has an explicit focus state instead of treating all keybinds as global:

```ts
export type PanelFocus = "sidebar" | "chat";
export type ChatFocus = "prompt" | "history";
```

`RenderState` stores both:

```ts
panelFocus: PanelFocus;
chatFocus: ChatFocus;
vim: VimState;
```

Focus transitions are centralized in `state.ts`:

```ts
focusPrompt(state): panelFocus="chat", chatFocus="prompt", vim.mode="insert"
focusHistory(state): panelFocus="chat", chatFocus="history", vim.mode="normal"
focusSidebar(state): panelFocus="sidebar", vim.mode="normal"
```

Top-level key handling is routed through `focus.ts`:

1. Hard global escapes/actions first, e.g. Ctrl-C, Ctrl-A.
2. Modal/prompt/search interceptors if open.
3. Global navigation/focus-cycle actions.
4. Vim engine / focused panel routing.
5. `panelFocus === "sidebar"` routes sidebar keys to `sidebar.ts`.
6. `panelFocus === "chat"` routes chat keys to `chat.ts`, which then uses
   `chatFocus` to route prompt vs history behavior.

The visual system mirrors focus:

- `sidebar/render.ts` receives `focused: boolean`.
- Sidebar right border uses:
  - focused: `theme.borderFocused` (`#1c94e5`)
  - unfocused: `theme.borderUnfocused` (`#555555`)
- Chat/history separator uses `theme.accent` when focused and `theme.dim` when
  not focused.
- Prompt glyph uses `theme.accent` when focused and `theme.dim` when not
  focused.
- Selected/active content can remain visible, but unfocused panel chrome is
  dimmed/gray.

Important distinction: selection/active item is not identical to focus.
A sidebar row can be selected/current, but the sidebar panel itself may still be
focused or unfocused. The focus border/glyph communicates keyboard ownership.

## vimbrowser target model

vimbrowser should adopt a similar explicit focus state.

Suggested focus enum:

```cpp
enum class FocusArea {
  kTabSidebar,
  kWebView,
  kCommandLine,
};
```

Suggested future web-mode enum, separate from focus:

```cpp
enum class WebMode {
  kWebsiteNormal, // default: qutebrowser-like page mode for hints/scrolling
  kNormal,        // regular Vim normal mode; future operators/text objects
  kInsert,        // page receives regular typing/input
  kVisual,        // future visual selection/operator mode
};
```

Initial experimental behavior:

- Startup focus: likely `kWebView` or `kTabSidebar` depending on desired default
  UX. See questions below.
- `kTabSidebar`:
  - Sidebar owns tab-navigation keybinds.
  - Current normal-mode tab keybinds move here:
    - `Shift+j` / `Shift+k` tab switching
    - possibly `o` / `O`, depending on desired command access policy
  - Sidebar border is focused/accented.
  - Web view and command line are unfocused.
- `kCommandLine`:
  - Command line owns all command typing.
  - When command line opens, focus becomes `kCommandLine`.
  - Enter sends command and clears/hides command line.
  - Escape clears/hides command line.
  - After close, focus returns to the previous focus area.
  - Top border/outline uses focused accent color while focused.
- `kWebView`:
  - Has its own web-mode state machine.
  - Default mode is website-normal.
  - Website-normal is the future home for hints, scrolling, and page commands.
  - `i` / `a` in website-normal enter insert mode.
  - Escape in insert mode enters regular Vim normal mode.
  - Escape in regular Vim normal or visual mode returns to website-normal.
  - Regular Vim normal and visual modes are skeleton states for now: they swallow
    plain printable keys but do not perform edits/operators yet.
  - Insert mode lets the page receive ordinary typing/input. We do not auto-enter
    insert mode when clicking editable fields in this lightweight pass.
  - The web view does not get an explicit focus outline.
  - Sidebar border becomes unfocused gray, but sidebar text and active marker do
    not dim solely because focus moved elsewhere.

## Visual rules

Use whale theme semantics exactly:

- Focused outline/border: `theme::kBorderFocused` / `#1c94e5`.
- Existing strong accent separators may remain `theme::kAccent` / `#1d9bf0`
  only if they are not representing focus. For focus ownership, prefer
  `kBorderFocused` to match Exocortex.
- Unfocused outline/border: `theme::kBorderUnfocused` / `#555555`.
- Dim/inactive text: `theme::kMuted` / `#646464`.
- Primary non-dimmed text: `theme::kText` / `#ffffff`.
- No rounded borders.
- No default light CEF chrome.

Concrete vimbrowser surfaces:

### Tab sidebar

- Current dedicated right border should become focus-aware:
  - `kTabSidebar` focused: `#1c94e5`.
  - not focused: `#555555`.
- Active tab row can stay `#0f193c` so selected tab remains visible.
- Active tab marker stays `#48cae4` even when sidebar focus moves elsewhere.
- Tab titles:
  - selected/current important text should stay `#ffffff`.
  - do not dim all sidebar text just because the sidebar lost focus.

### Web view

Do not draw an explicit focus outline around the web view. It is obvious enough
from the sidebar/command focus chrome, and page-adjacent outlines add visual
noise around arbitrary website content. The actual page content remains
untouched.

### Command line

Command line currently appears only when active and overlays page bottom. Keep
that behavior.

When open:

- focus becomes `kCommandLine`.
- command row starts in insert mode and displays a terminal-style block cursor.
- `Escape` from command insert mode switches to command normal mode without
  closing the command line.
- `Escape` from command normal mode closes/cancels the command line.
- `i` / `a` from command normal mode re-enter command insert mode.
- `h` / `l` move the command cursor in command normal mode.
- `x` deletes at the command cursor in command normal mode.
- `Enter` sends the command from either command insert or normal mode.
- command row top border should be focus color `#1c94e5`.
- command text uses existing command styling:
  - `:open` / `:open tab`: `#aed6fe`
  - typed URL/search: `#ffffff`
- On Enter/Escape:
  - clear command text
  - hide command overlay
  - restore previous focus area

## Key routing plan

Current state has `Mode` doing both command-mode and normal-mode jobs. Proposed
split:

```cpp
enum class FocusArea { kTabSidebar, kWebView, kCommandLine };
enum class CommandMode { kNone, kOpenCurrent, kOpenNext };
```

Then route keys like Exocortex:

1. Hard globals first:
   - `Ctrl+Shift+I` DevTools should likely remain global.
2. If `focus_area_ == kCommandLine`:
   - route to command-line key handler only.
3. If `focus_area_ == kTabSidebar`:
   - route sidebar/tab keys only.
4. If `focus_area_ == kWebView`:
   - route through the web-mode state machine first.
   - insert mode passes normal input through to CEF.
   - website-normal/regular-normal/visual currently swallow plain printable keys
     unless a future binding claims them.

This means the current normal-mode tab keybinds should stop being page-global.
They should only work when `focus_area_ == kTabSidebar`.

## Open implementation questions

Answered for the first experimental pass:

1. Startup focus: web view.
2. Focus switching: `Ctrl+j` / `Ctrl+k`, matching Exocortex's focus-cycle
   binding style.
3. `o` / `O`: only active when the tab sidebar is focused.
4. Unfocused active tab marker `>`: keep `#48cae4`; do not dim sidebar text/marker just because the sidebar lost focus.
5. Web-view focus outline: do not draw one; it is obvious enough from sidebar
   and command-line focus chrome.

Remaining notes/questions after these answers:

- `Ctrl+m` toggles the tab sidebar. Opening the sidebar focuses it; hiding the
  sidebar returns focus to the web view.
- The current experimental code uses focus-cycle behavior for both `Ctrl+j` and
  `Ctrl+k` because there are only two non-command focus targets so far.
- Website-view mode skeleton:
  - startup/default: website-normal
  - website-normal `i`/`a`: insert
  - insert `Escape`: regular Vim normal
  - regular Vim normal/visual `Escape`: website-normal
  - visual/operators are intentionally only wired as future enum states for now.
- `src/vim.h` / `src/vim.cc` now hold a small shared Vim-mode/line-edit module
  so command line and future Vim surfaces can share mode names, cursor movement,
  insert/normal transitions, and basic edit operations instead of each view
  growing separate ad-hoc Vim state.

Original questions:

1. What should startup focus be: tab sidebar or web view?
2. Which key should move focus between web view and tab sidebar? Should it mirror
   Exocortex focus-cycle, e.g. `Ctrl+W`, `Ctrl+Tab`, or something else?
3. Should `o` / `O` be available only when the tab sidebar is focused, or should
   opening command line remain a hard global action?
4. When sidebar is unfocused, should the active tab marker `>` remain `#48cae4`,
   or should it dim to `#646464` while the selected row remains visible?
5. Should the web-view focus outline be visible all the time when focused, or
   only when focus is ambiguous / sidebar is open?
