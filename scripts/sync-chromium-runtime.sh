#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

if [[ $# -ne 2 ]]; then
  cat >&2 <<'EOF'
usage: scripts/sync-chromium-runtime.sh <chromium-out-dir> <runtime-dir>

Incrementally sync Chromium/CEF build output directly into a vimbrowser runtime
directory. ELF files are stripped while copying so backend-only edit loops can
deploy the updated runtime without first refreshing the intermediate CEF binary
distribution or relinking the vimbrowser shell.
EOF
  exit 2
fi

out_dir=$1
runtime_dir=$2
strip_tool=${STRIP:-strip}
strip_enabled=${VIMBROWSER_STRIP_RELEASE:-1}

if [[ ! -d "${out_dir}" ]]; then
  echo "error: Chromium output dir missing: ${out_dir}" >&2
  exit 1
fi

mkdir -p "${runtime_dir}"
changed=0

is_elf() {
  local path=$1
  [[ -f "${path}" ]] || return 1
  local magic
  magic=$(LC_ALL=C head -c 4 "${path}" || true)
  [[ "${magic}" == $'\x7fELF' ]]
}

copy_file() {
  local src=$1
  local dst=$2
  [[ -f "${src}" ]] || return 0

  if [[ -f "${dst}" && ! "${src}" -nt "${dst}" ]]; then
    return 0
  fi

  mkdir -p "$(dirname "${dst}")"
  if [[ "${strip_enabled}" == "1" ]] && is_elf "${src}"; then
    local tmp
    tmp=$(mktemp "${dst}.XXXXXX")
    if ! "${strip_tool}" --strip-unneeded -o "${tmp}" "${src}" 2>/dev/null; then
      rm -f "${tmp}"
      tmp=$(mktemp "${dst}.XXXXXX")
      cp -a "${src}" "${tmp}"
      "${strip_tool}" --strip-unneeded "${tmp}" 2>/dev/null || \
        "${strip_tool}" "${tmp}"
    fi
    chmod --reference="${src}" "${tmp}" 2>/dev/null || true
    mv "${tmp}" "${dst}"
  else
    cp -a "${src}" "${dst}"
  fi

  changed=1
  echo "synced ${src#${repo_dir}/} -> ${dst#${repo_dir}/}"
}

locale_is_kept() {
  local base=$1
  local keep_locales_csv=${VIMBROWSER_KEEP_LOCALES:-en-US,en-GB}
  local locale
  IFS=',' read -ra locales <<<"${keep_locales_csv}"
  for locale in "${locales[@]}"; do
    locale=${locale//[[:space:]]/}
    [[ -n "${locale}" ]] || continue
    case "${base}" in
      "${locale}.pak"|"${locale}.pak.info"|"${locale}"_*.pak|"${locale}"_*.pak.info)
        return 0
        ;;
    esac
  done
  return 1
}

sync_locales() {
  local src_dir=$1
  local dst_dir=$2
  [[ -d "${src_dir}" ]] || return 0
  mkdir -p "${dst_dir}"

  local file base removed=0
  while IFS= read -r -d '' file; do
    base=$(basename "${file}")
    if locale_is_kept "${base}"; then
      copy_file "${file}" "${dst_dir}/${base}"
    fi
  done < <(find "${src_dir}" -type f \( -name '*.pak' -o -name '*.pak.info' \) -print0)

  while IFS= read -r -d '' file; do
    base=$(basename "${file}")
    if ! locale_is_kept "${base}" || [[ ! -f "${src_dir}/${base}" ]]; then
      rm -f "${file}"
      removed=$((removed + 1))
      changed=1
    fi
  done < <(find "${dst_dir}" -type f \( -name '*.pak' -o -name '*.pak.info' \) -print0)

  if (( removed > 0 )); then
    echo "removed ${removed} stale runtime locales from ${dst_dir#${repo_dir}/}"
  fi
}

copy_file "${out_dir}/libcef.so" "${runtime_dir}/libcef.so"
copy_file "${out_dir}/chrome_sandbox" "${runtime_dir}/chrome-sandbox"
copy_file "${out_dir}/libEGL.so" "${runtime_dir}/libEGL.so"
copy_file "${out_dir}/libGLESv2.so" "${runtime_dir}/libGLESv2.so"
copy_file "${out_dir}/libvk_swiftshader.so" "${runtime_dir}/libvk_swiftshader.so"
copy_file "${out_dir}/libvulkan.so.1" "${runtime_dir}/libvulkan.so.1"
copy_file "${out_dir}/vk_swiftshader_icd.json" "${runtime_dir}/vk_swiftshader_icd.json"
copy_file "${out_dir}/v8_context_snapshot.bin" "${runtime_dir}/v8_context_snapshot.bin"

copy_file "${out_dir}/chrome_100_percent.pak" "${runtime_dir}/chrome_100_percent.pak"
copy_file "${out_dir}/chrome_200_percent.pak" "${runtime_dir}/chrome_200_percent.pak"
copy_file "${out_dir}/resources.pak" "${runtime_dir}/resources.pak"
copy_file "${out_dir}/icudtl.dat" "${runtime_dir}/icudtl.dat"
sync_locales "${out_dir}/locales" "${runtime_dir}/locales"

if [[ "${changed}" == "1" ]]; then
  echo "[+] Runtime refreshed directly from Chromium output: ${runtime_dir}"
else
  echo "[+] Runtime already up to date with Chromium output: ${runtime_dir}"
fi
