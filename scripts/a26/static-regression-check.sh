#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo_dir"

require_literal() {
  file=$1
  literal=$2
  grep -Fq -- "$literal" "$file" || {
    echo "missing A26 regression marker in $file: $literal" >&2
    exit 1
  }
}

sh -n scripts/a26/phone-launch.sh
bash -n scripts/a26/install.sh

# Installation must detach every nested bind mount before rotating or deleting
# a rootfs. Otherwise rm would descend into the phone's live /proc, /sys, or
# /dev trees through the stale mount points.
require_literal scripts/a26/install.sh 'unmount_tree() {'
require_literal scripts/a26/install.sh 'unmount_tree \"\$base/rootfs\"'
require_literal scripts/a26/install.sh 'unmount_tree \"\$base/rootfs.old\"'
require_literal scripts/a26/install.sh \
  'Refusing to replace the browser rootfs while vimbrowser is running.'

# The phone rootfs must see Moon's original socket directory without gaining
# permission to replace it or its control socket.
require_literal scripts/a26/phone-launch.sh \
  'bind_dir_once_ro /run/a26-shell "$root/run/a26-shell"'
require_literal scripts/a26/phone-launch.sh \
  'bind_dir_once_rw_data /run/moon-audio "$root/run/moon-audio"'
require_literal scripts/a26/phone-launch.sh \
  '[ -p /run/moon-audio/pcm ]'
require_literal scripts/a26/phone-launch.sh 'ALSA_CONFIG_PATH=/etc/asound.conf'
require_literal scripts/a26/phone-launch.sh 'dns_staging="$root/etc/.resolv.conf.$$"'
require_literal scripts/a26/phone-launch.sh \
  'Moon DNS configuration has no nameserver'
require_literal scripts/a26/phone-launch.sh 'mv -f "$dns_staging" "$dns_target"'
require_literal scripts/a26/asound-moon.conf 'file "/run/moon-audio/pcm"'
require_literal scripts/a26/asound-moon.conf '48 kHz stereo signed-16-bit PCM'
require_literal scripts/a26/phone-launch.sh \
  'mount -o remount,bind,ro,nosuid,nodev,noexec "$target_path"'
require_literal scripts/a26/phone-launch.sh 'reject_symlink_components "$target_path"'
require_literal scripts/a26/phone-launch.sh \
  'bind destination escaped browser rootfs'
require_literal scripts/a26/phone-launch.sh \
  '[ -S /run/a26-shell/control.sock ]'
require_literal scripts/a26/phone-launch.sh 'private_dev_once "$root/dev"'
require_literal scripts/a26/phone-launch.sh \
  'private browser /dev unexpectedly exposes a video device'
if grep -Fq 'bind_dir_once /dev "$root/dev"' scripts/a26/phone-launch.sh; then
  echo 'A26 browser must not bind the phone host /dev into its rootfs' >&2
  exit 1
fi
require_literal scripts/a26/phone-launch.sh '--use-gl=angle'
require_literal scripts/a26/phone-launch.sh '--use-angle=swiftshader'
require_literal scripts/a26/phone-launch.sh '--enable-unsafe-swiftshader'
require_literal scripts/a26/phone-launch.sh '--disable-accelerated-video-decode'
require_literal scripts/a26/phone-launch.sh '--disable-accelerated-video-encode'
require_literal scripts/a26/phone-launch.sh '--no-zygote'
require_literal scripts/a26/phone-launch.sh '--touch-events=enabled'
require_literal scripts/a26/phone-launch.sh 'A26_VIMBROWSER_XTEST_CHAR_WORKAROUND=1'
require_literal src/browser_window.cc 'character_event.type = KEYEVENT_CHAR;'
if grep -F -- '      --disable-gpu \' scripts/a26/phone-launch.sh >/dev/null; then
  echo 'A26 launcher must retain the CEF Views compositor through SwiftShader' >&2
  exit 1
fi
if grep -E 'chmod .*(/run/a26-shell|control\.sock)' scripts/a26/phone-launch.sh >/dev/null; then
  echo 'phone launcher must preserve Moon socket permissions' >&2
  exit 1
fi

# Keep the client one-shot, bounded and close-on-exec, and keep its protocol
# vocabulary content-free. These are intentionally literal static assertions.
require_literal src/a26_keyboard.cc 'SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC'
require_literal src/a26_keyboard.cc 'constexpr size_t kMaxResponseBytes = 4096;'
require_literal src/browser_window.cc 'RestoreA26PageFocus'
require_literal src/browser_window.cc \
  'focus-independent delivery.'
require_literal src/main.cc 'Linux; Android 15; SM-A266M'
for command in \
  'keyboard hide\n' \
  'keyboard show text\n' \
  'keyboard show password\n' \
  'keyboard show search\n' \
  'keyboard show url\n' \
  'keyboard show number\n'; do
  require_literal src/a26_keyboard.cc "$command"
done

require_literal src/browser_window.cc 'if (a26_shell_) {'
require_literal src/browser_window.cc 'BuildA26Chrome();'
require_literal src/browser_window.cc 'XI_RawTouchBegin'
require_literal src/browser_window.cc 'BrowserWindow::HandleA26TouchScroll'
require_literal src/browser_window.cc 'root_height - 180'
require_literal src/browser_window_commands.cc 'void BrowserWindow::HandleA26TouchScroll'
require_literal src/browser_window_internal.h 'kA26BottomReserveHeight = 0'
require_literal src/app.cc 'purpose.empty() ? "text" : purpose'

echo 'A26 static regression checks passed'
