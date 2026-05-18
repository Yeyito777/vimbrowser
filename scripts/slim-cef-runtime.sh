#!/usr/bin/env bash
set -euo pipefail

# Trim the local CEF runtime payload without changing Chromium source code.
#
# This script accepts either:
#   - a CEF binary distribution root containing Release/ and Resources/, or
#   - a built runtime directory like build-source/Release containing libcef.so
#     and locales/.
#
# By default we keep the English locales used by this browser install. Override
# with VIMBROWSER_KEEP_LOCALES="en-US,es,..." if a wider locale set is desired.

keep_locales_csv=${VIMBROWSER_KEEP_LOCALES:-en-US,en-GB}
strip_tool=${STRIP:-strip}
strip_enabled=${VIMBROWSER_STRIP_RELEASE:-1}
prune_locales_enabled=${VIMBROWSER_PRUNE_LOCALES:-1}

if [[ $# -eq 0 ]]; then
  cat >&2 <<'EOF'
usage: scripts/slim-cef-runtime.sh <cef-dist-root-or-runtime-dir> [...]

Environment:
  VIMBROWSER_KEEP_LOCALES   comma-separated locales to keep, default en-US,en-GB
  VIMBROWSER_STRIP_RELEASE  1/0, strip ELF runtime binaries, default 1
  VIMBROWSER_PRUNE_LOCALES  1/0, prune locale packs, default 1
  STRIP                     strip tool, default strip
EOF
  exit 2
fi

human_size() {
  local bytes=$1
  python3 - "$bytes" <<'PY'
import sys
n = int(sys.argv[1])
units = ['B', 'KiB', 'MiB', 'GiB']
i = 0
v = float(n)
while v >= 1024 and i + 1 < len(units):
    v /= 1024
    i += 1
if i == 0:
    print(f"{int(v)} {units[i]}")
else:
    print(f"{v:.1f} {units[i]}")
PY
}

path_size() {
  local path=$1
  if [[ -e "${path}" ]]; then
    stat -c '%s' "${path}"
  else
    echo 0
  fi
}

dir_size() {
  local path=$1
  if [[ -d "${path}" ]]; then
    du -sb "${path}" | awk '{print $1}'
  else
    echo 0
  fi
}

is_elf() {
  local path=$1
  [[ -f "${path}" ]] || return 1
  local magic
  magic=$(LC_ALL=C head -c 4 "${path}" || true)
  [[ "${magic}" == $'\x7fELF' ]]
}

strip_if_elf() {
  local path=$1
  [[ "${strip_enabled}" == "1" ]] || return 0
  [[ -f "${path}" ]] || return 0
  is_elf "${path}" || return 0

  local before after
  before=$(path_size "${path}")
  "${strip_tool}" --strip-unneeded "${path}" 2>/dev/null || \
    "${strip_tool}" "${path}"
  after=$(path_size "${path}")
  if (( after < before )); then
    echo "stripped ${path}: $(human_size "${before}") -> $(human_size "${after}")"
  else
    echo "stripped ${path}: unchanged ($(human_size "${after}"))"
  fi
}

locale_is_kept() {
  local base=$1
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

prune_locale_dir() {
  local locale_dir=$1
  [[ "${prune_locales_enabled}" == "1" ]] || return 0
  [[ -d "${locale_dir}" ]] || return 0

  local before after removed kept file base
  before=$(dir_size "${locale_dir}")
  removed=0
  kept=0
  while IFS= read -r -d '' file; do
    base=$(basename "${file}")
    if locale_is_kept "${base}"; then
      kept=$((kept + 1))
    else
      rm -f "${file}"
      removed=$((removed + 1))
    fi
  done < <(find "${locale_dir}" -type f \( -name '*.pak' -o -name '*.pak.info' \) -print0)
  after=$(dir_size "${locale_dir}")
  echo "pruned ${locale_dir}: kept ${kept}, removed ${removed}, $(human_size "${before}") -> $(human_size "${after}")"
}

slim_one() {
  local root=$1
  if [[ ! -e "${root}" ]]; then
    echo "warning: ${root} does not exist; skipping" >&2
    return 0
  fi

  local release_dir resource_dir runtime_dir locale_dir
  if [[ -d "${root}/Release" || -d "${root}/Resources" ]]; then
    release_dir="${root}/Release"
    resource_dir="${root}/Resources"
    runtime_dir=""
    locale_dir="${resource_dir}/locales"
  else
    release_dir=""
    resource_dir=""
    runtime_dir="${root}"
    locale_dir="${runtime_dir}/locales"
  fi

  echo "[+] Slimming CEF runtime: ${root}"

  if [[ -n "${release_dir}" && -d "${release_dir}" ]]; then
    strip_if_elf "${release_dir}/libcef.so"
    strip_if_elf "${release_dir}/libEGL.so"
    strip_if_elf "${release_dir}/libGLESv2.so"
    strip_if_elf "${release_dir}/libvk_swiftshader.so"
    strip_if_elf "${release_dir}/libvulkan.so.1"
    strip_if_elf "${release_dir}/chrome-sandbox"
    strip_if_elf "${release_dir}/chrome_sandbox"
  fi

  if [[ -n "${runtime_dir}" && -d "${runtime_dir}" ]]; then
    strip_if_elf "${runtime_dir}/vimbrowser"
    strip_if_elf "${runtime_dir}/libcef.so"
    strip_if_elf "${runtime_dir}/libEGL.so"
    strip_if_elf "${runtime_dir}/libGLESv2.so"
    strip_if_elf "${runtime_dir}/libvk_swiftshader.so"
    strip_if_elf "${runtime_dir}/libvulkan.so.1"
    strip_if_elf "${runtime_dir}/chrome-sandbox"
    strip_if_elf "${runtime_dir}/chrome_sandbox"
  fi

  prune_locale_dir "${locale_dir}"
}

for root in "$@"; do
  slim_one "${root}"
done
