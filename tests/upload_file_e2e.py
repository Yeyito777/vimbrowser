#!/usr/bin/env python3
"""Isolated end-to-end coverage for the upload-file IPC command.

This test starts its own profile and browser process. It intentionally refuses
to run unless the caller marks the display as a nested X11 environment; use the
xenv command shown in docs/ipc.md and never run it on the host display.
"""

from __future__ import annotations

import argparse
import base64
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import threading
import time
import unittest


BINARY: Path
CLI: Path
XENV_INSTANCE: str | None


UPLOAD_FORM = """<!doctype html>
<meta charset="utf-8">
<title>vimbrowser upload-file test</title>
<label>Ordinary text <input id="ordinary" type="text"></label>
<label>Single text file <input id="single" type="file" accept=".txt"></label>
<input class="duplicate" type="file">
<input class="duplicate" type="file">
<label>Multiple text files
  <input id="multiple" type="file" multiple accept=".txt,text/plain">
</label>
<hr>
<button id="dynamic-button">Browse dynamic résumé</button>
<output id="dynamic-result">idle</output>
<button id="fsa-button">Browse FSA résumé</button>
<output id="fsa-result">idle</output>
<script>
document.querySelector('#dynamic-button').addEventListener('click', () => {
  const input = document.createElement('input');
  input.id = 'dynamic-upload';
  input.type = 'file';
  input.accept = '.pdf,application/pdf';
  input.hidden = true;
  const result = document.querySelector('#dynamic-result');
  const cleanup = () => input.remove();
  input.addEventListener('change', () => {
    result.textContent = input.files.length ? input.files[0].name : 'empty';
    cleanup();
  }, {once: true});
  input.addEventListener('cancel', () => {
    result.textContent = 'canceled';
    cleanup();
  }, {once: true});
  document.body.append(input);
  input.click();
});
document.querySelector('#fsa-button').addEventListener('click', async () => {
  const result = document.querySelector('#fsa-result');
  try {
    const handles = await showOpenFilePicker({
      multiple: false,
      types: [{description: 'PDF résumé', accept: {'application/pdf': ['.pdf']}}],
    });
    result.textContent = handles.length ? handles[0].name : 'empty';
  } catch (error) {
    result.textContent = error.name;
  }
});
</script>
"""


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args) -> None:
        pass


def send_ipc(socket_path: Path, command: str) -> dict:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(socket_path))
        connection.sendall((command + "\n").encode("utf-8"))
        chunks: list[bytes] = []
        while chunk := connection.recv(65536):
            chunks.append(chunk)
    return json.loads(b"".join(chunks))


def raw_upload(socket_path: Path, tabid: int, target: dict,
               paths: list[str]) -> dict:
    payload = json.dumps(
        {"version": 1, "target": target, "paths": paths},
        separators=(",", ":"),
    ).encode("utf-8")
    token = base64.b64encode(payload).decode("ascii")
    return send_ipc(socket_path, f"upload-file {tabid} {token}")


class UploadFileEndToEndTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if os.environ.get("VIMBROWSER_E2E_NESTED_X11") != "1":
            raise unittest.SkipTest(
                "refusing GUI launch outside a marked nested X11 environment"
            )
        if not os.environ.get("DISPLAY"):
            raise unittest.SkipTest("nested X11 DISPLAY is not set")

        cls.tempdir = tempfile.TemporaryDirectory(prefix="vimbrowser-upload-e2e-")
        cls.root = Path(cls.tempdir.name)
        cls.profile = cls.root / "profile"
        cls.socket_path = cls.profile / "ipc.sock"
        cls.page = cls.root / "upload-form.html"
        cls.page.write_text(UPLOAD_FORM, encoding="utf-8")
        cls.first = cls.root / "first file.txt"
        cls.second = cls.root / "second.txt"
        cls.bad_type = cls.root / "not-text.png"
        cls.resume = cls.root / "resume.pdf"
        cls.second_resume = cls.root / "second-resume.pdf"
        cls.first.write_text("first payload", encoding="utf-8")
        cls.second.write_text("second payload", encoding="utf-8")
        cls.bad_type.write_bytes(b"not actually a png; contents are irrelevant")
        cls.resume.write_bytes(b"controlled pdf fixture")
        cls.second_resume.write_bytes(b"second controlled pdf fixture")

        handler = partial(QuietHandler, directory=str(cls.root))
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls.http_thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.http_thread.start()
        cls.page_url = f"http://127.0.0.1:{cls.httpd.server_port}/{cls.page.name}"

        cls.browser = subprocess.Popen(
            [
                str(BINARY),
                "--disable-gpu",
                "--remote-debugging-port=0",
                f"--profile-dir={cls.profile}",
                cls.page_url,
            ],
            cwd=BINARY.parent,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if cls.browser.poll() is not None:
                raise RuntimeError(
                    f"isolated vimbrowser exited with {cls.browser.returncode}"
                )
            if cls.socket_path.exists():
                try:
                    status = send_ipc(cls.socket_path, "status")
                    cls.tabid = int(status["active_tabid"])
                    break
                except (ConnectionError, OSError, ValueError, KeyError):
                    pass
            time.sleep(0.05)
        else:
            raise RuntimeError("isolated vimbrowser IPC socket did not become ready")

        ready_deadline = time.monotonic() + 10
        while time.monotonic() < ready_deadline:
            result = cls.run_cli(
                "js", str(cls.tabid), "document.readyState",
            )
            if result.returncode == 0:
                try:
                    if json.loads(result.stdout).get("result") == "complete":
                        return
                except json.JSONDecodeError:
                    pass
            time.sleep(0.05)
        raise RuntimeError("controlled upload form did not finish loading")

    @classmethod
    def tearDownClass(cls) -> None:
        browser = getattr(cls, "browser", None)
        if browser and browser.poll() is None:
            os.killpg(browser.pid, signal.SIGTERM)
            try:
                browser.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(browser.pid, signal.SIGKILL)
                browser.wait(timeout=5)
        tempdir = getattr(cls, "tempdir", None)
        if tempdir:
            cls.httpd.shutdown()
            cls.httpd.server_close()
            cls.http_thread.join(timeout=2)
            tempdir.cleanup()

    @classmethod
    def run_cli(cls, *args: str) -> subprocess.CompletedProcess[str]:
        if not args:
            raise ValueError("CLI command is required")
        return subprocess.run(
            [
                str(CLI),
                args[0],
                "--socket",
                str(cls.socket_path),
                "--timeout",
                "5",
                *args[1:],
            ],
            capture_output=True,
            text=True,
            timeout=8,
            check=False,
        )

    def upload(self, target: str, *paths: Path) -> subprocess.CompletedProcess[str]:
        return self.run_cli(
            "upload-file", str(self.tabid), target, *(str(path) for path in paths)
        )

    def js_result(self, expression: str):
        result = self.run_cli("js", str(self.tabid), expression)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)["result"]

    def wait_for_js_result(self, expression: str, expected, timeout: float = 5):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            last = self.js_result(expression)
            if last == expected:
                return last
            time.sleep(0.05)
        self.fail(f"timed out waiting for {expected!r}; last value was {last!r}")

    def wait_for_chooser_state(self, expected: str, timeout: float = 5) -> dict:
        deadline = time.monotonic() + timeout
        chooser = {}
        while time.monotonic() < deadline:
            result = self.run_cli("upload-file-status", str(self.tabid))
            self.assertEqual(result.returncode, 0, result.stderr)
            chooser = json.loads(result.stdout)["chooser"]
            if chooser["state"] == expected:
                return chooser
            time.sleep(0.05)
        self.fail(f"timed out waiting for chooser state {expected!r}: {chooser!r}")

    def trusted_activation_point(self, selector: str) -> dict:
        if not XENV_INSTANCE:
            self.skipTest("--xenv-instance is required for trusted chooser tests")
        expression = f"""
JSON.stringify((() => {{
  const rect = document.querySelector({json.dumps(selector)}).getBoundingClientRect();
  return {{x: screenX + rect.left + rect.width / 2,
           y: screenY + rect.top + rect.height / 2}};
}})())
""".strip()
        return json.loads(self.js_result(expression))

    def trusted_click_point(self, point: dict) -> None:
        if not XENV_INSTANCE:
            self.skipTest("--xenv-instance is required for trusted chooser tests")
        result = subprocess.run(
            [
                "xenv", "click", "-e", XENV_INSTANCE,
                str(round(point["x"])), str(round(point["y"])),
            ],
            capture_output=True,
            text=True,
            timeout=3,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def trusted_activate(self, selector: str) -> None:
        self.trusted_click_point(self.trusted_activation_point(selector))

    def install_atomic_picker_fixture(
        self,
        name: str,
        *,
        button_style: str = "",
        covered: bool = False,
        child_target: bool = False,
    ) -> None:
        expression = f"""
(() => {{
  const name = {json.dumps(name)};
  document.querySelector(`#${{name}}-fixture`)?.remove();
  const fixture = document.createElement('div');
  fixture.id = `${{name}}-fixture`;
  fixture.style.cssText = 'position:relative;display:inline-block;margin:4px';
  const button = document.createElement('button');
  button.id = `${{name}}-button`;
  button.style.cssText = 'width:180px;height:44px;{button_style}';
  if ({str(child_target).lower()}) {{
    button.innerHTML = '<span style="display:block;width:100%;height:100%">Browse child</span>';
  }} else {{
    button.textContent = 'Browse fixture';
  }}
  const output = document.createElement('output');
  output.id = `${{name}}-result`;
  output.textContent = 'idle';
  button.addEventListener('click', () => {{
    const input = document.createElement('input');
    input.id = `${{name}}-input`;
    input.type = 'file';
    input.accept = '.pdf,application/pdf';
    input.hidden = true;
    input.addEventListener('change', () => {{
      output.textContent = input.files.length ? input.files[0].name : 'empty';
      input.remove();
    }}, {{once: true}});
    input.addEventListener('cancel', () => {{
      output.textContent = 'canceled';
      input.remove();
    }}, {{once: true}});
    document.body.append(input);
    input.click();
  }});
  fixture.append(button, output);
  if ({str(covered).lower()}) {{
    const cover = document.createElement('div');
    cover.id = `${{name}}-cover`;
    cover.style.cssText =
        'position:absolute;inset:0;z-index:10;background:rgba(0,0,0,.05)';
    fixture.append(cover);
  }}
  document.body.append(fixture);
  return true;
}})()
""".strip()
        self.assertTrue(self.js_result(expression))

    def remove_atomic_picker_fixture(self, name: str) -> None:
        self.js_result(
            f"document.querySelector({json.dumps(f'#{name}-fixture')})?.remove(); true"
        )

    def test_real_single_file_form_assignment(self) -> None:
        result = self.upload("#single", self.first)
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["file_count"], 1)
        self.assertFalse(payload["input"]["multiple"])
        self.assertEqual(
            self.js_result("document.querySelector('#single').files[0].name"),
            self.first.name,
        )

    def test_non_file_input_is_rejected(self) -> None:
        result = self.upload("#ordinary", self.first)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            json.loads(result.stdout)["error"]["code"], "target_not_file_input"
        )

    def test_ambiguous_css_target_is_rejected(self) -> None:
        result = self.upload(".duplicate", self.first)
        self.assertEqual(result.returncode, 1)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["error"]["code"], "ambiguous_target")
        self.assertEqual(payload["error"]["match_count"], 2)

    def test_multiple_files_are_rejected_for_single_input(self) -> None:
        result = self.upload("#single", self.first, self.second)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            json.loads(result.stdout)["error"]["code"], "multiple_not_allowed"
        )

    def test_multiple_files_are_assigned_to_multiple_input(self) -> None:
        result = self.upload("#multiple", self.first, self.second)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["file_count"], 2)
        names = self.js_result(
            "Array.from(document.querySelector('#multiple').files, f => f.name).join('|')"
        )
        self.assertEqual(names, f"{self.first.name}|{self.second.name}")

    def test_accept_constraint_is_enforced(self) -> None:
        result = self.upload("#single", self.bad_type)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout)["error"]["code"], "accept_mismatch")

    def test_explicit_index_target(self) -> None:
        result = self.upload("index:0", self.second)
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["target"]["kind"], "index")
        self.assertEqual(payload["target"]["index"], 0)

    def test_atomic_activation_handles_ephemeral_dynamic_file_input(self) -> None:
        self.assertEqual(
            self.js_result("document.querySelectorAll('#dynamic-upload').length"), 0
        )
        result = self.upload("activate:#dynamic-button", self.resume)
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["target"], {"kind": "activate", "match_count": 1})
        self.assertEqual(payload["chooser"]["state"], "consumed")
        self.assertEqual(payload["chooser"]["dialog_mode"], "open")
        self.wait_for_js_result(
            "document.querySelector('#dynamic-result').textContent", self.resume.name
        )
        self.assertEqual(
            self.js_result("document.querySelectorAll('#dynamic-upload').length"), 0
        )

    def test_atomic_activation_handles_file_system_access_picker(self) -> None:
        if not self.js_result("typeof showOpenFilePicker === 'function'"):
            self.skipTest("File System Access picker is unavailable")
        result = self.upload("activate:#fsa-button", self.resume)
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["target"], {"kind": "activate", "match_count": 1})
        self.assertEqual(payload["chooser"]["state"], "consumed")
        self.wait_for_js_result(
            "document.querySelector('#fsa-result').textContent", self.resume.name
        )

    def test_atomic_activation_accepts_a_hit_child_of_selected_control(self) -> None:
        name = "child-hit"
        self.install_atomic_picker_fixture(name, child_target=True)
        try:
            result = self.upload(f"activate:#{name}-button", self.resume)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.wait_for_js_result(
                f"document.querySelector('#{name}-result').textContent",
                self.resume.name,
            )
        finally:
            self.remove_atomic_picker_fixture(name)

    def test_atomic_activation_rejects_obscured_target(self) -> None:
        name = "obscured"
        self.install_atomic_picker_fixture(name, covered=True)
        try:
            result = self.upload(f"activate:#{name}-button", self.resume)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(
                json.loads(result.stdout)["error"]["code"], "target_obscured"
            )
            self.assertEqual(
                self.js_result(
                    f"document.querySelector('#{name}-result').textContent"
                ),
                "idle",
            )
        finally:
            self.remove_atomic_picker_fixture(name)

    def test_atomic_activation_rejects_pointer_events_none_target(self) -> None:
        name = "pointer-none"
        self.install_atomic_picker_fixture(name, button_style="pointer-events:none")
        try:
            result = self.upload(f"activate:#{name}-button", self.resume)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(
                json.loads(result.stdout)["error"]["code"], "target_obscured"
            )
        finally:
            self.remove_atomic_picker_fixture(name)

    def test_atomic_activation_rejects_zero_opacity_target(self) -> None:
        name = "transparent"
        self.install_atomic_picker_fixture(name, button_style="opacity:0")
        try:
            result = self.upload(f"activate:#{name}-button", self.resume)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(
                json.loads(result.stdout)["error"]["code"], "target_not_visible"
            )
        finally:
            self.remove_atomic_picker_fixture(name)

    def test_atomic_activation_does_not_consume_unrelated_trusted_chooser(self) -> None:
        if not XENV_INSTANCE:
            self.skipTest("--xenv-instance is required for trusted chooser tests")
        self.js_result("document.querySelector('#dynamic-result').textContent = 'idle'")
        point = self.trusted_activation_point("#dynamic-button")
        holder: dict[str, subprocess.CompletedProcess[str]] = {}

        def run_non_picker_activation() -> None:
            holder["result"] = self.upload("activate:#ordinary", self.resume)

        worker = threading.Thread(target=run_non_picker_activation)
        worker.start()
        time.sleep(1.0)
        self.trusted_click_point(point)
        worker.join(timeout=8)
        self.assertFalse(worker.is_alive(), "atomic upload command did not complete")
        result = holder["result"]
        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            json.loads(result.stdout)["error"]["code"], "chooser_not_opened"
        )
        self.wait_for_js_result(
            "document.querySelector('#dynamic-result').textContent", "canceled"
        )

    def test_atomic_activation_rejects_missing_and_ambiguous_targets(self) -> None:
        missing = self.upload("activate:#does-not-exist", self.resume)
        self.assertEqual(missing.returncode, 1)
        self.assertEqual(json.loads(missing.stdout)["error"]["code"], "target_not_found")

        ambiguous = self.upload("activate:button", self.resume)
        self.assertEqual(ambiguous.returncode, 1)
        payload = json.loads(ambiguous.stdout)
        self.assertEqual(payload["error"]["code"], "ambiguous_target")
        self.assertEqual(payload["error"]["match_count"], 2)

    def test_atomic_activation_reports_control_without_chooser(self) -> None:
        result = self.upload("activate:#ordinary", self.resume)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout)["error"]["code"], "chooser_not_opened")

    def test_chooser_target_handles_ephemeral_dynamic_file_input(self) -> None:
        self.assertEqual(
            self.js_result("document.querySelectorAll('#dynamic-upload').length"), 0
        )
        result = self.upload("chooser", self.resume)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["chooser"]["state"], "armed")

        self.trusted_activate("#dynamic-button")
        self.wait_for_js_result(
            "document.querySelector('#dynamic-result').textContent", self.resume.name
        )
        status = self.run_cli("upload-file-status", str(self.tabid))
        self.assertEqual(status.returncode, 0, status.stderr)
        chooser = json.loads(status.stdout)["chooser"]
        self.assertEqual(chooser["state"], "consumed")
        self.assertEqual(chooser["dialog_mode"], "open")
        self.assertEqual(self.js_result("document.querySelectorAll('input[type=file]').length"), 4)

    def test_chooser_target_handles_file_system_access_picker(self) -> None:
        if not self.js_result("typeof showOpenFilePicker === 'function'"):
            self.skipTest("File System Access picker is unavailable")
        result = self.upload("chooser", self.resume)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.trusted_activate("#fsa-button")
        self.wait_for_js_result(
            "document.querySelector('#fsa-result').textContent", self.resume.name
        )
        status = self.run_cli("upload-file-status", str(self.tabid))
        self.assertEqual(json.loads(status.stdout)["chooser"]["state"], "consumed")

    def test_chooser_accept_mismatch_fails_without_selecting(self) -> None:
        result = self.upload("chooser", self.first)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.trusted_activate("#dynamic-button")
        status = self.run_cli("upload-file-status", str(self.tabid))
        chooser = json.loads(status.stdout)["chooser"]
        self.assertEqual(chooser["state"], "failed")
        self.assertEqual(chooser["error"]["code"], "accept_mismatch")

    def test_chooser_arm_can_be_inspected_and_canceled(self) -> None:
        result = self.upload("chooser", self.resume)
        self.assertEqual(json.loads(result.stdout)["chooser"]["state"], "armed")
        status = self.run_cli("upload-file-status", str(self.tabid))
        self.assertEqual(json.loads(status.stdout)["chooser"]["state"], "armed")
        canceled = self.run_cli("upload-file-cancel", str(self.tabid))
        self.assertEqual(json.loads(canceled.stdout)["chooser"]["state"], "canceled")

    def test_chooser_z_arm_is_canceled_by_main_frame_navigation(self) -> None:
        result = self.upload("chooser", self.resume)
        self.assertEqual(json.loads(result.stdout)["chooser"]["state"], "armed")
        reloaded = self.run_cli("reload", str(self.tabid))
        self.assertEqual(reloaded.returncode, 0, reloaded.stderr)
        chooser = self.wait_for_chooser_state("canceled")
        self.assertEqual(chooser["error"]["code"], "navigation")
        self.wait_for_js_result("document.readyState", "complete")

    def test_chooser_rejects_multiple_files_for_single_picker(self) -> None:
        result = self.upload("chooser", self.resume, self.second_resume)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.trusted_activate("#dynamic-button")
        chooser = self.wait_for_chooser_state("failed")
        self.assertEqual(chooser["error"]["code"], "multiple_not_allowed")

    def test_chooser_revalidates_paths_when_request_arrives(self) -> None:
        transient = self.root / "transient.pdf"
        transient.write_bytes(b"delete before chooser")
        result = self.upload("chooser", transient)
        self.assertEqual(result.returncode, 0, result.stderr)
        transient.unlink()
        self.trusted_activate("#dynamic-button")
        chooser = self.wait_for_chooser_state("failed")
        self.assertEqual(chooser["error"]["code"], "path_not_found")

    def test_browser_rejects_nonexistent_and_non_regular_raw_paths(self) -> None:
        relative = raw_upload(
            self.socket_path,
            self.tabid,
            {"kind": "css", "value": "#single"},
            ["relative.txt"],
        )
        self.assertEqual(relative["error"]["code"], "path_not_absolute")

        missing = raw_upload(
            self.socket_path,
            self.tabid,
            {"kind": "css", "value": "#single"},
            [str(self.root / "missing.txt")],
        )
        self.assertEqual(missing["error"]["code"], "path_not_found")
        self.assertNotIn("missing.txt", json.dumps(missing))

        directory = raw_upload(
            self.socket_path,
            self.tabid,
            {"kind": "css", "value": "#single"},
            [str(self.root)],
        )
        self.assertEqual(directory["error"]["code"], "path_not_regular")

    def test_browser_rejects_malformed_payload(self) -> None:
        payload = send_ipc(
            self.socket_path, f"upload-file {self.tabid} definitely-not-base64"
        )
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["error"]["code"], "invalid_payload")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--xenv-instance")
    args, unittest_args = parser.parse_known_args()
    global BINARY, CLI, XENV_INSTANCE
    BINARY = args.binary.resolve(strict=True)
    CLI = args.cli.resolve(strict=True)
    XENV_INSTANCE = args.xenv_instance
    unittest.main(argv=["upload_file_e2e.py", *unittest_args], verbosity=2)


if __name__ == "__main__":
    main()
