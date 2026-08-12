# P02 Android product and build-branch cleanup

This checkpoint completes P02: Android browser/product Java, JNI bindings,
resources, tests, manifests, packaging, build branches, and active product
integrations have been removed from the retained Linux/macOS Chromium tree. It
is based on `855f1d8af4` and was developed and validated without changing the
installed stable browser.

## Scope removed

- Folded unsupported `is_android` GN/GNI and C/C++ branches to their retained
  desktop behavior instead of leaving dead conditionals around deleted files.
- Removed Android Java/JNI generators and product bindings, resource variants,
  GRIT Android formatting paths, manifests, APK/WebAPK/package definitions,
  test corpora, benchmarks, generated-source lists, and buildbot/infra entries.
- Removed the remaining Android product implementations embedded in shared
  Content, Chrome, Base, UI, Media, GPU, device, services, components, tools,
  and third-party aggregates.
- Removed Android-only browser services and metrics/error paths that were still
  compiled through shared targets, including Android password-store backend
  metrics, navigation screenshot transitions, mobile color/touch bridges, and
  their stale includes.
- Preserved desktop alternatives that had shared resource identifiers, including
  BNPL terms, desktop VR labels, and recovery-password UI text.

The exact staged change touches 6,806 files, physically deletes 5,312 files,
removes 901,423 lines, and adds 21,818 lines: a net reduction of **879,605
lines**. Deleted tracked blobs account for **53,997,382 bytes**. Of the deleted
files, 4,771 have Android-bearing path names; the remainder are predominantly
Android test/build/resource metadata and source files with generic names.

## Graph result and retained boundaries

For exact worktree tree `c49d6eff8314ddf828f5206f15cc3b1205a27fb7`:

- forced GN generation produced **27,176 targets from 4,269 files**;
- `gn desc out/Debloat_GN_x64 //cef:libcef deps --all` produced 16,427 lines
  and **zero Android-named labels**;
- `ninja -t inputs libcef.so` produced 186,462 transitive inputs and no Java or
  Kotlin source inputs;
- the only JNI-named production source is FFmpeg's generic `libavcodec/jni.c`,
  not Chromium Android product glue.

Fifty-eight Android-named transitive input paths remain, all in deliberate
boundaries rather than active Android browser targets:

- seven `*_non_android.cc` desktop browser/extension implementations;
- cross-platform web-payment classes and generated bindings deferred to stage A;
- the GCM Android-checkin wire protocol used by retained desktop GCM;
- Rust standard-library/vendor source trees parsed as toolchain inputs and
  deferred to P04;
- `gl_fence_android_native_fence_sync.cc`, retained because Linux uses the
  `EGL_ANDROID_native_fence_sync` extension;
- generated objects corresponding to those retained sources.

Central GN/toolchain/SDK declarations that Linux generation still parses remain
explicitly deferred to P04. This checkpoint does not claim to remove the
cross-platform Payments API or Rust toolchain merely because they contain
Android-named files.

## Validation

The exact tree passed on the isolated Google Cloud worker:

- forced Chromium/CEF GN generation;
- full `libcef` build;
- clean vimbrowser shell configuration and build;
- CTest: **1/1 passed**;
- checksummed runtime archive creation, fetch, extraction, and per-file
  verification.

The verified artifact is stored at:

`/home/yeyito/Workspace/vimbrowser-debloat-artifacts/c49d6eff8314ddf828f5206f15cc3b1205a27fb7/Release`

Artifact measurements:

- runtime tree: 363,734,923 bytes;
- compressed archive: 146,258,172 bytes;
- stripped `vimbrowser`: 2,032,456 bytes,
  SHA-256 `8a038f32c6d53e69e8cb5998853f567d253309ba190aacf80d51ca7c96f53b1d`;
- stripped `libcef.so`: 309,320,672 bytes,
  SHA-256 `3cceed7cc62621668eaf4af9a8ff1f29dfaeb4881010cffad20f54e38726186d`.

The exact fetched binary passed the full local integration benchmark with
`CHECK: PASS`, covering startup, session restore, normal page/resource loading,
tab switching, screenshots, background throttling, notification denial, and
retained service-worker behavior.

`scripts/debloat-sandbox.sh verify` confirmed throughout that
`build-source/Release` remained byte-for-byte unchanged. No artifact was
installed, the user's running stable browser was not restarted, and the cloud
worker is stopped.
