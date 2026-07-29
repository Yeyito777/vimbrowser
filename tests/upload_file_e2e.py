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

OOPIF_PICKER = """<!doctype html>
<meta charset="utf-8">
<title>cross-origin picker fixture</title>
<section id="drive-section">
  <h2>Choose an existing Drive file</h2>
  <button id="drive-browse">Browse</button>
  <output id="drive-result">idle</output>
</section>
<section id="computer-section">
  <h2>Upload files from your computer</h2>
  <button id="computer-browse">Browse</button>
  <output id="computer-result">idle</output>
</section>
<script>
function installPicker(buttonId, resultId) {
  document.querySelector(buttonId).addEventListener('click', () => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.pdf,application/pdf';
    input.hidden = true;
    input.addEventListener('change', () => {
      document.querySelector(resultId).textContent =
          input.files.length ? input.files[0].name : 'empty';
      input.remove();
    }, {once: true});
    input.addEventListener('cancel', () => {
      document.querySelector(resultId).textContent = 'canceled';
      input.remove();
    }, {once: true});
    document.body.append(input);
    input.click();
  });
}
installPicker('#drive-browse', '#drive-result');
installPicker('#computer-browse', '#computer-result');
</script>
"""

SAME_PROCESS_PICKER = """<!doctype html>
<meta charset="utf-8">
<title>same-process picker fixture</title>
<section>
  <h2>Upload from this embedded form</h2>
  <button id="same-process-browse">Browse</button>
  <output id="same-process-result">idle</output>
</section>
<script>
document.querySelector('#same-process-browse').addEventListener('click', () => {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.pdf,application/pdf';
  input.hidden = true;
  input.addEventListener('change', () => {
    document.querySelector('#same-process-result').textContent =
        input.files.length ? input.files[0].name : 'empty';
    input.remove();
  }, {once: true});
  document.body.append(input);
  input.click();
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
        cls.browser_log_path = cls.root / "browser-stderr.log"
        cls.browser_log = cls.browser_log_path.open("wb")
        cls.page = cls.root / "upload-form.html"
        cls.page.write_text(UPLOAD_FORM, encoding="utf-8")
        cls.picker = cls.root / "picker.html"
        cls.picker.write_text(OOPIF_PICKER, encoding="utf-8")
        cls.same_process_picker = cls.root / "same-process-picker.html"
        cls.same_process_picker.write_text(SAME_PROCESS_PICKER, encoding="utf-8")
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
        cls.picker_url = f"http://localhost:{cls.httpd.server_port}/{cls.picker.name}"
        cls.same_process_picker_url = (
            f"http://127.0.0.1:{cls.httpd.server_port}/"
            f"{cls.same_process_picker.name}"
        )
        cls.page.write_text(
            UPLOAD_FORM
            + f'\n<iframe id="cross-origin-picker" src="{cls.picker_url}" '
              'style="width:700px;height:420px"></iframe>\n'
            + f'<iframe id="same-process-picker" src="{cls.same_process_picker_url}" '
              'style="width:700px;height:220px"></iframe>\n',
            encoding="utf-8",
        )

        cls.browser = subprocess.Popen(
            [
                str(BINARY),
                "--disable-gpu",
                "--site-per-process",
                "--remote-debugging-port=0",
                f"--profile-dir={cls.profile}",
                cls.page_url,
            ],
            cwd=BINARY.parent,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=cls.browser_log,
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
        browser_log = getattr(cls, "browser_log", None)
        if browser_log:
            browser_log.close()
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

    @classmethod
    def browser_log_tail(cls) -> str:
        log = getattr(cls, "browser_log", None)
        if log:
            log.flush()
        path = getattr(cls, "browser_log_path", None)
        if not path or not path.exists():
            return ""
        return path.read_text(encoding="utf-8", errors="replace")[-8192:]

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

    def frame_tree(self) -> dict:
        result = self.run_cli("frame-tree", str(self.tabid))
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def frame_id_for_url(self, url: str, *, out_of_process: bool) -> str:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            tree = self.frame_tree()
            matches = [
                frame for frame in tree["frames"]
                if frame.get("url", "").startswith(url)
            ]
            if len(matches) == 1:
                self.assertEqual(
                    matches[0]["out_of_process"], out_of_process, matches[0]
                )
                return matches[0]["id"]
            time.sleep(0.05)
        self.fail(f"picker frame for {url!r} was not uniquely available")

    def picker_frame_id(self) -> str:
        return self.frame_id_for_url(self.picker_url, out_of_process=True)

    def frame_js_result(self, frame_id: str, expression: str):
        result = self.run_cli("frame-js", str(self.tabid), frame_id, expression)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)["result"]

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

    def test_exact_handle_uploads_through_cross_origin_picker(self) -> None:
        frame_id = self.picker_frame_id()
        tree = self.frame_tree()
        child = next(frame for frame in tree["frames"] if frame["id"] == frame_id)
        self.assertEqual(child["parent_id"], tree["main_frame_id"])

        ambiguous = self.run_cli(
            "inspect-controls", str(self.tabid), "--frame", frame_id,
            "--role", "button", "--name-exact", "Browse", "--limit", "1",
            "--require-one",
        )
        self.assertEqual(ambiguous.returncode, 1, ambiguous.stderr)
        ambiguous_payload = json.loads(ambiguous.stdout)
        self.assertEqual(
            ambiguous_payload["error"]["code"], "ambiguous_target",
            ambiguous.stdout + "\n" + self.browser_log_tail(),
        )
        self.assertIn("inspection", ambiguous_payload, ambiguous.stdout)
        self.assertEqual(ambiguous_payload["inspection"]["match_count"], 2)
        self.assertEqual(ambiguous_payload["inspection"]["returned_count"], 1)
        self.assertTrue(ambiguous_payload["inspection"]["truncated"])

        exact = self.run_cli(
            "inspect-controls", str(self.tabid), "--frame", frame_id,
            "--role", "button", "--name-exact", "Browse",
            "--context-contains", "Upload files from your computer",
            "--require-one",
        )
        self.assertEqual(exact.returncode, 0, exact.stderr)
        controls = json.loads(exact.stdout)["inspection"]["controls"]
        self.assertEqual(len(controls), 1)
        handle = controls[0]["handle"]

        # Add an identical-looking decoy after inspection. Exact-node activation
        # must still activate the originally inspected node, not re-run a selector.
        self.assertTrue(self.frame_js_result(
            frame_id,
            "(()=>{const b=document.querySelector('#computer-browse');"
            "const c=b.cloneNode(true);c.id='late-decoy';"
            "b.before(c);return true})()",
        ))
        result = self.upload(f"handle:{handle}", self.resume)
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["target"]["kind"], "handle")
        self.assertEqual(payload["chooser"]["state"], "consumed")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            value = self.frame_js_result(
                frame_id,
                "document.querySelector('#computer-result').textContent",
            )
            if value == self.resume.name:
                break
            time.sleep(0.05)
        else:
            self.fail("exact OOPIF handle did not upload to the intended control")
        self.assertEqual(
            self.frame_js_result(
                frame_id, "document.querySelector('#drive-result').textContent"
            ),
            "idle",
        )

        replay = self.upload(f"handle:{handle}", self.resume)
        self.assertEqual(replay.returncode, 1)
        self.assertEqual(json.loads(replay.stdout)["error"]["code"], "invalid_handle")

    def test_exact_handle_uploads_through_same_process_picker(self) -> None:
        frame_id = self.frame_id_for_url(
            self.same_process_picker_url, out_of_process=False
        )
        inspect_args = (
            "inspect-controls", str(self.tabid), "--frame", frame_id,
            "--role", "button", "--name-exact", "Browse", "--require-one",
        )

        covered = self.run_cli(*inspect_args)
        self.assertEqual(covered.returncode, 0, covered.stderr)
        covered_handle = json.loads(covered.stdout)["inspection"]["controls"][0][
            "handle"
        ]
        self.assertTrue(self.js_result(
            "(()=>{const f=document.querySelector('#same-process-picker');"
            "const r=f.getBoundingClientRect();const c=document.createElement('div');"
            "c.id='same-process-cover';c.style.cssText=`position:fixed;left:${r.left}px;"
            "top:${r.top}px;width:${r.width}px;height:${r.height}px;z-index:2147483647;"
            "background:rgba(255,0,0,.1)`;document.body.append(c);return true})()"
        ))
        try:
            obscured = self.upload(f"handle:{covered_handle}", self.resume)
            self.assertEqual(obscured.returncode, 1)
            self.assertEqual(
                json.loads(obscured.stdout)["error"]["code"], "target_obscured"
            )
        finally:
            self.js_result("document.querySelector('#same-process-cover')?.remove()")

        exact = self.run_cli(*inspect_args)
        self.assertEqual(exact.returncode, 0, exact.stderr)
        handle = json.loads(exact.stdout)["inspection"]["controls"][0]["handle"]
        result = self.upload(f"handle:{handle}", self.resume)
        self.assertEqual(result.returncode, 0, result.stderr)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            value = self.frame_js_result(
                frame_id,
                "document.querySelector('#same-process-result').textContent",
            )
            if value == self.resume.name:
                break
            time.sleep(0.05)
        else:
            self.fail("exact same-process frame handle did not upload")

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

    def test_z_backend_deadlines_release_stalled_renderer_operations(self) -> None:
        frame_id = self.picker_frame_id()
        inspected = self.run_cli(
            "inspect-controls", str(self.tabid), "--frame", frame_id,
            "--role", "button", "--name-exact", "Browse",
            "--context-contains", "Choose an existing Drive file",
            "--require-one",
        )
        self.assertEqual(inspected.returncode, 0, inspected.stderr)
        handle = json.loads(inspected.stdout)["inspection"]["controls"][0]["handle"]

        scheduled = self.frame_js_result(
            frame_id,
            "(()=>{setTimeout(()=>{while(true){}},50);return true})()",
        )
        self.assertTrue(scheduled)
        time.sleep(0.15)

        started = time.monotonic()
        activation = self.upload(f"handle:{handle}", self.resume)
        activation_elapsed = time.monotonic() - started
        self.assertEqual(activation.returncode, 1)
        self.assertEqual(
            json.loads(activation.stdout)["error"]["code"],
            "activation_backend_unavailable",
        )
        self.assertLess(activation_elapsed, 4.0)

        started = time.monotonic()
        inspection = self.run_cli(
            "inspect-controls", str(self.tabid), "--frame", frame_id,
            "--name-exact", "Browse",
        )
        inspection_elapsed = time.monotonic() - started
        self.assertEqual(inspection.returncode, 1)
        self.assertEqual(
            json.loads(inspection.stdout)["error"]["code"],
            "inspection_backend_unavailable",
        )
        self.assertLess(inspection_elapsed, 4.0)


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
