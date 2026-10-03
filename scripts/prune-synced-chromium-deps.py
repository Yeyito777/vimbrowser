#!/usr/bin/env python3
"""Apply vimbrowser's platform cuts to pinned, separately fetched Git deps.

The outer repository owns Chromium and vimbrowser source, but intentionally does
not duplicate selected DEPS Git checkouts. Keep the small, deterministic edits
needed by those pinned dependencies here so a clean worker reproduces the same
minimal graph as the developer checkout.
"""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys


REPO = Path(__file__).resolve().parents[1]
CHROMIUM = REPO / "backend" / "chromium"


def replace_once(path: Path, old: str, new: str) -> None:
    data = path.read_text()
    if old in data:
        path.write_text(data.replace(old, new, 1))
        print(f"pruned {path.relative_to(CHROMIUM)}")
        return
    if new and new in data:
        return
    if not new and old not in data:
        return
    raise RuntimeError(f"expected pinned dependency text is absent: {path}")


def ensure_dawn_runtime_dependencies() -> None:
    root = CHROMIUM / "third_party" / "dawn"
    if not root.is_dir():
        raise RuntimeError(
            "Dawn dependency is missing; run bootstrap-chromium-source.sh "
            "with SYNC_DEPS=1"
        )
    if not (root / ".git").exists():
        # Historical developer checkouts already contain these ignored nested
        # dependency files. A clean worker has Dawn metadata and can reproduce
        # them from the pinned submodule entries without vendoring 177 MiB into
        # the outer source repository.
        return
    for relative, url in (
        (
            "third_party/khronos/OpenGL-Registry",
            "https://chromium.googlesource.com/external/github.com/"
            "KhronosGroup/OpenGL-Registry",
        ),
        (
            "third_party/khronos/EGL-Registry",
            "https://chromium.googlesource.com/external/github.com/"
            "KhronosGroup/EGL-Registry",
        ),
    ):
        entry = subprocess.check_output(
            ["git", "-C", str(root), "ls-tree", "HEAD", relative], text=True
        ).strip()
        fields = entry.split()
        if len(fields) < 3 or fields[0] != "160000":
            raise RuntimeError(f"Dawn has no pinned gitlink for {relative}")
        revision = fields[2]
        destination = root / relative
        if (destination / ".git").exists():
            current = subprocess.check_output(
                ["git", "-C", str(destination), "rev-parse", "HEAD"], text=True
            ).strip()
            if current == revision:
                continue
        else:
            destination.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "init", "-q", str(destination)], check=True)
            subprocess.run(
                ["git", "-C", str(destination), "remote", "add", "origin", url],
                check=True,
            )
        print(f"[+] Fetching Dawn runtime dependency {relative}@{revision}")
        subprocess.run(
            [
                "git",
                "-C",
                str(destination),
                "fetch",
                "--depth=1",
                "--filter=blob:none",
                "origin",
                revision,
            ],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(destination), "checkout", "-q", "--detach", "FETCH_HEAD"],
            check=True,
        )


def prune_swiftshader() -> None:
    root = CHROMIUM / "third_party" / "swiftshader"
    if not root.is_dir():
        raise RuntimeError(
            "SwiftShader dependency is missing; run bootstrap-chromium-source.sh "
            "with SYNC_DEPS=1"
        )

    # A clean worker gives separately fetched dependencies their own Git
    # metadata. Older developer checkouts can contain the same pinned files
    # without a nested .git directory, so enforce the revision when available
    # and otherwise rely on the exact-text assertions below.
    if (root / ".git").exists():
        revision = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
        ).strip()
        expected = "313545f85af72f954820e54f4110cda591a6cf7b"
        if revision != expected:
            raise RuntimeError(
                f"unsupported SwiftShader revision {revision}; expected {expected}"
            )

    vulkan = root / "src" / "Vulkan" / "BUILD.gn"
    replace_once(vulkan, 'import("//build_overrides/wayland.gni")\n', "")
    replace_once(
        vulkan,
        '''    if (ozone_platform_wayland) {
      defines += [ "VK_USE_PLATFORM_WAYLAND_KHR" ]
    }
''',
        "",
    )

    wsi = root / "src" / "WSI" / "BUILD.gn"
    replace_once(wsi, 'import("//build_overrides/wayland.gni")\n', "")
    replace_once(
        wsi,
        '''
    if (ozone_platform_wayland) {
      sources += [
        "WaylandSurfaceKHR.cpp",
        "WaylandSurfaceKHR.hpp",
        "libWaylandClient.cpp",
        "libWaylandClient.hpp",
      ]
    }
''',
        "",
    )
    replace_once(
        wsi,
        '''
  # Do not try to depend on Wayland if the |wayland_gn_dir| is not set.
  if (is_linux && ozone_platform_wayland && wayland_gn_dir != "") {
    # Use third-party targets
    deps += [ "$wayland_gn_dir:wayland_client" ]
  }
''',
        "",
    )

    for name in (
        "WaylandSurfaceKHR.cpp",
        "WaylandSurfaceKHR.hpp",
        "libWaylandClient.cpp",
        "libWaylandClient.hpp",
    ):
        path = root / "src" / "WSI" / name
        if path.exists():
            path.unlink()
            print(f"deleted {path.relative_to(CHROMIUM)}")


def main() -> int:
    ensure_dawn_runtime_dependencies()
    prune_swiftshader()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
