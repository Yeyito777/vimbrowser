#!/usr/bin/env bash
set -euo pipefail

app=${1:?usage: codesign-mac-app.sh APP IDENTITY}
identity=${2:?usage: codesign-mac-app.sh APP IDENTITY}
framework="${app}/Contents/Frameworks/Chromium Embedded Framework.framework"

if [[ ! -d "${framework}" ]]; then
  echo "error: CEF framework missing from ${app}" >&2
  exit 1
fi

# CEF's very large framework dylib must remain ad-hoc signed on local builds.
# Replacing its embedded signature with an Apple Development signature passes
# codesign's bundle walk but macOS rejects the image when cef_load_library()
# calls dlopen. The outer app keeps the stable developer identity used by
# Keychain, while the framework receives the valid local signature dyld needs.
codesign --force --deep --sign - "${framework}"

shopt -s nullglob
helpers=("${app}"/Contents/Frameworks/vimbrowser\ Helper*.app)
if [[ ${#helpers[@]} -ne 5 ]]; then
  echo "error: expected 5 vimbrowser helper apps, found ${#helpers[@]}" >&2
  exit 1
fi

for helper in "${helpers[@]}"; do
  codesign --force --deep --sign "${identity}" --timestamp=none "${helper}"
done

# Sign the outer bundle last so its resource seal records the final nested-code
# signatures. Do not use --deep here; recursively replacing the CEF framework's
# working ad-hoc signature recreates the dlopen failure above.
codesign --force --sign "${identity}" --timestamp=none "${app}"
codesign --verify --deep --strict "${app}"
