#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir="${repo_dir}/build-a26-arm64"
release_dir="${build_dir}/Release"
artifacts="${build_dir}/artifacts"
runtime_image=localhost/vimbrowser-a26-runtime:bookworm

"${repo_dir}/scripts/a26/build.sh"
rm -rf "${artifacts}"
mkdir -p "${artifacts}"

STRIP=${A26_STRIP:-aarch64-linux-gnu-strip} \
  "${repo_dir}/scripts/slim-cef-runtime.sh" "${release_dir}"

container=$(podman create --arch arm64 "${runtime_image}")
cleanup() { podman rm -f "${container}" >/dev/null 2>&1 || true; }
trap cleanup EXIT
podman export --output "${artifacts}/debian-bookworm-arm64-runtime.tar" "${container}"

tar -C "${release_dir}" -czf "${artifacts}/vimbrowser-a26-release.tar.gz" .
gzip -9 "${artifacts}/debian-bookworm-arm64-runtime.tar"

sha256sum \
  "${artifacts}/debian-bookworm-arm64-runtime.tar.gz" \
  "${artifacts}/vimbrowser-a26-release.tar.gz" \
  >"${artifacts}/SHA256SUMS"
cat "${artifacts}/SHA256SUMS"
