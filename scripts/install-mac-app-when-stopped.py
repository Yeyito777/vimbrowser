#!/usr/bin/env python3
"""Install an already staged/signed bundle only after its old process exits.

Never quits or launches any application. Keeps the previous bundle for rollback.
"""
import argparse
from datetime import datetime
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staged-app", required=True, type=Path)
    parser.add_argument("--destination", type=Path,
                        default=Path.home() / "Applications/vimbrowser.app")
    parser.add_argument("--ipc-binary", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    staged, destination = args.staged_app.resolve(), args.destination.absolute()
    if staged == destination or not (staged / "Contents/MacOS/vimbrowser").is_file():
        parser.error("a separate complete staged app is required")
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict",
                    str(staged)], check=True)
    architecture = subprocess.check_output(
        ["/usr/bin/lipo", "-archs", str(staged / "Contents/MacOS/vimbrowser")],
        text=True).strip()
    if architecture != "arm64":
        parser.error(f"expected arm64 staged executable, got {architecture}")
    # comm is the executable path, not a shell command mentioning the path.
    processes = subprocess.check_output(
        ["/bin/ps", "-axo", "pid=,comm="], text=True)
    running = []
    for line in processes.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) == 2 and parts[1].startswith(str(destination) + "/Contents/"):
            running.append(parts[0])
    if running:
        parser.exit(2, "REFUSED: installed browser/helpers still running (PIDs " +
                    ", ".join(running) + "). User must quit first; nothing changed.\n")
    if args.check:
        print("Ready to install; old browser is stopped. No changes made.")
        return
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    rollback = destination.parent / f".vimbrowser-rollback-{stamp}-{os.getpid()}"
    rollback.mkdir(mode=0o700)
    previous = rollback / destination.name
    if destination.exists():
        destination.rename(previous)
    try:
        staged.rename(destination)
    except BaseException:
        if previous.exists() and not destination.exists():
            previous.rename(destination)
        raise
    if args.ipc_binary:
        target = Path.home() / ".local/bin/vimbrowser-ipc"
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(f".vimbrowser-ipc-{os.getpid()}")
        shutil.copy2(args.ipc_binary, temporary)
        temporary.chmod(0o755)
        temporary.replace(target)
    register = Path("/System/Library/Frameworks/CoreServices.framework/Frameworks/"
                    "LaunchServices.framework/Support/lsregister")
    if register.exists():
        subprocess.run([str(register), "-f", str(destination)], check=True)
    print(f"Installed: {destination}")
    print(f"Rollback: {previous}")
    print("No application was launched. User may now open vimbrowser.")


if __name__ == "__main__":
    main()
