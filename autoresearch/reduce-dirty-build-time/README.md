# Autoresearch: reduce dirty build time

Topic: `reduce-dirty-build-time`

Primary metric: **wall-clock seconds**.

The first artifact in this autoresearch lane is the benchmark. Experiments are
only accepted when they improve this benchmark objectively; failures are logged
here and reverted/stashed/deleted instead of accumulating in the branch.

## Optimization target

The user asked for “the whole thing”, with backend edits being the main pain.
Therefore the canonical benchmark is the full backend edit loop:

```bash
make backend-dev
```

That command rebuilds the Chromium/CEF backend target, syncs runtime artifacts
into the source CEF distribution, rebuilds the vimbrowser shell, slims the
runtime, and reinstalls the wrapper.

The benchmark also exposes smaller scenarios so an experiment can identify and
attack the bottleneck quickly before re-running the canonical whole-loop case.

## Benchmark scenarios

`./autoresearch/reduce-dirty-build-time/benchmark.sh --suite fast` runs:

- `noop_backend_dev`: no dirty source; measures fixed overhead of the whole loop.
- `cef_cc_backend_dev`: touches a representative CEF backend `.cc` file and runs
  `make backend-dev`.
- `shell_cc_build_source`: touches a top-level vimbrowser shell `.cc` file and
  runs `make build-source`.

`--suite full` additionally runs:

- `blink_style_backend_dev`: touches Blink style resolution, which contains
  vimbrowser's native shader work, and runs `make backend-dev`.
- `native_theme_backend_dev`: touches Chromium native theme/scrollbar code and
  runs `make backend-dev`.

## Acceptance rule

For fast iteration, use one run to reject obvious failures. Before keeping an
experiment, compare against the same benchmark suite and accept only if wall time
improves the relevant backend scenario by at least **5%** with no material
regression in the canonical `cef_cc_backend_dev` whole-loop scenario.

When time permits, confirm with `--runs 3` and compare medians.

## Commands

```bash
# Fast exploratory baseline/candidate run.
./autoresearch/reduce-dirty-build-time/benchmark.sh --suite fast --runs 1

# Slower confirmation.
./autoresearch/reduce-dirty-build-time/benchmark.sh --suite full --runs 3
```

Results are written under `autoresearch/reduce-dirty-build-time/runs/` with a
machine-readable `results.jsonl`, `summary.md`, and raw per-command logs.
