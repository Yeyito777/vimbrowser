#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
backend_root="${repo_dir}/backend"
chromium_src="${backend_root}/chromium"
depot_tools="${backend_root}/depot_tools"
build_dir=${CHROMIUM_BUILD_DIR:-Release_GN_x64}
# Build the production CEF shared library by default. The //cef group also
# builds cefclient/ceftests and drags in GTK/test dependencies that vimbrowser
# does not ship or need.
target=${CEF_TARGET:-libcef}

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
cd "${chromium_src}"

mkdir -p "out/${build_dir}"
cat > "out/${build_dir}/args.gn" <<'EOF'
blink_heap_inside_shared_library=true
clang_use_chrome_plugins=false
disable_fieldtrial_testing_config=true
enable_background_mode=false
enable_backup_ref_ptr_support=false
enable_downgrade_processing=false
enable_linux_installer=false
enable_resource_allowlist_generation=false
enable_widevine=true
forbid_non_component_debug_builds=false
is_component_build=false
is_debug=false
is_official_build=false
optimize_webui=true
symbol_level=0
target_cpu="x64"
treat_warnings_as_errors=false
use_partition_alloc_as_malloc=false
use_system_libffi=false
use_qt5=false
use_qt6=false
use_sysroot=true
cef_use_gtk=false
chrome_pgo_phase=0
EOF

if [[ -n "${GN_DEFINES:-}" ]]; then
  printf '\n# Appended from GN_DEFINES\n%s\n' "${GN_DEFINES}" >> "out/${build_dir}/args.gn"
fi

echo "[+] Generating Chromium/CEF GN files in backend/chromium/out/${build_dir}"
gn gen "out/${build_dir}" ${GN_ARGUMENTS:-}

echo "[+] Building ${target} in backend/chromium/out/${build_dir}"
autoninja -C "out/${build_dir}" "${target}"

cat <<EOF
[+] Build finished.

CEF artifacts are in:
  ${chromium_src}/out/${build_dir}

To create a CEF binary distribution suitable for vimbrowser's CMake build:
  cd ${chromium_src}
  autoninja -C out/${build_dir} chrome_sandbox
  cd cef/tools
  ./make_distrib.sh --ninja-build --x64-build --minimal --allow-partial --no-archive --output-dir ../binary_distrib

Then configure vimbrowser with CEF_ROOT pointing at the generated distribution.
EOF
