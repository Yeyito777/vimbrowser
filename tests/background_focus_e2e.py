#!/usr/bin/env python3
"""Isolated macOS IPC regression; never connects to a user's browser/profile."""
import argparse
import base64
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--cli", required=True)
    parser.add_argument("--foreground-monitor", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="vb-focus-", dir="/tmp") as tmp:
        root = Path(tmp)
        profile = root / "profile"
        profile.mkdir()
        page = root / "page.html"
        page.write_text("<title>Focus fixture</title><body>BACKGROUND_FIXTURE"
                        "<input id=edit><script>"
                        "window.addEventListener('load',()=>window.focus());"
                        "</script></body>")
        url = page.as_uri()
        # Above the lazy restore threshold: only the active page starts loaded.
        (profile / "state").write_text(
            "active=0\nshader=off\n" + "".join(
                f"tab={url}?tab={i}\n" for i in range(10)) +
            f"context_tab=restored\t0\t0\t11264\toff\t{url}?context=1\n" +
            f"context_tab=restored\t0\t0\t12288\toff\t{url}?context=2\n")
        env = dict(os.environ, VIMBROWSER_TEST_NO_ACTIVATE="1")
        env.pop("VIMBROWSER_IPC", None)
        env.pop("VIMBROWSER_PROFILE_DIR", None)
        env.pop("VIMBROWSER_STATE_PATH", None)
        with (root / "browser.log").open("w") as log:
            monitor = subprocess.Popen([args.foreground_monitor],
                                       stdout=subprocess.PIPE, text=True)
            proc = subprocess.Popen(
                [args.binary, f"--profile-dir={profile}", "--disable-gpu"],
                env=env, stdout=log, stderr=log)

            def ipc(command, allow_error=False):
                with socket.socket(socket.AF_UNIX) as sock:
                    sock.settimeout(20)
                    sock.connect(str(profile / "ipc.sock"))
                    sock.sendall(command.encode() + b"\n")
                    chunks = []
                    while chunk := sock.recv(65536):
                        chunks.append(chunk)
                result = b"".join(chunks).decode()
                if not allow_error:
                    assert not result.startswith("ERR "), (command, result)
                return result

            def data(command):
                return json.loads(ipc(command))

            def js(tab, source):
                encoded = base64.b64encode(source.encode()).decode()
                return ipc(f"js-base64 {tab} {encoded}")

            try:
                deadline = time.monotonic() + 60
                while not (profile / "ipc.sock").exists():
                    assert proc.poll() is None, (root / "browser.log").read_text()
                    assert time.monotonic() < deadline, "test startup timeout"
                    time.sleep(.1)
                while True:
                    status = data("status")
                    if status["visible_tabid"] == status["active_tabid"]:
                        break
                    assert time.monotonic() < deadline
                    time.sleep(.1)
                tabs = data("tabs")["tabs"]
                active, dormant = tabs[0]["id"], tabs[2]["id"]
                sidebar = data("sidebar")
                keys = ("current_folder_id", "selected_type", "selected_id",
                        "focused", "scroll_offset")
                baseline = {k: sidebar[k] for k in keys}

                def unchanged():
                    status = data("status")
                    assert status["active_tabid"] == active, status
                    assert status["visible_tabid"] == active, status
                    current = data("sidebar")
                    assert {k: current[k] for k in keys} == baseline, current

                assert "BACKGROUND_FIXTURE" in ipc(f"text {dormant}")
                unchanged()
                assert "BACKGROUND_FIXTURE" in ipc(f"text {tabs[10]['id']}")
                assert tabs[10]["context"] == "restored"
                unchanged()
                print("PASS lazy-restored text loads without selecting", flush=True)
                for command in ("open-tab", "open-background-tab"):
                    result = data(f"{command} {url}")
                    target = result["tabs"][-1]["id"]
                    assert "BACKGROUND_FIXTURE" in ipc(f"text {target}")
                    js(target, "window.focus();document.querySelector('input').focus();'ok'")
                    unchanged()
                for command in ("open-context-tab", "open-background-context-tab"):
                    result = data(f"{command} isolated {url}")
                    target = result["tabs"][-1]["id"]
                    assert result["tabs"][-1]["context"] == "isolated"
                    assert "BACKGROUND_FIXTURE" in ipc(f"text {target}")
                    unchanged()
                print("PASS default/background/context opens and DOM focus", flush=True)
                result = subprocess.run(
                    [args.cli, "open", "--profile-dir", str(profile), url],
                    text=True, capture_output=True, timeout=20, env=env)
                assert result.returncode == 0, result.stderr
                json.loads(result.stdout)
                unchanged()
                result = subprocess.run(
                    [args.cli, "open-context", "--profile-dir", str(profile),
                     "cli-context", url], text=True, capture_output=True,
                    timeout=20, env=env)
                assert result.returncode == 0, result.stderr
                unchanged()
                print("PASS installed CLI background contract", flush=True)
                folder_result = data("folder-create 0 Background")
                folder = folder_result["folders"][-1]["id"]
                unchanged()
                ipc(f"tab-folder {dormant} {folder}")
                unchanged()
                ipc(f"tab-order {dormant} 0")
                unchanged()
                assert "use --force" in ipc(
                    f"tab-folder {baseline['selected_id']} {folder}",
                    allow_error=True)
                unchanged()
                ipc(f"folder-rename {folder} Renamed")
                unchanged()
                print("PASS organization/sidebar selection guard", flush=True)
                # Loading another dormant tab must not require selecting it.
                target = tabs[4]["id"]
                ipc(f"open {target} {url}?loaded")
                assert "BACKGROUND_FIXTURE" in ipc(f"text {target}")
                unchanged()
                assert data("status")["rejected_background_focus_requests"] > 0
                print("PASS lazy load and native focus requests rejected", flush=True)
                # Explicit focus is allowed inside the invisible test window.
                data(f"tab-focus {target}")
                deadline = time.monotonic() + 10
                while data("status")["visible_tabid"] != target:
                    assert time.monotonic() < deadline
                    time.sleep(.1)
                assert data("status")["active_tabid"] == target
                result = data(f"open-focus-tab {url}")
                assert result["active_tabid"] != target
                print("PASS explicit focus opt-in", flush=True)
                saved = (profile / "state").read_text()
                assert saved.count("context_tab=restored\t") == 2
                assert saved.count("context_tab=isolated\t") == 2
                assert saved.count("context_tab=cli-context\t") == 1
                print("PASS named contexts persist as isolated state records", flush=True)
            except BaseException:
                print((root / "browser.log").read_text()[-12000:])
                raise
            finally:
                # Only the exact child created above; never a user's browser.
                proc.terminate()
                try:
                    proc.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=10)
                monitor.terminate()
                observed, _ = monitor.communicate(timeout=10)
                assert str(proc.pid) not in observed.split(), (
                    "isolated browser became the OS foreground application",
                    observed)
                assert observed.strip(), "OS-focus monitor produced no samples"
                print("PASS OS foreground monitor: test browser never activated",
                      flush=True)
    print("PASS all isolated background-focus regressions")


if __name__ == "__main__":
    main()
