#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
backend_root="${repo_dir}/backend"
chromium_src="${backend_root}/chromium"
depot_tools="${backend_root}/depot_tools"
case "$(uname -s):$(uname -m)" in
  Darwin:arm64)
    default_build_dir=Release_GN_arm64
    default_target=cef_framework
    target_cpu=arm64
    ;;
  Linux:x86_64)
    default_build_dir=Release_GN_x64
    default_target=libcef
    target_cpu=x64
    ;;
  *)
    echo "error: unsupported Chromium build host $(uname -s)/$(uname -m)" >&2
    exit 1
    ;;
esac
build_dir=${CHROMIUM_BUILD_DIR:-${default_build_dir}}
# Build the production CEF shared library by default. The //cef group also
# builds cefclient/ceftests and drags in GTK/test dependencies that vimbrowser
# does not ship or need.
target=${CEF_TARGET:-${default_target}}

if [[ ! -f "${chromium_src}/BUILD.gn" || ! -d "${chromium_src}/cef" ]]; then
  echo "error: Chromium/CEF source is missing from backend/chromium" >&2
  echo "       This repo now tracks Chromium directly; restore backend/chromium from git." >&2
  exit 1
fi

if [[ ! -x "${depot_tools}/gn" || ! -x "${depot_tools}/autoninja" ]]; then
  echo "error: depot_tools is missing from backend/depot_tools; run scripts/bootstrap-chromium-source.sh" >&2
  exit 1
fi

export PATH="${depot_tools}:$PATH"

if [[ "$(uname -s)" == "Darwin" ]] && ! xcrun metal --version >/dev/null 2>&1; then
  cat >&2 <<'EOF'
error: Xcode's Metal Toolchain component is not installed.
       Install it with: xcodebuild -downloadComponent MetalToolchain
       Then rerun this build with DEVELOPER_DIR pointing at that Xcode installation.
EOF
  exit 1
fi

cd "${chromium_src}"

mkdir -p "out/${build_dir}"
args_path="out/${build_dir}/args.gn"
args_tmp=$(mktemp "out/${build_dir}/args.gn.XXXXXX")
cat > "${args_tmp}" <<'EOF'
blink_heap_inside_shared_library=true
clang_use_chrome_plugins=false
disable_fieldtrial_testing_config=true
enable_background_mode=false
enable_backup_ref_ptr_support=false
enable_downgrade_processing=false
enable_resource_allowlist_generation=false
enable_widevine=true
# Sites such as X/Twitter and Steam serve MP4/H.264/AAC media.  Chromium's
# default open-source FFmpeg branding advertises only the free codec set, which
# makes those players fail with "media could not be played".
proprietary_codecs=true
ffmpeg_branding="Chrome"
rtc_use_h264=true
forbid_non_component_debug_builds=false
# Chromium enables DCHECKs by default for non-official optimized builds. That is
# useful for Chromium developer builds, but vimbrowser ships this backend as the
# interactive browser runtime; the extra hot-path checks make heavy pages feel
# throttled and can turn benign upstream DCHECKs into renderer crashes.
dcheck_always_on=false
enable_expensive_dchecks=false
is_component_build=false
is_debug=false
is_official_build=false
optimize_webui=true
symbol_level=0
treat_warnings_as_errors=false
use_partition_alloc_as_malloc=false
use_qt5=false
use_qt6=false
chrome_pgo_phase=0
EOF

if [[ "$(uname -s)" == "Darwin" ]]; then
  cat >> "${args_tmp}" <<EOF
target_cpu="${target_cpu}"
enable_cdm_host_verification=true
enable_cdm_storage_id=true
alternate_cdm_storage_id_key="968b476909da4373b08903c28e859454"
enable_rlz=true
EOF
else
  cat >> "${args_tmp}" <<EOF
target_cpu="${target_cpu}"
enable_linux_installer=false
use_sysroot=true
cef_use_gtk=false
EOF
fi

if [[ -n "${GN_DEFINES:-}" ]]; then
  printf '\n# Appended from GN_DEFINES\n%s\n' "${GN_DEFINES}" >> "${args_tmp}"
fi

args_changed=1
if [[ -f "${args_path}" ]] && cmp -s "${args_tmp}" "${args_path}"; then
  args_changed=0
  rm -f "${args_tmp}"
else
  mv "${args_tmp}" "${args_path}"
fi

if [[ "${args_changed}" == "1" || ! -f "out/${build_dir}/build.ninja" || -n "${GN_ARGUMENTS:-}" || "${VIMBROWSER_FORCE_GN_GEN:-0}" == "1" ]]; then
  echo "[+] Generating Chromium/CEF GN files in backend/chromium/out/${build_dir}"
  gn gen "out/${build_dir}" ${GN_ARGUMENTS:-}
else
  echo "[+] Reusing existing Chromium/CEF GN files in backend/chromium/out/${build_dir}"
  echo "    args.gn unchanged; autoninja will regenerate the manifest if GN inputs changed."
fi

echo "[+] Building ${target} in backend/chromium/out/${build_dir}"
autoninja_args=(-C "out/${build_dir}" --fast_nop)
if [[ -n "${JOBS:-}" ]]; then
  if [[ ! "${JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: JOBS must be a positive integer, got '${JOBS}'" >&2
    exit 1
  fi
  autoninja_args+=(-j "${JOBS}")
fi
autoninja "${autoninja_args[@]}" "${target}"

cat <<EOF
[+] Build finished.

CEF artifacts are in:
  ${chromium_src}/out/${build_dir}

To create a CEF binary distribution suitable for vimbrowser's CMake build,
run the platform-specific source-distrib target from the repository root.

Then configure vimbrowser with CEF_ROOT pointing at the generated distribution.
EOF
