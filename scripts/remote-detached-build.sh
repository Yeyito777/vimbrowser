#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
job_root=${VIMBROWSER_REMOTE_JOB_ROOT:-${HOME}/vimbrowser-build-jobs}
unit_name=${VIMBROWSER_REMOTE_BUILD_UNIT:-vimbrowser-build.service}

usage() {
  cat <<'EOF'
usage: scripts/remote-detached-build.sh <run|status> <tree>

The run command is intended for the persistent systemd unit installed by
gcloud-build-worker.sh. A completed attempt writes its exit status atomically;
an interrupted VM can therefore rerun the same tree and resume from Chromium's
persistent incremental output.
EOF
}

job_dir_for_tree() {
  printf '%s/%s\n' "${job_root}" "$1"
}

run_job() {
  local tree=$1 job_dir attempt_file attempt rc
  job_dir=$(job_dir_for_tree "${tree}")
  mkdir -p "${job_dir}"

  if [[ -f "${job_dir}/exit-code" ]]; then
    sudo systemctl disable "${unit_name}" >/dev/null 2>&1 || true
    return 0
  fi

  test "$(git -C "${repo_dir}" rev-parse 'HEAD^{tree}')" = "${tree}"
  attempt_file="${job_dir}/attempts"
  attempt=1
  if [[ -f "${attempt_file}" ]]; then
    attempt=$(( $(cat "${attempt_file}") + 1 ))
  fi
  printf '%s\n' "${attempt}" >"${attempt_file}.tmp"
  mv "${attempt_file}.tmp" "${attempt_file}"
  printf '%s\n' "$(date --iso-8601=seconds)" >"${job_dir}/started-at"
  printf 'tree=%s\nattempt=%s\n' "${tree}" "${attempt}" \
    >"${job_dir}/metadata.txt"

  # A VM shutdown or unit stop must leave the job incomplete so the enabled
  # unit resumes it on the next boot. Only an ordinary build return writes the
  # terminal exit-code marker.
  trap 'exit 143' TERM INT
  set +e
  JOBS=$(nproc) "${repo_dir}/scripts/remote-build-worker.sh" build \
    >>"${job_dir}/build.log" 2>&1
  rc=$?
  set -e

  printf '%s\n' "${rc}" >"${job_dir}/exit-code.tmp"
  mv "${job_dir}/exit-code.tmp" "${job_dir}/exit-code"
  printf '%s\n' "$(date --iso-8601=seconds)" >"${job_dir}/finished-at"
  sync
  sudo systemctl disable "${unit_name}" >/dev/null 2>&1 || true
  return "${rc}"
}

status_job() {
  local tree=$1 job_dir state
  job_dir=$(job_dir_for_tree "${tree}")
  printf 'tree=%s\njob_dir=%s\n' "${tree}" "${job_dir}"
  if [[ -f "${job_dir}/exit-code" ]]; then
    printf 'state=finished\nexit_code=%s\n' "$(cat "${job_dir}/exit-code")"
    [[ ! -f "${job_dir}/attempts" ]] || \
      printf 'attempts=%s\n' "$(cat "${job_dir}/attempts")"
    [[ ! -f "${job_dir}/finished-at" ]] || \
      printf 'finished_at=%s\n' "$(cat "${job_dir}/finished-at")"
    return 0
  fi
  state=$(systemctl is-active "${unit_name}" 2>/dev/null || true)
  printf 'state=%s\n' "${state:-unknown}"
  [[ ! -f "${job_dir}/attempts" ]] || \
    printf 'attempts=%s\n' "$(cat "${job_dir}/attempts")"
  [[ ! -f "${job_dir}/started-at" ]] || \
    printf 'started_at=%s\n' "$(cat "${job_dir}/started-at")"
}

case "${1:-}" in
  run)
    [[ $# == 2 ]] || { usage >&2; exit 2; }
    run_job "$2"
    ;;
  status)
    [[ $# == 2 ]] || { usage >&2; exit 2; }
    status_job "$2"
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
