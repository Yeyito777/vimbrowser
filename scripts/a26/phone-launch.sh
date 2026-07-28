#!/bin/sh
set -eu

outer=/opt/vimbrowser-a26
root="$outer/rootfs"
log="$outer/vimbrowser.log"
proxy=${A26_VIMBROWSER_PROXY:-}

die() {
  echo "vimbrowser A26 launch: $*" >&2
  exit 1
}

reject_symlink_components() {
  checked_path=$1
  old_ifs=$IFS
  IFS=/
  set -- ${checked_path#/}
  IFS=$old_ifs
  current=/
  for component do
    [ -n "$component" ] || continue
    current=${current%/}/$component
    [ ! -L "$current" ] || die "symlink is not allowed in bind path: $current"
  done
}

reject_symlink_components "$root"
canonical_root=$(readlink -f "$root") || die "cannot canonicalize browser rootfs"
[ "$canonical_root" = "$root" ] || die "browser rootfs must be a canonical path"

bind_dir_once() {
  source_path=$1
  target_path=$2

  [ -d "$source_path" ] || die "bind source is not a directory: $source_path"
  reject_symlink_components "$target_path"
  if [ -e "$target_path" ] && [ ! -d "$target_path" ]; then
    die "bind destination is not a directory: $target_path"
  fi
  mkdir -p "$target_path"
  reject_symlink_components "$target_path"
  [ -d "$target_path" ] || die "could not create bind directory: $target_path"
  canonical_target=$(readlink -f "$target_path") ||
    die "could not canonicalize bind destination: $target_path"
  case "$canonical_target" in
    "$canonical_root"/*) ;;
    *) die "bind destination escaped browser rootfs: $target_path" ;;
  esac

  if ! grep -Fq " $target_path " /proc/mounts; then
    mount --bind "$source_path" "$target_path"
  fi

  # A pre-existing mount at the destination must be this exact directory, not
  # an unrelated stale mount. Bind-mounted directory roots retain device/inode.
  [ "$(stat -c '%d:%i' "$source_path")" = \
    "$(stat -c '%d:%i' "$target_path")" ] ||
    die "unexpected mount at bind destination: $target_path"
}

bind_dir_once_ro() {
  source_path=$1
  target_path=$2
  bind_dir_once "$source_path" "$target_path"
  mount -o remount,bind,ro,nosuid,nodev,noexec "$target_path"
  mount_options=$(awk -v target="$target_path" '$2 == target { print $4; exit }' /proc/mounts)
  case ",$mount_options," in
    *,ro,*) ;;
    *) die "bind destination is not read-only: $target_path" ;;
  esac
  for required in nosuid nodev noexec; do
    case ",$mount_options," in
      *,$required,*) ;;
      *) die "bind destination lacks $required: $target_path" ;;
    esac
  done
}

bind_dir_once_rw_data() {
  source_path=$1
  target_path=$2
  bind_dir_once "$source_path" "$target_path"
  mount -o remount,bind,rw,nosuid,nodev,noexec "$target_path"
  mount_options=$(awk -v target="$target_path" '$2 == target { print $4; exit }' /proc/mounts)
  case ",$mount_options," in
    *,rw,*) ;;
    *) die "bind destination is not read/write: $target_path" ;;
  esac
  for required in nosuid nodev noexec; do
    case ",$mount_options," in
      *,$required,*) ;;
      *) die "bind destination lacks $required: $target_path" ;;
    esac
  done
}

mount_type_at() {
  mount_target=$1
  awk -v target="$mount_target" '$2 == target { print $3; exit }' /proc/mounts
}

private_dev_once() {
  target_path=$1

  reject_symlink_components "$target_path"
  if [ -e "$target_path" ] && [ ! -d "$target_path" ]; then
    die "private /dev destination is not a directory: $target_path"
  fi
  mkdir -p "$target_path"
  reject_symlink_components "$target_path"
  canonical_target=$(readlink -f "$target_path") ||
    die "could not canonicalize private /dev destination: $target_path"
  case "$canonical_target" in
    "$canonical_root"/*) ;;
    *) die "private /dev destination escaped browser rootfs: $target_path" ;;
  esac

  existing_type=$(mount_type_at "$target_path")
  if [ -z "$existing_type" ]; then
    mount -t tmpfs -o mode=0755,nosuid,noexec \
      a26-vimbrowser-dev "$target_path"
    existing_type=$(mount_type_at "$target_path")
  fi
  [ "$existing_type" = tmpfs ] ||
    die "unexpected mount at private /dev destination: $target_path"

  # Chromium must never see Samsung's host /dev/video* nodes. Merely probing
  # those nodes can enter the downstream FIMC camera driver and panic this
  # kernel. The browser only needs basic character devices and shared memory;
  # Xorg owns the real display and input devices.
  for spec in \
    'null 1 3 0666' \
    'zero 1 5 0666' \
    'full 1 7 0666' \
    'random 1 8 0666' \
    'urandom 1 9 0666' \
    'tty 5 0 0666'; do
    set -- $spec
    node=$1
    major=$2
    minor=$3
    mode=$4
    if [ ! -c "$target_path/$node" ]; then
      rm -f "$target_path/$node"
      mknod "$target_path/$node" c "$major" "$minor"
    fi
    chmod "$mode" "$target_path/$node"
  done
  mkdir -p "$target_path/shm"
  chmod 1777 "$target_path/shm"

  if find "$target_path" -maxdepth 1 -name 'video*' -print -quit | grep -q .; then
    die "private browser /dev unexpectedly exposes a video device"
  fi
}

bind_dir_once /proc "$root/proc"
bind_dir_once /sys "$root/sys"
private_dev_once "$root/dev"
bind_dir_once /tmp/.X11-unix "$root/tmp/.X11-unix"
[ -S /run/a26-shell/control.sock ] || die "Moon control socket is unavailable"
[ "$(stat -c '%u' /run/a26-shell/control.sock)" = 0 ] ||
  die "Moon control socket is not root-owned"
[ "$(stat -c '%a' /run/a26-shell/control.sock)" = 600 ] ||
  die "Moon control socket permissions are not 0600"
bind_dir_once_ro /run/a26-shell "$root/run/a26-shell"
[ -d /run/moon-audio ] || die "Moon audio runtime is unavailable"
[ -p /run/moon-audio/pcm ] || die "Moon audio PCM endpoint is unavailable"
[ -f /run/moon-audio/volume ] || die "Moon audio volume endpoint is unavailable"
[ "$(stat -c '%u' /run/moon-audio/pcm)" = 1000 ] ||
  die "Moon audio endpoint is not system-owned"
bind_dir_once_rw_data /run/moon-audio "$root/run/moon-audio"
dns_source=/etc/resolv.conf
dns_target="$root/etc/resolv.conf"
dns_staging="$root/etc/.resolv.conf.$$"
[ -s "$dns_source" ] || die "Moon DNS configuration is unavailable"
rm -f "$dns_staging"
if ! cp "$dns_source" "$dns_staging"; then
  rm -f "$dns_staging"
  die "cannot stage browser DNS configuration"
fi
if ! grep -q '^[[:space:]]*nameserver[[:space:]]' "$dns_staging"; then
  rm -f "$dns_staging"
  die "Moon DNS configuration has no nameserver"
fi
chmod 0644 "$dns_staging"
mv -f "$dns_staging" "$dns_target"
[ -s "$dns_target" ] || die "browser DNS installation failed"
mkdir -p "$root/var/lib/vimbrowser/profile" "$root/tmp"

exec chroot "$root" /usr/bin/env -i \
  HOME=/var/lib/vimbrowser \
  ALSA_CONFIG_PATH=/etc/asound.conf \
  DISPLAY="${DISPLAY:-:0}" \
  PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  XDG_CACHE_HOME=/var/lib/vimbrowser/.cache \
  XDG_CONFIG_HOME=/var/lib/vimbrowser/.config \
  XDG_RUNTIME_DIR=/tmp/vimbrowser-runtime \
  A26_VIMBROWSER_XTEST_CHAR_WORKAROUND=1 \
  A26_VIMBROWSER_PROXY="$proxy" \
  /bin/sh -c '
    mkdir -p "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME" "$XDG_RUNTIME_DIR"
    chmod 0700 "$XDG_RUNTIME_DIR"
    cd /opt/vimbrowser/Release
    set -- ./vimbrowser \
      --a26-shell \
      --use-gl=angle \
      --use-angle=swiftshader \
      --enable-unsafe-swiftshader \
      --disable-gpu-sandbox \
      --disable-accelerated-video-decode \
      --disable-accelerated-video-encode \
      --no-zygote \
      --force-device-scale-factor=2.5 \
      --touch-events=enabled \
      --profile-dir=/var/lib/vimbrowser/profile \
      --remote-debugging-port=9222
    if [ -n "$A26_VIMBROWSER_PROXY" ]; then
      set -- "$@" "--proxy-server=$A26_VIMBROWSER_PROXY"
    fi
    exec "$@" https://example.com
  ' >>"$log" 2>&1
