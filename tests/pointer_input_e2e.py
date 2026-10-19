#!/usr/bin/env python3
"""Isolated end-to-end coverage for region-bound pointer gestures and key input.

Starts its own profile and browser process on a controlled local page with a
cross-origin out-of-process iframe. It refuses to run unless the caller marks
the display as a nested X11 environment; see docs/ipc.md. It never touches the
host display or a normal profile.
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

# Shared by both documents: a canvas and a slider that record every pointer
# event with isTrusted, positions and buttons; an input that counts trusted
# keydowns; and a partial overlay that must block gestures under it.
RECORDER = """
<style>
  body { margin: 0; font: 14px sans-serif; }
  .stage { position: relative; width: 300px; margin: 20px; }
  canvas { display: block; background: #eef; }
  .track { position: relative; width: 400px; height: 40px; background: #ddd; margin: 20px; }
  .knob { position: absolute; left: 0; top: 0; width: 40px; height: 40px; background: #36c;
          touch-action: none; }
  #cover { position: absolute; left: 0; top: 0; width: 60px; height: 60px; background: #c33a; }
</style>
<div class="stage"><canvas id="canvas" width="300" height="200" aria-label="canvas"></canvas>
<div id="cover"></div></div>
<div class="track"><div class="knob" id="knob" aria-label="knob"></div></div>
<input id="field" aria-label="field">
<script>
window.events = [];
const log = (e, extra) => window.events.push({type: e.type, trusted: e.isTrusted,
  x: e.offsetX, y: e.offsetY, buttons: e.buttons, ...extra});
const canvas = document.getElementById('canvas');
for (const type of ['pointermove', 'pointerdown', 'pointerup', 'mousedown', 'mouseup', 'click'])
  canvas.addEventListener(type, e => log(e));
const knob = document.getElementById('knob');
let drag = null;
knob.addEventListener('pointerdown', e => { log(e); drag = {x: e.clientX, left: knob.offsetLeft};
  knob.setPointerCapture(e.pointerId); });
knob.addEventListener('pointermove', e => { if (!drag) return; log(e);
  knob.style.left = Math.max(0, Math.min(360, drag.left + e.clientX - drag.x)) + 'px'; });
knob.addEventListener('pointerup', e => { log(e, {left: knob.offsetLeft}); drag = null; });
window.keys = {trusted: 0, untrusted: 0};
document.getElementById('field').addEventListener('keydown', e =>
  window.keys[e.isTrusted ? 'trusted' : 'untrusted']++);
</script>
"""

CHILD = "<!doctype html><meta charset=utf-8><title>child</title>" + RECORDER

PARENT = """<!doctype html><meta charset=utf-8><title>pointer-input test</title>
<h3>Pointer input fixture</h3>
""" + RECORDER + """
<div style="height:600px"></div>
<iframe id="child" src="CHILD_URL" style="width:520px;height:420px;border:3px solid #888"></iframe>
<div style="height:1200px"></div>
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


class PointerInputEndToEndTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if os.environ.get("VIMBROWSER_E2E_NESTED_X11") != "1":
            raise unittest.SkipTest("refusing GUI launch outside a marked nested X11 environment")
        if not os.environ.get("DISPLAY"):
            raise unittest.SkipTest("nested X11 DISPLAY is not set")
        cls.tempdir = tempfile.TemporaryDirectory(prefix="vimbrowser-pointer-e2e-")
        cls.root = Path(cls.tempdir.name)
        cls.profile = cls.root / "profile"
        cls.socket_path = cls.profile / "ipc.sock"
        cls.browser_log = (cls.root / "browser-stderr.log").open("wb")
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(cls.root)))
        cls.http_thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.http_thread.start()
        port = cls.httpd.server_port
        (cls.root / "child.html").write_text(CHILD, encoding="utf-8")
        (cls.root / "parent.html").write_text(
            PARENT.replace("CHILD_URL", f"http://localhost:{port}/child.html"), encoding="utf-8")
        (cls.root / "blank.html").write_text("<!doctype html><title>front</title>", encoding="utf-8")
        cls.base = f"http://127.0.0.1:{port}"
        cls.browser = subprocess.Popen(
            [str(BINARY), "--disable-gpu", "--site-per-process", f"--profile-dir={cls.profile}",
             f"{cls.base}/parent.html"],
            cwd=BINARY.parent, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=cls.browser_log, start_new_session=True)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if cls.browser.poll() is not None:
                raise RuntimeError(f"isolated vimbrowser exited with {cls.browser.returncode}")
            if cls.socket_path.exists():
                try:
                    cls.tabid = int(send_ipc(cls.socket_path, "status")["active_tabid"])
                    break
                except (ConnectionError, OSError, ValueError, KeyError):
                    pass
            time.sleep(0.05)
        else:
            raise RuntimeError("isolated vimbrowser IPC socket did not become ready")
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            tree = cls.cli_json("frame-tree", str(cls.tabid))
            children = [f for f in tree.get("frames", []) if f.get("parent_id")]
            if children and cls.frame_js(children[0]["id"], "document.readyState") == "complete":
                cls.main = tree["main_frame_id"]
                cls.child = children[0]["id"]
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("fixture frames did not load")

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
        if getattr(cls, "browser_log", None):
            cls.browser_log.close()
        if getattr(cls, "tempdir", None):
            cls.httpd.shutdown()
            cls.httpd.server_close()
            cls.tempdir.cleanup()

    @classmethod
    def cli(cls, *args: str, stdin: str | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run([str(CLI), args[0], "--socket", str(cls.socket_path), *args[1:]],
                              input=stdin, capture_output=True, text=True, timeout=30, check=False)

    @classmethod
    def cli_json(cls, *args: str, stdin: str | None = None) -> dict:
        out = cls.cli(*args, stdin=stdin).stdout
        try:
            return json.loads(out)
        except json.JSONDecodeError:
            return {"ok": False, "raw": out}

    @classmethod
    def frame_js(cls, frame: str, expression: str, tab: int | None = None):
        reply = cls.cli_json("frame-js", str(tab or cls.tabid), frame,
                             stdin=f"JSON.stringify({expression})")
        return json.loads(reply["result"]) if reply.get("ok") else None

    def setUp(self) -> None:
        for frame in (self.main, self.child):
            self.frame_js(frame, "(window.events.length = 0, window.keys = {trusted: 0, untrusted: 0}, "
                                 "document.getElementById('knob').style.left = '0px', true)")

    def point(self, frame: str, element: str, dx: float = 0, dy: float = 0) -> tuple[float, float]:
        """Viewport CSS px of an element's centre (+dx, dy), as the caller would see it."""
        rect = "(() => { const r = document.getElementById('%s').getBoundingClientRect();" \
               " return [r.left + r.width / 2, r.top + r.height / 2]; })()" % element
        x, y = self.frame_js(frame, rect)
        if frame == self.child:
            ox, oy = self.frame_js(self.main, "(() => { const f = document.getElementById('child');"
                                   " const r = f.getBoundingClientRect();"
                                   " return [r.left + f.clientLeft, r.top + f.clientTop]; })()")
            x, y = x + ox, y + oy
        return x + dx, y + dy

    def region(self, frame: str, at: tuple[float, float]) -> dict:
        return self.cli_json("input-region", str(self.tabid), "--frame", frame,
                             "--at", f"{at[0]:.2f},{at[1]:.2f}")

    def handle(self, frame: str, at: tuple[float, float]) -> str:
        minted = self.region(frame, at)
        self.assertTrue(minted.get("ok"), minted)
        return minted["handle"]

    def click(self, frame: str, at: tuple[float, float], handle_at=None) -> dict:
        h = self.handle(frame, handle_at or at)
        return self.cli_json("pointer", str(self.tabid), h, "click", "--at", f"{at[0]:.2f},{at[1]:.2f}")

    def events(self, frame: str) -> list[dict]:
        return self.frame_js(frame, "window.events")

    def scroll_into_view(self, frame: str, element: str) -> None:
        if frame == self.main:
            self.frame_js(frame, f"(document.getElementById('{element}').scrollIntoView({{block: 'center'}}), true)")
        else:
            self.frame_js(self.main, "(document.getElementById('child').scrollIntoView({block: 'center'}), true)")
        time.sleep(0.3)

    def assert_click_at(self, frame: str, dx: float, dy: float) -> None:
        reply = self.click(frame, self.point(frame, "canvas", dx, dy))
        self.assertTrue(reply.get("ok"), reply)
        self.assertEqual(reply["gesture"]["dispatched_steps"], 3)
        events = self.events(frame)
        self.assertEqual([e["type"] for e in events if e["type"] != "pointermove"],
                         ["pointerdown", "mousedown", "pointerup", "mouseup", "click"])
        self.assertTrue(all(e["trusted"] for e in events))
        click = events[-1]
        self.assertLessEqual(abs(click["x"] - (150 + dx)), 1.5)
        self.assertLessEqual(abs(click["y"] - (100 + dy)), 1.5)

    def test_click_at_exact_point_in_main_frame(self) -> None:
        self.scroll_into_view(self.main, "canvas")
        self.assert_click_at(self.main, 97, 41)

    def test_click_in_cross_origin_frame_after_scroll(self) -> None:
        self.scroll_into_view(self.child, "canvas")
        self.assert_click_at(self.child, -120, 60)
        self.cli("scroll-tab", str(self.tabid), "53")
        time.sleep(0.3)
        self.setUp()
        self.assert_click_at(self.child, 30, -70)

    def test_click_under_page_zoom_uses_css_pixels(self) -> None:
        try:
            self.cli("zoom", str(self.tabid), "1")
            time.sleep(0.6)
            self.scroll_into_view(self.child, "canvas")
            self.assert_click_at(self.child, 60, 30)
        finally:
            self.cli("zoom", str(self.tabid), "reset")
            time.sleep(0.4)

    def test_drag_is_trusted_with_buttons_and_exact_distance(self) -> None:
        self.scroll_into_view(self.child, "knob")
        start = self.point(self.child, "knob")
        h = self.handle(self.child, start)
        reply = self.cli_json("pointer", str(self.tabid), h, "drag",
                              "--from", f"{start[0]},{start[1]}",
                              "--to", f"{start[0] + 150},{start[1]}",
                              "--via", f"{start[0] + 60},{start[1] + 8}",
                              "--duration", "500", "--frame-bound")
        self.assertTrue(reply.get("ok"), reply)
        events = self.events(self.child)
        moves = [e for e in events if e["type"] == "pointermove"]
        self.assertGreaterEqual(len(moves), 20)
        self.assertTrue(all(e["trusted"] for e in events))
        self.assertTrue(all(e["buttons"] == 1 for e in moves))
        self.assertEqual(events[-1]["type"], "pointerup")
        self.assertLessEqual(abs(events[-1]["left"] - 150), 1)

    def test_out_of_bounds_and_stale_input_is_refused(self) -> None:
        self.scroll_into_view(self.main, "canvas")
        at = self.point(self.main, "canvas", 40, 20)
        # Off the bound element (still inside the frame): nothing is sent.
        off = self.click(self.main, self.point(self.main, "canvas", 0, 160), handle_at=at)
        self.assertEqual(off["error"]["code"], "outside_region")
        self.assertEqual(off["gesture"]["dispatched_steps"], 0)
        # Covered after the handle was minted.
        h = self.handle(self.main, at)
        self.frame_js(self.main, "(() => { const d = document.createElement('div'); d.id = 'veil';"
                      " d.style.cssText = 'position:fixed;inset:0;background:#0001'; document.body.append(d);"
                      " return true; })()")
        try:
            covered = self.cli_json("pointer", str(self.tabid), h, "click", "--at", f"{at[0]},{at[1]}")
        finally:
            self.frame_js(self.main, "(document.getElementById('veil').remove(), true)")
        self.assertEqual(covered["error"]["code"], "target_obscured")
        self.assertEqual(self.events(self.main), [])
        # A handle is one-shot, and a refused gesture spends it.
        self.assertEqual(self.cli_json("pointer", str(self.tabid), h, "click", "--at",
                                       f"{at[0]},{at[1]}")["error"]["code"], "invalid_handle")
        # The point must belong to the named frame, not a parent or child of it.
        self.scroll_into_view(self.child, "canvas")
        child_at = self.point(self.child, "canvas")
        self.assertEqual(self.region(self.main, child_at)["error"]["code"], "point_in_other_frame")
        self.scroll_into_view(self.main, "canvas")
        self.assertEqual(self.region(self.child, self.point(self.main, "canvas"))["error"]["code"],
                         "point_in_other_frame")
        # The page scrolled after minting: the bound element moved.
        at = self.point(self.main, "canvas")
        h = self.handle(self.main, at)
        self.cli("scroll-tab", str(self.tabid), "17")
        time.sleep(0.3)
        moved = self.cli_json("pointer", str(self.tabid), h, "click", "--at", f"{at[0]},{at[1]}")
        self.assertEqual(moved["error"]["code"], "target_moved")
        # A node-bound drag leaving the element stops and releases the button.
        self.scroll_into_view(self.main, "canvas")
        at = self.point(self.main, "canvas")
        h = self.handle(self.main, at)
        self.setUp()
        dragged = self.cli_json("pointer", str(self.tabid), h, "drag", "--from", f"{at[0]},{at[1]}",
                                "--to", f"{at[0]},{at[1] + 180}")
        self.assertEqual(dragged["error"]["code"], "outside_region")
        self.assertTrue(dragged["gesture"]["button_released"])
        self.assertEqual(self.events(self.main)[-1]["type"], "click")  # the release, on the canvas
        # A frame-bound drag may cross the frame, never leave it.
        self.scroll_into_view(self.child, "knob")
        start = self.point(self.child, "knob")
        h = self.handle(self.child, start)
        left = self.cli_json("pointer", str(self.tabid), h, "drag", "--from", f"{start[0]},{start[1]}",
                             "--to", f"{start[0] + 700},{start[1]}", "--frame-bound")
        self.assertEqual(left["error"]["code"], "outside_region")
        self.assertTrue(left["gesture"]["button_released"])
        self.assertEqual(self.events(self.child)[-1]["type"], "pointerup")
        # Malformed gestures are refused before the handle is spent.
        self.scroll_into_view(self.main, "canvas")
        h = self.handle(self.main, self.point(self.main, "canvas"))
        payload = {"version": 1, "handle": h, "steps": [{"type": "down", "x": 1, "y": 1}]}
        token = base64.b64encode(json.dumps(payload).encode()).decode()
        bad = send_ipc(self.socket_path, f"pointer-gesture {self.tabid} {token}")
        self.assertEqual(bad["error"]["code"], "invalid_gesture")
        # Handles expire after 15 s.
        time.sleep(15.5)
        x, y = self.point(self.main, "canvas")
        late = self.cli_json("pointer", str(self.tabid), h, "click", "--at", f"{x},{y}")
        self.assertEqual(late["error"]["code"], "expired_handle")

    def test_key_input_is_trusted_and_needs_the_focused_frame(self) -> None:
        self.scroll_into_view(self.main, "canvas")
        self.assertTrue(self.click(self.main, self.point(self.main, "canvas")).get("ok"))
        self.scroll_into_view(self.child, "field")
        field = self.point(self.child, "field")
        refused = self.cli_json("key-input", str(self.tabid), self.handle(self.child, field), stdin="abc")
        self.assertEqual(refused["error"]["code"], "frame_not_focused")
        self.assertEqual(refused["keys"]["dispatched_keys"], 0)
        self.assertTrue(self.click(self.child, field).get("ok"))
        scroll_before = self.frame_js(self.main, "scrollY")
        typed = self.cli_json("key-input", str(self.tabid), self.handle(self.child, field), stdin="jkG é1\n")
        self.assertTrue(typed.get("ok"), typed)
        self.assertEqual(typed["keys"]["dispatched_keys"], 6)
        back = self.cli_json("key-input", str(self.tabid), self.handle(self.child, field), "--key", "Backspace")
        self.assertTrue(back.get("ok"), back)
        self.assertEqual(self.frame_js(self.child, "document.getElementById('field').value"), "jkG é")
        self.assertEqual(self.frame_js(self.child, "window.keys"), {"trusted": 7, "untrusted": 0})
        # Keys reached the page, not vimbrowser's own j/k/G bindings.
        self.assertEqual(self.frame_js(self.main, "scrollY"), scroll_before)

    def test_background_tab_is_not_activated(self) -> None:
        self.cli("open", f"{self.base}/blank.html")
        tabs = self.cli_json("tabs", "--json")
        front = next(t["id"] for t in tabs["tabs"] if t["url"].endswith("/blank.html"))
        self.cli("focus", str(front))
        time.sleep(0.5)
        try:
            self.assertEqual(self.cli_json("tabs", "--json")["active_tabid"], front)
            self.scroll_into_view(self.child, "canvas")
            self.assert_click_at(self.child, 10, 10)
            self.assertEqual(self.cli_json("tabs", "--json")["active_tabid"], front)
        finally:
            self.cli("focus", str(self.tabid))
            self.cli("close-tab", str(front))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    args, unittest_args = parser.parse_known_args()
    global BINARY, CLI
    BINARY = args.binary.resolve(strict=True)
    CLI = args.cli.resolve(strict=True)
    unittest.main(argv=["pointer_input_e2e.py", *unittest_args], verbosity=2)


if __name__ == "__main__":
    main()
