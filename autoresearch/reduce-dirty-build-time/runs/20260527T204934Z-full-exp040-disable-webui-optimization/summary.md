# Dirty build benchmark run

- UTC timestamp: 20260527T204934Z
- Suite: full
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
| `blink_style_backend_dev` | `backend/chromium/third_party/blink/renderer/core/css/resolver/style_resolver.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Blink style resolver edit; representative native shader/backend edit. |
| `native_theme_backend_dev` | `backend/chromium/ui/native_theme/native_theme.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Native theme/scrollbar implementation edit; representative Chromium UI backend edit. |

## Results

| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |
| --- | ---: | ---: | ---: | ---: | --- |
| `noop_backend_dev` | 1 | 1.885 | 1.885 | 1.885 | `0` |
| `cef_cc_backend_dev` | 1 | 16.931 | 16.931 | 16.931 | `0` |
| `vimbrowser_cef_api_backend_dev` | 1 | 16.825 | 16.825 | 16.825 | `0` |
| `cef_platform_config_backend_dev` | 1 | 16.757 | 16.757 | 16.757 | `0` |
| `cef_platform_accessibility_backend_dev` | 1 | 16.617 | 16.617 | 16.617 | `0` |
| `cef_platform_printing_backend_dev` | 1 | 16.816 | 16.816 | 16.816 | `0` |
| `shell_cc_build_source` | 1 | 8.368 | 8.368 | 8.368 | `0` |
| `blink_style_backend_dev` | 1 | 36.696 | 36.696 | 36.696 | `0` |
| `native_theme_backend_dev` | 1 | 20.028 | 20.028 | 20.028 | `0` |
