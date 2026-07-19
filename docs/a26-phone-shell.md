# Galaxy A26 phone-shell bring-up

This port keeps the normal x86-64 desktop build and its Chromium object cache
untouched.  The first phone experiment deliberately uses the exact matching
official CEF Linux ARM64 binary distribution instead of rebuilding Chromium.

The compatibility build therefore retains normal Chromium page rendering,
vimbrowser tabs, DevTools/CDP and Unix-socket IPC, while temporarily stubbing the
five private symbols exported only by vimbrowser's custom CEF backend.  Native
hint/shader/FPS backend behavior is not part of this first proof.

## Build and package

```sh
scripts/a26/build.sh
scripts/a26/package.sh
```

The scripts use a cached ARM64 Debian Bookworm container under QEMU.  CEF and
Chromium themselves are not compiled.  Generated files stay under ignored
`build-a26-arm64/` and `third_party/cef-a26-arm64/` directories.

## Install

```sh
A26_SERIAL=<adb-serial> scripts/a26/install.sh
```

The phone keeps Alpine/Xorg/A26 Shell as its base.  Vimbrowser runs inside a
small nested Debian ARM64 application root at
`/opt/vimbrowser-a26/rootfs`, sharing only the existing display, kernel mounts
and network configuration needed by this proof.

The self-contained app installation also provides its launcher icon at
`/opt/vimbrowser-a26/share/browser-app.bgrx`. Regenerate or verify that asset
with `scripts/a26/prepare-icon.py [--check]` using the pinned Pillow version in
`requirements-a26-assets.txt`.

A26 Shell exposes this installation as the separate **Browser** launcher tile
and starts `/opt/vimbrowser-a26/bin/vimbrowser-a26` without replacing the
standalone System app. The shell owns fullscreen stacking and the global
bottom-edge close gesture; vimbrowser owns the page surface and browser state.

Android currently drops the Wi-Fi carrier when its Java framework is suspended
for the DRM handoff.  During USB development, run the loopback-only proxy and
reverse its port before opening the browser:

```sh
scripts/a26/adb-http-proxy.py --port 18777
adb reverse tcp:18777 tcp:18777
```

The host resolves names and opens Internet connections; TLS remains end-to-end
between Chromium and the destination because HTTPS uses `CONNECT` tunneling.

## Expected limitations

- software rendering (`--disable-gpu`)
- unsandboxed/root execution for the controlled bring-up only
- page surface starts full-screen with desktop browser chrome hidden
- browser-owned touch controls have not yet been adapted
- no on-screen keyboard yet
- the normal bottom-edge A26 Shell gesture remains responsible for closing the app

Do not use this proof build for routine untrusted browsing.
