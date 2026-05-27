# Dirty build benchmark run

- UTC timestamp: 20260527T230329Z
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
| `shell_commands_build_source` | `src/browser_window_commands.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser command-line/autocomplete shell edit; tracks code split out of browser_window.cc. |
| `shell_tabs_build_source` | `src/browser_window_tabs.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser tab lifecycle/state shell edit; tracks code split out of browser_window.cc. |
| `shell_ipc_build_source` | `src/browser_window_ipc.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser IPC/cookie/network/screenshot shell edit; tracks code split out of browser_window.cc. |
| `shell_internal_build_source` | `src/browser_window_internal.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser stable helper shell edit; tracks helper code split out of browser_window.cc. |

## Results

| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |
| --- | ---: | ---: | ---: | ---: | --- |
| `noop_backend_dev` | 1 | 1.884 | 1.884 | 1.884 | `0` |
| `cef_cc_backend_dev` | 1 | 18.744 | 18.744 | 18.744 | `0` |
| `vimbrowser_cef_api_backend_dev` | 1 | 17.173 | 17.173 | 17.173 | `0` |
| `cef_platform_config_backend_dev` | 1 | 16.987 | 16.987 | 16.987 | `0` |
| `cef_platform_accessibility_backend_dev` | 1 | 17.060 | 17.060 | 17.060 | `0` |
| `cef_platform_printing_backend_dev` | 1 | 17.203 | 17.203 | 17.203 | `0` |
| `shell_cc_build_source` | 1 | 3.277 | 3.277 | 3.277 | `0` |
| `shell_commands_build_source` | 1 | 3.867 | 3.867 | 3.867 | `0` |
| `shell_tabs_build_source` | 1 | 1.997 | 1.997 | 1.997 | `0` |
| `shell_ipc_build_source` | 1 | 3.136 | 3.136 | 3.136 | `0` |
| `shell_internal_build_source` | 1 | 2.707 | 2.707 | 2.707 | `0` |
