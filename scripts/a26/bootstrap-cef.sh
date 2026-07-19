#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
version=147.0.10+gd58e84d+chromium-147.0.7727.118
file="cef_binary_${version}_linuxarm64_minimal.tar.bz2"
sha1=b42ff1dc31a534161a7856889dbda17ac0f9670c
url="https://cef-builds.spotifycdn.com/${file}"
archive="${repo_dir}/third_party/downloads/${file}"
cef_root="${repo_dir}/third_party/cef-a26-arm64"

mkdir -p "$(dirname "${archive}")" "${repo_dir}/third_party"
if [[ ! -f "${archive}" ]]; then
  echo "[+] Downloading exact CEF ${version} Linux ARM64 minimal distribution"
  curl --fail --location --continue-at - --output "${archive}" "${url}"
fi

actual=$(sha1sum "${archive}" | awk '{print $1}')
if [[ "${actual}" != "${sha1}" ]]; then
  printf 'CEF SHA-1 mismatch\nexpected: %s\nactual:   %s\n' \
    "${sha1}" "${actual}" >&2
  exit 20
fi

if [[ ! -f "${cef_root}/Release/libcef.so" ]]; then
  tmp="${repo_dir}/third_party/.cef-a26-extract.$$"
  rm -rf "${tmp}"
  mkdir -p "${tmp}"
  tar -xjf "${archive}" -C "${tmp}"
  extracted=$(find "${tmp}" -mindepth 1 -maxdepth 1 -type d -print -quit)
  [[ -n "${extracted}" ]] || { echo 'CEF archive had no root directory' >&2; exit 21; }
  rm -rf "${cef_root}"
  mv "${extracted}" "${cef_root}"
  rm -rf "${tmp}"
fi

file "${cef_root}/Release/libcef.so"
printf 'CEF_ROOT=%s\n' "${cef_root}"
