#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sandbox_root=${VIMBROWSER_DEBLOAT_SANDBOX_ROOT:-/home/yeyito/Workspace/vimbrowser-debloat-sandbox}
chromium_build_dir=${VIMBROWSER_DEBLOAT_CHROMIUM_BUILD_DIR:-Debloat_GN_x64}
stable_chromium_out="${repo_dir}/backend/chromium/out/Release_GN_x64"
chromium_out="${repo_dir}/backend/chromium/out/${chromium_build_dir}"
stable_runtime="${repo_dir}/build-source/Release"
sandbox_cef="${sandbox_root}/cef"
sandbox_build="${sandbox_root}/build"
sandbox_install="${sandbox_root}/install"
sandbox_profile="${sandbox_root}/profile"
checksum_file="${sandbox_root}/stable-runtime.sha256"

usage() {
  cat <<EOF
usage: scripts/debloat-sandbox.sh <command> [make-arguments...]

Commands:
  setup       Independently copy the stable GN output and CEF distribution once
  build       Run make with all mutable build/runtime paths sandboxed
  install     Run make install into the sandbox prefix and profile
  benchmark   Run the local benchmark against the sandbox binary
  test        Run CTest for the sandbox shell build
  verify      Verify the normal installed/runtime files were not modified
  paths       Print the isolated paths used by this wrapper

The wrapper deliberately has no promotion command. Stable installation remains
an explicit separate decision after a debloat stage passes its full test gate.
EOF
}

latest_stable_cef() {
  find "${repo_dir}/backend/chromium/cef/binary_distrib" -maxdepth 1 -type d \
    -name 'cef_binary_*_linux64_minimal' -print | sort | tail -n 1
}

write_stable_checksums() {
  local files=(vimbrowser libcef.so resources.pak v8_context_snapshot.bin)
  (
    cd "${stable_runtime}"
    sha256sum "${files[@]}"
  ) >"${checksum_file}"
}

verify_stable() {
  if [[ ! -f "${checksum_file}" ]]; then
    echo "error: sandbox is not initialized; run setup first" >&2
    return 1
  fi
  (
    cd "${stable_runtime}"
    sha256sum --check --status "${checksum_file}"
  ) || {
    echo "error: stable runtime changed during sandboxed work: ${stable_runtime}" >&2
    return 1
  }
  echo "[+] Stable runtime unchanged: ${stable_runtime}"
}

require_setup() {
  [[ -f "${chromium_out}/build.ninja" ]] || {
    echo "error: isolated Chromium output is missing; run setup first" >&2
    exit 1
  }
  [[ -d "${sandbox_cef}/Release" ]] || {
    echo "error: isolated CEF distribution is missing; run setup first" >&2
    exit 1
  }
  [[ "${chromium_out}" != "${stable_chromium_out}" ]]
  [[ "${sandbox_build}" != "${repo_dir}/build-source" ]]
  [[ "${sandbox_install}" != "/home/yeyito/.local" ]]
}

run_make() {
  require_setup
  trap verify_stable EXIT
  env \
    CHROMIUM_BUILD_DIR="${chromium_build_dir}" \
    CEF_DIST_DIR="${sandbox_cef}" \
    make -C "${repo_dir}" \
      BUILD_DIR="${sandbox_build}" \
      CEF_ROOT="${sandbox_cef}" \
      "$@"
}

case "${1:-}" in
  setup)
    mkdir -p "${sandbox_root}" "$(dirname "${chromium_out}")"
    stable_cef=$(latest_stable_cef)
    [[ -d "${stable_chromium_out}" && -n "${stable_cef}" && -d "${stable_cef}" ]]
    if [[ ! -e "${chromium_out}" ]]; then
      chromium_setup="${chromium_out}.setup"
      rm -rf "${chromium_setup}"
      echo "[+] Copying independent Chromium output (no hard links):"
      echo "    ${stable_chromium_out} -> ${chromium_out}"
      cp -a "${stable_chromium_out}" "${chromium_setup}"
      mv "${chromium_setup}" "${chromium_out}"
    fi
    if [[ ! -e "${sandbox_cef}" ]]; then
      cef_setup="${sandbox_cef}.setup"
      rm -rf "${cef_setup}"
      echo "[+] Copying independent CEF distribution:"
      echo "    ${stable_cef} -> ${sandbox_cef}"
      cp -a "${stable_cef}" "${cef_setup}"
      mv "${cef_setup}" "${sandbox_cef}"
    fi
    printf '%s\n' "$(git -C "${repo_dir}" rev-parse HEAD)" \
      >"${sandbox_root}/baseline-commit"
    write_stable_checksums
    mkdir -p "${sandbox_profile}"
    verify_stable
    "$0" paths
    ;;
  build)
    shift
    run_make "$@"
    ;;
  install)
    shift
    run_make install \
      INSTALL_BIN="${sandbox_install}/bin/vimbrowser" \
      INSTALL_IPC_BIN="${sandbox_install}/bin/vimbrowser-ipc" \
      INSTALL_IPC_SCREENSHOT_BIN="${sandbox_install}/bin/vimbrowser-ipc-screenshot" \
      INSTALL_XDG_BIN="${sandbox_install}/bin/vimbrowser-xdg-open" \
      INSTALL_DESKTOP="${sandbox_install}/share/applications/vimbrowser.desktop" \
      INSTALL_ICON="${sandbox_install}/share/icons/vimbrowser.png" \
      WRAPPER_PROFILE_DIR="${sandbox_profile}" \
      "$@"
    ;;
  benchmark)
    shift
    run_make benchmark "$@"
    ;;
  test)
    require_setup
    ctest --test-dir "${sandbox_build}" --output-on-failure
    verify_stable
    ;;
  verify)
    verify_stable
    ;;
  paths)
    cat <<EOF
sandbox_root=${sandbox_root}
chromium_out=${chromium_out}
cef_root=${sandbox_cef}
shell_build=${sandbox_build}
install_prefix=${sandbox_install}
profile=${sandbox_profile}
stable_runtime=${stable_runtime}
EOF
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
