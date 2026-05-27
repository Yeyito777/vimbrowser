#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
backend_root="${repo_dir}/backend"
chromium_src="${backend_root}/chromium"
build_dir=${CHROMIUM_BUILD_DIR:-Release_GN_x64}
out_dir="${chromium_src}/out/${build_dir}"

dist_dir=${CEF_DIST_DIR:-}
if [[ -z "${dist_dir}" ]]; then
  dist_dir=$(ls -d "${chromium_src}"/cef/binary_distrib/cef_binary_*_linux64_minimal 2>/dev/null | tail -n 1 || true)
fi

if [[ -z "${dist_dir}" || ! -d "${dist_dir}" ]]; then
  echo "error: no source CEF binary distribution found." >&2
  echo "       Run 'make source-distrib' once, or set CEF_DIST_DIR." >&2
  exit 1
fi

if [[ ! -d "${out_dir}" ]]; then
  echo "error: Chromium output dir missing: ${out_dir}" >&2
  echo "       Run 'make build-chromium-cef' first." >&2
  exit 1
fi

changed=0

copy_file() {
  local src=$1
  local dst=$2
  if [[ -f "${src}" ]]; then
    if [[ -f "${dst}" && ! "${src}" -nt "${dst}" ]]; then
      return 0
    fi
    mkdir -p "$(dirname "${dst}")"
    cp -a "${src}" "${dst}"
    changed=1
    echo "synced ${src#${repo_dir}/} -> ${dst#${repo_dir}/}"
  fi
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

  local file base dst kept=0 removed=0
  while IFS= read -r -d '' file; do
    base=$(basename "${file}")
    if locale_is_kept "${base}"; then
      copy_file "${file}" "${dst_dir}/${base}"
      kept=$((kept + 1))
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

  echo "synced kept locales ${src_dir#${repo_dir}/}/ -> ${dst_dir#${repo_dir}/}/ (kept ${kept}, removed ${removed})"
}

# Runtime binaries used by vimbrowser's CEF distribution. This is the fast path
# after an incremental //cef:libcef build; it avoids regenerating the entire CEF
# binary distribution for each local backend edit.
copy_file "${out_dir}/libcef.so" "${dist_dir}/Release/libcef.so"
copy_file "${out_dir}/chrome_sandbox" "${dist_dir}/Release/chrome-sandbox"
copy_file "${out_dir}/libEGL.so" "${dist_dir}/Release/libEGL.so"
copy_file "${out_dir}/libGLESv2.so" "${dist_dir}/Release/libGLESv2.so"
copy_file "${out_dir}/libvk_swiftshader.so" "${dist_dir}/Release/libvk_swiftshader.so"
copy_file "${out_dir}/libvulkan.so.1" "${dist_dir}/Release/libvulkan.so.1"
copy_file "${out_dir}/vk_swiftshader_icd.json" "${dist_dir}/Release/vk_swiftshader_icd.json"
copy_file "${out_dir}/v8_context_snapshot.bin" "${dist_dir}/Release/v8_context_snapshot.bin"

copy_file "${out_dir}/chrome_100_percent.pak" "${dist_dir}/Resources/chrome_100_percent.pak"
copy_file "${out_dir}/chrome_200_percent.pak" "${dist_dir}/Resources/chrome_200_percent.pak"
copy_file "${out_dir}/resources.pak" "${dist_dir}/Resources/resources.pak"
copy_file "${out_dir}/icudtl.dat" "${dist_dir}/Resources/icudtl.dat"

sync_locales "${out_dir}/locales" "${dist_dir}/Resources/locales"

if [[ "${changed}" == "1" || "${VIMBROWSER_FORCE_RUNTIME_SLIM:-0}" == "1" ]]; then
  "${repo_dir}/scripts/slim-cef-runtime.sh" "${dist_dir}"
else
  echo "[+] Existing CEF binary distribution already up to date; runtime slim skipped."
fi

cat <<EOF
[+] Existing CEF binary distribution refreshed from incremental Chromium output:
    ${dist_dir}

For normal backend edit loops use:
    make backend-dev
EOF
