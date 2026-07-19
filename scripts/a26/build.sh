#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
builder_image=localhost/vimbrowser-a26-builder:bookworm
runtime_image=localhost/vimbrowser-a26-runtime:bookworm

"${repo_dir}/scripts/a26/bootstrap-cef.sh"

podman build --arch arm64 --target builder -t "${builder_image}" \
  -f "${repo_dir}/scripts/a26/Containerfile" "${repo_dir}/scripts/a26"
podman build --arch arm64 --target runtime -t "${runtime_image}" \
  -f "${repo_dir}/scripts/a26/Containerfile" "${repo_dir}/scripts/a26"

podman run --rm --arch arm64 \
  --volume "${repo_dir}:/src" \
  --workdir /src \
  "${builder_image}" \
  sh -euxc '
    cmake -S /src -B /src/build-a26-arm64 -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCEF_ROOT=/src/third_party/cef-a26-arm64 \
      -DPROJECT_ARCH=arm64 \
      -DUSE_SANDBOX=OFF \
      -DVIMBROWSER_USE_PRIVATE_CEF_API=OFF
    cmake --build /src/build-a26-arm64 -j2
  '

binary="${repo_dir}/build-a26-arm64/Release/vimbrowser"
[[ -x "${binary}" ]] || { echo "missing A26 vimbrowser binary" >&2; exit 30; }
file "${binary}"
readelf -h "${binary}" | grep -E 'Class:|Machine:'

echo '[+] A26 compatibility build complete (official ARM64 CEF, private backend hooks stubbed).'
