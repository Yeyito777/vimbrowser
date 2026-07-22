#!/usr/bin/env python3
"""Fetch active Chromium Git dependencies absent from the vendored tree.

The outer vimbrowser repository owns the Chromium source snapshot and many of
its dependencies directly. Running a normal gclient Git sync would therefore
mistake those repository-owned directories for broken nested checkouts and move
them aside. This script leaves every populated vendored directory's files
untouched and creates only the external checkouts that the pinned Chromium DEPS
file requires but the repository does not contain. For build tools that inspect
a vendored dependency's revision, it creates metadata without checking out or
rewriting files.
"""

from __future__ import annotations

import platform
import subprocess
import sys
from pathlib import Path


REPO_DIR = Path(__file__).resolve().parent.parent
BACKEND_DIR = REPO_DIR / "backend"
CHROMIUM_DIR = BACKEND_DIR / "chromium"
DEPOT_TOOLS_DIR = BACKEND_DIR / "depot_tools"

sys.path.insert(0, str(DEPOT_TOOLS_DIR))
import gclient_eval  # noqa: E402


METADATA_ONLY_DEPENDENCIES = {
    # Dawn's version generator calls Git from this directory. Without local
    # metadata it walks up to Chromium and emits Chromium's decorated revision,
    # which is not the 40-character Dawn hash required by generated headers.
    "src/third_party/dawn",
}


def host_values() -> dict[str, object]:
    host_os = {
        "Darwin": "mac",
        "Linux": "linux",
        "Windows": "win",
    }.get(platform.system())
    if not host_os:
        raise RuntimeError(f"unsupported Chromium host {platform.system()}")

    host_cpu = {
        "aarch64": "arm64",
        "arm64": "arm64",
        "x86_64": "x64",
    }.get(platform.machine(), platform.machine())
    result: dict[str, object] = {
        "host_os": host_os,
        "host_cpu": host_cpu,
        "checkout_android": False,
        "checkout_chromeos": False,
        "checkout_fuchsia": False,
        "checkout_ios": False,
        "checkout_linux": host_os == "linux",
        "checkout_mac": host_os == "mac",
        "checkout_win": host_os == "win",
    }
    for cpu in (
        "arm",
        "arm64",
        "x86",
        "mips",
        "mips64",
        "ppc",
        "riscv64",
        "s390",
        "x64",
        "loong64",
    ):
        result[f"checkout_{cpu}"] = host_cpu == cpu
    return result


def git_output(*args: str, cwd: Path | None = None) -> str:
    return subprocess.check_output(
        ["git", *args], cwd=cwd, text=True, stderr=subprocess.DEVNULL
    ).strip()


def fetch_dependency(name: str, url_with_revision: str) -> None:
    if not name.startswith("src/"):
        raise RuntimeError(f"unexpected dependency path {name!r}")

    relative = Path(name).relative_to("src")
    destination = CHROMIUM_DIR / relative
    metadata_only = name in METADATA_ONLY_DEPENDENCIES
    if destination.exists() and not (destination / ".git").exists():
        if any(destination.iterdir()) and not metadata_only:
            return

    url, separator, revision = url_with_revision.rpartition("@")
    if not separator or not url or not revision:
        raise RuntimeError(f"dependency is not pinned: {name}={url_with_revision}")

    if (destination / ".git").exists():
        origin = git_output("remote", "get-url", "origin", cwd=destination)
        if origin.rstrip("/") != url.rstrip("/"):
            raise RuntimeError(
                f"{destination} has origin {origin!r}, expected {url!r}"
            )
        try:
            if git_output("rev-parse", "HEAD", cwd=destination) == revision:
                return
        except subprocess.CalledProcessError:
            pass
    else:
        destination.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "init", "-q", str(destination)], check=True)
        subprocess.run(
            ["git", "-C", str(destination), "remote", "add", "origin", url],
            check=True,
        )

    print(f"[+] Fetching {name}@{revision}", flush=True)
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
    if metadata_only:
        branch = "refs/heads/vimbrowser-base"
        subprocess.run(
            ["git", "-C", str(destination), "update-ref", branch, "FETCH_HEAD"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(destination), "symbolic-ref", "HEAD", branch],
            check=True,
        )
        return
    subprocess.run(
        ["git", "-C", str(destination), "checkout", "-q", "--detach", "FETCH_HEAD"],
        check=True,
    )


def main() -> int:
    deps_file = CHROMIUM_DIR / "DEPS"
    builtins = host_values()
    data = gclient_eval.Parse(
        deps_file.read_text(), str(deps_file), builtin_vars=builtins
    )
    variables = dict(data.get("vars", {}))
    variables.update(builtins)
    variables.update(
        {
            "checkout_pgo_profiles": False,
            "source_tarball": False,
            "siso_version": "latest",
        }
    )

    for name, dependency in sorted(data["deps"].items()):
        if not dependency or dependency.get("dep_type", "git") != "git":
            continue
        condition = dependency.get("condition")
        if condition and not gclient_eval.EvaluateCondition(condition, variables):
            continue
        fetch_dependency(name, dependency["url"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
