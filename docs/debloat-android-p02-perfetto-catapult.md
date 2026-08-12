# P02 Android Perfetto/Catapult cleanup

This checkpoint completes the P02 subtask for Android-only Perfetto schemas,
trace importers/probes, Winscope support, StatsD telemetry, and Catapult trace
resources. It is based on `da59ecff51` and is isolated from the installed
stable browser.

## Scope removed

- **Perfetto schemas:** removed the Android, StatsD, and Android-power config and
  trace proto trees from their aggregates. Retired protobuf tags remain reserved
  so reduced desktop traces retain wire-format forward compatibility. Merged
  config/trace protos, Python bindings, and public C proto headers were
  regenerated from the reduced schemas.
- **Trace processor:** removed Android bugreport/log/dumpstate, app-wakelock,
  camera, per-UID CPU, kernel-wakelock, frame-timeline, graphics-frame, network,
  Pixel-modem, StatsD, user-tracker, and Winscope importers and their table
  schemas/functions. The generic TrackEvent log path remains through a small
  `LogTable` retaining the existing SQL table name for compatibility.
- **Platform tracing:** removed Android-only traced probes, power/properties,
  package/user-list sources, StatsD client and telemetry, trace-redaction product,
  Android builtin service producer, Android perfetto command implementation, and
  Android Soong build metadata. Generic Linux ftrace, filesystem, process,
  system, metatrace, and power-sysfs sources remain.
- **Catapult:** removed Android systrace importers/parsers, auditors, metrics,
  model helpers, fixtures, and generated resource-list entries.
- **System information:** replaced the Android-specific utility surface with a
  generic system-info implementation containing only retained platform data.

Across the implementation, 465 files change, 375 files are physically deleted,
118,396 lines are removed, and 244 lines are added (net 118,152 lines removed).
The deleted files account for 106,660 source lines and approximately 6.8 MB of
tracked content; 347 are Perfetto files and 28 are Catapult files.

## Graph reduction

The pre-change Linux `//cef:libcef` closure contained 54 Android/Winscope/StatsD
Perfetto labels and 58 corresponding active source inputs. After forced GN
regeneration, both checks returned zero:

- `gn desc out/Debloat_GN_x64 //cef:libcef deps --all`: 0 matching labels;
- `ninja -t inputs libcef.so`: 0 matching Perfetto/Catapult Android, Winscope,
  StatsD, or atom inputs.

## Validation

The exact worktree tree `3e5eedcd6333c6fba9a7ceb3dfc937139878e052` passed on
the isolated Google Cloud worker:

- forced GN generation: 27,202 targets from 4,271 files;
- Chromium/CEF build;
- clean vimbrowser shell build;
- CTest: 1/1 passed;
- checksummed runtime archive fetch and extraction.

The fetched runtime at
`/home/yeyito/Workspace/vimbrowser-debloat-artifacts/3e5eedcd6333c6fba9a7ceb3dfc937139878e052/Release`
then passed the local integration benchmark (`CHECK: PASS`), including startup,
session restore, page and resource loading, tab switching, screenshot capture,
and background-throttling behavior. The fetched runtime is 347 MB; its stripped
`vimbrowser` executable is 2,032,456 bytes and `libcef.so` is 309,328,864 bytes.

`scripts/debloat-sandbox.sh verify` confirmed that
`build-source/Release` remained byte-for-byte unchanged throughout remote build,
artifact fetch, and local benchmark. The worker is stopped.

## Deliberate boundary

Generic Perfetto tracing and trace-processor support remain because Chromium,
DevTools, benchmarking, and diagnostics use them. Android-named metrics/test
corpora outside the active aggregates are addressed by later source/test-corpus
sweeps; broad residual Android GN/GNI conditionals remain a separate P02/P04
checkpoint. The next sequential P02 item explicitly audits and retains
`*_non_android*` implementations before that broad conditional sweep.
