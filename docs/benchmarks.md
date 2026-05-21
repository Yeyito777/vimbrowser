# vimbrowser benchmarks

`./scripts/vimbrowser-benchmark` is the programmatic performance harness for
tracking browser responsiveness. It launches vimbrowser inside a nested `xenv`,
drives the app through the native IPC protocol, and emits both a text report and
machine-readable JSON.

The harness is intentionally split into two classes of benchmarks:

- **Local deterministic suite** (`--suite local`): code-level regression test
  backed by an in-process HTTP fixture. This is the suite to run before/after
  shell/backend responsiveness work.
- **Live web suite** (`--suite live`): diagnostic tracking for real network
  pages such as YouTube, GitHub, Discord, and Reddit. These numbers include
  network, server, geolocation, and bot-check variance, so they are not used as
  hard regression gates by default.

## Commands

```sh
# Deterministic local performance test. Exits non-zero on regression.
make benchmark

# Live page-load tracking for YouTube/GitHub/Discord/Reddit.
make benchmark-live

# Run both suites. Local thresholds are checked; live failures are reported.
make benchmark-all

# Direct invocation with JSON saved for time-series tracking.
scripts/vimbrowser-benchmark --suite all --output /tmp/vimbrowser-benchmark.json
```

By default the Make targets benchmark `build-source/Release/vimbrowser`. Override
with `BENCH_BINARY=/path/to/vimbrowser` or pass `--binary` directly.

## Local suite metrics

The local suite tracks:

- **App startup**
  - `startup.single.ipc_ready_ms`: process launch to IPC availability
  - `startup.single.page_complete_elapsed_ms`: process launch to first page load
    completion
  - `startup.restore.*`: restored multi-tab session startup/load timings
- **Web page loading/drawing**
  - `local.open_page.open_cmd_ms`: IPC `open-tab` command latency
  - `local.open_page.observed_complete_ms`: navigation command to complete page
  - `local.open_page.load_event_ms`: page `load` event from browser performance
    timings
  - `local.open_page.first_contentful_paint_ms`: renderer-reported FCP where
    available
  - `local.screenshot.active_ms`: active-tab IPC screenshot/capture latency
- **Tab responsiveness**
  - `local.tab_switch.cmd_ms`: IPC `tab-focus` command latency
  - `local.tab_switch.visible_ms`: switch command to target
    `document.visibilityState === "visible"`
  - `local.tab_switch.first_raf_ms`: switch command to first fresh rAF in the
    target tab
  - `local.tab_switch.first_interval_ms`: switch command to first fresh interval
    tick
- **Background throttling sanity**
  - active vs background `requestAnimationFrame` and timer deltas over a fixed
    window. Hidden background tabs should have near-zero rAF deltas.

`--check` enforces conservative local budgets encoded in
`scripts/vimbrowser-benchmark`. These are deliberately loose enough to survive
normal machine variance while still catching big regressions such as tabs not
being hidden/throttled.

## Live suite metrics

The live suite currently benchmarks:

- `https://www.youtube.com/`
- `https://github.com/`
- `https://discord.com/`
- `https://www.reddit.com/`

Override or extend the list with repeated `--live-url` arguments:

```sh
scripts/vimbrowser-benchmark --suite live \
  --live-url https://www.youtube.com/ \
  --live-url https://github.com/
```

For each page the harness records the requested/final URL, title, observed ready
time, navigation timing, `load` timing, and paint timing entries where Chromium
exposes them. Some SPAs and bot-check pages do not fire or finalize all standard
navigation fields in a short benchmark window; the raw JSON keeps the page record
so trend analysis can decide which field is most meaningful for that site.

Use `--strict-live` if a live failure should make the process exit non-zero.

## JSON output

Every run prints a final `JSON_RESULTS=...` line and can also write pretty JSON:

```sh
scripts/vimbrowser-benchmark --suite local --output bench.json
```

The JSON schema is intentionally simple:

```json
{
  "benchmark_version": 1,
  "binary": ".../vimbrowser",
  "suites": [
    {
      "suite": "local",
      "metrics": { "local.tab_switch.visible_ms": { "median": 55.5 } },
      "records": { "...": "raw per-page/per-tab diagnostics" }
    }
  ]
}
```

Persist these JSON files externally if we want a long-term time series. The repo
stores the harness and thresholds, not generated benchmark artifacts.

## Baseline snapshot: 2026-05-20

This is the first checked-in baseline after adding the harness. It was measured
against:

- Browser code: `e46911a6d8 Throttle hidden tab web contents`
- Harness/docs commit: `2aa1a6de82 Add browser performance benchmark harness`
- Binary: `build-source/Release/vimbrowser`
- Environment: nested `xenv`, temporary fresh profiles

Use this as the initial reference point for future optimization work. Re-run the
same commands on a candidate build and compare medians/p95s rather than treating
single live-web samples as exact constants.

### Local deterministic baseline

Command:

```sh
make benchmark
```

Result: `CHECK: PASS`

| Metric | Median | p95 | Notes |
| --- | ---: | ---: | --- |
| `startup.single.ipc_ready_ms` | 425.8 ms | 425.8 ms | app launch to IPC availability |
| `startup.single.page_complete_elapsed_ms` | 598.1 ms | 598.1 ms | app launch to first page complete |
| `startup.single.first_contentful_paint_ms` | 132.0 ms | 132.0 ms | renderer FCP for first local page |
| `startup.restore.ipc_ready_ms` | 654.8 ms | 654.8 ms | restored six-tab session to IPC |
| `startup.restore.all_complete_elapsed_ms` | 1356.9 ms | 1356.9 ms | restored six-tab session all complete |
| `startup.restore.page_load_event_ms` | 684.8 ms | 731.2 ms | restored pages' load events |
| `startup.restore.first_contentful_paint_ms` | 336.0 ms | 336.0 ms | restored pages' FCP sample |
| `local.open_page.open_cmd_ms` | 55.4 ms | 68.5 ms | IPC `open-tab` command latency |
| `local.open_page.observed_complete_ms` | 272.8 ms | 303.9 ms | heavy local page complete observed by IPC |
| `local.open_page.load_event_ms` | 212.6 ms | 224.4 ms | heavy local page load event |
| `local.open_page.first_contentful_paint_ms` | 124.0 ms | 134.8 ms | heavy local page FCP |
| `local.screenshot.active_ms` | 71.5 ms | 71.5 ms | active-tab screenshot IPC |
| `local.tab_switch.cmd_ms` | 31.4 ms | 33.3 ms | IPC `tab-focus` command |
| `local.tab_switch.visible_ms` | 62.6 ms | 68.3 ms | switch to `visibilityState === "visible"` |
| `local.tab_switch.first_raf_ms` | 62.6 ms | 68.3 ms | switch to first fresh rAF |
| `local.tab_switch.first_interval_ms` | 62.6 ms | 68.3 ms | switch to first fresh interval tick |

Background throttling sanity from the same run:

| Metric | Value |
| --- | ---: |
| `local.background_throttle.active_raf_delta` | 130 |
| `local.background_throttle.background_raf_delta_max` | 0 |
| `local.background_throttle.active_interval_delta` | 43 |
| `local.background_throttle.background_interval_delta_max` | 3 |

Interpretation: the active tab continued painting/ticking, while hidden tabs had
near-zero rAF activity. This is the desired baseline after the CEF visibility
fix.

### Live web baseline

Command:

```sh
make benchmark-live
```

Live pages are network- and site-dependent; use these as rough tracking values,
not hard pass/fail thresholds.

| Site | Observed ready | Load event | FCP | Final title / note |
| --- | ---: | ---: | ---: | --- |
| YouTube | 10084.1 ms | 3411.9 ms | 972.0 ms | `YouTube` |
| GitHub | 1078.9 ms | 0.0 ms | n/a | `GitHub · Change is constant. GitHub keeps you ahead. · GitHub` |
| Discord | 1270.0 ms | 0.0 ms | 1080.0 ms | `Discord - Group Chat That’s All Fun & Games` |
| Reddit | 382.5 ms | 0.0 ms | n/a | `Reddit - Please wait for verification` |

Aggregate live metrics from that run:

| Metric | Median | p95 | Notes |
| --- | ---: | ---: | --- |
| `live.open_cmd_ms` | 51.3 ms | 61.7 ms | IPC open command for each live URL |
| `live.observed_ready_ms` | 1174.4 ms | 8761.9 ms | dominated by YouTube in this run |
| `live.first_contentful_paint_ms` | 1026.0 ms | 1074.6 ms | available for YouTube/Discord only in this run |
| `live.load_event_ms` | 0.0 ms | 2900.1 ms | SPAs often leave load timing at 0 during the benchmark window |

Notes for future comparisons:

- YouTube was the outlier in observed-ready time; its browser `load` event was
  ~3.4s, but the harness waited longer for its readiness heuristic.
- GitHub and Discord were visibly/title/text ready quickly, but standard load
  timing fields were still zero in this short SPA-oriented sample.
- Reddit returned a verification/interstitial page, so it should be tracked as
  a network/site behavior signal rather than a real reddit feed load.
