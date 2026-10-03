# Exact context-aware session recovery — Sep 11 follow-up

## Confirmed loss mechanism (not a cap)

The parent's activation snapshot had 223 tabs; the subsequent snapshot had 200.
All 23 missing IDs exactly equalled all 23 nonempty-context tabs. Old
`BrowserWindow::SaveState` deliberately filtered on `tab.context.empty()`; there
was no 200-tab restoration limit. The old format preserved context storage but
excluded these shell tab records. This is a confirmed separate restart issue,
not an explanation for the original socket timeout. Do not claim all tabs were
preserved by the first activation.

## Fix and compatibility

State now carries aligned `tab_contexts` through config, app and browser startup;
SaveState includes named tabs. Native restore uses the existing exact restored-ID
allocator plus original stack order, folder ID, sidebar sort order, pinned flag,
active index and context name. Named contexts use their existing persistent
context directories. Unavailable/invalid contexts never fall back to global.

Named tabs use a single independent line:
`context_tab=NAME<TAB>ID<TAB>FOLDER<TAB>ORDER<TAB>on|off<TAB>ESCAPED_URL`.
Legacy binaries ignore this record; they must never read a plain `tab=URL` plus
an unknown context annotation that would restore an isolated URL globally.
URLs use the existing backslash/newline/carriage-return/tab escaping rules.
Default-context records retain their legacy format. Rollback to old binaries
will again omit named tabs; retain recovery snapshots/candidates.

## Offline public API

```
vimbrowser-cli recover-session --snapshot SNAPSHOT.json \
  --state-source FINAL-SAVED-STATE --output NEW-CANDIDATE
```

This never connects to a browser. It validates IDs, context names, folder
references and fields; refuses duplicate IDs or missing folder definitions;
writes a NEW mode-0600 file, refusing overwrites. Snapshot tab order is preserved
exactly. Source state supplies folder definitions/settings/history and allocator
high-water mark. `--active-tab-id ID` can explicitly override snapshot selection.
Existing snapshot null contexts mean the global context.

**Snapshot tab records are authoritative.** Reconcile any owner navigation,
new/closed/reordered tabs since the snapshot before applying it. Reading a live
state source to prepare a candidate does not modify it, but the parent should
regenerate from the final saved state after graceful shutdown to preserve latest
non-tab settings/history. Never write a recovery file over a running profile:
its next state save would overwrite recovery.

## Parent-owned activation/recovery sequence

1. Keep original snapshot/state backups; reconcile any subsequent owner changes.
2. Gracefully close the live browser and confirm exit. Preserve window/workspace
   placement by the parent's established launch process. This lane does not do it.
3. Generate a new candidate using the final state as source; validate exact
   metadata against the reconciled snapshot. Back up final state before copying
   candidate to `~/.runtime/vimbrowser-yeyito/state`.
4. Select the already-built **`build-session-recovery/Release/vimbrowser`** runtime.
   `make install-wrapper BUILD_DIR=build-session-recovery` updates the launcher
   without launching/restarting anything. Never rebuild/overwrite a live runtime.
5. Relaunch, then compare count, every ID/URL/context/folder/sort/pin and active ID.
   Only after that comparison report recovered metadata, not unrecorded DOM state.

At handoff, the live `build-diagnostics` runtime and profile were **not modified**
by this follow-up lane. After QA and explicit parent approval, the installed
wrapper was updated to `build-session-recovery` (`/tmp/vb-session-install.log`).
Parent owns all live recovery/activation actions; this lane did not restart live.
Immediately before installation, regeneration from the latest read-only state
source matched the candidate byte-for-byte: settings/history/allocator unchanged.

## Supplied candidate and QA evidence

`build-session-recovery/state-recovered.candidate` was generated from the parent's
`/tmp/uoft-browser-activation/tabs-before.json` and read-only current state source.
It contains **223 tabs, 23 named-context tabs, active ID 528, next ID 583**.
An independent decoder compared every tab ID, URL, context, folder, sort order
and pinned flag plus active ID exactly with that snapshot. Mode is 0600; SHA256:
`7053d758f08e3b10bcf6fb85c8e6a99315fb9748f4e331262b74fae939d102be`.

- C++ config-state tests pass, including 223 mixed-context exact round trip,
  config argument alignment and malformed-context fail-closed behavior:
  `/tmp/vb-session-unit.log`.
- CLI tests: 43 pass, including offline metadata/escaping, duplicate/folder/context
  rejection and input/candidate overwrite protection: `/tmp/vb-session-cli-tests.log`.
- `tests/session_context_test.py` passed in its own xenv/temp profile: starts with
  223 mixed tabs, creates ID 700 in a second context, then performs TWO graceful
  WM_DELETE/relaunch cycles within xenv. All 224 retain exact metadata and
  localStorage isolation on both restarts. `/tmp/vb-session-xenv.log`.
- Diagnostic/controls and prior activity-lease xenv regressions also pass on the
  fixed runtime: `/tmp/vb-session-diagnostic.log`, `/tmp/vb-session-background.log`.

URL-only snapshots cannot reconstruct unsaved page DOM/form values, scroll state,
or full navigation history. Existing persisted context storage is reused; this
lane has not inspected account/session validity or performed any sign-in/send.
