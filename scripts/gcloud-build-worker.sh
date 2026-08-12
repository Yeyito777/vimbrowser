#!/usr/bin/env bash
set -euo pipefail
shopt -s inherit_errexit

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
project=${VIMBROWSER_GCLOUD_PROJECT:-unified-adviser-462618-s0}
zone=${VIMBROWSER_GCLOUD_ZONE:-us-east1-b}
instance=${VIMBROWSER_GCLOUD_INSTANCE:-vimbrowser-build-worker}
preferred_machine=${VIMBROWSER_GCLOUD_MACHINE:-c2d-highcpu-56}
remote_repo=${VIMBROWSER_GCLOUD_REPO:-vimbrowser}
artifact_root=${VIMBROWSER_GCLOUD_ARTIFACT_ROOT:-/home/yeyito/Workspace/vimbrowser-debloat-artifacts}
quota_preference=vimbrowser-build-global-cpu-96
snapshot_ref=refs/vimbrowser-build/current

usage() {
  cat <<'EOF'
usage: scripts/gcloud-build-worker.sh <command>

Commands:
  status       Show VM, quota-request, cache, and stable-runtime status
  start        Start the persistent compilation worker
  stop         Stop compute while retaining its persistent build disk
  resize       Resize a stopped worker to VIMBROWSER_GCLOUD_MACHINE
  snapshot     Create an exact tracked-worktree Git bundle in /tmp
  sync         Start the worker and transfer/apply the current source snapshot
  bootstrap    Initialize Chromium dependencies on the worker, then stop it
  build        Snapshot, sync, build, fetch, verify, and stop automatically
  fetch        Retrieve the artifact for the currently checked-out remote tree
  ssh          Open an interactive IAP SSH session

The worker never installs into the normal local prefix. Fetched runtimes are
stored by Git tree ID under /home/yeyito/Workspace/vimbrowser-debloat-artifacts.
EOF
}

instance_status() {
  gcloud compute instances describe "${instance}" \
    --project="${project}" --zone="${zone}" --format='value(status)' 2>/dev/null || true
}

remote() {
  local command=$1 attempt rc
  for attempt in $(seq 1 18); do
    if gcloud compute ssh "${instance}" \
        --project="${project}" --zone="${zone}" --tunnel-through-iap --quiet \
        --command="${command}"; then
      return 0
    else
      rc=$?
    fi
    if [[ "${rc}" != 255 || "${attempt}" == 18 ]]; then
      return "${rc}"
    fi
    echo "[+] Worker SSH is not ready; retrying (${attempt}/18)." >&2
    sleep 5
  done
}

copy_with_retry() {
  local source=$1 destination=$2 attempt rc
  for attempt in $(seq 1 18); do
    if gcloud compute scp "${source}" "${destination}" \
        --project="${project}" --zone="${zone}" --tunnel-through-iap --quiet; then
      return 0
    else
      rc=$?
    fi
    if [[ "${rc}" != 255 || "${attempt}" == 18 ]]; then
      return "${rc}"
    fi
    echo "[+] Worker SCP is not ready; retrying (${attempt}/18)." >&2
    sleep 5
  done
}

start() {
  local status
  status=$(instance_status)
  case "${status}" in
    RUNNING) ;;
    TERMINATED)
      gcloud compute instances start "${instance}" \
        --project="${project}" --zone="${zone}" --quiet
      ;;
    '')
      echo "error: worker does not exist: ${instance}" >&2
      exit 1
      ;;
    *)
      echo "error: worker is in unexpected state ${status}" >&2
      exit 1
      ;;
  esac
}

stop() {
  if [[ "$(instance_status)" == RUNNING ]]; then
    gcloud compute instances stop "${instance}" \
      --project="${project}" --zone="${zone}" --quiet
  fi
}

stable_verify() {
  "${repo_dir}/scripts/debloat-sandbox.sh" verify
}

create_snapshot() {
  local index pathspec bundle tree commit ref
  index=$(mktemp /tmp/vimbrowser-build-index.XXXXXX)
  pathspec=$(mktemp /tmp/vimbrowser-build-pathspec.XXXXXX)
  bundle=$(mktemp /tmp/vimbrowser-build-snapshot.XXXXXX.bundle)
  rm -f "${index}" "${bundle}"
  trap 'rm -f "${index:-}" "${pathspec:-}"' RETURN

  # Seed a temporary index with HEAD, then update only paths that actually
  # differ. Running `git add` across Chromium's entire checkout would reapply
  # line-ending filters to hundreds of thousands of unchanged vendor files.
  GIT_INDEX_FILE="${index}" git -C "${repo_dir}" read-tree HEAD
  git -C "${repo_dir}" diff --name-only -z HEAD -- >"${pathspec}"
  git -C "${repo_dir}" ls-files --others --exclude-standard -z -- \
    >>"${pathspec}"
  if [[ -s "${pathspec}" ]]; then
    # -f preserves deliberately tracked files whose upstream dependency has a
    # broad ignore rule (for example libc++ include/ext/__hash).
    GIT_INDEX_FILE="${index}" git -C "${repo_dir}" add -A -f \
      --pathspec-from-file="${pathspec}" --pathspec-file-nul
  fi
  tree=$(GIT_INDEX_FILE="${index}" git -C "${repo_dir}" write-tree)
  commit=$(printf 'Remote build snapshot for tree %s\n' "${tree}" | \
    git -C "${repo_dir}" commit-tree "${tree}" -p HEAD)
  ref="refs/vimbrowser-build/snapshot-${tree}"
  git -C "${repo_dir}" update-ref "${ref}" "${commit}"
  git -C "${repo_dir}" bundle create "${bundle}" \
    "${ref}" ^origin/main >/dev/null
  git -C "${repo_dir}" update-ref -d "${ref}"

  printf '%s\t%s\t%s\n' "${bundle}" "${tree}" "${commit}"
}

sync_snapshot() {
  local snapshot bundle tree commit remote_bundle
  start
  snapshot=$(create_snapshot)
  IFS=$'\t' read -r bundle tree commit <<<"${snapshot}"
  remote_bundle="/tmp/vimbrowser-${tree}.bundle"

  echo "[+] Transferring source tree ${tree}"
  copy_with_retry "${bundle}" "${instance}:${remote_bundle}"
  rm -f "${bundle}"

  remote "set -euo pipefail
    cd \"\${HOME}/${remote_repo}\"
    git fetch --force \"${remote_bundle}\" \
      \"refs/vimbrowser-build/snapshot-${tree}:${snapshot_ref}\"
    git reset --hard \"${snapshot_ref}\"
    git clean -fd
    rm -f \"${remote_bundle}\"
    test \"\$(git rev-parse 'HEAD^{tree}')\" = \"${tree}\"
    printf 'tree=%s\\ncommit=%s\\n' \"${tree}\" \"${commit}\""

  printf '%s\n' "${tree}"
}

remote_artifact_path() {
  remote "cd \"\${HOME}/${remote_repo}\" && ./scripts/remote-build-worker.sh artifact-path" | tail -n 1
}

fetch_artifact() {
  local remote_dir tree local_dir archive
  remote_dir=$(remote_artifact_path)
  tree=${remote_dir##*/}
  local_dir="${artifact_root}/${tree}"
  archive="${local_dir}/Release.tar.zst"
  mkdir -p "${local_dir}"

  echo "[+] Fetching runtime for tree ${tree}"
  for file in Release.tar.zst Release.tar.zst.sha256 Release.sha256 metadata.txt; do
    copy_with_retry "${instance}:${remote_dir}/${file}" "${local_dir}/${file}"
  done
  (
    cd "${local_dir}"
    archive_hash=$(awk 'NR == 1 { print $1 }' Release.tar.zst.sha256)
    printf '%s  Release.tar.zst\n' "${archive_hash}" | sha256sum --check -
    rm -rf Release
    tar --zstd -xf Release.tar.zst
    cd Release
    sha256sum --check ../Release.sha256
  )
  stable_verify
  echo "[+] Verified local sandbox artifact: ${local_dir}/Release"
}

case "${1:-}" in
  status)
    gcloud compute instances describe "${instance}" \
      --project="${project}" --zone="${zone}" \
      --format='table(name,status,machineType.basename(),disks[0].diskSizeGb:label=DISK_GB,lastStartTimestamp,lastStopTimestamp)' || true
    gcloud beta quotas preferences describe "${quota_preference}" \
      --project="${project}" \
      --format='yaml(quotaConfig.grantedValue,quotaConfig.preferredValue,reconciling,updateTime)' || true
    stable_verify
    ;;
  start)
    start
    ;;
  stop)
    stop
    ;;
  resize)
    stop
    gcloud compute instances set-machine-type "${instance}" \
      --project="${project}" --zone="${zone}" \
      --machine-type="${preferred_machine}" --quiet
    ;;
  snapshot)
    create_snapshot
    ;;
  sync)
    sync_snapshot
    stable_verify
    ;;
  bootstrap)
    start
    trap stop EXIT
    remote "sudo shutdown -c >/dev/null 2>&1 || true; sudo shutdown -h +360; cd \"\${HOME}/${remote_repo}\" && JOBS=\$(nproc) ./scripts/remote-build-worker.sh bootstrap"
    ;;
  build)
    start
    trap stop EXIT
    sync_output=$(sync_snapshot)
    printf '%s\n' "${sync_output}" >&2
    tree=$(tail -n 1 <<<"${sync_output}")
    remote "sudo shutdown -c >/dev/null 2>&1 || true; sudo shutdown -h +360; cd \"\${HOME}/${remote_repo}\" && JOBS=\$(nproc) ./scripts/remote-build-worker.sh build"
    fetch_artifact
    printf '[+] Remote build and fetch complete for tree %s\n' "${tree}"
    ;;
  fetch)
    start
    trap stop EXIT
    fetch_artifact
    ;;
  ssh)
    start
    exec gcloud compute ssh "${instance}" \
      --project="${project}" --zone="${zone}" --tunnel-through-iap
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
