# vimbrowser performance research

This directory records performance experiments, both successful and failed.

Loop:
1. Establish a baseline on a fixed commit/binary.
2. Make one logically motivated performance change.
3. Rebuild.
4. Run repeated local and/or live benchmarks appropriate to the change.
5. Keep and commit successful, repeatable improvements; revert failed variants.

No pushes/tags are done from this loop unless explicitly requested.

## Safety rules

- Benchmark-launched vimbrowser processes run with `ulimit -c 0` so Chromium crash storms cannot fill `/var/lib/systemd/coredump`.
- Keep benchmark temp data under `/tmp` unless explicitly needed; on this machine `/tmp` is tmpfs and far below the 100GB experiment budget.
- Before any custom non-harness vimbrowser/xenv crash-prone run, launch through `bash -lc 'ulimit -c 0 || true; exec ...'`.
- If an experiment starts crash-looping, stop xenv/benchmark processes first, then inspect disk/coredump usage before continuing.
