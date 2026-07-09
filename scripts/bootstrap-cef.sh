#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cef_version=${CEF_VERSION:-147.0.10+gd58e84d+chromium-147.0.7727.118}
base_url=${CEF_BASE_URL:-https://cef-builds.spotifycdn.com}

host_os=$(uname -s)
host_arch=$(uname -m)
case "${host_os}:${host_arch}" in
  Darwin:arm64)
    cef_platform=macosarm64
    default_cef_sha1=f3bdb0a67de82dd984b610f0a87858d636cf4da6
    default_cef_dir="${repo_dir}/third_party/cef-mac"
    ;;
  Linux:x86_64)
    cef_platform=linux64
    default_cef_sha1=d16055d803052b53074a46828587fe0c4b9986e3
    default_cef_dir="${repo_dir}/third_party/cef"
    ;;
  *)
    echo "error: unsupported CEF host ${host_os}/${host_arch}" >&2
    exit 1
    ;;
esac

cef_file=${CEF_FILE:-cef_binary_${cef_version}_${cef_platform}_minimal.tar.bz2}
cef_sha1=${CEF_SHA1:-${default_cef_sha1}}

download_dir="${repo_dir}/third_party/downloads"
cef_dir=${CEF_DIR:-${default_cef_dir}}
archive="${download_dir}/${cef_file}"

mkdir -p "${download_dir}" "${repo_dir}/third_party"

if [[ ! -f "${archive}" ]]; then
  echo "[+] Downloading CEF ${cef_version}"
  curl -L --fail --continue-at - --output "${archive}" "${base_url}/${cef_file}"
fi

if command -v sha1sum >/dev/null 2>&1; then
  actual_sha1=$(sha1sum "${archive}" | awk '{print $1}')
else
  actual_sha1=$(shasum -a 1 "${archive}" | awk '{print $1}')
fi
if [[ "${actual_sha1}" != "${cef_sha1}" ]]; then
  echo "error: SHA1 mismatch for ${archive}" >&2
  echo "  expected: ${cef_sha1}" >&2
  echo "  actual:   ${actual_sha1}" >&2
  exit 1
fi

if [[ ! -d "${cef_dir}" ]]; then
  tmp="${repo_dir}/third_party/.cef-extract.$$"
  rm -rf "${tmp}"
  mkdir -p "${tmp}"
  echo "[+] Extracting CEF"
  tar -xjf "${archive}" -C "${tmp}"
  extracted=$(find "${tmp}" -mindepth 1 -maxdepth 1 -type d | head -n 1)
  mv "${extracted}" "${cef_dir}"
  rm -rf "${tmp}"
fi

echo "CEF_ROOT=${cef_dir}"
