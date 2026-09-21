#!/usr/bin/env python3
"""Isolated xenv-only controls/diagnostic/cancellation regressions; localhost only."""
import argparse
import http.server
import json
import os
from pathlib import Path
import shlex
import signal
import socket
import subprocess
import tempfile
import threading
import time
import urllib.parse

from background_activity_test import run, eventually

SUBMISSIONS = []


class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path == '/owner':
            body = '<input id="owner" aria-label="Owner" autofocus>'
        elif path == '/work':
            body = f'<iframe src="http://localhost:{self.server.server_port}/form" width="700" height="500"></iframe>'
        elif path == '/submitted':
            SUBMISSIONS.append(urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query))
            body = '<p>Local fixture accepted</p>'
        elif path == '/popup':
            body = '<script>opener.postMessage("fixture-approved",location.origin)</script>Fixture auth only'
        else:
            body = '''<form action="/submitted" target="receipt">
<input name="submit" value="shadowed" aria-label="Shadows submit method">
<div id="recipient" role="combobox" contenteditable="true" aria-label="To">fixture recipient</div>
<button id="submitter" type="submit" name="operation" value="fixture-deliver">Submit fixture</button>
<button disabled id="disabled">Disabled fixture</button>
<button type="button" id="auth">Authenticate fixture</button>
<button type="button">Duplicate fixture</button><button type="button">Duplicate fixture</button>
<input type="hidden" aria-label="Invisible secret" value="NEVER_INSPECT">
</form><iframe name="receipt" hidden></iframe>
<script>
window.events=[];
document.querySelector('form').addEventListener('submit', e=>events.push({trusted:e.isTrusted,id:e.submitter.id}));
auth.onclick=e=>{window.authTrusted=e.isTrusted;window.open('/popup','fixture-auth')};
window.addEventListener('message',e=>{if(e.origin===location.origin&&e.data==='fixture-approved')window.approved=true});
</script>'''
        body = ('<!doctype html><style>body{font:18px sans-serif}button,input,[contenteditable]{font:18px sans-serif;margin:4px} [contenteditable]{border:1px solid;min-height:30px}</style>' + body).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def flatten(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from flatten(child)
    elif isinstance(value, list):
        for child in value:
            yield from flatten(child)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build-diagnostics/Release/vimbrowser')
    p.add_argument('--artifacts', default='/tmp/vb-diagnostic-controls-test')
    args = p.parse_args()
    binary = Path(args.binary).resolve()
    artifacts = Path(args.artifacts)
    artifacts.mkdir(parents=True, exist_ok=True)
    name = f'vb-diagnostic-test-{os.getpid()}'
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}'
    with tempfile.TemporaryDirectory(prefix='vb-diag-profile-') as profile:
        env = dict(os.environ, VIMBROWSER_IPC=profile+'/ipc.sock', VIMBROWSER_PROFILE_DIR=profile)
        def cli(command, *parts, payload=None, check=True):
            result = subprocess.run(['vimbrowser-cli', command, *map(str, parts)], env=env,
                                    input=payload, text=True, capture_output=True, timeout=12)
            if check:
                assert result.returncode == 0, (command, result.stdout, result.stderr)
            return result
        def data(command, *parts, **kwargs):
            return json.loads(cli(command, *parts, **kwargs).stdout)
        def js(tab, code, frame=None):
            result = data('frame-js' if frame else 'js', tab, *([frame] if frame else []), payload=code)
            assert result['ok'], result
            return result['result']
        def health():
            return data('raw', '--socket', profile+'/ipc.sock.health', payload='health')
        def owner_unchanged():
            state = data('tabs', '--json')
            assert state['active_tabid'] == state['visible_tabid'] == owner, state
            assert js(owner, 'document.activeElement.id') == 'owner'
        stopped_pid = None
        try:
            run('xenv', 'start', name)
            cmd = f'cd {shlex.quote(str(binary.parent))}; exec env VIMBROWSER_CONTROL_TRACE=1 {shlex.quote(str(binary))} --no-sandbox --profile-dir={shlex.quote(profile)} {url}/owner > {shlex.quote(str(artifacts / "browser.log"))} 2>&1'
            run('xenv', 'launch', '-e', name, '--workspace-index', '0', '--', 'bash', '-c', cmd)
            eventually(lambda: Path(profile, 'ipc.sock.health').exists())
            owner = data('tabs', '--json')['active_tabid']
            eventually(lambda: js(owner, 'document.readyState') == 'complete')
            js(owner, 'document.getElementById("owner").focus()')
            brief = data('open-context', '--brief', 'fixture', url+'/work')
            assert set(brief) == {'ok', 'tab'}, brief
            work = brief['tab']['id']
            eventually(lambda: len([f for f in flatten(data('frame-tree', work)) if f.get('url') == f'http://localhost:{server.server_port}/form']) == 1)
            frame_record, = [f for f in flatten(data('frame-tree', work)) if f.get('url') == f'http://localhost:{server.server_port}/form']
            frame = frame_record.get('frame_id', frame_record.get('id'))
            assert frame_record['out_of_process']
            eventually(lambda: js(work, 'document.readyState', frame) == 'complete')
            def inspect(name, one=True):
                return data('inspect-controls', work, '--frame', frame, '--name-exact', name,
                            *(['--require-one'] if one else []))
            def control(name):
                result = inspect(name)
                controls = result['inspection']['controls']
                assert len(controls) == 1, result
                return controls[0]
            def activate(name):
                return data('activate-control', work, control(name)['handle'])
            recipient = control('To')
            assert recipient['role'] == 'combobox', recipient
            assert data('activate-control', work, recipient['handle'])['ok']
            assert js(work, 'document.activeElement.id', frame) == 'recipient'
            disabled = control('Disabled fixture')
            assert disabled['disabled'], disabled
            rejected = data('activate-control', work, disabled['handle'], check=False)
            assert not rejected['ok'], rejected
            # Preflight retries may wait for a surface, never bypass an overlay.
            covered = control('To')['handle']
            js(work, 'const cover=document.createElement("div");cover.id="cover";cover.style="position:fixed;inset:0;background:white;z-index:99999";document.body.append(cover);"covered"', frame)
            rejected = data('activate-control', work, covered, check=False)
            assert rejected['error']['code'] == 'target_obscured', rejected
            js(work, 'document.getElementById("cover").remove();"uncovered"', frame)
            duplicate = inspect('Duplicate fixture', one=False)
            assert duplicate['inspection']['match_count'] == 2, duplicate
            assert inspect('Invisible secret', one=False)['inspection']['match_count'] == 0
            assert activate('Submit fixture')['ok']
            eventually(lambda: bool(SUBMISSIONS))
            assert SUBMISSIONS == [{'submit': ['shadowed'], 'operation': ['fixture-deliver']}], SUBMISSIONS
            assert json.loads(js(work, 'JSON.stringify(window.events)', frame)) == [{'trusted': True, 'id': 'submitter'}]
            assert activate('Authenticate fixture')['ok']
            eventually(lambda: js(work, 'window.approved===true&&window.authTrusted===true', frame))
            owner_unchanged()
            # Metadata diagnostics never renew an idle tab's compositor lease.
            cli('activity', work, 0)
            diagnosis = data('diagnose', work, '--expected-origin', url)
            assert diagnosis['target']['activity_remaining_ms'] == 0, diagnosis
            assert diagnosis['build']['control_backend_build'].startswith('controls-v2'), diagnosis
            assert diagnosis['renderer_readiness'] == 'unknown_not_probed'
            assert diagnosis['auth']['state'] == 'unknown'
            (artifacts/'diagnosis.json').write_text(json.dumps(diagnosis, indent=2))
            # Stall only this fixture renderer. Backend deadline must not claim
            # the implementation is missing. No endpoint/account accessed.
            js(work, 'setTimeout(()=>{const end=performance.now()+3600;while(performance.now()<end){}},20);"armed"', frame)
            time.sleep(.1)
            deadline_result = data('inspect-controls', work, '--frame', frame, '--name-exact', 'To', check=False)
            assert deadline_result['error']['code'] == 'inspection_timeout', deadline_result
            eventually(lambda: js(work, 'document.readyState', frame) == 'complete')
            # Client timeout does not cancel an already-dispatched operation.
            timed = cli('frame-js', work, frame, '--timeout', '.12',
                        payload='const end=performance.now()+1600;while(performance.now()<end){};window.lateEffect=true;"completed"', check=False)
            assert timed.returncode != 0 and 'not rollback' in timed.stderr, timed.stderr
            observed = health()
            assert observed['operation']['state'] == 'in_flight', observed
            assert observed['operation']['client_disconnected'], observed
            diagnosis = data('diagnose', work, '--timeout', '.12')
            assert diagnosis['daemon_socket']['responsive'] and not diagnosis['ui_queue']['responsive'], diagnosis
            (artifacts/'in-flight.json').write_text(json.dumps(diagnosis, indent=2))
            eventually(lambda: health()['operation']['state'] in {'completed', 'cancelled_before_dispatch'})
            assert js(work, 'window.lateEffect===true', frame)
            owner_unchanged()
            # EOF half-close remains valid protocol, unlike full disconnect.
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(2)
                sock.connect(profile+'/ipc.sock')
                sock.sendall(b'status')
                sock.shutdown(socket.SHUT_WR)
                assert json.loads(sock.recv(262144))['active_tabid'] == owner
            # A mutation that times out before dispatch is skipped, not retried.
            before = [t['id'] for t in data('tabs', '--json')['tabs']]
            stopped_pid = health()['pid']
            os.kill(stopped_pid, signal.SIGSTOP)  # isolated fixture process only
            timed = cli('open-context', '--brief', '--timeout', '.1', 'cancelled-fixture', url+'/work', check=False)
            assert timed.returncode != 0
            os.kill(stopped_pid, signal.SIGCONT)
            stopped_pid = None
            eventually(lambda: health()['operation']['state'] == 'cancelled_before_dispatch')
            after = [t['id'] for t in data('tabs', '--json')['tabs']]
            assert before == after, (before, after)
            owner_unchanged()
            print('PASS: concise context, hidden OOPIF recipient/disabled/duplicate controls, trusted named submit/native form data, auth popup, metadata-only diagnosis, renderer deadline, in-flight timeout ambiguity, EOF, queued cancellation, owner focus/selection')
        finally:
            if stopped_pid:
                os.kill(stopped_pid, signal.SIGCONT)
            subprocess.run(['xenv', 'stop', name], check=False)
            server.shutdown()

if __name__ == '__main__':
    main()
