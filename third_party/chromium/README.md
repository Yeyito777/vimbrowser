# Chromium/CEF source backend

This directory is the source-built backend for vimbrowser.

- Chromium tag: `147.0.7727.118`
- Chromium commit: `e46e70b7112e24cb0501b746c09f8228ff88850a`
- CEF commit: `d58e84d17dd3f646c906ac633156cd0ec46638e9`
- Local Chromium branch: `vimbrowser-element-shader`
- Local Chromium patch: `../../patches/chromium/element-shader.patch`

The patch is native Chromium/Blink C++ code. There is intentionally no JS/CSS
shader implementation in vimbrowser. The element shader is applied in Blink style
resolution before layout/paint and native scrollbar painting is hooked in
`ui/native_theme`.

The heavy checkouts/build outputs are ignored by the vimbrowser git repo:

- `third_party/chromium/src/`
- `third_party/chromium/depot_tools/`

Use `scripts/bootstrap-chromium-source.sh` to recreate/update the source checkout.
The bootstrap order matters:

1. checkout Chromium at the pinned tag;
2. checkout CEF at the pinned commit;
3. run CEF's project generator so CEF patches Chromium first;
4. apply `patches/chromium/element-shader.patch` on top.

Do not run `cef_create_projects.sh` after the shader patch is applied; CEF's
light-mode patch touches the same native theme files and will conflict. Use
`scripts/build-chromium-cef.sh` for repeat builds because it only regenerates GN
and runs `autoninja`.
