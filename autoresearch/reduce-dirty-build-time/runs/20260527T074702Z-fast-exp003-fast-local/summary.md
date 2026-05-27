# Dirty build benchmark run

- UTC timestamp: 20260527T074702Z
- Suite: fast
- Runs per scenario: 1
- Jobs: 12
- Primary metric: wall-clock seconds

| Scenario | Dirty path | Command | Description |
| --- | --- | --- | --- |
| `noop_backend_dev` | `` | `make backend-dev JOBS=${BENCH_JOBS}` | No source dirtied; fixed overhead of the canonical whole backend-dev loop. |
| `cef_cc_backend_dev` | `backend/chromium/cef/libcef/browser/browser_platform_delegate.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Representative CEF backend implementation edit; canonical whole-loop dirty backend case. |
| `shell_cc_build_source` | `src/browser_window.cc` | `make build-source JOBS=${BENCH_JOBS}` | Top-level vimbrowser shell edit; isolates CMake/source-distribution shell loop. |

## Results

| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |
| --- | ---: | ---: | ---: | ---: | --- |
| `noop_backend_dev` | 1 | 8.207 | 8.207 | 8.207 | `0` |
| `cef_cc_backend_dev` | 1 | 35.284 | 35.284 | 35.284 | `0` |
| `shell_cc_build_source` | 1 | 8.553 | 8.553 | 8.553 | `0` |
