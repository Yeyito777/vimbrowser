# Diagnostic/control QA and installation — 2026-09-11

Historical first-handoff report below. The parent's subsequent activation exposed
old named-context session omission (223 → 200 tabs), not a cap. See
[session-recovery.md](session-recovery.md) for the confirmed mechanism, fixed
context-aware runtime, exact candidate and two-restart QA. Do not interpret this
initial report as claiming all tabs survived the parent's first activation.

## Conclusions and boundaries

The original Sept 11 socket timeout cause remains UNKNOWN. No accounts, sign-ins,
mail sends, live tab mutations or host-display GUI tests were used in this lane.
A later healthy status or authentication URL does not identify a timeout cause.

Initial passive process evidence: PID 2600833 started Sep 7 and mapped
`build-source/Release/vimbrowser` inode 184192141 and `libcef.so` inode 184190838.
Those inodes matched the installed files, not deleted/replaced images. The old
library exported both `vimbrowser_inspect_frame_controls` and
`vimbrowser_activate_element_handle`; its ELF build ID is `ee7dcd6691014107`.
The exact old installed runtime passed the isolated background activity/OOPIF
inspection/trusted-popup fixture (`/tmp/vb-diagnostic-before`). Therefore a
missing control implementation or live/on-disk mismatch was not established.
Its inspection deadline and disconnect failures shared the misleading
`inspection_backend_unavailable` code; which occurred on Sept 11 is unknown.

## Defects reproduced during isolated QA

1. A hidden static OOPIF could lack a Viz display transform. Native activation
   preflight returned local point `(165,53)` instead of expected `(350,63)`;
   checked transform lookup separately reported unavailable. The old unchecked
   helper silently allowed a partial/identity transform into preflight. This was
   a separate fixture activation defect, NOT evidence of the historical timeout.
   The fix checks transform availability and independently validates any native
   placement proposal through exact-view/local-point round trip and exact-node
   hit testing. Actual overlay coverage still rejects activation. No screenshot
   workaround, guessed click, page-JS first match or dispatched-action retry is
   used by the passing final fixture.
2. A shell rebuild copied the old CEF distribution into the staged runtime with
   a newer destination timestamp. `sync-chromium-runtime.sh` incorrectly skipped
   the new backend by mtime. The staged library then lacked the new build export.
   Sync now compares ELF build IDs/content. A regression fixture verifies newer
   mtime cannot mask older content, replacement preserves the old open inode,
   and identity matches are real no-ops. This was caught before installation.

## Final QA

- CLI fake-socket/unit tests: **40 passed**, `/tmp/vb-cli-tests.log`.
- Runtime identity sync regression: **1 passed**, `/tmp/vb-runtime-sync-test.log`.
- New xenv fixture: **passed**, `/tmp/vb-controls-fixture.log` and
  `/tmp/vb-diagnostic-controls-test/diagnosis.json`, `in-flight.json`.
  Covers hidden static OOPIF recipient/disabled/duplicate controls, overlay
  rejection, native named-submit form data, trusted fixture auth popup, concise
  context response, no-wake diagnostics, renderer deadline, in-flight timeout
  ambiguity, EOF half-close, pre-dispatch cancellation and owner focus/selection.
- Preserved activity-lease xenv fixture: **passed**, `/tmp/vb-background-after.log`.
  Covers hidden rAF/viewport, trusted OOPIF forms/popup, screenshots, owner native
  typing/focus, lease expiry/renewal/idle, reorder and close.
- Existing upload/chooser/OAuth/PiP xenv regression: **32 passed**,
  `/tmp/vb-upload-qa.log`. The one expected deadline code was updated to
  `inspection_timeout`; no test behavior was weakened.

All GUI fixtures used separate named xenv desktops and temporary profiles,
localhost fixture content/files only. Those fixture desktops were stopped.
The initially dirty approved activity-lease changes were preserved. No commits
or pushes were made. Backend edits are confined to the two control implementation
files; CEF builds were incremental, not a clean unrelated Chromium rebuild.

## Installed, not live-activated

The installed launcher now points to `build-diagnostics/Release/vimbrowser`.
`make install-wrapper BUILD_DIR=build-diagnostics` completed successfully;
`/tmp/vb-diagnostics-install.log` records installation. The former launcher is
saved at `build-diagnostics/install/previous-wrapper` for rollback.
CLI updates are already available through the existing installed source entry.

Built and staged libcef ELF build IDs were verified equal: `cb067f2f69cd98ef`.
The staged libcef is approximately 309 MB, the same scale as the old 309 MB
runtime; this is not an installation of a full Chromium build tree.
After installation, PID 2600833 still used the original `build-source` executable
and original mapped libcef inode 184190838. Neither live runtime file was changed.
**Parent must coordinate a full browser exit/relaunch to activate native changes.**
No browser restart or exocortexd restart was performed.

Public APIs: `vimbrowser-cli diagnose TAB [--expected-origin URL]`,
`open-context --brief CONTEXT URL` (new browser only, opt-in),
`inspect-controls TAB --frame FRAME --name-exact NAME --require-one`,
`activate-control TAB HANDLE`; raw `diagnose-tab TAB` and `ipc.sock.health`.
Default diagnosis is metadata-only: renderer responsiveness is explicitly
unknown/not probed. Authentication host evidence is not session validation or a
historical cause. Activation backend failure outcomes remain ambiguous; do not
blindly retry mutations. Exact nodes with unsupported geometry still fail closed.

`command -v Astra` and `command -v astra` found neither executable on PATH.
Availability through some other service/model registry was not investigated.
