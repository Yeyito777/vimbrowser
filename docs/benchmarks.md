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
