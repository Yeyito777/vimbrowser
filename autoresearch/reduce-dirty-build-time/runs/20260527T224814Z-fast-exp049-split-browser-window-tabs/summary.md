# Dirty build benchmark run

- UTC timestamp: 20260527T224814Z
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
| `noop_backend_dev` | 3 | 1.961 | 1.844 | 1.974 | `0, 0, 0` |
| `cef_cc_backend_dev` | 3 | 17.217 | 16.710 | 17.917 | `0, 0, 0` |
| `vimbrowser_cef_api_backend_dev` | 3 | 16.989 | 16.962 | 17.043 | `0, 0, 0` |
| `cef_platform_config_backend_dev` | 3 | 16.895 | 16.820 | 16.981 | `0, 0, 0` |
| `cef_platform_accessibility_backend_dev` | 3 | 17.043 | 16.968 | 17.189 | `0, 0, 0` |
| `cef_platform_printing_backend_dev` | 3 | 16.953 | 16.948 | 17.138 | `0, 0, 0` |
| `shell_cc_build_source` | 3 | 4.765 | 4.668 | 4.836 | `0, 0, 0` |
