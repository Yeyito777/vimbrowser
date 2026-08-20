# Vimbrowser one-week Chromium debloat swarm plan

## Purpose

Reorient the debloat project around two primary outcomes:

1. physically reduce tracked backend source LOC; and
2. substantially reduce honest clean-build time by shrinking the active
   Chromium/CEF production graph.

Deletion outside the production graph remains useful for checkout size and
maintenance, but it is not sufficient. A candidate should normally reduce LOC
and/or active compile work without materially worsening the other objective.

## Current position

Against the `pre-cleanup` tag, the committed backend has removed 38,000,799
lines and added 24,143 lines, for a net reduction of 37,976,656 lines. The
current uncommitted platform-branch candidate removes another net 154,542
backend lines. If validated and committed, the combined net backend reduction
will be 38,131,198 lines, approximately 38.5% of the roughly 99.16-million-line
pre-cleanup checkout.

The active Linux graph is still large:

- `//cef:libcef`: approximately 16,420 transitive labels;
- `//chrome:dependencies`: approximately 16,398 transitive labels;
- `//chrome/browser:browser`: approximately 16,199 transitive labels;
- `//chrome/browser:browser`: roughly 1,260 direct dependencies and 2,326 direct
  source entries;
- `libcef.so`: approximately 186,406 transitive input paths;
- GN generation: approximately 27,166 targets from 4,265 parsed files.

The active dominator is:

```text
//cef:libcef
  -> //cef:libcef_static
     -> //chrome:dependencies
        -> //chrome/browser:browser
```

## Principal one-week deliverable

`//cef:libcef_static` should stop depending on both `//chrome:dependencies` and
the monolithic `//chrome/browser:browser` target. Replace that chain with a
small, explicitly enumerated CEF/vimbrowser runtime aggregation containing only
retained functionality:

- Content browser startup and process integration;
- one persistent profile and retained storage;
- navigation, history, and bookmarks;
- tabs/windows and popup-as-tab behavior;
- downloads and file chooser;
- required permissions;
- DevTools;
- PDF;
- ordinary audio/video, proprietary codecs, Widevine, and fullscreen;
- camera, microphone, and WebRTC;
- cookies, IndexedDB, Cache Storage, service workers, and retained PWA support;
- Linux X11 and macOS platform integration;
- the minimum CEF API compatibility layer.

Link failures during this work must be resolved by adding narrow dependencies
or replacing unwanted interfaces. Reintroducing `//chrome:dependencies` or
`//chrome/browser:browser` as a convenience fix is not acceptable.

## Swarm hierarchy

Exo is the root program orchestrator and sole integration authority. Under it,
run three lead agents and approximately eight leaf agents.

### Graph architecture lead

Owns the critical runtime-boundary work:

- create `//cef:vimbrowser_runtime` or its equivalent;
- inventory every Chrome dependency CEF actually requires;
- split retained Chrome browser portions into narrow C++ source sets;
- isolate or replace CEF use of Chrome toolbar/tab-strip/window/WebUI APIs;
- remove `//chrome:dependencies` and the Browser monolith;
- maintain graph-dominator, target, input, and compile-action measurements.

Suggested leaves:

- CEF startup/profile/storage boundary;
- Chrome UI/window/tab dependency replacement;
- DevTools/PDF/runtime resources;
- download/permissions/media/WebRTC integration.

### Feature demolition lead

Owns approved feature-family removal after the runtime boundary permits it:

- Cast and Media Router;
- extensions beyond the smallest required compatibility skeleton;
- WebAuthn/passkeys;
- WebUSB, Bluetooth, Serial, MIDI, and HID;
- accessibility;
- multiple profiles, guest, incognito, profile picker, and switching;
- residual password/autofill/payment machinery;
- enterprise management, reporting, updater, and official packaging;
- each removed family's WebUI, GRIT, strings, prefs, flags, metrics, policy
  schemas, tests, generators, and third-party dependencies.

Leaves receive exclusive feature-directory leases. Shared `BUILD.gn`, `DEPS`,
CEF resource manifests, and generated aggregate lists remain owned by the graph
lead and are changed through queued requests.

### Build and validation lead

Owns the serialized merge train:

- immutable candidate commits and tree IDs;
- ownership/path checks;
- formatting and static validation;
- affected-object compilation;
- one heavy Chromium/CEF build at a time;
- periodic honest cold-clean builds;
- CTest and retained-feature smoke tests;
- artifact checksums, graph comparisons, and benchmark reports;
- automatic recovery, rejection, revert, and VM shutdown.

Suggested leaves:

- deterministic retained-feature smoke coverage;
- LOC, graph, clean-build, runtime-size, and benchmark measurement.

## Source isolation and Git policy

`main` remains the only long-lived/release branch, but agents must not all edit
it directly.

- Only the root integration orchestrator writes to `main`.
- Every ticket uses a temporary branch such as `swarm/week1/<ticket>` and an
  isolated worktree.
- Every submitted candidate is an immutable commit/tree snapshot.
- A ticket declares allowed paths, forbidden paths, expected graph/LOC effect,
  required tests, and rollback commit.
- The queue rejects diffs outside the ticket's path lease.
- Temporary branches and worktrees are deleted immediately after merge or
  rejection.
- No agent may touch the primary checkout, installed browser, stable runtime,
  another agent's output, or the normal user installation prefix.

This preserves `main` as the sole durable product branch while making parallel
agent work safe and bisectable.

## Cloud execution model

Use a minimal durable control plane rather than building unnecessary cloud
infrastructure:

- one persistent high-CPU integration worker;
- a bare Git mirror and isolated per-ticket worktrees on persistent SSD;
- one serialized persistent integration output for the merge train;
- separate disposable outputs for cold-build measurements;
- a durable SQLite or append-only JSON queue protected by `flock`;
- systemd-managed detached jobs that survive the controlling PC/session;
- persistent build logs, tree IDs, exit codes, and checksummed artifacts;
- automatic VM poweroff after terminal success or failure;
- automatic incremental resume when an interrupted job is restarted.

Parallelism is applied to analysis and non-overlapping source work. Heavy
`libcef` compilation remains serialized so agents cannot corrupt shared
Ninja/Siso state. No mutable output is shared by concurrent jobs.

## Machine requirement

The current global CPU quota is 32 and another worker consumes 16, leaving the
vimbrowser worker limited to 16 vCPUs. That is inadequate for the intended
one-week pace.

Preferred setup:

- 56–96 high-frequency vCPUs;
- approximately 1 TB fast persistent SSD;
- enough memory to avoid Chromium link/compile pressure;
- automatic shutdown and a pre-approved weekly cost ceiling.

Possible paths:

1. temporarily stop the unrelated 16-vCPU PMV4 worker and give vimbrowser all
   currently available quota;
2. obtain the pending 96-vCPU quota and resize/provision a 56–96-vCPU worker;
3. retain the 16-vCPU worker only for bootstrap/cache work until quota arrives.

A real macOS worker is also required before aggressive shared-platform changes
can be claimed to preserve macOS. GCP Linux builds alone are insufficient.

## Seven-day schedule

### Day 0–1: checkpoint and establish truth

- Let the current detached platform-branch build finish.
- If it passes, commit it as the final pre-swarm checkpoint.
- Record a fresh controlled cold-clean baseline on the selected worker.
- Record GN time, `libcef` time, distribution/shell time, compile actions,
  transitive labels, normalized inputs, parsed BUILD files, tracked LOC/bytes,
  runtime bytes, peak RSS, and disk writes.
- Establish worktrees, ownership ledger, queue, artifact provenance, and smoke
  gates.

### Day 1–2: cut the root graph

- Add the explicit minimal vimbrowser runtime target.
- Remove `//chrome:dependencies` from CEF.
- Split the retained pieces out of `//chrome/browser:browser`.
- Resolve missing symbols with narrow dependencies or interface replacement.
- Produce the first buildable runtime without either monolithic dependency.

This is the riskiest and highest-value part of the sprint.

### Day 2–4: high-value feature cuts

Prepare in parallel worktrees and admit serially:

- Cast/Media Router;
- extension skeleton;
- WebAuthn and hardware/device APIs;
- multiple-profile/incognito surfaces;
- enterprise/updater/packaging machinery.

### Day 4–5: physical deletion and dependency cleanup

After graph disconnection:

- delete implementations;
- remove GRIT/WebUI/resources and generated inputs;
- remove feature-specific prefs, flags, metrics, policies, protos, and tests;
- remove third-party dependencies with no retained consumer;
- verify that source deletion corresponds to graph/input reduction.

### Day 6: clean builds and retained-feature gates

Validate:

- normal browsing and navigation;
- tabs/windows and popup-as-tab semantics;
- history and bookmarks;
- downloads/uploads/file chooser;
- cookies and all retained site storage;
- service workers and retained PWA behavior;
- audio/video, codecs, Widevine, and fullscreen;
- PDF and DevTools;
- camera, microphone, and WebRTC;
- Linux X11;
- real macOS build and smoke test when a Mac worker is available.

### Day 7: stabilization and landing

- fix regressions;
- reject graph-growing or marginal candidates;
- run final clean-build measurements and benchmarks;
- merge accepted work into `main` through the integration authority;
- delete all temporary branches/worktrees;
- push the final checkpoint;
- only then consider promoting/installing the new stable runtime.

## Dual-objective acceptance rules

Maintain both total tracked-source LOC and first-party/non-generated LOC so
third-party corpus deletion cannot obscure product progress.

Measure two build classes separately:

- **cold clean:** fresh output, fixed machine/toolchain, no object/action-cache
  hits; this is the honest buildability metric;
- **warm developer loop:** persistent output and Siso state; this measures edit
  latency but must not be reported as clean-build improvement.

A candidate is accepted only if:

- LOC or clean build work improves materially;
- the other objective does not regress materially (normally no more than 2%);
- the active closure does not grow without explicit architectural justification;
- all retained-feature gates pass;
- artifact provenance and rollback are complete.

## Aggressive first-week targets

| Metric | Current | Week-one target | Stretch |
|---|---:|---:|---:|
| Transitive labels | 16,420 | below 12,000 | below 10,000 |
| `libcef.so` inputs | 186,406 | below 140,000 | near 100,000 |
| Additional backend LOC | about 61M estimated remaining | remove 1–3M | more if the root cut exposes large trees |
| Controlled clean build | fresh baseline required | 20–40% faster | 50% or more |
| CEF dependency on Chrome monolith | present | removed | — |

The architectural outcome—removing the monolithic Chrome dependency—is more
important than maximizing a cosmetic LOC number.

## Realistic build-time floor

Retaining Blink, V8, Skia, networking, media, WebRTC, PDF, DevTools, storage,
service workers, PWA behavior, Linux X11, and macOS imposes a substantial floor.

Longer-term expectations after major graph reduction:

- warm incremental builds: seconds to a few minutes;
- local 12-core clean builds: approximately 30–60 minutes;
- 64–96-thread cloud clean builds: potentially 10–30 minutes;
- single-digit-minute cold builds: unlikely without distributed compilation,
  substantially larger hardware, or removing retained capability families.

## Decisions required before implementation

- [ ] Approve temporary disposable branches/worktrees while keeping `main` as
      the only long-lived branch.
- [ ] Set a cloud budget ceiling for the one-week sprint.
- [ ] Approve stopping the unrelated PMV4 worker temporarily, or wait/request
      quota for a 56–96-vCPU vimbrowser worker.
- [ ] Provide a macOS build worker, or explicitly accept deferred real macOS
      validation.
- [ ] Confirm that the current detached platform sweep should be validated and
      checkpointed before the swarm baseline is frozen.
