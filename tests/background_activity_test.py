#!/usr/bin/env python3
"""Real CEF two-person browsing regression. GUI runs ONLY in its own xenv.

Usage: python3 tests/background_activity_test.py [--binary build-source/Release/vimbrowser]
Requires xenv, vimbrowser-cli and Pillow. No live profile/socket is used.
"""
import argparse
import http.server
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import threading
import time
import urllib.parse

from PIL import Image, ImageChops

METRICS = {}


class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        METRICS[data['name']] = data
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        name = urllib.parse.urlparse(self.path).path.strip('/') or 'user'
        port = self.server.server_port
        extra = ''
        if name == 'user':
            extra = '<input id="owner" aria-label="Owner typing" autofocus>'
        elif name == 'work':
            extra = f'<iframe src="http://localhost:{port}/child" width="600" height="350"></iframe>'
        elif name == 'child':
            extra = '''<input id="value" aria-label="Value"><button id="save">Save value</button>
<button id="auth">Authenticate fixture</button><output id="saved"></output>
<script>
value.addEventListener('input',()=>requestAnimationFrame(()=>window.persisted=value.value));
save.onclick=e=>requestAnimationFrame(()=>{saved.textContent=window.persisted;window.trusted=e.isTrusted});
auth.onclick=e=>{window.authTrusted=e.isTrusted;window.open('/popup','fixture-auth')};
window.addEventListener('message',e=>{if(e.origin===location.origin&&e.data==='approved')requestAnimationFrame(()=>window.approved=true)});
</script>'''
        elif name == 'popup':
            extra = "<script>requestAnimationFrame(()=>{document.title='Approved';opener.postMessage('approved',location.origin)})</script>"
        body = f'''<!doctype html><style>body{{background:#f0e8d0;color:#102040;font:24px sans-serif}}input,button{{font:20px sans-serif}}iframe{{display:block}}</style>
<h1>{name.upper()} FIXTURE</h1>{extra}<div id="ready"></div>
<script>
window.n=0;window.ticks=0;function tick(){{n++;ready.textContent='rAF ready';requestAnimationFrame(tick)}}requestAnimationFrame(tick);
setInterval(()=>{{ticks++;fetch('/metrics',{{method:'POST',body:JSON.stringify({{name:{json.dumps(name)},n,ticks,visibility:document.visibilityState,w:innerWidth,h:innerHeight}})}})}},200);
</script>'''.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def run(*args, **kwargs):
    return subprocess.check_output([str(x) for x in args], text=True, **kwargs).strip()


def eventually(check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(.1)
    raise AssertionError('timed out waiting for fixture condition')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build-source/Release/vimbrowser')
    parser.add_argument('--artifacts', default='/tmp/vimbrowser-activity-test')
    args = parser.parse_args()
    binary = Path(args.binary).resolve()
    artifacts = Path(args.artifacts).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    name = f'vb-activity-test-{os.getpid()}'
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}'
    with tempfile.TemporaryDirectory(prefix='vb-activity-profile-') as profile:
        env = dict(os.environ, VIMBROWSER_PROFILE_DIR=profile,
                   VIMBROWSER_IPC=profile + '/ipc.sock')

        def cli(command, *parts, payload=None):
            return run('vimbrowser-cli', command, *parts, env=env, input=payload)

        def js(tab, code, frame=None):
            response = json.loads(cli('frame-js' if frame else 'js', tab,
                                      *([frame] if frame else []), payload=code))
            assert response['ok'], response
            return response['result']

        def tabs():
            return json.loads(cli('tabs', '--json'))

        def owner_unchanged():
            state = tabs()
            assert state['active_tabid'] == owner, state
            assert state['visible_tabid'] == owner, state
            assert js(owner, 'document.activeElement.id') == 'owner'

        try:
            run('xenv', 'start', name)
            cmd = (f'cd {shlex.quote(str(binary.parent))}; exec {shlex.quote(str(binary))} '
                   f'--profile-dir={shlex.quote(profile)} --no-sandbox {url}/user '
                   f'> {shlex.quote(str(artifacts / "browser.log"))} 2>&1')
            run('xenv', 'launch', '-e', name, '--workspace-index', '0', '--', 'bash', '-c', cmd)
            eventually(lambda: Path(profile, 'ipc.sock').exists())
            owner = tabs()['active_tabid']
            eventually(lambda: METRICS.get('user', {}).get('n', 0) > 2)
            windows = json.loads(run('xenv', 'window', '-e', name, 'list', '--json'))['windows']
            assert len(windows) == 1, windows
            run('xenv', 'focus', '-e', name, int(windows[0]['win'], 16))
            js(owner, 'document.getElementById("owner").focus()')
            time.sleep(.3)
            run('xenv', 'key', '-e', name, 'i')  # owner enters native insert mode
            run('xenv', 'type', '-e', name, 'OWNER-')
            typed = js(owner, 'document.getElementById("owner").value')
            assert typed == 'OWNER-', (typed, cli('status'), js(owner, 'document.activeElement.outerHTML'))
            work = json.loads(cli('open', url + '/work'))['tabs'][-1]['id']
            idle = json.loads(cli('open', url + '/idle'))['tabs'][-1]['id']
            cli('activity', idle, 0)
            eventually(lambda: METRICS.get('child', {}).get('n', 0) > 4)
            owner_unchanged()
            assert METRICS['work']['visibility'] == METRICS['child']['visibility'] == 'visible'
            assert METRICS['child']['w'] == 600
            frames = json.loads(cli('frame-tree', work))
            (artifacts / 'frame-tree.json').write_text(json.dumps(frames, indent=2))
            # Match the exact child URL, never an arbitrary frame/control.
            def flatten(value):
                if isinstance(value, dict):
                    yield value
                    for child in value.values():
                        yield from flatten(child)
                elif isinstance(value, list):
                    for child in value:
                        yield from flatten(child)
            children = [f for f in flatten(frames) if f.get('url') == f'http://localhost:{server.server_port}/child']
            assert len(children) == 1, frames
            assert children[0]['out_of_process'], frames
            frame = children[0].get('frame_id', children[0].get('id'))
            js(work, 'value.value="persisted background";value.dispatchEvent(new Event("input",{bubbles:true}));"set"', frame)
            eventually(lambda: js(work, 'window.persisted||""', frame) == 'persisted background')

            def activate(exact_name):
                controls = json.loads(cli('inspect-controls', work, '--frame', frame,
                                         '--name-exact', exact_name, '--require-one'))
                (artifacts / ('controls-' + exact_name.split()[0] + '.json')).write_text(json.dumps(controls, indent=2))
                handles = [x['handle'] for x in flatten(controls) if 'handle' in x]
                assert len(handles) == 1, controls
                return json.loads(cli('activate-control', work, handles[0]))

            activate('Save value')
            eventually(lambda: js(work, 'saved.textContent', frame) == 'persisted background')
            assert js(work, 'window.trusted', frame) is True
            cli('screenshot', owner, '-o', artifacts / 'owner-before.png')
            for i in range(3):
                cli('screenshot', work, '-o', artifacts / f'background-{i}.png')
                owner_unchanged()
            cli('screenshot', owner, '-o', artifacts / 'owner-after.png')
            before = Image.open(artifacts / 'owner-before.png').convert('RGB')
            after = Image.open(artifacts / 'owner-after.png').convert('RGB')
            # Ignore only the owner input's blinking caret (top 150px).
            crop = (0, 150, before.width, before.height)
            assert not ImageChops.difference(before.crop(crop), after.crop(crop)).getbbox()
            background = Image.open(artifacts / 'background-0.png').convert('RGB')
            assert ImageChops.difference(before, background).getbbox()
            run('xenv', 'screenshot', '-e', name, '-o', artifacts / 'nested-during-lease.png')
            # Background attempts at real window focus must remain rejected.
            js(work, 'window.focus();"requested"')
            activate('Authenticate fixture')
            eventually(lambda: js(work, 'window.approved===true', frame) is True)
            assert js(work, 'window.authTrusted', frame) is True
            popup = [t for t in tabs()['tabs'] if '/popup' in t['url']]
            assert len(popup) == 1 and not popup[0]['active']
            assert popup[0]['activity_remaining_ms'] > 0
            owner_unchanged()
            # Native typing overlaps repeated background compositor captures.
            failures = []
            def concurrent_captures():
                try:
                    for i in range(3):
                        cli('screenshot', work, '-o', artifacts / f'concurrent-{i}.png')
                except Exception as exc:
                    failures.append(exc)
            capture_thread = threading.Thread(target=concurrent_captures)
            capture_thread.start()
            run('xenv', 'type', '-e', name, 'STILL-USABLE')
            capture_thread.join(timeout=15)
            assert not capture_thread.is_alive() and not failures, failures
            assert js(owner, 'document.getElementById("owner").value') == 'OWNER-STILL-USABLE'
            # Passive HTTP telemetry, NOT JS IPC, observes expiry without renewing it.
            cli('activity', work, 1500)
            cli('activity', popup[0]['id'], 0)
            eventually(lambda: METRICS.get('work', {}).get('visibility') == 'hidden', 6)
            eventually(lambda: METRICS.get('child', {}).get('visibility') == 'hidden', 6)
            idle_before = dict(METRICS['idle'])
            stopped = dict(METRICS['work'])
            child_stopped = dict(METRICS['child'])
            time.sleep(2)
            for _ in range(5):
                state = tabs()  # Passive metadata must not wake tabs.
            assert METRICS['work']['n'] == stopped['n']
            assert METRICS['child']['n'] == child_stopped['n']
            assert METRICS['idle']['n'] == idle_before['n']
            assert METRICS['work']['ticks'] > stopped['ticks']
            assert next(t for t in state['tabs'] if t['id'] == work)['activity_remaining_ms'] == 0
            # Renewing revives rAF; reordering and closing do not invalidate expiry IDs.
            cli('activity', work, 1200)
            eventually(lambda: METRICS['work']['n'] > stopped['n'])
            cli('activity', work, 2500)
            cli('tab-order', work, 1)
            time.sleep(1.4)
            assert METRICS['work']['visibility'] == 'visible'
            cli('close-tab', work)
            time.sleep(1.5)
            owner_unchanged()
            assert all(t['id'] != work for t in tabs()['tabs'])
            (artifacts / 'metrics.json').write_text(json.dumps(METRICS, indent=2))
            run('xenv', 'screenshot', '-e', name, '-o', artifacts / 'nested-display.png')
            print('PASS: background rAF/viewport, OOPIF forms/trusted popup, screenshots, owner native typing/focus, expiry/renewal/idle/reorder/close')
            print(f'Artifacts: {artifacts}')
        finally:
            run('xenv', 'stop', name)
            server.shutdown()


if __name__ == '__main__':
    main()
