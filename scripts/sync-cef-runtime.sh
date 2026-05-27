#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

if [[ $# -ne 2 ]]; then
  cat >&2 <<'EOF'
usage: scripts/sync-cef-runtime.sh <cef-dist-root> <runtime-dir>

Incrementally copy the already-slimmed CEF runtime payload from a CEF binary
distribution into a built vimbrowser runtime directory. This intentionally does
not relink vimbrowser; libcef.so is a shared library and backend-only edits do
not require relinking the small shell executable.
EOF
  exit 2
fi

cef_root=$1
runtime_dir=$2
release_dir="${cef_root}/Release"
resources_dir="${cef_root}/Resources"

if [[ ! -d "${release_dir}" || ! -d "${resources_dir}" ]]; then
  echo "error: not a CEF binary distribution root: ${cef_root}" >&2
  exit 1
fi

mkdir -p "${runtime_dir}"
changed=0

path_size() {
  local path=$1
  if [[ -f "${path}" ]]; then
    stat -c '%s' "${path}"
  else
    echo -1
  fi
}

copy_file() {
  local src=$1
  local dst=$2
  [[ -f "${src}" ]] || return 0

  if [[ -f "${dst}" && ! "${src}" -nt "${dst}" && "$(path_size "${src}")" == "$(path_size "${dst}")" ]]; then
    return 0
  fi

  mkdir -p "$(dirname "${dst}")"
  cp -a "${src}" "${dst}"
  changed=1
  echo "synced ${src#${repo_dir}/} -> ${dst#${repo_dir}/}"
}

sync_dir_files() {
  local src_dir=$1
  local dst_dir=$2
  [[ -d "${src_dir}" ]] || return 0

  mkdir -p "${dst_dir}"

  local file rel removed=0
  while IFS= read -r -d '' file; do
    rel=${file#"${src_dir}/"}
    copy_file "${file}" "${dst_dir}/${rel}"
  done < <(find "${src_dir}" -type f -print0)

  while IFS= read -r -d '' file; do
    rel=${file#"${dst_dir}/"}
    if [[ ! -f "${src_dir}/${rel}" ]]; then
      rm -f "${file}"
      removed=$((removed + 1))
      changed=1
    fi
  done < <(find "${dst_dir}" -type f -print0)

  if (( removed > 0 )); then
    echo "removed ${removed} stale files from ${dst_dir#${repo_dir}/}"
  fi
}

copy_file "${release_dir}/libcef.so" "${runtime_dir}/libcef.so"
copy_file "${release_dir}/chrome-sandbox" "${runtime_dir}/chrome-sandbox"
copy_file "${release_dir}/libEGL.so" "${runtime_dir}/libEGL.so"
copy_file "${release_dir}/libGLESv2.so" "${runtime_dir}/libGLESv2.so"
copy_file "${release_dir}/libvk_swiftshader.so" "${runtime_dir}/libvk_swiftshader.so"
copy_file "${release_dir}/libvulkan.so.1" "${runtime_dir}/libvulkan.so.1"
copy_file "${release_dir}/vk_swiftshader_icd.json" "${runtime_dir}/vk_swiftshader_icd.json"
copy_file "${release_dir}/v8_context_snapshot.bin" "${runtime_dir}/v8_context_snapshot.bin"

copy_file "${resources_dir}/chrome_100_percent.pak" "${runtime_dir}/chrome_100_percent.pak"
copy_file "${resources_dir}/chrome_200_percent.pak" "${runtime_dir}/chrome_200_percent.pak"
copy_file "${resources_dir}/resources.pak" "${runtime_dir}/resources.pak"
copy_file "${resources_dir}/icudtl.dat" "${runtime_dir}/icudtl.dat"
sync_dir_files "${resources_dir}/locales" "${runtime_dir}/locales"

if [[ "${changed}" == "1" ]]; then
  echo "[+] Runtime refreshed: ${runtime_dir}"
else
  echo "[+] Runtime already up to date: ${runtime_dir}"
fi
