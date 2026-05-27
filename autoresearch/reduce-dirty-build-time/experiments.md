# Experiment log

All dirty-build-time experiments go here. Successful experiments get committed;
failed experiments are reverted/stashed/deleted and logged with the benchmark
evidence that rejected them.

| ID | Status | Change | Benchmark evidence | Decision |
| --- | --- | --- | --- | --- |
| baseline-000 | success | Created deterministic dirty-build benchmark and ran `--suite fast --runs 1`. | `noop_backend_dev` 17.782s; `cef_cc_backend_dev` 67.735s; `shell_cc_build_source` 13.347s. See `runs/20260527T073708Z-fast-baseline-000/summary.md`. | Baseline accepted; start experiments against this wall-clock benchmark. |
| exp001-skip-gn-gen | success | Keep Chromium `args.gn` mtime stable when content is unchanged and skip explicit `gn gen` when `build.ninja` already exists. | `--suite fast --runs 1`: `noop_backend_dev` 11.281s (-36.6%); `cef_cc_backend_dev` 39.824s (-41.2%); `shell_cc_build_source` 8.633s (-35.3%). See `runs/20260527T074154Z-fast-exp001-skip-gn-gen/summary.md`. | Accepted and committed: removes repeated GN generation/config churn from backend edit loop. |
