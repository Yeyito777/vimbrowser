# Dirty build benchmark run

- UTC timestamp: 20260527T150526Z
- Suite: full
- Runs per scenario: 1
- Jobs: 12
- Primary metric: wall-clock seconds

| Scenario | Dirty path | Command | Description |
| --- | --- | --- | --- |
| `noop_backend_dev` | `` | `make backend-dev JOBS=${BENCH_JOBS}` | No source dirtied; fixed overhead of the canonical whole backend-dev loop. |
| `cef_cc_backend_dev` | `backend/chromium/cef/libcef/browser/browser_platform_delegate.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Representative CEF backend implementation edit; canonical whole-loop dirty backend case. |
| `vimbrowser_cef_api_backend_dev` | `backend/chromium/cef/libcef/browser/vimbrowser_browser_api.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Vimbrowser-specific CEF browser API edit; measures the backend code this project changes most often. |
| `shell_cc_build_source` | `src/browser_window.cc` | `make build-source JOBS=${BENCH_JOBS}` | Top-level vimbrowser shell edit; isolates CMake/source-distribution shell loop. |
| `blink_style_backend_dev` | `backend/chromium/third_party/blink/renderer/core/css/resolver/style_resolver.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Blink style resolver edit; representative native shader/backend edit. |
| `native_theme_backend_dev` | `backend/chromium/ui/native_theme/native_theme.cc` | `make backend-dev JOBS=${BENCH_JOBS}` | Native theme/scrollbar implementation edit; representative Chromium UI backend edit. |

## Results

| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |
| --- | ---: | ---: | ---: | ---: | --- |
| `noop_backend_dev` | 1 | 1.753 | 1.753 | 1.753 | `0` |
| `cef_cc_backend_dev` | 1 | 33.415 | 33.415 | 33.415 | `0` |
| `vimbrowser_cef_api_backend_dev` | 1 | 21.922 | 21.922 | 21.922 | `0` |
| `shell_cc_build_source` | 1 | 8.270 | 8.270 | 8.270 | `0` |
| `blink_style_backend_dev` | 1 | 38.419 | 38.419 | 38.419 | `0` |
| `native_theme_backend_dev` | 1 | 21.936 | 21.936 | 21.936 | `0` |
