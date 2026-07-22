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
adb -s "${serial}" push \
  "${repo_dir}/assets/a26-browser.bgrx" \
  /data/local/tmp/vimbrowser-a26-icon.bgrx

adb -s "${serial}" shell '/data/local/tmp/su -c "set -e
  base=/data/local/a26-linux/opt/vimbrowser-a26
  bb=/data/local/a26-linux/busybox.static
  if pidof vimbrowser >/dev/null 2>&1; then
    echo \"Refusing to replace the browser rootfs while vimbrowser is running.\" >&2
    exit 30
  fi
  unmount_tree() {
    root=\$1
    while :; do
      target=\$(\"\$bb\" awk -v root=\"\$root\" '\''\$2 == root || index(\$2, root \"/\") == 1 { print length(\$2), \$2 }'\'' /proc/mounts | \"\$bb\" sort -nr | \"\$bb\" head -n 1 | \"\$bb\" cut -d '\'' '\'' -f 2-)
      [ -n \"\$target\" ] || break
      umount -l \"\$target\"
    done
  }
  unmount_tree \"\$base/rootfs.new\"
  rm -rf \"\$base/rootfs.new\"
  mkdir -p \"\$base/rootfs.new\" \"\$base/bin\" \"\$base/share\"
  \"\$bb\" gzip -dc /data/local/tmp/vimbrowser-a26-rootfs.tar.gz | \"\$bb\" tar -xf - -C \"\$base/rootfs.new\"
  mkdir -p \"\$base/rootfs.new/opt/vimbrowser/Release\"
  \"\$bb\" gzip -dc /data/local/tmp/vimbrowser-a26-release.tar.gz | \"\$bb\" tar -xf - -C \"\$base/rootfs.new/opt/vimbrowser/Release\"
  cp /data/local/tmp/vimbrowser-a26-launch \"\$base/bin/vimbrowser-a26.new\"
  cp /data/local/tmp/vimbrowser-a26-icon.bgrx \"\$base/share/browser-app.bgrx.new\"
  chmod 0755 \"\$base/bin/vimbrowser-a26.new\" \"\$base/rootfs.new/opt/vimbrowser/Release/vimbrowser\"
  chmod 0644 \"\$base/share/browser-app.bgrx.new\"
  unmount_tree \"\$base/rootfs.old\"
  rm -rf \"\$base/rootfs.old\"
  if [ -d \"\$base/rootfs\" ]; then
    unmount_tree \"\$base/rootfs\"
    mv \"\$base/rootfs\" \"\$base/rootfs.old\"
  fi
  mv \"\$base/rootfs.new\" \"\$base/rootfs\"
  mv \"\$base/bin/vimbrowser-a26.new\" \"\$base/bin/vimbrowser-a26\"
  mv \"\$base/share/browser-app.bgrx.new\" \"\$base/share/browser-app.bgrx\"
  unmount_tree \"\$base/rootfs.old\"
  rm -rf \"\$base/rootfs.old\"
  rm -f /data/local/tmp/vimbrowser-a26-rootfs.tar.gz /data/local/tmp/vimbrowser-a26-release.tar.gz /data/local/tmp/vimbrowser-a26-launch /data/local/tmp/vimbrowser-a26-icon.bgrx
  \"\$bb\" chroot \"\$base/rootfs\" /bin/sh -c true
"'

echo 'Installed the A26 vimbrowser compatibility runtime at /opt/vimbrowser-a26.'
