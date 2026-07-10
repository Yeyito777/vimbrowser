# vimbrowser

Fresh, minimal CEF-based vim browser.

Goals:

- lightweight native shell
- Chromium/CEF backend for modern web compatibility
- Chrome DevTools Protocol via CEF remote debugging
- no Qt, no QtWebEngine, no qutebrowser dependency
- minimal C++ source, built directly against CEF headers/wrapper

## Build

```bash
make build
make install
```

The legacy quick build downloads the Linux x86_64 CEF **minimal** binary/source
distribution into `third_party/cef/` and builds only the small C++ shell plus the
CEF wrapper library.

For the real vimbrowser backend, use the source Chromium/CEF tree under
`backend/chromium/`:

```bash
make bootstrap-chromium
make build-chromium-cef
make source-distrib
make build-source
make install-source
```

After that first full source distribution exists, use the fast backend edit loop:

```bash
make backend-dev
```

`backend-dev` keeps Chromium's incremental build cache in
`backend/chromium/out/Release_GN_x64`, rebuilds `libcef`, syncs the changed
runtime artifacts into the existing CEF binary distribution, strips release ELF
payload, prunes locale packs to the English runtime set, rebuilds the small
vimbrowser shell, and reinstalls `~/.local/bin/vimbrowser`. Do not delete
`backend/chromium/out/Release_GN_x64` unless you intentionally want to pay for a
full Chromium rebuild again. Use `make source-distrib` only for a fresh package
or major CEF distribution layout/API changes.

The local CEF runtime is slimmed by `scripts/slim-cef-runtime.sh`: final release
ELF binaries are stripped and only `en-US`/`en-GB` locale packs are kept by
default. Set `VIMBROWSER_KEEP_LOCALES="en-US,es,..."` before the build if you
want additional Chromium UI locales in the generated runtime.

For fast visual mockups that do not need a C++/Chromium rebuild, there is also a
small Vite lab under `frontend/`:

```bash
make vite-install
make vite-dev
vimbrowser http://127.0.0.1:5173
```

This is only an iteration aid for chrome layout/colors. The production browser
chrome remains native C++/CEF Views and Chromium backend code.

The running browser exposes the canonical local Unix-socket IPC endpoint for
scripts, tests, automation, and diagnostics. Prefer extending this protocol for
app control instead of adding one-off debug paths:

```bash
scripts/vimbrowser-ipc status
scripts/vimbrowser-ipc version
scripts/vimbrowser-ipc tabs
scripts/vimbrowser-ipc sidebar
scripts/vimbrowser-ipc sidebar-select tab 3
scripts/vimbrowser-ipc tab-focus 3
scripts/vimbrowser-ipc html 3
scripts/vimbrowser-ipc screenshot 3 > tab.png
scripts/vimbrowser-ipc js 3 'document.title'
scripts/vimbrowser-ipc network 3 list
scripts/vimbrowser-ipc fps
scripts/vimbrowser-ipc scroll 280
```

With `--profile-dir DIR`, the socket lives at `DIR/ipc.sock`; the installed
wrapper profile is auto-detected by the built `vimbrowser-ipc` client and the
source `scripts/vimbrowser-ipc` fallback. Set
`VIMBROWSER_PROFILE_DIR=DIR` or `VIMBROWSER_IPC=/path/to/ipc.sock` to target a
separate test instance.

See [`docs/ipc.md`](docs/ipc.md) for protocol framing, command semantics, and
compatibility rules. IPC now has stable tab IDs (separate from reorderable tab
indexes), ID-based tab focus/delete/order/open commands, complete folder/sidebar
inspection and control, native HTML/text/JS, backend tab screenshots, backend
cookie inspection/mutation, and per-tab native network capture/replay.

Performance tracking lives in [`docs/benchmarks.md`](docs/benchmarks.md). Run
`make benchmark` for the deterministic local regression suite,
`make benchmark-live` for YouTube/GitHub/Discord/Reddit page-load tracking, or
`scripts/vimbrowser-benchmark --suite all --output bench.json` for a combined
machine-readable run.

That backend is pinned to Chromium `147.0.7727.118` / CEF
`d58e84d17dd3f646c906ac633156cd0ec46638e9`. Chromium and CEF source now live
directly in the main vimbrowser git repository under `backend/chromium/` with
their nested upstream `.git` history removed. Edit Chromium files directly and
commit them normally; there is no exported Chromium patch file or submodule.

After Chromium/CEF builds, `make source-distrib` creates a minimal CEF binary
distribution from the patched source tree, and `make build-source` points
vimbrowser at the newest generated source distribution automatically. The manual
equivalent is:

```bash
cd backend/chromium
PATH=$PWD/../depot_tools:$PATH autoninja -C out/Release_GN_x64 chrome_sandbox
cd cef/tools
./make_distrib.sh --ninja-build --x64-build --minimal --allow-partial --no-archive --output-dir ../binary_distrib

cd ../../../
CEF_ROOT=$PWD/backend/chromium/cef/binary_distrib/<generated-cef-binary-dir> make build-source install-source
```

The current source-backend build is intentionally native/hardcore: CEF's patch
stack has already been folded into `backend/chromium`, then vimbrowser modifies
Chromium/Blink C++ directly. Shadered page colors are computed before layout/paint in
`StyleResolver::ResolveStyle()`, and native scrollbar painting is hooked in
`ui/native_theme`. Nothing is applied by page JavaScript, injected CSS, or a
post-load browser callback.

Run:

```bash
vimbrowser --disable-gpu https://example.com
```

or directly:

```bash
./build/Release/vimbrowser --disable-gpu https://example.com
```

`~/.local/bin/vimbrowser` is a tiny launcher script that `cd`s into the chosen
build directory's `Release/` directory before execing the binary. CEF needs
that runtime directory for `icudtl.dat`, pak files, locales, and shared
libraries. The installed launcher also passes
`--profile-dir /home/yeyito/.runtime/vimbrowser-yeyito` so the user's main
browser profile has durable tabs/state plus Chromium cookies, IndexedDB,
localStorage, CacheStorage, etc. `make install-wrapper` also installs a
`vimbrowser.desktop` entry plus a detached `vimbrowser-xdg-open` launcher for
XDG/desktop URL and local-file opens; if the profile is already running, URL or
file arguments are forwarded to the existing window over native IPC as new tabs.
If the profile is closed, XDG launch arguments are appended after restored saved
tabs and the last opened argument is focused at the bottom of the tab stack.

Profile semantics:

- without `--profile-dir`, vimbrowser uses per-process instance storage for app
  state and CEF data; independent ad-hoc instances do not share login/session
  data by default
- with `--profile-dir DIR`, vimbrowser stores app state in `DIR/state` and the
  CEF web profile in `DIR/cef/Default`
- `--cache-path PATH` remains an advanced CEF-cache override; use
  `--profile-dir` for normal persistent browser profiles

## DevTools / CDP

Remote debugging defaults to `127.0.0.1:9222`:

```bash
curl http://127.0.0.1:9222/json/version
curl http://127.0.0.1:9222/json/list
```

Use another port:

```bash
vimbrowser --remote-debugging-port=9333 https://example.com
```

Use `--remote-debugging-port=0` to disable remote CDP.

## Current shell behavior

- one top-level CEF Views window; page-created popups are captured into the tab
  strip instead of being allowed to escape as separate native windows, while
  retaining real CEF popup/opener plumbing for OAuth-style auth flows
- Alloy runtime style, no Chrome toolbar
- URL/search startup argument
- per-process isolated state/cache by default; persistent profiles require
  `--profile-dir DIR` or the installed `~/.local/bin/vimbrowser` wrapper
- `Ctrl+Shift+I` opens DevTools
- web view focused by default in website-normal mode
- media autoplay is disabled by default; pages need an explicit user gesture to
  start playback after fresh loads or browser restarts
- the source-built CEF backend enables Chrome-branded FFmpeg proprietary codec
  support, including MP4/H.264/AAC/MP3 media used by sites like X/Twitter and
  Steam
- native content blocking is enabled by default for known ad-auction,
  identity-sync, and analytics hosts; set `VIMBROWSER_CONTENT_BLOCKING=0` or
  pass `--disable-vimbrowser-content-blocking` for a diagnostic run without it
- native network body capture/replay is off by default to keep normal browsing
  and benchmarks on the fast path; set `VIMBROWSER_NETWORK_CAPTURE=1` or pass
  `--enable-vimbrowser-network-capture` when you need IPC `network` diagnostics
- `Ctrl+j` / `Ctrl+k` cycle focus between the tab sidebar and the web view
- `Ctrl+m` toggles the tab sidebar
- when the tab sidebar is focused, `o` opens the command line to navigate the
  current tab
- when the tab sidebar is focused, `O` opens the command line to open a new tab
- the sidebar has durable, nested Exocortex-style folders. The root starts
  directly with its entries instead of a redundant `Tabs` title or permanent
  separator; nested folders show a compact folder-name breadcrumb, `..`, and
  `📁 name/ N` rows with recursive tab counts and descendant audio activity.
  Folder structure, tab placement, sibling order, and the browsed folder survive
  profile restarts
- when the tab sidebar is focused, `j` / `k` move its selection without changing
  the visible page; `Enter` activates a selected tab or enters a selected folder,
  `l` enters a folder, and `h` / `Backspace` returns to its parent. Outside insert
  mode, `Shift+j` / `Shift+k` focus the sidebar and continue from that same visual
  selection, following the visible sibling order inside the browsed folder rather
  than the tabs' backing-vector enumeration
- sidebar `p` pins or unpins the selected tab or folder. Pinned items are
  durable, share the leading `Pinned` section, and get the only section
  separator in the list; unpinning returns an item to the top of the regular
  entries
- sidebar `/` and `?` open Exocortex-style forward/backward live search over all
  folder names and tab titles/URLs, including results inside nested folders.
  `Escape` restores the exact pre-search folder and selection, `Enter` confirms
  the filter, `n` / `N` repeat it in the same/opposite direction, a second
  `Enter` opens the selected result, and sidebar `:noh` clears the filter while
  revealing that result in its containing folder
- sidebar `f` creates a folder in the current folder; `F` moves the selected tab,
  folder, or visual range to `/`, `..`, or an autocompleted folder path; `r`
  renames a folder; `e` / `E` reorder the selected sibling; and `x` unwraps a
  folder while retaining its children. Press `v`/`V` to start or clear a
  contiguous visual selection. `dd`/`DD` confirms deletion of the focused item or
  visual range; recursive folder deletion closes/destroys all contained tab
  backends. After deletion or a move out of the current folder, focus stays near
  the removed visual position and prefers the same pinned/unpinned section
- tabs currently emitting audio are shown in the sidebar as `◉` before their
  URL; only that status icon is rendered with the theme accent color. When the
  tab sidebar is focused, `[` / `]` move to the previous/next audible tab with
  wraparound
- in website-normal and regular Vim normal web modes, `i` / `a` enter insert
  mode
- in website-normal/normal web modes, `f` starts native backend hints and `F`
  (`Shift-f`) opens hinted links in a new tab immediately after the active tab
- in website-normal/normal web modes, `Ctrl+l` starts native backend
  right-click hints; selecting a label dispatches a context-menu/right-click on
  that element
- in website-normal/normal web modes, `Ctrl+h` starts native backend hover hints;
  selecting a label dispatches a synthetic hover/mouse-move over that element
- in website-normal/normal web modes, `Ctrl+Space` starts native backend
  scrollable hints; selecting a label focuses that scroll container and makes
  subsequent `j`/`k`/page-scroll commands target it
- in website-normal/normal web modes, `/` opens a forward in-page search prompt,
  `?` opens a backward in-page search prompt, and `n` / `N` repeat the last page
  search in the same/opposite direction; `:noh` clears the current page search
  highlights while keeping the last search term available for repeat
- in website-normal/normal web modes, `p` opens the clipboard in the current tab
  and `P` opens the clipboard in a new tab
- in website-normal/normal web modes, `d` closes the current tab and `D` closes
  it while focusing the previous tab; closed tabs are destroyed in the CEF
  backend, so media and page activity stop
- `u` / `:undo-close-tab` reopens the most recently closed tab at the index it
  was closed from, pushing later tabs right
- `src/shortcuts.c` owns page-specific shortcut overrides; on YouTube in
  website-normal/normal web modes, `Space` toggles playback; in insert mode,
  `h` / `l` seek -/+5s and `j` / `k` adjust volume -/+5% unless a page text
  field is focused, in which case the keys type into the field normally
- `Escape` from insert mode enters regular Vim normal mode; `Escape` again
  returns to website-normal mode
- when a page text field receives focus, including via native hints, web mode
  automatically enters insert mode so typing can start immediately
- left qutebrowser-style tab sidebar
- bottom qutebrowser-style command line while command mode is active
- command line starts in insert mode, shows a block cursor, supports `Escape` to
  command-normal mode, then `i` / `a` / `h` / `l` / `x` for a minimal shared Vim
  editing skeleton
- `:open` / `:open tab` autocomplete includes the last 1000 command-opened
  entries, shortest matching URLs first; long history entries are ellipsized in
  the popup while still completing to the full text. `:open tab` inserts the new
  tab immediately below the currently focused tab. `:open yt <query>`,
  `:open gh <query>`, and `:open ai <query>` search YouTube, GitHub, and
  ChatGPT respectively; the ChatGPT engine fills and submits the prompt on page
  load. Each engine keeps its own shared current-tab/new-tab query history. When
  an open/search history completion is selected with `Tab`, `Ctrl+x` deletes it
  from history.
- `:tab-focus` is a first-class command; command autocomplete lists it and its
  argument autocomplete offers open tabs by number/title/URL
- `:shader [on|off]` toggles the native Blink page color shader; with the
  shader enabled, YouTube's decorative `#cinematics` ambient-mode canvas glow is
  hidden natively so it cannot leave an unshadered dark square around videos
- `:mspdf` downloads every page of the current MuseScore score and assembles a
  native PDF in `~/Desktop/musescore-sheets` (override with
  `MUSESCORE_DOWNLOAD_DIR`). The network transfer and SVG/PNG-to-PDF conversion
  are implemented in C/C++ with libcurl, librsvg, and Cairo; no qutebrowser
  userscript, Python process, ImageMagick, or `pdfunite` is involved

Next work: broader qutebrowser command compatibility on top of this CEF/CDP
core.

## Design

The visual source of truth is `docs/design.md`. vimbrowser follows the
Exocortex TUI whale theme: terminal-esque, square corners, explicit dark chrome
backgrounds, and `#ffffff` for normal non-dimmed text.

## License

MIT. See `LICENSE`.
