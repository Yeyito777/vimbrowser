#!/usr/bin/env python3
"""223-tab context/ID persistence across isolated xenv fixture process lifetimes."""
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
from background_activity_test import run, eventually

class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self,*_): pass
    def do_GET(self):
        self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers()
        self.wfile.write(b'<!doctype html><title>Session fixture</title><input id="owner" autofocus>')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',required=True);args=p.parse_args()
    binary=Path(args.binary).resolve();name=f'vb-session-test-{os.getpid()}'
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    url=f'http://127.0.0.1:{server.server_port}'
    with tempfile.TemporaryDirectory(prefix='vb-session-fixture-') as tmp:
        root=Path(tmp);profile=root/'profile';profile.mkdir()
        snapshot={'active_tabid':622,'tabs':[{'id':400+i,'url':url+f'/{i}',
            'context':'fixture-mail' if i>=200 else None, 'folder_id':7 if i%2 else 0,
            'sidebar_sort_order':1000+i*1024,'pinned':i%3==0} for i in range(223)]}
        (root/'tabs.json').write_text(json.dumps(snapshot))
        (root/'source').write_text('folder=7\t0\t1024\tFixture\nfolder_pinned=7\nnext_tab_id=700\n')
        run('vimbrowser-cli','recover-session','--snapshot',root/'tabs.json','--state-source',root/'source','--output',profile/'state')
        env=dict(os.environ,VIMBROWSER_IPC=str(profile/'ipc.sock'),VIMBROWSER_PROFILE_DIR=str(profile))
        def cli(command,*parts,payload=None): return run('vimbrowser-cli',command,*parts,env=env,input=payload)
        def data(command,*parts,**kwargs):return json.loads(cli(command,*parts,**kwargs))
        def js(tab,code):
            result=data('js',tab,payload=code);assert result['ok'],result;return result['result']
        def launch():
            run('xenv','start',name)
            cmd=f'cd {shlex.quote(str(binary.parent))};exec {shlex.quote(str(binary))} --profile-dir={shlex.quote(str(profile))} --no-sandbox > {shlex.quote(str(root/"browser.log"))} 2>&1'
            run('xenv','launch','-e',name,'--workspace-index','0','--','bash','-c',cmd)
            eventually(lambda: (profile/'ipc.sock.health').exists())
        def assert_tabs(expected):
            state=data('tabs','--json');assert state['active_tabid']==622,state['active_tabid']
            assert len(state['tabs'])==len(expected),(len(state['tabs']),len(expected))
            for want,got in zip(expected,state['tabs']):
                for key in ('id','url','context','folder_id','sidebar_sort_order','pinned'):
                    assert got[key]==want[key],(key,want['id'],got[key],want[key])
        try:
            launch();assert_tabs(snapshot['tabs'])
            eventually(lambda: js(622,'document.readyState')=='complete')
            js(622,'localStorage.setItem("session-fixture","named");"ok"')
            assert js(400,'localStorage.getItem("session-fixture")||"absent"')=='absent'
            extra=data('open-context','--brief','fixture-second',url+'/extra')['tab']
            assert extra['id']==700,extra['id']
            expected=snapshot['tabs']+[extra]
            assert_tabs(expected)
            state=(profile/'state').read_text();assert state.count('context_tab=')==24,state.count('context_tab=')
            for restart in range(2):
                windows=json.loads(run('xenv','window','-e',name,'list','--json'))['windows']
                assert len(windows)==1,windows
                # Graceful WM_DELETE inside the isolated display flushes CEF storage.
                script='from Xlib import X,display,protocol;import sys;d=display.Display();w=d.create_resource_object("window",int(sys.argv[1],16));e=protocol.event.ClientMessage(window=w,client_type=d.intern_atom("WM_PROTOCOLS"),data=(32,[d.intern_atom("WM_DELETE_WINDOW"),X.CurrentTime,0,0,0]));w.send_event(e,event_mask=0);d.sync()'
                run('xenv','exec','-e',name,'python3','-c',script,windows[0]['win'])
                eventually(lambda: not (profile/'ipc.sock.health').exists())
                run('xenv','stop',name)
                # All assertions concern this fixture profile; no owner socket/display.
                launch();assert_tabs(expected)
                eventually(lambda: js(622,'document.readyState')=='complete')
                assert js(622,'localStorage.getItem("session-fixture")')=='named'
                assert js(400,'localStorage.getItem("session-fixture")||"absent"')=='absent'
            print('PASS: 223 mixed-context tabs restored; 224 persisted through TWO graceful restarts with exact IDs/order/folders/pins/active/allocator and context storage isolation')
        finally:
            subprocess.run(['xenv','stop',name],check=False);server.shutdown()

if __name__=='__main__':main()
