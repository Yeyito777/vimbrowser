# Read-only diagnostics and control readiness

`vimbrowser-cli diagnose TAB --expected-origin https://outlook.office.com --pretty`
is the shared diagnostic entry point. It never focuses/selects/wakes a tab, runs
page JavaScript, reads cookies, or initiates authentication. The expected origin
is comparison data, not a navigation target. Output omits page titles, URL paths,
queries, fragments and other tabs. A recognized authentication host plus an origin
change is **redirect evidence**, not proof that a session expired, a redirect
chain was followed, or authentication caused an earlier timeout.

## Independent layers

* `ipc.sock.health` is an independent, mode-0600 Unix socket served off the UI
  thread. Send `health\n`; it reports process/build identity and the latest plus
  last 16 operation metadata snapshots. No command arguments or responses are
  retained in history. `completed` means callback returned, not semantic success.
  Older operations, including unresolved ones, may fall outside the bounded
  history. Commands not yet accepted from the kernel backlog are not counted.
* `diagnose-tab TAB` uses the normal serial IPC/UI queue and returns only the
  exact tab's metadata, browser/main-frame presence and backend build identity.
  It does not renew activity leases. A valid frame or `loading:false` is **not**
  proof of renderer responsiveness or application readiness. The CLI deliberately
  reports renderer readiness as unknown/not probed.
* The CLI uses a total deadline per probe, not a sliding per-read timeout. It
  distinguishes connect/not-sent from send/receive/outcome-unknown failures and
  counts partial response bytes. A healthy health socket with a timed-out UI
  request indicates layers differ now; it does not establish a historical cause.
* Old browsers lack the independent socket and `diagnose-tab`; an explicit
  protocol rejection can use read-only `tabs` fallback. A timeout never causes
  a retry. An explicit missing environment socket/profile never falls through
  to the owner's live profile.

## Cancellation and ambiguity

The normal command queue remains serialized. A full client disconnect observed
before UI dispatch skips that queued command. An EOF write-half-close is still
valid protocol. After dispatch, socket closure does not undo or reliably cancel
an operation. The server continues tracking it up to its 35-second deadline;
late callbacks can change `timeout_outcome_unknown` to `completed` in bounded
history. Client deadlines can be much shorter. Never blindly retry mutations.
A two-second socket read/write timeout bounds idle/slow peers; partial commands
are not executed after a read timeout. No in-flight operation cancellation or
rollback guarantee is introduced.

## Exact controls

Inspection uses custom Blink accessible names/roles and exact document-bound
node handles. In addition to clickable hints it enumerates native forms,
disabled controls and editable/ARIA recipient-like widgets. Hidden controls are
excluded. This is inspection, not permission to activate every returned node:
activation still checks document/node identity, disabled state, visibility,
viewport, local hit test and cross-process parent/compositor hit testing.

The backend checks whether Viz actually supplies a target-to-root transform.
Fully covered background OOPIFs can be missing from that display table; the old
unchecked helper could silently use a partial/identity transform. If unavailable,
native frame placement supplies only a read-only hit-test proposal. The native
router must independently return the exact expected view AND local point (within
2 DIP), then the renderer revalidates the same document/node. Unsupported
scaled/rotated/stale placement still fails closed, as do ancestor overlays.
`VIMBROWSER_CONTROL_TRACE=1` enables opt-in native preflight stage/geometry logs;
normal operation does not enable them.

The browser may repeat only **read-only preflight**, up to three times at 50ms,
to allow transient hit-test/surface readiness to settle. It never retries a
dispatched event or bypasses an overlay, picks the first match, or substitutes
page JavaScript clicks. Duplicate names remain multiple exact handles.
Native trusted submit-button activation preserves the clicked submitter's name
and value, including forms containing a control named `submit` (which shadows
JavaScript `form.submit`). Do not replace it with `form.submit()` or generic JS.

The backend reports `controls-v2 DATE TIME` separately from the shell build.
Inspection's 2500ms renderer deadline returns `inspection_timeout`; this proves
the call started, not why it was slow. Legacy backend failures can mean deadline
or disconnection, not absence of the implementation. Activation backend failure
still has an explicitly unknown outcome; do not retry the consumed handle.

## Concise context opening (opt-in compatibility)

`vimbrowser-cli open-context --brief fixture https://example.test/` sends
`open-background-context-tab-brief` and returns `{ok:true,tab:{...}}` for precisely
the newly created tab. Existing default/raw responses remain unchanged. `--brief`
requires the new browser; there is no automatic mutation fallback or retry.

## Build / activation discipline

Build and test into a separate runtime, e.g. `build-diagnostics`, then install the
wrapper with `make install-wrapper BUILD_DIR=build-diagnostics`. Runtime sync
compares ELF build IDs/content, not mtimes: a shell build can copy an older CEF
distribution with a newer destination timestamp. `tests/runtime_sync_test.py`
regresses this exact case and verifies replacement does not alter the old inode.
Do not overwrite
the running browser's runtime: its future child processes must continue loading
the matching old library until a coordinated full browser restart. Installing a
new wrapper does not activate changes in the running browser. Never restart
exocortexd to activate browser changes.

Tests: `tests/diagnostic_controls_test.py` and `tests/background_activity_test.py`
create their own named xenv desktops and temporary profiles. The CLI unittest
suite uses fake Unix sockets only. No live accounts or host display are used.
