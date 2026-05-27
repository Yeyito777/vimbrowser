# Dirty build benchmark run

- UTC timestamp: 20260527T230632Z
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
| `shell_commands_build_source` | `src/browser_window_commands.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser command-line/autocomplete shell edit; tracks code split out of browser_window.cc. |
| `shell_autocomplete_build_source` | `src/browser_window_autocomplete.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser command autocomplete/rendering shell edit; tracks code split out of command handling. |
| `shell_tabs_build_source` | `src/browser_window_tabs.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser tab lifecycle/state shell edit; tracks code split out of browser_window.cc. |
| `shell_ipc_build_source` | `src/browser_window_ipc.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser IPC/cookie/network/screenshot shell edit; tracks code split out of browser_window.cc. |
| `shell_internal_build_source` | `src/browser_window_internal.cc` | `make build-source JOBS=${BENCH_JOBS}` | Vimbrowser stable helper shell edit; tracks helper code split out of browser_window.cc. |

## Results

| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |
| --- | ---: | ---: | ---: | ---: | --- |
| `noop_backend_dev` | 3 | 1.923 | 1.918 | 1.965 | `0, 0, 0` |
| `cef_cc_backend_dev` | 3 | 17.099 | 16.925 | 17.795 | `0, 0, 0` |
| `vimbrowser_cef_api_backend_dev` | 3 | 16.952 | 16.867 | 17.045 | `0, 0, 0` |
| `cef_platform_config_backend_dev` | 3 | 16.033 | 15.820 | 16.604 | `0, 0, 0` |
| `cef_platform_accessibility_backend_dev` | 3 | 16.830 | 16.438 | 16.945 | `0, 0, 0` |
| `cef_platform_printing_backend_dev` | 3 | 16.882 | 16.823 | 17.207 | `0, 0, 0` |
| `shell_cc_build_source` | 3 | 3.228 | 3.133 | 3.316 | `0, 0, 0` |
| `shell_commands_build_source` | 3 | 2.116 | 2.099 | 2.221 | `0, 0, 0` |
| `shell_autocomplete_build_source` | 3 | 3.682 | 3.544 | 3.737 | `0, 0, 0` |
| `shell_tabs_build_source` | 3 | 1.992 | 1.922 | 2.006 | `0, 0, 0` |
| `shell_ipc_build_source` | 3 | 3.089 | 2.983 | 3.095 | `0, 0, 0` |
| `shell_internal_build_source` | 3 | 2.604 | 2.535 | 2.718 | `0, 0, 0` |
