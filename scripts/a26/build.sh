#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
builder_image=localhost/vimbrowser-a26-builder:bookworm
runtime_image=localhost/vimbrowser-a26-runtime:bookworm
cef_root=${A26_CEF_ROOT:-}

if [[ -z "${cef_root}" ]]; then
  echo 'error: A26_CEF_ROOT must point to a source-built Linux ARM64 vimbrowser CEF distribution' >&2
  exit 1
fi
cef_root=$(cd "${cef_root}" && pwd)
libcef="${cef_root}/Release/libcef.so"
if [[ ! -f "${libcef}" ]] ||
   ! grep -aFq 'cef_browser_host_vimbrowser_send_browser_command_key_event' \
       "${libcef}"; then
  echo 'error: A26_CEF_ROOT is not a source-built vimbrowser CEF distribution' >&2
  exit 1
fi

podman build --arch arm64 --target builder -t "${builder_image}" \
  -f "${repo_dir}/scripts/a26/Containerfile" "${repo_dir}/scripts/a26"
podman build --arch arm64 --target runtime -t "${runtime_image}" \
  -f "${repo_dir}/scripts/a26/Containerfile" "${repo_dir}/scripts/a26"

podman run --rm --arch arm64 \
  --volume "${repo_dir}:/src" \
  --volume "${cef_root}:/cef:ro" \
  --workdir /src \
  "${builder_image}" \
  sh -euxc '
    cmake -S /src -B /src/build-a26-arm64 -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCEF_ROOT=/cef \
      -DPROJECT_ARCH=arm64 \
      -DUSE_SANDBOX=OFF
    cmake --build /src/build-a26-arm64 -j2
  '

binary="${repo_dir}/build-a26-arm64/Release/vimbrowser"
[[ -x "${binary}" ]] || { echo "missing A26 vimbrowser binary" >&2; exit 30; }
file "${binary}"
readelf -h "${binary}" | grep -E 'Class:|Machine:'

echo '[+] A26 native-backend build complete.'
