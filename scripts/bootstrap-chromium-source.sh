#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
chromium_root="${repo_dir}/third_party/chromium"
chromium_src="${chromium_root}/src"
depot_tools="${chromium_root}/depot_tools"
patch_file="${repo_dir}/patches/chromium/element-shader.patch"

chromium_tag=${CHROMIUM_TAG:-147.0.7727.118}
chromium_commit=${CHROMIUM_COMMIT:-e46e70b7112e24cb0501b746c09f8228ff88850a}
cef_commit=${CEF_COMMIT:-d58e84d17dd3f646c906ac633156cd0ec46638e9}
branch=${CHROMIUM_BRANCH:-vimbrowser-element-shader}
sync_deps=${SYNC_DEPS:-1}
run_project_gen=${RUN_CEF_PROJECT_GEN:-1}

mkdir -p "${chromium_root}"

if [[ ! -d "${depot_tools}/.git" ]]; then
  echo "[+] Cloning depot_tools"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "${depot_tools}"
fi
export PATH="${depot_tools}:$PATH"

if [[ ! -d "${chromium_src}/.git" ]]; then
  echo "[+] Cloning Chromium ${chromium_tag}"
  git clone --filter=blob:none --no-checkout https://chromium.googlesource.com/chromium/src.git "${chromium_src}"
fi

cd "${chromium_src}"
git fetch --filter=blob:none origin "refs/tags/${chromium_tag}:refs/tags/${chromium_tag}"
git checkout -B "${branch}" "${chromium_tag}"
if [[ "$(git rev-parse HEAD)" != "${chromium_commit}" ]]; then
  echo "error: Chromium tag ${chromium_tag} resolved to $(git rev-parse HEAD), expected ${chromium_commit}" >&2
  exit 1
fi

if [[ ! -d cef/.git ]]; then
  echo "[+] Cloning CEF ${cef_commit}"
  git clone https://github.com/chromiumembedded/cef.git cef
fi
cd cef
git fetch origin "${cef_commit}"
git checkout -B vimbrowser-cef-backend "${cef_commit}"

cd "${chromium_root}"
cat > .gclient <<EOF
solutions = [
  {
    "name": "src",
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
  echo "[+] Syncing Chromium dependencies (this is large/slow)"
  gclient sync --with_branch_heads --with_tags --nohooks --jobs "${JOBS:-16}"
  gclient runhooks
fi

cd "${chromium_src}"
if [[ "${run_project_gen}" != "0" ]]; then
  # Apply CEF's own Chromium patch stack before applying vimbrowser's shader
  # patch. Running this after the shader patch is present will intentionally fail
  # because CEF's light-mode patch and vimbrowser's native-theme hooks touch the
  # same files.
  export GN_DEFINES=${GN_DEFINES:-"is_official_build=false is_component_build=false symbol_level=0 use_sysroot=true use_system_libffi=false cef_use_gtk=false chrome_pgo_phase=0 treat_warnings_as_errors=false"}
  echo "[+] Applying CEF patches and generating projects"
  (cd cef && ./cef_create_projects.sh)
  if ! git diff --quiet || ! git diff --cached --quiet; then
    git add -u
    git commit -m 'Apply CEF patches for vimbrowser backend'
  fi
fi

if ! git log --format=%s -1 | grep -qx 'Add vimbrowser native element shader'; then
  if git apply --check "${patch_file}"; then
    git am "${patch_file}"
  else
    echo "error: Chromium shader patch does not apply cleanly after CEF patches" >&2
    exit 1
  fi
fi

echo "[+] Chromium source backend is ready at ${chromium_src}"
echo "    branch: $(git -C "${chromium_src}" branch --show-current)"
echo "    head:   $(git -C "${chromium_src}" rev-parse HEAD)"
