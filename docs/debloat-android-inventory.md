# Residual Android Platform Inventory

This is the P01 inventory checkpoint for the residual unsupported-platform
sweep.  It describes commit `f27836adf8` and the isolated Linux production
graph in `backend/chromium/out/Debloat_GN_x64`.  Android is not a supported
vimbrowser target; Linux X11 (including a possible future ARM/ARM64 target) and
macOS remain supported.

The purpose of this inventory is to avoid two opposite mistakes:

1. retaining thousands of files merely because Android's former product roots
   were already deleted; or
2. deleting every path containing `android` even when the Linux graph still
   uses a generated protocol, a non-Android implementation, or a toolchain
   source list that must first be disconnected.

## Method

The inventory considered tracked files below:

- `chrome/browser`
- `content`
- `components`
- `media`
- `services`
- `ui`
- `third_party`
- `tools`
- `build`

It grouped files by the first directory component containing `android`, then
separately found Chromium-owned `java`, `javatests`, `jni`, `junit`, and
`robolectric` trees outside those directories.  Standalone Android-named files
and build metadata containing Android conditions or paths were counted
separately.

Production reachability was checked with:

```sh
gn desc out/Debloat_GN_x64 //cef:libcef deps --all
ninja -C out/Debloat_GN_x64 -t inputs libcef.so
```

The first command produced 16,517 target labels and the second 187,049
transitive build inputs.  A source listed as an input is not necessarily
compiled on Linux: GN source sets, Rust module lists, generators, and resource
pipelines may declare platform-specific inputs more broadly.  It nevertheless
means that the path must be disconnected before physical deletion.

The complete path set can always be reconstructed from the checkpoint with
`git ls-tree -r --name-only f27836adf8 -- backend/chromium` rather than storing
a second multi-thousand-line file list in this repository.

## Directory totals

There are 532 Android-bearing directory roots containing 23,276 tracked files,
145,209,229 bytes, and approximately 2,580,986 lines.

| Scope | Roots | Files | Tracked bytes | Approx. lines | `libcef` inputs | Descendant closure labels |
|---|---:|---:|---:|---:|---:|---:|
| `chrome/browser` | 148 | 8,809 | 68,793,952 | 1,107,826 | 1 | 3 |
| `content` | 11 | 775 | 4,923,633 | 121,457 | 0 | 0 |
| `components` | 143 | 5,336 | 29,559,243 | 554,997 | 0 | 0 |
| `media` | 5 | 209 | 1,808,321 | 47,813 | 0 | 1 |
| `services` | 16 | 129 | 871,226 | 22,576 | 0 | 0 |
| `ui` | 7 | 106 | 526,677 | 13,581 | 0 | 0 |
| `third_party` | 191 | 7,382 | 35,482,357 | 624,985 | 61 | 39 |
| `tools` | 5 | 494 | 2,737,027 | 73,215 | 2 | 0 |
| `build` | 6 | 36 | 506,793 | 14,536 | 0 | 0 |

Of those, **520 roots / 22,273 files / 132,640,697 bytes / about 2.425
million lines** have neither a descendant production label nor a source input
in the Linux `libcef` graph.  This is the first physical-deletion batch.  It
includes the large remaining Chrome Android UI, Content Android bindings,
component Java/JNI/UI implementations, Android tests and test data, Android
tooling, and platform resources.

An additional 35 Chromium-owned generic Java/JNI roots contain 519 files,
3,081,258 bytes, and about 71,332 lines.  They have no production labels or
inputs and belong in the same Android product-removal batch.  Generic Java
implementations in `third_party` are not assumed to be Android-only merely
because they use Java.

## Directory exceptions still touching the production graph

Only the following 12 directory roots had a descendant closure label or a
tracked source input.  They are explicit P02/P04 disconnect tasks, not grounds
for retaining the other 520 roots.

| Directory | Files | Inputs | Labels | Disposition |
|---|---:|---:|---:|---|
| `chrome/browser/android` | 788 | 1 | 3 | Remove the WebAPK database proto dependency, then delete the product tree. |
| `media/base/android` | 73 | 0 | 1 | Remove the empty/platform target from shared media deps, then delete. |
| `third_party/perfetto/protos/perfetto/trace/android` | 68 | 26 | 25 | Remove Android trace schemas from Perfetto's aggregate descriptors. |
| `third_party/perfetto/src/trace_processor/importers/android_bugreport` | 21 | 1 | 2 | Remove Android bugreport parsing from the trace processor. |
| `third_party/perfetto/protos/perfetto/config/android` | 19 | 18 | 10 | Remove Android-only tracing configuration schemas. |
| `third_party/angle/src/libANGLE/renderer/vulkan/android` | 10 | 2 | 0 | Remove the files from ANGLE's unconditional Vulkan source list and isolate any shared helpers. |
| `third_party/rust/chromium_crates_io/vendor/libc-v0_2/src/unix/linux_like/android` | 8 | 8 | 0 | Defer to P04's target/toolchain-source pruning; do not damage Linux Rust libc. |
| `third_party/catapult/tracing/tracing/extras/importer/android` | 5 | 3 | 0 | Remove from Catapult's generated trace-viewer resources. |
| `third_party/perfetto/src/android_stats` | 4 | 1 | 2 | Remove Android stats/atoms from Perfetto. |
| `tools/metrics/histograms/metadata/android` | 3 | 2 | 0 | Remove from histogram metadata aggregation. |
| `third_party/blink/public/mojom/android_font_lookup` | 2 | 1 | 0 | Remove from the aggregate Blink mojom source list. |
| `third_party/catapult/tracing/tracing/extras/android` | 2 | 1 | 0 | Remove Android trace-viewer extras from generated resources. |

Perfetto is the dominant exception.  Its Android schemas are included in
cross-platform descriptor and trace-processing aggregates even on Linux.  They
do not provide ordinary web browsing, DevTools inspection, media, WebRTC, or
service-worker behavior and can be removed, but their aggregate BUILD and
proto lists must be edited first.

## Standalone files and metadata

There are 2,825 Android-named files outside Android-bearing directory roots.
Forty-eight are present in the `libcef.so` input closure.  This class needs
semantic handling rather than a filename-only deletion:

- Files named `*_non_android.cc`, `*_nonandroid.cc`, and similar are the Linux
  and macOS implementations and **must be retained**.
- Chrome's remote-Android DevTools bridge is compiled on desktop.  Remote
  inspection of Android devices is outside the retained product contract, so
  its sources and USB bridge can be removed while keeping ordinary local
  DevTools.
- Payment Request's Android-app communication classes are cross-platform web
  payment plumbing and are deferred to the password/autofill/payments stage so
  ordinary form and payment behavior can be tested coherently.
- Perfetto Android schemas/importers and Catapult Android trace resources are
  handled with the directory exceptions above.
- Rust standard-library/vendor Android modules are handled only with P04's
  target/toolchain audit.
- Android-specific Skia, ANGLE, GL, and media files in shared source lists must
  be removed from those lists before deleting the files.
- `tools/grit` Android resource-conversion utilities are host tools, not an
  Android browser runtime.  Delete them only after all Android resource rules
  are gone and the retained resource build no longer imports them.

Finally, 3,565 tracked GN/GNI/resource/tool metadata files in the inventoried
scopes contain an Android condition, Android path, or Java-template reference.
The scope counts are: `chrome/browser` 802, `content` 60, `components` 685,
`media` 19, `services` 43, `ui` 75, `third_party` 1,014, `tools` 241, and
`build` 86.  P02 removes product source/resource/test branches; P04 and P07 own
the remaining toolchain, SDK, DEPS, and download declarations.

## Ordered deletion plan

1. Delete the 520 production-disconnected Android directory roots and the 35
   Chromium-owned Java/JNI roots.
2. Remove their GN/GNI, resource, test, Java/JNI, manifest, and packaging
   branches rather than leaving dead Android conditions.
3. Disconnect WebAPK, the Android remote-DevTools bridge, Blink Android-font
   mojom, Android-only shared-source entries, and Android resource generation.
4. Prune Perfetto/Catapult Android tracing schemas and resources while retaining
   ordinary DevTools and tracing functionality.
5. Preserve non-Android implementations and defer payment and Rust/toolchain
   exceptions to their explicitly named stages.
6. Force GN regeneration and build/test/install only through
   `scripts/debloat-sandbox.sh`; verify the stable runtime checksum after every
   operation.

