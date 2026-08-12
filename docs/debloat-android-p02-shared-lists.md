# P02 Android shared-source cleanup

This checkpoint completes the P02 subtask for Android-only entries embedded in
shared Skia, ANGLE, GL, GPU, and media source lists. It is based on
`bfc08c9c4f` and is intentionally isolated from the installed stable browser.

## Scope removed

- **ANGLE:** removed the Android Vulkan display/window/AHardwareBuffer backend,
  common Android utility and system-property implementation, Android GPU-info
  and unwind implementations, Android external-buffer support, and its dedicated
  test. Retained EGL ABI entry points now reject Android native buffers
  deterministically instead of retaining the implementation.
- **Skia:** removed the Android XML/NDK font-manager implementations, parser,
  headers, tests, GN variables, and Chromium/PDFium shared-source wiring. Linux
  continues to use FontConfig/Fontations and macOS continues to use CoreText.
- **GPU:** removed Android Vulkan initialization, AHardwareBuffer backing and
  representation machinery, image-reader/texture-owner plumbing, surface lookup
  and tracking, Android image transport, and Android-only test support.
- **Media:** removed Android overlays and their cross-platform callback plumbing,
  Android Mojo media clients/services, MediaDrm support service, Android MIDI,
  and the Android demuxer-memory specialization. Retained platforms continue to
  use the existing default/Linux/macOS media paths.
- **GL:** removed Android GL initialization and JNI/native-window branches while
  retaining `gl_fence_android_native_fence_sync`. Despite its name, that source
  implements the `EGL_ANDROID_native_fence_sync` extension used by Linux GPU
  fences and therefore is not Android product code.

The checkpoint changes 162 files, physically deletes 95 files, removes 16,875
lines, and adds 61 lines (net 16,814 lines removed).

## Validation

The exact worktree tree `95bb27acc3391c71d82107b3e3fb5eff0f3944fa` passed on
the isolated Google Cloud worker:

- forced GN generation: 27,317 targets from 4,279 files;
- Chromium/CEF build;
- clean vimbrowser shell build;
- CTest: 1/1 passed;
- checksummed runtime archive fetch and extraction.

The fetched runtime at
`/home/yeyito/Workspace/vimbrowser-debloat-artifacts/95bb27acc3391c71d82107b3e3fb5eff0f3944fa/Release`
then passed the local integration benchmark (`CHECK: PASS`), including normal
startup, restore, page/resource loading, tab switching, screenshots, background
throttling, notification denial, and retained service-worker registration.

`scripts/debloat-sandbox.sh verify` confirmed that
`build-source/Release` remained byte-for-byte unchanged before and after remote
build, artifact fetch, and local benchmark. The worker was stopped after each
build.

## Deliberate boundary

This does not claim that every residual `is_android` branch anywhere in Chromium
is gone. Android Perfetto/Catapult schemas and trace importers are the next
explicit P02 subtask. Broad dead Android GN/GNI/resource/manifest/packaging
branches remain tracked separately in P02/P04 so globally parsed toolchain
inputs are not deleted prematurely.
