# Dirty build benchmark run

- UTC timestamp: 20260527T223849Z
- Suite: fast
- Runs per scenario: 3
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
| `noop_backend_dev` | 3 | 1.957 | 1.947 | 2.024 | `0, 0, 0` |
| `cef_cc_backend_dev` | 3 | 17.468 | 17.279 | 18.621 | `0, 0, 0` |
| `vimbrowser_cef_api_backend_dev` | 3 | 17.462 | 17.446 | 17.562 | `0, 0, 0` |
| `cef_platform_config_backend_dev` | 3 | 17.522 | 17.440 | 17.552 | `0, 0, 0` |
| `cef_platform_accessibility_backend_dev` | 3 | 17.508 | 17.206 | 17.647 | `0, 0, 0` |
| `cef_platform_printing_backend_dev` | 3 | 16.983 | 16.956 | 17.034 | `0, 0, 0` |
| `shell_cc_build_source` | 3 | 5.430 | 5.420 | 5.504 | `0, 0, 0` |
