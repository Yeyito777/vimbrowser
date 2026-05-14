#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
backend_root="${repo_dir}/backend"
chromium_src="${backend_root}/chromium"
depot_tools="${backend_root}/depot_tools"
sync_deps=${SYNC_DEPS:-0}

mkdir -p "${backend_root}"

if [[ ! -x "${depot_tools}/gn" || ! -x "${depot_tools}/autoninja" ]]; then
  echo "[+] Cloning depot_tools into backend/depot_tools"
  rm -rf "${depot_tools}"
  git clone --depth=1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "${depot_tools}"
  # depot_tools is a bootstrap tool, not part of the tracked Chromium backend.
  rm -rf "${depot_tools}/.git"
fi
export PATH="${depot_tools}:$PATH"

if [[ ! -f "${chromium_src}/BUILD.gn" || ! -d "${chromium_src}/cef" ]]; then
  cat >&2 <<EOF
error: Chromium/CEF source is missing from backend/chromium.

Chromium now lives directly in the main vimbrowser git repo instead of being a
nested checkout under third_party/. Restore backend/chromium from git rather
than applying patches into a separate repository.
EOF
  exit 1
fi

cat > "${backend_root}/.gclient" <<EOF
solutions = [
  {
    "name": "chromium",
    "url": "https://chromium.googlesource.com/chromium/src.git",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {},
  },
]
target_os = ["linux"]
EOF

if [[ "${sync_deps}" != "0" ]]; then
  echo "[+] Syncing Chromium dependencies (large/slow)"
  cd "${backend_root}"
  gclient sync --with_branch_heads --with_tags --nohooks --jobs "${JOBS:-16}"
  gclient runhooks
else
  echo "[+] Skipping gclient sync (set SYNC_DEPS=1 to refresh dependencies)."
fi

cat <<EOF
[+] Chromium backend is ready.
    source:      ${chromium_src}
    depot_tools: ${depot_tools}

Chromium source is tracked directly by the main vimbrowser git repository; edit
backend/chromium files directly and commit them normally.
EOF
