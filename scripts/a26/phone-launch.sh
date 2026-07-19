#!/bin/sh
set -eu

outer=/opt/vimbrowser-a26
root="$outer/rootfs"
log="$outer/vimbrowser.log"

bind_once() {
  source_path=$1
  target_path=$2
  mkdir -p "$target_path"
  if ! grep -Fq " $target_path " /proc/mounts; then
    mount --bind "$source_path" "$target_path"
  fi
}

bind_once /proc "$root/proc"
bind_once /sys "$root/sys"
bind_once /dev "$root/dev"
bind_once /tmp/.X11-unix "$root/tmp/.X11-unix"
cp /etc/resolv.conf "$root/etc/resolv.conf" 2>/dev/null || true
mkdir -p "$root/var/lib/vimbrowser/profile" "$root/tmp"

exec chroot "$root" /usr/bin/env -i \
  HOME=/var/lib/vimbrowser \
  DISPLAY="${DISPLAY:-:0}" \
  PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  XDG_CACHE_HOME=/var/lib/vimbrowser/.cache \
  XDG_CONFIG_HOME=/var/lib/vimbrowser/.config \
  XDG_RUNTIME_DIR=/tmp/vimbrowser-runtime \
  /bin/sh -c '
    mkdir -p "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME" "$XDG_RUNTIME_DIR"
    chmod 0700 "$XDG_RUNTIME_DIR"
    cd /opt/vimbrowser/Release
    exec ./vimbrowser \
      --a26-shell \
      --disable-gpu \
      --force-device-scale-factor=2.5 \
      --proxy-server=http://127.0.0.1:18777 \
      --touch-events=enabled \
      --profile-dir=/var/lib/vimbrowser/profile \
      --remote-debugging-port=9222 \
      https://example.com
  ' >>"$log" 2>&1
