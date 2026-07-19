#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
artifacts="${repo_dir}/build-a26-arm64/artifacts"

if [[ -n "${A26_SERIAL:-}" ]]; then
  serial=${A26_SERIAL}
else
  mapfile -t devices < <(adb devices | awk 'NR > 1 && $2 == "device" {print $1}')
  if [[ ${#devices[@]} -ne 1 ]]; then
    echo 'Set A26_SERIAL or connect exactly one authorized ADB device.' >&2
    exit 2
  fi
  serial=${devices[0]}
fi

"${repo_dir}/scripts/a26/package.sh"
(cd "${artifacts}" && sha256sum -c SHA256SUMS)

adb -s "${serial}" get-state >/dev/null
adb -s "${serial}" push \
  "${artifacts}/debian-bookworm-arm64-runtime.tar.gz" \
  /data/local/tmp/vimbrowser-a26-rootfs.tar.gz
adb -s "${serial}" push \
  "${artifacts}/vimbrowser-a26-release.tar.gz" \
  /data/local/tmp/vimbrowser-a26-release.tar.gz
adb -s "${serial}" push \
  "${repo_dir}/scripts/a26/phone-launch.sh" \
  /data/local/tmp/vimbrowser-a26-launch

adb -s "${serial}" shell '/data/local/tmp/su -c "set -e
  base=/data/local/a26-linux/opt/vimbrowser-a26
  bb=/data/local/a26-linux/busybox.static
  rm -rf \"\$base/rootfs.new\"
  mkdir -p \"\$base/rootfs.new\" \"\$base/bin\"
  \"\$bb\" gzip -dc /data/local/tmp/vimbrowser-a26-rootfs.tar.gz | \"\$bb\" tar -xf - -C \"\$base/rootfs.new\"
  mkdir -p \"\$base/rootfs.new/opt/vimbrowser/Release\"
  \"\$bb\" gzip -dc /data/local/tmp/vimbrowser-a26-release.tar.gz | \"\$bb\" tar -xf - -C \"\$base/rootfs.new/opt/vimbrowser/Release\"
  cp /data/local/tmp/vimbrowser-a26-launch \"\$base/bin/vimbrowser-a26.new\"
  chmod 0755 \"\$base/bin/vimbrowser-a26.new\" \"\$base/rootfs.new/opt/vimbrowser/Release/vimbrowser\"
  rm -rf \"\$base/rootfs.old\"
  if [ -d \"\$base/rootfs\" ]; then mv \"\$base/rootfs\" \"\$base/rootfs.old\"; fi
  mv \"\$base/rootfs.new\" \"\$base/rootfs\"
  mv \"\$base/bin/vimbrowser-a26.new\" \"\$base/bin/vimbrowser-a26\"
  rm -rf \"\$base/rootfs.old\"
  rm -f /data/local/tmp/vimbrowser-a26-rootfs.tar.gz /data/local/tmp/vimbrowser-a26-release.tar.gz /data/local/tmp/vimbrowser-a26-launch
  \"\$bb\" chroot \"\$base/rootfs\" /bin/sh -c true
"'

echo 'Installed the A26 vimbrowser compatibility runtime at /opt/vimbrowser-a26.'
