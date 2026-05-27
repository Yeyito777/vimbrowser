# Dirty build benchmark run

- UTC timestamp: 20260527T214708Z
- Suite: fast
- Runs per scenario: 1
- Jobs: 12
- Primary metric: wall-clock seconds

| Scenario | Dirty path | Command | Description |
| --- | --- | --- | --- |
| `noop_backend_dev` | `` | `make backend-dev JOBS=${BENCH_JOBS}` | No source dirtied; fixed overhead of the canonical whole backend-dev loop. |
| `cef_cc_backend_dev` | `backend/chromium/cef/libcef/browser/browser_platform_delegate.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Representative CEF backend implementation edit; canonical whole-loop dirty backend case. |
| `vimbrowser_cef_api_backend_dev` | `backend/chromium/cef/libcef/browser/vimbrowser_browser_api.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Vimbrowser-specific CEF browser API edit; measures the backend code this project changes most often. |
| `cef_platform_config_backend_dev` | `backend/chromium/cef/libcef/browser/browser_platform_delegate_config.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | CEF platform delegate configuration/printing/accessibility edit; tracks heavy code split out of the canonical delegate. |
| `cef_platform_accessibility_backend_dev` | `backend/chromium/cef/libcef/browser/browser_platform_delegate_accessibility.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | CEF platform delegate accessibility edit; tracks accessibility-specific code split out of the canonical delegate. |
| `cef_platform_printing_backend_dev` | `backend/chromium/cef/libcef/browser/browser_platform_delegate_printing.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | CEF platform delegate printing edit; tracks print-preview-specific code split out of the canonical delegate. |
| `shell_cc_build_source` | `src/browser_window.cc` | `make build-source JOBS=${BENCH_JOBS}` | Top-level vimbrowser shell edit; isolates CMake/source-distribution shell loop. |

## Results

| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |
| --- | ---: | ---: | ---: | ---: | --- |
| `noop_backend_dev` | 1 | 1.835 | 1.835 | 1.835 | `0` |
| `cef_cc_backend_dev` | 1 | 17.348 | 17.348 | 17.348 | `0` |
| `vimbrowser_cef_api_backend_dev` | 1 | 17.326 | 17.326 | 17.326 | `0` |
| `cef_platform_config_backend_dev` | 1 | 17.453 | 17.453 | 17.453 | `0` |
| `cef_platform_accessibility_backend_dev` | 1 | 16.935 | 16.935 | 16.935 | `0` |
| `cef_platform_printing_backend_dev` | 1 | 17.270 | 17.270 | 17.270 | `0` |
| `shell_cc_build_source` | 1 | 7.939 | 7.939 | 7.939 | `0` |
