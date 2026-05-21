# Website mode keybind implementation checklist

Approved website-mode keybinds to port from the user's qutebrowser config/defaults.

## Scrolling / page movement

- [x] `j` — scroll down 280px
- [x] `k` — scroll up 280px
- [x] `Ctrl-e` — scroll down 140px
- [x] `Ctrl-y` — scroll up 140px
- [x] `Ctrl-d` — scroll down 560px
- [x] `Ctrl-u` — scroll up 560px
- [x] `Ctrl-f` — scroll down 1120px
- [x] `Ctrl-b` — scroll up 1120px
- [x] `gg` — scroll to top
- [x] `G` — scroll to bottom

## History / reload

- [x] `H` — history back
- [x] `L` — history forward
- [x] `r` — reload
- [x] `R` — hard reload / bypass cache

## Open / clipboard

- [x] `p` — open clipboard in current tab
- [x] `P` — open clipboard in new tab

## Hints

- [x] `f` — native backend link/control hints; activate in current tab
- [x] `F` / `Shift-f` — native backend link/control hints; open links in a new tab immediately after the active tab
- [x] `Ctrl-Space` — native backend scrollable-container hints; focus selected
  scroll target for subsequent scroll commands

## Tab selection / tab control

- [x] `Ctrl-1` — focus tab 1
- [x] `Ctrl-2` — focus tab 2
- [x] `Ctrl-3` — focus tab 3
- [x] `Ctrl-4` — focus tab 4
- [x] `Ctrl-5` — focus tab 5
- [x] `Ctrl-6` — focus tab 6
- [x] `Ctrl-7` — focus tab 7
- [x] `Ctrl-8` — focus tab 8
- [x] `Ctrl-9` — focus tab 9
- [x] `J` — next tab
- [x] `K` — previous tab
- [x] `d` — close tab
- [x] `u` — undo closed tab
- [x] `g0` — first tab
- [x] `g$` — last tab
- [x] `e` — move tab up / previous position
- [x] `E` — move tab down / next position
- [x] `c` — clone current tab
- [x] `t` — command line prefilled with `:tab-focus `

## Zoom

- [x] `=` — zoom in
- [x] `-` — zoom out
- [x] `)` — zoom reset

## Yank / copy

- [x] `yy` — copy current URL
- [x] `yt` — copy title
- [x] `ym` — copy markdown link: `[title](url)`
- [x] `Ctrl-Shift-Y` — yank DOM

## Already implemented / keep

- [x] `:` — command line prefilled with `:`
- [x] `o` — command line prefilled with `:open `
- [x] `O` — command line prefilled with `:open tab `
- [x] `:open yt <query>` / `:open tab yt <query>` — search YouTube with a
  shared per-engine query history
- [x] `:open gh <query>` / `:open tab gh <query>` — search GitHub with a shared
  per-engine query history

## Explicitly not part of this batch

- [ ] shader toggle
- [ ] advanced hint modes beyond basic click/open-in-new-tab hints
- [ ] `Ctrl-M`
- [ ] `Ctrl-N`
- [ ] `ac`
