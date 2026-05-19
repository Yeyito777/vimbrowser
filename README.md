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
scripts/vimbrowser-ipc tab-focus 3
scripts/vimbrowser-ipc html 3
scripts/vimbrowser-ipc js 3 'document.title'
scripts/vimbrowser-ipc network 3 list
scripts/vimbrowser-ipc fps
scripts/vimbrowser-ipc scroll 280
```

With `--profile-dir DIR`, the socket lives at `DIR/ipc.sock`; the installed
wrapper profile is auto-detected by `scripts/vimbrowser-ipc`. Set
`VIMBROWSER_PROFILE_DIR=DIR` or `VIMBROWSER_IPC=/path/to/ipc.sock` to target a
separate test instance.

See [`docs/ipc.md`](docs/ipc.md) for protocol framing, command semantics, and
compatibility rules. IPC now has stable tab IDs (separate from reorderable tab
indexes), ID-based tab focus/delete/order/open commands, native HTML/text/JS,
backend cookie inspection/mutation, and per-tab native network capture/replay.

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
XDG/desktop URL opens; if the profile is already running, URL arguments are
forwarded to the existing window over native IPC as new tabs.

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
- `Ctrl+j` / `Ctrl+k` cycle focus between web view and tab sidebar
- `Ctrl+m` toggles the tab sidebar
- when the tab sidebar is focused, `o` opens the command line to navigate the
  current tab
- when the tab sidebar is focused, `O` opens the command line to open a new tab
- when the tab sidebar is focused, `Shift+j` / `Shift+k` switch tabs
- in website-normal and regular Vim normal web modes, `i` / `a` enter insert
  mode
- in website-normal/normal web modes, `f` starts native backend hints and `F`
  (`Shift-f`) opens hinted links in a new tab
- in website-normal/normal web modes, `p` opens the clipboard in the current tab
  and `P` opens the clipboard in a new tab
- in website-normal/normal web modes, `d` closes the current tab and `D` closes
  it while focusing the previous tab; closed tabs are destroyed in the CEF
  backend, so media and page activity stop
- `u` / `:undo-close-tab` reopens the most recently closed tab at the index it
  was closed from, pushing later tabs right
- `src/shortcuts.c` owns page-specific shortcut overrides; on YouTube in
  website-normal/normal web modes, `Space` toggles playback; in insert mode,
  `h` / `l` seek -/+5s and `j` / `k` adjust volume -/+5%
- `Escape` from insert mode enters regular Vim normal mode; `Escape` again
  returns to website-normal mode
- left qutebrowser-style tab sidebar
- bottom qutebrowser-style command line while command mode is active
- command line starts in insert mode, shows a block cursor, supports `Escape` to
  command-normal mode, then `i` / `a` / `h` / `l` / `x` for a minimal shared Vim
  editing skeleton
- `:open` / `:open tab` autocomplete includes the last 1000 command-opened
  entries, shortest matching URLs first; long history entries are ellipsized in
  the popup while still completing to the full text
- `:tab-focus` is a first-class command; command autocomplete lists it and its
  argument autocomplete offers open tabs by number/title/URL
- `:shader [on|off]` toggles the native Blink page color shader

Next work: broader qutebrowser command compatibility on top of this CEF/CDP
core.

## Design

The visual source of truth is `docs/design.md`. vimbrowser follows the
Exocortex TUI whale theme: terminal-esque, square corners, explicit dark chrome
backgrounds, and `#ffffff` for normal non-dimmed text.

## License

MIT. See `LICENSE`.
