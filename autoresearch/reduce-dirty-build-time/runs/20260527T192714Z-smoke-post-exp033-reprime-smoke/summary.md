# Dirty build benchmark run

- UTC timestamp: 20260527T192714Z
- Suite: smoke
- Runs per scenario: 1
- Jobs: 12
- Primary metric: wall-clock seconds

| Scenario | Dirty path | Command | Description |
| --- | --- | --- | --- |
| `noop_backend_dev` | `` | `make backend-dev JOBS=${BENCH_JOBS}` | No source dirtied; fixed overhead of the canonical whole backend-dev loop. |

## Results

| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |
| --- | ---: | ---: | ---: | ---: | --- |
| `noop_backend_dev` | 1 | 1.830 | 1.830 | 1.830 | `0` |
