#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
backend_root="${repo_dir}/backend"
chromium_src="${backend_root}/chromium"
cef_src="${chromium_src}/cef"
gclient_solution="${backend_root}/src"
depot_tools="${backend_root}/depot_tools"
sync_deps=${SYNC_DEPS:-0}

mkdir -p "${backend_root}"

if [[ ! -x "${depot_tools}/gn" || ! -x "${depot_tools}/autoninja" ||
      ! -d "${depot_tools}/.git" ]]; then
  echo "[+] Cloning depot_tools into backend/depot_tools"
  rm -rf "${depot_tools}"
  git clone --depth=1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "${depot_tools}"
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

# The main vimbrowser repository tracks Chromium files directly and therefore
# omits Chromium's nested Git metadata. gclient still requires solution metadata
# to resolve DEPS, but cloning another full Chromium worktree would duplicate
# tens of gigabytes. Attach a filtered, single-tag metadata repository to the
# existing source tree without changing any checked-out file.
if [[ ! -d "${chromium_src}/.git" ]]; then
  chromium_revision=${CHROMIUM_REVISION:-refs/tags/147.0.7727.118}
  echo "[+] Initializing Chromium solution metadata at ${chromium_revision}"
  git -C "${chromium_src}" init -q
  git -C "${chromium_src}" remote add origin \
    https://chromium.googlesource.com/chromium/src.git
  git -C "${chromium_src}" fetch --depth=1 --filter=blob:none origin \
    "${chromium_revision}"
  git -C "${chromium_src}" update-ref refs/heads/vimbrowser-base FETCH_HEAD
  git -C "${chromium_src}" symbolic-ref HEAD refs/heads/vimbrowser-base
  git -C "${chromium_src}" read-tree FETCH_HEAD
fi

# CEF's distribution tooling requires its own Git identity to stamp package
# metadata. The source files are still owned by the outer vimbrowser repo; add
# only a filtered checkout at the pinned CEF revision and never check it out
# over the repository snapshot.
if [[ ! -d "${cef_src}/.git" ]]; then
  cef_revision=${CEF_REVISION:-d58e84d17dd3f646c906ac633156cd0ec46638e9}
  echo "[+] Initializing CEF package metadata at ${cef_revision}"
  git -C "${cef_src}" init -q
  git -C "${cef_src}" remote add origin \
    https://bitbucket.org/chromiumembedded/cef.git
  git -C "${cef_src}" fetch --depth=1 --filter=blob:none origin \
    "${cef_revision}"
  git -C "${cef_src}" update-ref refs/heads/vimbrowser-base FETCH_HEAD
  git -C "${cef_src}" symbolic-ref HEAD refs/heads/vimbrowser-base
  git -C "${cef_src}" read-tree FETCH_HEAD
fi

# The shallow metadata intentionally has no unrelated upstream branches, but
# make_distrib.py asks Git for changes relative to origin/master when writing
# README metadata. Point that comparison ref at the pinned snapshot so package
# generation stays quiet and deterministic.
if ! git -C "${cef_src}" show-ref --verify --quiet refs/remotes/origin/master; then
  git -C "${cef_src}" update-ref refs/remotes/origin/master HEAD
fi

# Chromium tracks these paths even though its own ignore rules exclude them.
# They were consequently omitted when the snapshot was imported into the outer
# repository, but GN needs them to generate CEF resource and USB tables.
# Materialize only missing paths from the pinned Chromium metadata checkout;
# never overwrite a repository-provided file.
for upstream_path in \
  third_party/protobuf/python/google/protobuf/descriptor_pb2.py \
  third_party/protobuf/python/google/protobuf/compiler/plugin_pb2.py \
  third_party/usb_ids; do
  if [[ ! -e "${chromium_src}/${upstream_path}" ]]; then
    echo "[+] Restoring ${upstream_path} from pinned Chromium"
    git -C "${chromium_src}" restore --source=HEAD --worktree -- "${upstream_path}"
  fi
done

# The checked-out files belong to the outer vimbrowser repository. The nested
# metadata exists only so gclient can resolve DEPS; it must not inspect or
# rewrite this source snapshot. In particular, allowing gclient's solution
# status check to diff the sparse metadata would lazily download Chromium blobs
# for every locally modified backend file and duplicate work already owned by
# the outer repository.
git -C "${chromium_src}" ls-files -z |
  git -C "${chromium_src}" update-index --assume-unchanged -z --stdin

# Chromium's DEPS entries are rooted at "src/...". Keep the repository's
# human-readable backend/chromium location while presenting the standard
# solution name to gclient. Refuse to replace a real directory here; only the
# generated symlink is managed by this bootstrap script.
if [[ -L "${gclient_solution}" ]]; then
  if [[ "$(readlink "${gclient_solution}")" != "chromium" ]]; then
    echo "error: ${gclient_solution} points somewhere other than chromium" >&2
    exit 1
  fi
elif [[ -e "${gclient_solution}" ]]; then
  echo "error: ${gclient_solution} must be a symlink to chromium" >&2
  exit 1
else
  ln -s chromium "${gclient_solution}"
fi

cat > "${backend_root}/.gclient" <<EOF
solutions = [
  {
    "name": "src",
    "url": "https://chromium.googlesource.com/chromium/src.git",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {
      "checkout_pgo_profiles": False,
      "source_tarball": False,
      "siso_version": "latest",
    },
  },
]
target_os = []
EOF

if [[ "${sync_deps}" != "0" ]]; then
  echo "[+] Syncing missing Chromium Git dependencies"
  "${repo_dir}/scripts/sync-chromium-git-deps.py"
  echo "[+] Syncing Chromium binary/tool dependencies"
  cd "${backend_root}"
  # Git dependencies are vendored in the outer vimbrowser repository. Sync
  # only CIPD/GCS packages here; letting gclient manage Git dependency paths
  # would move the repository-owned source trees into _bad_scm.
  gclient sync --ignore-dep-type=git --nohooks --jobs "${JOBS:-16}"

  # gclient intentionally skips the vendored devtools-frontend Git dependency,
  # which also prevents it from recursing into that checkout's DEPS file. CEF
  # still bundles DevTools resources, so ensure its pinned native tool packages
  # explicitly.
  devtools_esbuild="${chromium_src}/third_party/devtools-frontend/src/third_party/esbuild/esbuild"
  echo "[+] Syncing DevTools esbuild tool"
  "${depot_tools}/cipd" ensure \
    -root "$(dirname "${devtools_esbuild}")" \
    -ensure-file - <<'EOF'
infra/3pp/tools/esbuild/${platform} version:3@0.25.1.chromium.2
EOF

  devtools_rollup="${chromium_src}/third_party/devtools-frontend/src/third_party/rollup_libs"
  echo "[+] Syncing DevTools Rollup native library"
  "${depot_tools}/cipd" ensure \
    -root "${devtools_rollup}" \
    -ensure-file - <<'EOF'
infra/3pp/tools/rollup_libs/${platform} version:3@4.22.4
EOF
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
