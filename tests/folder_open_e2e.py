#!/usr/bin/env python3
"""Isolated folder-targeted IPC/CLI regression; never uses the user's profile."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--cli", required=True)
    parser.add_argument("--foreground-monitor", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="vb-folder-", dir="/tmp") as tmp:
        root = Path(tmp)
        profile = root / "profile"
        profile.mkdir()
        page = root / "folder fixture.html"
        page.write_text("<title>Folder fixture</title><body>FOLDER_FIXTURE</body>")
        url = page.as_uri()
        env = dict(os.environ, VIMBROWSER_TEST_NO_ACTIVATE="1")
        for name in ("VIMBROWSER_IPC", "VIMBROWSER_PROFILE_DIR",
                     "VIMBROWSER_STATE_PATH"):
            env.pop(name, None)
        processes = []
        proc = None
        monitor = subprocess.Popen([args.foreground_monitor],
                                   stdout=subprocess.PIPE, text=True)

        def ipc(command, error=False):
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(20)
                sock.connect(str(profile / "ipc.sock"))
                sock.sendall(command.encode() + b"\n")
                chunks = []
                while chunk := sock.recv(65536):
                    chunks.append(chunk)
            result = b"".join(chunks).decode()
            if error:
                assert result.startswith("ERR "), (command, result)
                return result
            assert not result.startswith("ERR "), (command, result)
            return json.loads(result)

        def tabs():
            return ipc("tabs")["tabs"]

        def ui():
            status, sidebar = ipc("status"), ipc("sidebar")
            return (status["active_tabid"], status["visible_tabid"],
                    {key: sidebar[key] for key in
                     ("current_folder_id", "selected_type", "selected_id",
                      "focused", "scroll_offset")})

        def stop():
            if proc and proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=10)

        def start(log):
            child = subprocess.Popen(
                [args.binary, f"--profile-dir={profile}", "--disable-gpu", url],
                env=env, stdout=log, stderr=log)
            processes.append(child)
            deadline = time.monotonic() + 60
            while True:
                assert child.poll() is None, "isolated browser exited"
                try:
                    status = ipc("status")
                    if status["active_tabid"] == status["visible_tabid"]:
                        return child
                except (OSError, json.JSONDecodeError):
                    pass
                assert time.monotonic() < deadline, "isolated startup timeout"
                time.sleep(.1)

        expected = {}

        def verify_new(before, folder, context="", baseline=None):
            created = [tab for tab in tabs() if tab["id"] not in before]
            assert len(created) == 1, created
            tab = created[0]
            assert tab["folder_id"] == folder, tab
            assert (tab["context"] or "") == context, tab
            assert tab["url"] == url, tab
            if baseline is not None:
                assert ui() == baseline, (ui(), baseline)
            expected[tab["id"]] = (folder, context, url)
            return tab

        def ids():
            return {tab["id"] for tab in tabs()}

        try:
            with (root / "browser.log").open("w") as log:
                proc = start(log)
                parent = ipc("folder-create 0 Parent")["folders"][-1]["id"]
                nested = ipc(f"folder-create {parent} Nested")["folders"][-1]["id"]
                ipc(f"sidebar-folder {nested}")
                ipc("sidebar-focus sidebar")
                baseline = ui()
                normal = ("open-tab", "open-background-tab", "open-focus-tab")
                context = ("open-context-tab", "open-background-context-tab",
                           "open-focus-context-tab")
                names = {entry["name"] for entry in ipc("commands")["commands"]}
                for command in normal + context:
                    assert command + "-in-folder" in names

                # Root must override a non-root inherited folder. Also target
                # the parent without navigating out of the viewed nested folder.
                for folder in (0, parent, nested):
                    for command in normal[:2] + context[:2]:
                        before = ids()
                        context_arg = "folder-test " if command in context else ""
                        ipc(f"{command}-in-folder {folder} {context_arg}{url}")
                        verify_new(before, folder, context_arg.strip(), baseline)
                print("PASS atomic background/default/context opens, root and nested folders",
                      flush=True)

                for command in ("open", "open-context"):
                    for folder in (0, parent):
                        before = ids()
                        context_args = ["cli-folder"] if command == "open-context" else []
                        result = subprocess.run(
                            [args.cli, command, "--profile-dir", str(profile),
                             "--folder", str(folder), *context_args, url],
                            capture_output=True, text=True, timeout=20, env=env)
                        assert result.returncode == 0, result.stderr
                        json.loads(result.stdout)
                        verify_new(before, folder, "".join(context_args), baseline)
                print("PASS real CLI --folder integration without UI changes", flush=True)

                # Omission keeps old inherited placement, including --folder
                # appearing later as literal search text (covered in CLI tests).
                before = ids()
                ipc(f"open-background-tab {url}")
                verify_new(before, nested, baseline=baseline)
                ipc("sidebar-focus web")
                baseline = ui()
                before = ids()
                ipc(f"open-background-tab {url}")
                verify_new(before, 0, baseline=baseline)
                print("PASS legacy inherited placement", flush=True)

                for command in normal + context:
                    command += "-in-folder"
                    context_arg = "invalid-folder-context " if "context" in command else ""
                    for folder in ("999999999", "-1", "-0", "+0", "1x",
                                   "18446744073709551616"):
                        before = ids()
                        ipc(f"{command} {folder} {context_arg}{url}", error=True)
                        assert ids() == before and ui() == baseline
                    for suffix in ("", "0"):
                        before = ids()
                        ipc(f"{command} {suffix}", error=True)
                        assert ids() == before and ui() == baseline
                before = ids()
                ipc(f"open-background-context-tab-in-folder {nested} INVALID {url}",
                    error=True)
                assert ids() == before and ui() == baseline
                assert not (profile / "cef/contexts-invalid-folder-context").exists()
                print("PASS invalid requests have no tab, context, or UI side effects",
                      flush=True)

                for command in (normal[-1], context[-1]):
                    before = ids()
                    context_arg = "focus-folder " if command in context else ""
                    result = ipc(f"{command}-in-folder {nested} {context_arg}{url}")
                    created = verify_new(before, nested, context_arg.strip())
                    assert result["active_tabid"] == created["id"], result
                print("PASS explicit foreground variants", flush=True)

                # Background opens save immediately; force a synchronous save
                # after the last foreground open before restarting the fixture.
                ipc(f"folder-rename {parent} Parent")
                stop()
                proc = start(log)
                restored = {tab["id"]: (tab["folder_id"], tab["context"] or "", tab["url"])
                            for tab in tabs()}
                for tab_id, value in expected.items():
                    assert restored.get(tab_id) == value, (tab_id, restored.get(tab_id), value)
                print("PASS folders, stable IDs and isolated contexts survive restart",
                      flush=True)
        except BaseException:
            print((root / "browser.log").read_text()[-12000:])
            raise
        finally:
            # Only child processes created above, never the user's browser.
            for proc in processes:
                stop()
            monitor.terminate()
            observed, _ = monitor.communicate(timeout=10)
            assert observed.strip(), "foreground monitor produced no samples"
            assert not {str(child.pid) for child in processes} & set(observed.split()), observed
            print("PASS test browser never became the OS foreground app", flush=True)
    print("PASS all folder-open regressions")


if __name__ == "__main__":
    main()
