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

The first build downloads the Linux x86_64 CEF **minimal** binary/source
distribution into `third_party/cef/` and builds only the small C++ shell plus the
CEF wrapper library.

Run:

```bash
vimbrowser --disable-gpu https://example.com
```

or directly:

```bash
./build/Release/vimbrowser --disable-gpu https://example.com
```

`~/.local/bin/vimbrowser` is a tiny launcher script that `cd`s into
`build/Release` before execing the binary. CEF needs that runtime directory for
`icudtl.dat`, pak files, locales, and shared libraries.

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

- one top-level CEF Views window
- Alloy runtime style, no Chrome toolbar
- URL/search startup argument
- persistent cache under `~/.cache/vimbrowser/cef` by default
- `Ctrl+Shift+I` opens DevTools
- normal mode by default
- `o` opens the command line to navigate the current tab
- `O` opens the command line to open a new tab
- `Shift+j` / `Shift+k` switch tabs
- left qutebrowser-style tab sidebar
- bottom qutebrowser-style command line while in command mode

Next work: hints and broader qutebrowser command compatibility on top of this
CEF/CDP core.

## Design

The visual source of truth is `docs/design.md`. vimbrowser follows the
Exocortex TUI whale theme: terminal-esque, square corners, explicit dark chrome
backgrounds, and `#ffffff` for normal non-dimmed text.

## License

MIT. See `LICENSE`.
