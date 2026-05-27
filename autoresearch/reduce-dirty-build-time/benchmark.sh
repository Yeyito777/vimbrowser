#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "${script_dir}/../.." && pwd)
runs_dir="${script_dir}/runs"

suite=fast
runs=1
jobs=${JOBS:-$(nproc)}
tag=""

usage() {
  cat <<'EOF'
usage: autoresearch/reduce-dirty-build-time/benchmark.sh [options]

Options:
  --suite fast|full|smoke     Scenario set to run. Default: fast.
  --runs N                    Repetitions per scenario. Default: 1.
  --jobs N                    JOBS value passed to make. Default: nproc/JOBS.
  --tag NAME                  Human label included in output directory name.
  -h, --help                  Show this help.

Primary metric: wall-clock seconds.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite)
      suite=${2:?missing value for --suite}
      shift 2
      ;;
    --runs)
      runs=${2:?missing value for --runs}
      shift 2
      ;;
    --jobs)
      jobs=${2:?missing value for --jobs}
      shift 2
      ;;
    --tag)
      tag=${2:?missing value for --tag}
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "${suite}" in
  fast|full|smoke) ;;
  *)
    echo "error: --suite must be fast, full, or smoke" >&2
    exit 2
    ;;
esac

if ! [[ "${runs}" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: --runs must be a positive integer" >&2
  exit 2
fi

if ! [[ "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: --jobs must be a positive integer" >&2
  exit 2
fi

require_path() {
  local rel=$1
  if [[ ! -e "${repo_dir}/${rel}" ]]; then
    echo "error: required path is missing: ${rel}" >&2
    exit 1
  fi
}

require_path Makefile
require_path backend/chromium/out/Release_GN_x64/build.ninja
require_path backend/depot_tools/autoninja

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
safe_tag="${tag//[^A-Za-z0-9_.-]/_}"
if [[ -n "${safe_tag}" ]]; then
  run_dir="${runs_dir}/${timestamp}-${suite}-${safe_tag}"
else
  run_dir="${runs_dir}/${timestamp}-${suite}"
fi
logs_dir="${run_dir}/logs"
mkdir -p "${logs_dir}"
results_jsonl="${run_dir}/results.jsonl"
summary_md="${run_dir}/summary.md"

declare -a scenario_names=()
declare -A scenario_dirty=()
declare -A scenario_command=()
declare -A scenario_description=()

add_scenario() {
  local name=$1
  local dirty=$2
  local command=$3
  local description=$4
  scenario_names+=("${name}")
  scenario_dirty["${name}"]="${dirty}"
  scenario_command["${name}"]="${command}"
  scenario_description["${name}"]="${description}"
}

make_cmd='make backend-dev JOBS=${BENCH_JOBS}'
shell_cmd='make build-source JOBS=${BENCH_JOBS}'

add_scenario \
  noop_backend_dev \
  '' \
  "${make_cmd}" \
  'No source dirtied; fixed overhead of the canonical whole backend-dev loop.'

if [[ "${suite}" != "smoke" ]]; then
  add_scenario \
    cef_cc_backend_dev \
    'backend/chromium/cef/libcef/browser/browser_platform_delegate.cc' \
    "${make_cmd}" \
    'Representative CEF backend implementation edit; canonical whole-loop dirty backend case.'

  add_scenario \
    shell_cc_build_source \
    'src/browser_window.cc' \
    "${shell_cmd}" \
    'Top-level vimbrowser shell edit; isolates CMake/source-distribution shell loop.'
fi

if [[ "${suite}" == "full" ]]; then
  add_scenario \
    blink_style_backend_dev \
    'backend/chromium/third_party/blink/renderer/core/css/resolver/style_resolver.cc' \
    "${make_cmd}" \
    'Blink style resolver edit; representative native shader/backend edit.'

  add_scenario \
    native_theme_backend_dev \
    'backend/chromium/ui/native_theme/native_theme.cc' \
    "${make_cmd}" \
    'Native theme/scrollbar implementation edit; representative Chromium UI backend edit.'
fi

touch_dirty_file() {
  local rel=$1
  [[ -n "${rel}" ]] || return 0
  require_path "${rel}"
  python3 - "${repo_dir}/${rel}" <<'PY'
import os
import sys
path = sys.argv[1]
os.utime(path, None)
PY
}

json_escape() {
  python3 -c 'import json, sys; print(json.dumps(sys.stdin.read()))'
}

write_json_result() {
  local scenario=$1
  local run_index=$2
  local dirty=$3
  local command=$4
  local seconds=$5
  local status=$6
  local log_rel=$7
  python3 - "$results_jsonl" "$timestamp" "$suite" "$scenario" "$run_index" "$dirty" "$command" "$seconds" "$status" "$log_rel" "$jobs" <<'PY'
import json
import sys

out, timestamp, suite, scenario, run_index, dirty, command, seconds, status, log_rel, jobs = sys.argv[1:]
record = {
    "timestamp_utc": timestamp,
    "suite": suite,
    "scenario": scenario,
    "run": int(run_index),
    "dirty_path": dirty or None,
    "command": command,
    "wall_seconds": float(seconds),
    "exit_code": int(status),
    "jobs": int(jobs),
    "log": log_rel,
}
with open(out, "a", encoding="utf-8") as f:
    f.write(json.dumps(record, sort_keys=True) + "\n")
PY
}

run_one() {
  local scenario=$1
  local run_index=$2
  local dirty=${scenario_dirty["${scenario}"]}
  local command=${scenario_command["${scenario}"]}
  local log_path="${logs_dir}/${scenario}-${run_index}.log"
  local log_rel="${log_path#${script_dir}/}"

  echo "[benchmark] scenario=${scenario} run=${run_index}/${runs} dirty=${dirty:-<none>}"
  touch_dirty_file "${dirty}"

  local start_ns end_ns status seconds
  start_ns=$(date +%s%N)
  set +e
  (
    cd "${repo_dir}"
    export BENCH_JOBS="${jobs}"
    export PATH="${repo_dir}/backend/depot_tools:${PATH}"
    bash -lc "${command}"
  ) >"${log_path}" 2>&1
  status=$?
  set -e
  end_ns=$(date +%s%N)
  seconds=$(python3 - "$start_ns" "$end_ns" <<'PY'
import sys
start = int(sys.argv[1])
end = int(sys.argv[2])
print(f"{(end - start) / 1_000_000_000:.3f}")
PY
)

  write_json_result "${scenario}" "${run_index}" "${dirty}" "${command}" "${seconds}" "${status}" "${log_rel}"
  printf '[benchmark] %-28s run=%s wall=%ss exit=%s log=%s\n' "${scenario}" "${run_index}" "${seconds}" "${status}" "${log_rel}"
  return "${status}"
}

cat >"${summary_md}" <<EOF
# Dirty build benchmark run

- UTC timestamp: ${timestamp}
- Suite: ${suite}
- Runs per scenario: ${runs}
- Jobs: ${jobs}
- Primary metric: wall-clock seconds

| Scenario | Dirty path | Command | Description |
| --- | --- | --- | --- |
EOF

for scenario in "${scenario_names[@]}"; do
  printf '| `%s` | `%s` | `%s` | %s |\n' \
    "${scenario}" \
    "${scenario_dirty["${scenario}"]:-}" \
    "${scenario_command["${scenario}"]}" \
    "${scenario_description["${scenario}"]}" >>"${summary_md}"
done

echo >>"${summary_md}"
echo '## Results' >>"${summary_md}"
echo >>"${summary_md}"
echo '| Scenario | Runs | Median wall seconds | Min | Max | Exit codes |' >>"${summary_md}"
echo '| --- | ---: | ---: | ---: | ---: | --- |' >>"${summary_md}"

overall_status=0
for scenario in "${scenario_names[@]}"; do
  for ((run_index = 1; run_index <= runs; run_index++)); do
    if ! run_one "${scenario}" "${run_index}"; then
      overall_status=1
    fi
  done
done

python3 - "$results_jsonl" >>"${summary_md}" <<'PY'
import json
import statistics
import sys
from collections import defaultdict

path = sys.argv[1]
rows = []
with open(path, encoding="utf-8") as f:
    for line in f:
        rows.append(json.loads(line))

by_scenario = defaultdict(list)
for row in rows:
    by_scenario[row["scenario"]].append(row)

for scenario in by_scenario:
    values = [row["wall_seconds"] for row in by_scenario[scenario]]
    exits = ", ".join(str(row["exit_code"]) for row in by_scenario[scenario])
    print(
        f"| `{scenario}` | {len(values)} | {statistics.median(values):.3f} | "
        f"{min(values):.3f} | {max(values):.3f} | `{exits}` |"
    )
PY

ln -sfn "${run_dir}" "${runs_dir}/latest"

echo
echo "[benchmark] results: ${results_jsonl#${repo_dir}/}"
echo "[benchmark] summary: ${summary_md#${repo_dir}/}"

exit "${overall_status}"
