# P03 residual platform pruning: disconnected sources and Wayland capture

This checkpoint begins P03 from `d45e0b8777847e3da768314d11375eb571709793` without changing the installed or running stable browser. It removes residual unsupported-platform files proven absent from the Linux `//cef:libcef` production closure and removes the remaining active Wayland desktop-capture implementation while retaining X11 capture and PipeWire camera support.

## Inventory and retained boundaries

The baseline Linux graph contained 16,427 `gn desc //cef:libcef deps --all` label lines and 162,379 unique normalized `ninja -t inputs libcef.so` paths. Candidate platform paths were classified against both sets before deletion.

The physical sweep deliberately retained:

- every production input and its required sibling headers/resources;
- `media/gpu/chromeos`, whose DMA-BUF and video-frame code is reused on Linux;
- Rust vendor platform modules and central SDK/toolchain declarations deferred to P04;
- generic browser-window code whose names contain `windows`;
- generic C++ casts such as `bit_cast`, `raw_ptr_cast`, and `saturate_cast`;
- active Cast/media-router code, pending deliberate graph disconnection later in P03;
- macOS and architecture-neutral Linux ARM/ARM64 code.

The final checkpoint physically deletes 3,351 files and removes 546,751 lines, with only nine replacement lines added. Deleted Git blobs total 35,689,862 bytes. Candidate-path deletions are:

- iOS: 1,272 files;
- Windows: 1,200 files;
- ChromeOS/Ash: 271 files;
- Fuchsia: 383 files;
- Cast: 199 files;
- Wayland: 30 files.

Some files match more than one category, so the category total is not intended as a unique-file sum.

## Wayland desktop-capture boundary

The WebRTC PipeWire flag previously pulled `modules/desktop_capture/linux/wayland` into the X11 production library. This checkpoint removes that desktop capturer, its screen-cast portal bridge, tests, public configuration, and GTK Wayland bridge. X11 screen/window/cursor capture now goes directly to the X11 implementations.

WebRTC's separate PipeWire camera portal remains enabled under `modules/video_capture` and `modules/portal`; removing Wayland screen capture therefore does not remove retained camera/microphone/WebRTC support.

## Validation

Exact source snapshot tree `8fdcfdbef52e050c7f7354c1909bd28047bfefc3` passed on the isolated Google Cloud worker:

- forced Chromium/CEF GN generation: 27,176 targets from 4,269 files;
- complete `libcef` build;
- clean vimbrowser shell configuration and build;
- CTest: 1/1 passed;
- checksummed runtime archive creation, fetch, extraction, and per-file verification.

The fetched artifact is at:

`/home/yeyito/Workspace/vimbrowser-debloat-artifacts/8fdcfdbef52e050c7f7354c1909bd28047bfefc3/Release`

Artifact measurements:

- runtime tree: 363,616,139 bytes;
- stripped `vimbrowser`: 2,032,456 bytes, SHA-256 `8a038f32c6d53e69e8cb5998853f567d253309ba190aacf80d51ca7c96f53b1d`;
- stripped `libcef.so`: 309,201,888 bytes, SHA-256 `0786a8ab4f36a76c1be4d8cb8fca793ac38c0ceee9253d7ca0e3f4a0e7360ec6`.

Compared with final P02, `libcef.so` and the runtime tree are 118,784 bytes smaller. The vimbrowser executable is byte-identical.

The exact fetched binary also passed `make benchmark ...` with `CHECK: PASS`, covering startup, session restore, page/resource loading, tab switching, screenshots, background throttling, notification denial, and retained service-worker behavior.

`scripts/debloat-sandbox.sh verify` confirmed throughout that `build-source/Release` remained byte-for-byte unchanged. No artifact was installed, the user's running stable browser was not restarted, and the cloud worker is stopped.
