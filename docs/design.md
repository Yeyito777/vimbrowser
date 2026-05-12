# vimbrowser design

vimbrowser should feel like a native graphical extension of the Exocortex TUI:
terminal-esque, dark, crisp, keyboard-first, and never visually flashbangy.

This document is the source of truth for vimbrowser UI styling. When styling
changes conflict with implementation convenience, prefer this document.

## Source theme

Use the **whale** theme from:

```text
~/Workspace/exocortex/tui/src/themes/whale.ts
```

Colors should be used for the same semantic purpose as in the TUI. Do not invent
near-matches unless a platform API cannot represent the exact value.

## Palette

| Semantic name | Hex | Source whale token | Intended vimbrowser use |
| --- | --- | --- | --- |
| app background | `#00050f` | `appBg` | Whole app/root background; any otherwise-empty browser chrome area |
| topbar background / primary accent background | `#1d9bf0` | `topbarBg`, `accent` | Active top/status strip, strong primary accent surfaces |
| accent foreground | `#1d9bf0` | `accent` | Active markers, separators, important focused UI affordances |
| primary text | `#ffffff` | `text` | All normal non-dimmed text |
| muted text | `#646464` | `muted` | Inactive/secondary labels, disabled-ish text, placeholders |
| command text | `#aed6fe` | `command` | Valid commands and command names in the command line |
| normal-mode color | `#48cae4` | `vimNormal`, `cursorBg`, `cursorColor` | Normal mode indicator, caret/cursor emphasis, current-mode glyphs |
| insert-mode color | `#2ec4b6` | `vimInsert` | Future insert-mode indicator |
| visual-mode color | `#c792ea` | `vimVisual` | Future visual-mode indicator/selection mode marker |
| terminal cursor | `#48cae4` | `cursorColor`, st color 258 | Vim cursor glyph/bar color |
| terminal reverse cursor text | `#f1faee` | st color 259 | Text/foreground used by the terminal's reverse cursor; use only where a native reverse cursor can be implemented cleanly |
| user/bubble background | `#090d35` | `userBg`, `historyLineBg` | Elevated/content-ish dark panel surfaces when needed |
| sidebar background | `#030814` | `sidebarBg` | Left tab sidebar body |
| sidebar selected background | `#0f193c` | `sidebarSelBg` | Active tab row background |
| selection background | `#4f5258` | `selectionBg` | Text selections / selected ranges |
| search highlight background | `#fce094` | `searchBg` | Future search match highlight background |
| search highlight foreground | `#00050f` | `searchFg` | Text on search highlight |
| success | `#50c878` | `success` | Success/connected indicators |
| focused border | `#1c94e5` | `borderFocused` | Focused panel/command line border if borders are used |
| unfocused border | `#555555` | `borderUnfocused` | Unfocused separators/borders |

The whale theme uses ANSI default red/yellow/blue/magenta for some tokens:

| Semantic name | Source token | Vimbrowser rule |
| --- | --- | --- |
| error | `error` / ANSI red | Use for destructive/error states only |
| warning | `warning` / ANSI yellow | Use for warnings/loading/retry states only |
| prompt | `prompt` / ANSI blue | Use only if we intentionally mirror the TUI prompt token; prefer `accent` for browser chrome prompts unless there is a reason not to |
| tool | `tool` / ANSI magenta | Reserve for future tool/CDP/devtools labels |

## Global rules

1. **No default white/near-white surfaces.**
   - Uncolored CEF/Views areas are bugs.
   - Any app chrome, sidebar, command line, status area, empty panel, overlay,
     or placeholder surface must explicitly use a whale background color.
   - `#ffffff` is for foreground text only, never large backgrounds.

2. **Non-dimmed text is always `#ffffff`.**
   - If text is normal/primary/readable, use `#ffffff`.
   - Do not use off-white grays for primary text.
   - Use `#646464` only when the text is intentionally muted.

3. **No border rounding, ever.**
   - Corners are square.
   - No rounded command line, tab rows, overlays, popups, badges, or buttons.
   - If an API applies rounded corners by default, disable it or avoid that
     widget/style.

4. **Terminal-esque, not desktop-widget-esque.**
   - Prefer flat color blocks, crisp separators, monospace text, compact rows,
     and direct state indication.
   - Avoid gradients, shadows, glass, animations-for-flair, pill buttons, and
     native toolkit-looking decoration.

5. **Same color, same purpose.**
   - Sidebar background means `#030814` everywhere.
   - Selected sidebar row means `#0f193c` everywhere.
   - Focused border/accent means `#1c94e5`/`#1d9bf0`, not arbitrary blues.

6. **Every pixel of browser chrome is intentionally colored.**
   - The web page can be whatever the web page is.
   - vimbrowser chrome must never leave default toolkit background showing.

## Current UI mapping

### Left tab sidebar

- Body background: `sidebarBg` / `#030814`.
- Active tab row background: `sidebarSelBg` / `#0f193c`.
- Active tab marker (`▸` or future equivalent): `accent` / `#1d9bf0` or
  `vimNormal` / `#48cae4` if it specifically represents normal-mode focus.
- Active tab text: `#ffffff` unless deliberately muted.
- Inactive tab text: `#ffffff` for the title/domain; optional inactive metadata
  may use `muted` / `#646464`.
- Row height should stay compact and terminal-like.

### Command line

- Background: `appBg` / `#00050f` or `sidebarBg` / `#030814` depending on
  surrounding composition. Prefer `appBg` for a bottom full-width command line.
- The bottom command area appears only when a command/prompt/mode needs it. It
  is not a normal-mode informational status line.
- When visible, the command area overlays the page bottom. It must not resize,
  reflow, push, or otherwise shift web content.
- Focus border/separator: `borderFocused` / `#1c94e5`, square corners only.
- Prompt/command prefix (`open`, `open -t`): `command` / `#aed6fe` or `accent`
  if acting as a prompt marker.
- User-entered URL/search text: `#ffffff`.
- Placeholder text: `muted` / `#646464`.
- Selection: `selectionBg` / `#4f5258` with `#ffffff` text unless platform
  constraints require another high-contrast pairing.

### Cursor

Match the cursor behavior from `~/Config/st` where possible:

- Cursor color: `#48cae4` (`defaultcs`, st color 258).
- Bar/underline cursor thickness in st is `2px`; vimbrowser bar cursors should
  read as a thin terminal caret, not a wide widget caret.
- Vim normal mode uses a steady block cursor (`cursorshape = 2`, DECSCUSR
  `\e[2 q`).
- Vim insert mode uses a steady bar cursor (DECSCUSR `\e[6 q`).
- The cursor does not blink.
- The cursor is square/terminal-like: no rounded caret, no native toolkit glow.
- When an implementation can draw a true reverse-video block, use cursor
  background `#48cae4` with text/background reversed in the spirit of st. If a
  toolkit text widget cannot draw per-cell backgrounds, approximate with an
  explicit `#48cae4` block glyph in normal mode and a thin `#48cae4` bar glyph in
  insert mode.
- If the vimbrowser window/surface is unfocused in the future, follow st's
  unfocused convention: show an outline/hollow cursor rather than a filled one.

### Root/app background

- Root view/background: `appBg` / `#00050f`.
- Empty sidebar/content gaps: `appBg` or `sidebarBg` as semantically appropriate.
- Never allow CEF Views default light gray/white to show in browser chrome.

### Mode indicators

- Normal: `vimNormal` / `#48cae4`.
- Insert: `vimInsert` / `#2ec4b6`.
- Visual: `vimVisual` / `#c792ea`.

Mode indicators should be small and terminal-like, not badges/pills with rounded
edges.

## Implementation notes

- Keep colors centralized in code, preferably a small `theme` module/struct that
  mirrors these semantic names.
- Avoid styling by raw hex at call sites once the theme module exists.
- When CEF widgets have native styling that conflicts with this document, prefer
  custom-painted/simple widgets or different CEF Views primitives.
- Screenshots should be checked for accidental `#ffffff`/near-white chrome areas.
  The page content may be light; vimbrowser-owned chrome may not be.
