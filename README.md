# vimbrowser

Fresh, minimal CEF-based vim browser.

Goals:

- lightweight native shell
- Chromium/CEF backend for modern web compatibility
- Chrome DevTools Protocol via CEF remote debugging
- no Qt, no QtWebEngine, no qutebrowser dependency
- minimal C++ source, built directly against CEF headers/wrapper

Bootstrap/build:

```bash
./scripts/bootstrap-cef.sh
cmake -S . -B build -G Ninja
cmake --build build
./build/vimbrowser --disable-gpu https://example.com
```

DevTools/CDP:

- remote debugging defaults to `127.0.0.1:9222`
- pass `--remote-debugging-port=0` to disable or another port to override

Current state: skeleton app with one browser window, URL startup, basic app/client
hooks, and a clean place to build vim command handling next.
