#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

if [[ $# -ne 3 ]]; then
  cat >&2 <<'EOF'
usage: scripts/backend-dev-build.sh <source-build-dir> <cef-root-or-empty> <jobs>

Runs the backend-dev build loop and writes <source-build-dir>/.backend-dev-changed
with 1 when the shell/wrapper needs reinstalling, or 0 when the installed
wrapper is already up to date.
EOF
  exit 2
fi

source_build_dir=$1
cef_root_arg=$2
jobs=$3
chromium_build_dir=${CHROMIUM_BUILD_DIR:-Release_GN_x64}
chromium_out_dir="${repo_dir}/backend/chromium/out/${chromium_build_dir}"

case "${source_build_dir}" in
  /*) ;;
  *) source_build_dir="${repo_dir}/${source_build_dir}" ;;
esac

mkdir -p "${source_build_dir}"
changed_file="${source_build_dir}/.backend-dev-changed"
printf '1\n' >"${changed_file}"

latest_cef_root() {
  ls -d "${repo_dir}"/backend/chromium/cef/binary_distrib/cef_binary_*_linux64_minimal 2>/dev/null | tail -n 1 || true
}

resolve_cef_root() {
  if [[ -n "${cef_root_arg}" ]]; then
    case "${cef_root_arg}" in
      /*) printf '%s\n' "${cef_root_arg}" ;;
      *) printf '%s\n' "${repo_dir}/${cef_root_arg}" ;;
    esac
  else
    latest_cef_root
  fi
}

shell_inputs_newer_than() {
  local output=$1
  [[ -e "${output}" ]] || return 0

  local input
  for input in \
    "${repo_dir}/CMakeLists.txt" \
    "${repo_dir}/src"; do
    [[ -e "${input}" ]] || continue
    if [[ -n "$(find "${input}" -type f -newer "${output}" -print -quit)" ]]; then
      return 0
    fi
  done

  return 1
}

configure_needed() {
  local cef_root=$1
  [[ -f "${source_build_dir}/build.ninja" && -f "${source_build_dir}/CMakeCache.txt" ]] || return 0
  grep -qx "CEF_ROOT:PATH=${cef_root}" "${source_build_dir}/CMakeCache.txt" || return 0
  [[ "${repo_dir}/CMakeLists.txt" -nt "${source_build_dir}/build.ninja" ]] && return 0
  return 1
}

cef_root=$(resolve_cef_root)
if [[ -z "${cef_root}" || ! -d "${cef_root}" ]]; then
  echo "error: no source-built CEF distribution found" >&2
  echo "       Run 'make source-distrib' once, or set CEF_ROOT." >&2
  exit 1
fi

vimbrowser_bin="${source_build_dir}/Release/vimbrowser"
need_configure=0
if configure_needed "${cef_root}"; then
  need_configure=1
fi

need_shell_build=0
if [[ "${need_configure}" == "1" || ! -x "${vimbrowser_bin}" ]] || shell_inputs_newer_than "${vimbrowser_bin}"; then
  need_shell_build=1
fi

"${repo_dir}/scripts/build-chromium-cef.sh"

if [[ "${need_shell_build}" == "0" ]]; then
  "${repo_dir}/scripts/sync-chromium-runtime.sh" "${chromium_out_dir}" "${source_build_dir}/Release"
  echo "[+] vimbrowser shell is up to date; source CEF distribution sync skipped for this backend-only loop."
  printf '0\n' >"${changed_file}"
  exit 0
fi

"${repo_dir}/scripts/sync-chromium-cef-distrib.sh"

if [[ "${need_configure}" == "1" ]]; then
  cmake -S "${repo_dir}" -B "${source_build_dir}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCEF_ROOT="${cef_root}"
fi

cmake --build "${source_build_dir}" -j"${jobs}"
"${repo_dir}/scripts/slim-cef-runtime.sh" "${source_build_dir}/Release"
printf '1\n' >"${changed_file}"
