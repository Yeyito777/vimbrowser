#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
jobs=${JOBS:-$(nproc)}
chromium_build_dir=${CHROMIUM_BUILD_DIR:-Release_GN_x64}
shell_build=${VIMBROWSER_REMOTE_SHELL_BUILD:-${HOME}/vimbrowser-shell}
artifact_root=${VIMBROWSER_REMOTE_ARTIFACT_ROOT:-${HOME}/vimbrowser-artifacts}
bootstrap_marker=${HOME}/.cache/vimbrowser-build-worker/bootstrap-v1

usage() {
  cat <<'EOF'
usage: scripts/remote-build-worker.sh <bootstrap|build|artifact-path>

This script runs only inside the disposable GCloud compilation worker. The
local orchestration wrapper transfers an exact Git tree, invokes this script,
retrieves its checksummed Release bundle, and stops the VM.
EOF
}

latest_cef_dist() {
  find "${repo_dir}/backend/chromium/cef/binary_distrib" -maxdepth 1 -type d \
    -name 'cef_binary_*_linux64_minimal' -print 2>/dev/null | sort | tail -n 1
}

bootstrap() {
  export DEBIAN_FRONTEND=noninteractive
  local package missing_packages=()
  for package in libcurl4-openssl-dev librsvg2-dev; do
    if ! dpkg-query -W -f='${Status}' "${package}" 2>/dev/null | \
        grep -q '^install ok installed$'; then
      missing_packages+=("${package}")
    fi
  done
  if (( ${#missing_packages[@]} )); then
    sudo apt-get update
    sudo apt-get install -y "${missing_packages[@]}"
  fi

  if [[ -f "${bootstrap_marker}" ]]; then
    echo "[+] Worker dependencies already initialized."
    return
  fi

  sudo "${repo_dir}/backend/chromium/build/install-build-deps.sh" \
    --no-prompt --no-arm --no-chromeos-fonts

  SYNC_DEPS=1 JOBS="${jobs}" "${repo_dir}/scripts/bootstrap-chromium-source.sh"
  mkdir -p "$(dirname "${bootstrap_marker}")"
  printf '%s\n' "$(date --iso-8601=seconds)" >"${bootstrap_marker}"
}

build() {
  bootstrap
  cd "${repo_dir}"
  ./scripts/prune-synced-chromium-deps.py

  CHROMIUM_BUILD_DIR="${chromium_build_dir}" \
    JOBS="${jobs}" \
    VIMBROWSER_BUILD_QUIET=1 \
    VIMBROWSER_FORCE_GN_GEN=1 \
    ./scripts/build-chromium-cef.sh

  cef_dist=$(latest_cef_dist || true)
  if [[ -z "${cef_dist}" ]]; then
    echo "[+] Creating the worker's initial minimal CEF distribution."
    PATH="${repo_dir}/backend/depot_tools:${PATH}" \
      autoninja -C "backend/chromium/out/${chromium_build_dir}" \
        --fast_nop -j "${jobs}" chrome_sandbox
    (
      cd backend/chromium/cef/tools
      PATH="${repo_dir}/backend/depot_tools:${PATH}" \
        ./make_distrib.sh --ninja-build --x64-build --minimal \
          --allow-partial --no-archive --output-dir ../binary_distrib
    )
    cef_dist=$(latest_cef_dist)
    ./scripts/slim-cef-runtime.sh "${cef_dist}"
  else
    CHROMIUM_BUILD_DIR="${chromium_build_dir}" \
      CEF_DIST_DIR="${cef_dist}" \
      ./scripts/sync-chromium-cef-distrib.sh
  fi

  # Reconfigure from an empty shell tree so pkg-config results never retain a
  # distro-specific library variant from an older worker image.
  rm -rf "${shell_build}"
  cmake -S . -B "${shell_build}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCEF_ROOT="${cef_dist}"
  cmake --build "${shell_build}" -j "${jobs}"
  ./scripts/slim-cef-runtime.sh "${shell_build}/Release"
  ctest --test-dir "${shell_build}" --output-on-failure

  tree=$(git rev-parse 'HEAD^{tree}')
  commit=$(git rev-parse HEAD)
  artifact_dir="${artifact_root}/${tree}"
  runtime_dir="${shell_build}/Release"
  mkdir -p "${artifact_dir}"

  (
    cd "${runtime_dir}"
    find . -type f -print0 | sort -z | xargs -0 sha256sum \
      >"${artifact_dir}/Release.sha256"
  )
  cat >"${artifact_dir}/metadata.txt" <<EOF
commit=${commit}
tree=${tree}
created_at=$(date --iso-8601=seconds)
host=$(hostname)
workers=${jobs}
chromium_build_dir=${chromium_build_dir}
EOF
  tar --zstd -cf "${artifact_dir}/Release.tar.zst" \
    -C "${shell_build}" Release
  (
    cd "${artifact_dir}"
    sha256sum Release.tar.zst >Release.tar.zst.sha256
  )

  echo "[+] Remote runtime artifact: ${artifact_dir}/Release.tar.zst"
}

case "${1:-}" in
  bootstrap)
    bootstrap
    ;;
  build)
    build
    ;;
  artifact-path)
    tree=$(git -C "${repo_dir}" rev-parse 'HEAD^{tree}')
    printf '%s/%s\n' "${artifact_root}" "${tree}"
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
