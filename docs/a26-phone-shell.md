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

The native A26 environment now owns `wlan0` directly while Android's Java Wi-Fi
framework is suspended, so the default launcher uses the phone's system-wide
Linux route and DNS configuration. No browser-specific proxy is required.

Browser audio uses Moon's system audio bridge rather than direct access to the
Samsung ALSA devices. Chromium's normal Linux audio service opens a dedicated
ALSA file PCM at `/run/moon-audio/pcm`; the app root bind-mounts only that
root/system-owned runtime directory. Moon converts the fixed 48 kHz stereo
signed-16-bit stream into an AudioTrack that was authorized before Android's
Java framework was suspended. Physical volume keys and gain policy remain global
Moon controls, so audio and volume continue working regardless of which Browser
descendant window owns X focus.

A loopback-only ADB proxy remains available as a recovery fallback. Start it,
reverse its port, and launch the wrapper with an explicit proxy value:

```sh
scripts/a26/adb-http-proxy.py --port 18777
adb reverse tcp:18777 tcp:18777
A26_VIMBROWSER_PROXY=http://127.0.0.1:18777 \
  /opt/vimbrowser-a26/bin/vimbrowser-a26
```

With that optional fallback, the host resolves names and opens connections;
HTTPS remains end-to-end because Chromium uses `CONNECT` tunneling.

## Phone interaction and rendering

The A26 build has a touch-first bottom bar with Back, Forward, editable URL/search,
Reload/Stop, and tab cycling. The bar reaches the physical display edge without
an empty gesture strip. Moon distinguishes taps from upward movement, so a swipe
that begins there still closes the app while ordinary control taps are forwarded
normally. Desktop builds retain their normal side UI and keyboard behavior.

Editable browser and page fields request Moon's global on-screen keyboard over
the root-only control socket. Moon keeps X focus on the browser, injects keys
with XTEST, resizes the browser above the keyboard, and restores fullscreen when
the keyboard hides. No field value or password is copied into that IPC protocol.

The Samsung kernel has no Mesa-compatible Mali path. The launcher therefore
selects ANGLE's packaged ARM64 SwiftShader implementation for software
compositing. Chromium's `--disable-gpu` mode is not used because CEF Views still
requires a compositing GPU process and terminates after repeated startup failures
on this target. The launcher also uses `--no-zygote`: CEF's zygote children exit
on this Android-derived userspace/kernel combination before creating a renderer,
whereas direct no-sandbox renderer processes are stable.

The application root has a private minimal `/dev` containing only the basic
character devices and shared-memory directory required by Chromium. In
particular, the phone's `/dev/video*` devices are never exposed to the browser.
This is a hard safety boundary rather than an optimization: enumerating those
nodes enters Samsung's downstream FIMC camera driver, and the stock AYB8 kernel
can panic while an unprivileged media client probes its camera pipelines. Xorg
continues to own the real display and input devices outside the nested root.
Accelerated video encode and decode are disabled as a second layer of defense.
The private root does not expose `/dev/snd`; speaker playback crosses only the
bounded Moon PCM endpoint described above.

This target exposes one additional CEF/X11 quirk: XTEST printable key presses
reach the browser as raw events but do not synthesize a renderer CHAR event. The
phone launcher enables a narrowly scoped A26 workaround that forwards exactly one
ephemeral CHAR event while an editable page node is focused. It does not inspect,
store, or log field contents. URL-bar text remains on CEF Views' normal key path.

## Expected limitations

- software rendering is CPU-intensive;
- the app still runs unsandboxed as root during this controlled bring-up;
- the normal bottom-edge Moon gesture remains responsible for closing the app;
- native hint/shader/FPS behavior still depends on the official-CEF compatibility
  stubs described above.

Do not use this proof build for routine untrusted browsing.
