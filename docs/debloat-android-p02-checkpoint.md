# Android Product-Source Deletion Checkpoint

This document records the first P02 recovery checkpoint after the inventory in
`docs/debloat-android-inventory.md`.  The checkpoint is based on
`148b71b203` and deliberately stops short of claiming that all residual Android
support is gone.  It physically removes the largest production-disconnected
batch, proves that the retained Linux X11 product still works, and leaves the
remaining active and globally parsed exceptions explicit for the next P02/P04
steps.

## Physical deletion

The P01 inventory identified 520 Android-bearing roots with no descendant
`//cef:libcef` production target and no tracked `libcef.so` input, plus 35
Chromium-owned generic Java/JNI roots with the same status.

- 511 of those 520 Android-bearing roots are deleted.
- All 35 generic Java/JNI roots are deleted.
- Nine build roots were restored after clean GN generation proved that Chromium
  parses them unconditionally even for a Linux target:
  - `build/config/android`
  - `build/toolchain/android`
  - `build/modules/android-arm`
  - `build/modules/android-arm64`
  - `build/modules/android-x64`
  - `build/modules/android-x86`
  - `third_party/android_build_tools`
  - `third_party/android_sdk`
  - `third_party/android_toolchain`

Those nine paths are build-configuration dependencies, not a restored Android
browser product.  P04 owns their removal after the root GN/toolchain files no
longer load them for retained Linux and macOS configurations.

The complete checkpoint changes 22,656 files: 22,609 deletions, 38
modifications, eight additions, and one symlink-to-file conversion.  Its text
delta is 1,450 insertions and 2,398,271 deletions.  The additional 46 deleted
files outside the 546 inventoried roots are Android-only shared-list entries
that were disconnected while repairing the retained graph.

## Retained-graph repairs

Physical deletion exposed cross-platform code that was incorrectly coupled to
Android implementation details.  The checkpoint removes those dependencies
rather than restoring the deleted products:

- Skia no longer includes or dispatches to Android-only pinnable raster-image
  helpers from shared Ganesh/image code.
- The fake desktop audio output stream implements `AudioOutputStream` directly
  instead of inheriting an Android muteable-stream interface.
- `libsync` uses a small portable public sync header instead of a deleted
  Android/NDK header layout.
- Chrome shared clients no longer include deleted Android autofill, password,
  permission, or tab-helper headers.
- The desktop HaTS/permission interface no longer carries an Android-only
  custom-message invitation type.
- Android metrics and SQL aggregate entries are removed from Perfetto's shared
  build lists, and Dawn's deleted Android JNI proxy is disconnected.
- The source bootstrap no longer runs unsupported Android/ChromeOS location-tag
  hooks.  A post-sync pruning script handles generated dependency checkouts that
  are not represented directly by the main repository snapshot.

Two ignored-but-required upstream source dependencies are made explicit so an
empty worker checkout is reproducible: libc++'s `ext/__hash`, and CPUinfo's
minimal `clog` implementation (`LICENSE`, public header, and C source).

## Isolated worker and validation

Broad Chromium builds now use three checked-in tools:

- `scripts/gcloud-build-worker.sh` creates a temporary Git index and bundle for
  the exact local staged/unstaged tree, transfers it to the worker, fetches a
  checksummed tree-addressed artifact, and stops the VM.
- `scripts/remote-build-worker.sh` performs worker-only Chromium/CEF generation,
  distribution generation, a clean CMake/Ninja shell build, CTest, and runtime
  archiving.
- `scripts/prune-synced-chromium-deps.py` applies the narrow cleanup needed to
  separately synced SwiftShader/Dawn dependency trees.

The local stable runtime at `build-source/Release` is checksummed before and
after every operation.  No remote or sandbox command has a stable promotion
path, and normal `make install` was not run.

Validation completed for the checkpoint:

1. Linux GN generation produced 27,322 targets.
2. The remote Chromium/CEF build completed from the worker's build lineage.
3. The CEF distribution and a clean 205-step vimbrowser shell build completed.
4. CTest passed 1/1 (`vimbrowser-config-state`).
5. Every fetched archive and runtime file passed its SHA-256 check.
6. The fetched binary had no missing dynamic libraries on the local host.
7. In a nested X11 environment with a disposable profile, startup, IPC status,
   tab listing, text/JavaScript inspection, navigation to `example.com`, reload,
   back, forward, and screenshot capture passed.
8. `make benchmark` against the fetched binary completed with `CHECK: PASS`.
9. `scripts/debloat-sandbox.sh verify` continued to report the installed/running
   stable runtime unchanged.

The fully smoke-tested artifact corresponds to worker tree
`0b3d23d11c83139180783b8880f785a2ee0781ae` and is retained under
`~/Workspace/vimbrowser-debloat-artifacts/`.  Final build-tree snapshot
`1162021e8519f5877da8c3d21c7538f5fc0e11db` also passed GN generation,
build, clean shell compilation, CTest, checksummed fetch, and stable-runtime
verification immediately before commit.  The only subsequent edits correct the
checkpoint counts in this documentation and checklist.

## Remaining P02 work

This is a checkpoint, not P02 completion.  The following active exceptions from
P01 remain and must be disconnected semantically before deletion:

- `chrome/browser/android`, including the WebAPK database proto dependency;
- desktop remote-Android DevTools and its USB bridge, while retaining local
  DevTools;
- `media/base/android` and Android-only entries in shared Skia, ANGLE, GL, and
  media source lists;
- Blink's Android font-lookup mojom aggregate entry;
- Perfetto Android trace/config schemas, Android bugreport importers and stats;
- Catapult Android trace-viewer resources and Android histogram metadata;
- dead Android GN/GNI/resource/manifest/packaging branches left in otherwise
  retained cross-platform files.

Rust target-source modules, the nine globally parsed SDK/toolchain roots, and
cross-platform host tooling are deferred to P04/P07.  `*_non_android*` Linux and
macOS implementations remain intentionally retained, and web-payment code stays
deferred to stage A as specified by P01.
