# Dirty build benchmark run

- UTC timestamp: 20260527T225418Z
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
| `noop_backend_dev` | 3 | 2.039 | 1.989 | 2.112 | `0, 0, 0` |
| `cef_cc_backend_dev` | 3 | 17.129 | 16.018 | 17.744 | `0, 0, 0` |
| `vimbrowser_cef_api_backend_dev` | 3 | 17.023 | 16.951 | 17.827 | `0, 0, 0` |
| `cef_platform_config_backend_dev` | 3 | 17.153 | 16.801 | 17.342 | `0, 0, 0` |
| `cef_platform_accessibility_backend_dev` | 3 | 17.070 | 17.055 | 17.119 | `0, 0, 0` |
| `cef_platform_printing_backend_dev` | 3 | 17.075 | 17.073 | 17.215 | `0, 0, 0` |
| `shell_cc_build_source` | 3 | 3.146 | 3.118 | 3.439 | `0, 0, 0` |
