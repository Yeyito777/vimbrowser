# Dirty build benchmark run

- UTC timestamp: 20260527T172408Z
- Suite: fast
- Runs per scenario: 1
- Jobs: 12
- Primary metric: wall-clock seconds

| Scenario | Dirty path | Command | Description |
| --- | --- | --- | --- |
| `noop_backend_dev` | `` | `make backend-dev JOBS=${BENCH_JOBS}` | No source dirtied; fixed overhead of the canonical whole backend-dev loop. |
| `cef_cc_backend_dev` | `backend/chromium/cef/libcef/browser/browser_platform_delegate.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Representative CEF backend implementation edit; canonical whole-loop dirty backend case. |
| `vimbrowser_cef_api_backend_dev` | `backend/chromium/cef/libcef/browser/vimbrowser_browser_api.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Vimbrowser-specific CEF browser API edit; measures the backend code this project changes most often. |
| `shell_cc_build_source` | `src/browser_window.cc` | `make build-source JOBS=${BENCH_JOBS}` | Top-level vimbrowser shell edit; isolates CMake/source-distribution shell loop. |

## Results

| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |
| --- | ---: | ---: | ---: | ---: | --- |
| `noop_backend_dev` | 1 | 3.671 | 3.671 | 3.671 | `0` |
| `cef_cc_backend_dev` | 1 | 31.754 | 31.754 | 31.754 | `0` |
| `vimbrowser_cef_api_backend_dev` | 1 | 20.004 | 20.004 | 20.004 | `0` |
| `shell_cc_build_source` | 1 | 9.098 | 9.098 | 9.098 | `0` |
