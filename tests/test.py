#!/usr/bin/env python3
"""Offline tests: never read kubeconfig or contact a real cluster."""
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import shutil
import signal
import struct
import subprocess
import tarfile
import tempfile
import termios
import time

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'viewbernetes'

def run(args, env, code=0):
    p = subprocess.run([str(BIN), *args], env=env, text=True, capture_output=True, timeout=15)
    assert p.returncode == code, (args, p.returncode, p.stdout, p.stderr)
    return p.stdout + p.stderr

with tempfile.TemporaryDirectory() as tmp:
    base = Path(tmp)
    fake = base / 'kubectl'
    fake.write_text('''#!/usr/bin/env python3
import json, os, sys, time
from pathlib import Path
args=sys.argv[1:]
with open(os.environ['CALLS'],'a') as f: f.write(json.dumps(args)+'\\n')
if 'current-context' in args: print('test-context'); sys.exit(0)
assert args[:3] == ['--request-timeout=8s','--context','test-context']
mode=os.environ.get('MODE','')
if mode=='fail': print('SECRET_TOKEN_DO_NOT_DISPLAY',file=sys.stderr); sys.exit(1)
if mode=='slow': time.sleep(30)
if '--raw' in args:
    key='pods' if args[-1].endswith('/pods') else 'metrics'
    if mode=='podfail' and key=='pods': sys.exit(1)
else: key='nodes'
if mode=='bad': print('{broken'); sys.exit(0)
print(Path(os.environ['FIXTURES'],key+'.json').read_text())
''')
    fake.chmod(0o755)
    env = dict(os.environ, PATH=str(base)+os.pathsep+os.environ['PATH'], FIXTURES=tmp, CALLS=str(base/'calls'))
    nodes = {'items': [{'metadata': {'name': f'worker-{i}'}, 'status': {'capacity': {'cpu': '4', 'memory': '8Gi'}, 'conditions': [{'type': 'Ready', 'status': 'True'}]}} for i in range(3)]}
    metrics = {'items': [{'metadata': {'name': f'worker-{i}'}, 'timestamp': '2026-10-26T12:00:00Z', 'usage': {'cpu': f'{(i+1)*500000000}n', 'memory': '2Gi'}} for i in range(2)]}
    pods = {'items': [{'metadata': {'namespace': 'default', 'name': name}, 'containers': [{'usage': {'cpu': cpu, 'memory': mem}}, {'usage': {'cpu': '250m', 'memory': '128Mi'}}]} for name,cpu,mem in [('small','1m','1Mi'),('large','1000m','1Gi')]]}
    for name,data in [('nodes',nodes),('metrics',metrics),('pods',pods)]:
        (base/(name+'.json')).write_text(json.dumps(data))
    output=run(['--once'],env)
    assert '2/3' in output and '18.8%' in output and '25.0%' in output, output
    assert 'worker-2                 metrics unavailable' in output
    assert output.index('large') < output.index('small') and '1250' in output and '1152.0' in output
    run(['--once','-n','default'],env)
    calls=[json.loads(x) for x in (base/'calls').read_text().splitlines()]
    assert calls[-1][-1]=='/apis/metrics.k8s.io/v1beta1/namespaces/default/pods'
    assert all('config' in c or c[1:3]==['--context','test-context'] for c in calls)
    for mode in ['fail','bad','podfail']:
        error=run(['--once'],dict(env,MODE=mode),1)
        assert 'ERROR:' in error and 'SECRET_TOKEN' not in error
    for args in [['-i','0'],['-i','1x'],['-n','../../secret'],['-n','Bad'],['--wat']]: run(args,env,2)
    run([],env,2)
    before=(base/'calls').read_text()
    assert '[DEMO]' in run(['--demo','--once'],env)
    assert (base/'calls').read_text()==before

    def terminal(args, rows, cols, keys=b'q', slow=False):
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',rows,cols,0,0))
        p=subprocess.Popen([str(BIN),*args],stdin=slave,stdout=slave,stderr=slave,env=dict(env,TERM='xterm-256color',**({'MODE':'slow'} if slow else {})))
        os.close(slave)
        output=b''
        def drain(seconds):
            nonlocal output
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                if select.select([master],[],[],.05)[0]:
                    try: output+=os.read(master,65536)
                    except OSError as e:
                        if e.errno==errno.EIO: break
                        raise
        drain(.6)
        if keys: os.write(master,keys)
        else: p.send_signal(signal.SIGTERM)
        drain(.6)
        p.wait(timeout=3)
        assert p.returncode==0
        os.close(master)
        return output
    screen=terminal(['--demo'],32,120,b'mjnprcq')
    assert b'CLUSTER CPU' in screen and b'POD' in screen and b'MEMORY' in screen
    small=terminal(['--demo'],12,45)
    assert b'70 columns x 18 rows' in small
    terminal(['--demo'],24,80,b'')
    start=time.monotonic()
    terminal([],24,80,slow=True)
    assert time.monotonic()-start<4, 'quit must cancel pending kubectl'
    print('CLI, metrics, scope, errors, demo, PTY layout and responsive quit checks passed')

    # Exercise the installer offline, substituting only the archive download.
    archive=base/'source.tar.gz'
    with tarfile.open(archive,'w:gz') as tar:
        for file in ['Makefile','src/viewbernetes.c']:
            tar.add(ROOT/file,arcname='viewbernetes-test/'+file)
    curl=base/'curl'
    curl.write_text('#!/bin/sh\nwhile [ "$1" != "-o" ]; do shift; done\ncp "$ARCHIVE" "$2"\n')
    curl.chmod(0o755)
    prefix=base/'install path'
    subprocess.run(['sh',str(ROOT/'install-source.sh')],env=dict(env,PREFIX=str(prefix),ARCHIVE=str(archive)),check=True,capture_output=True,timeout=30)
    assert subprocess.check_output([str(prefix/'bin/viewbernetes'),'--version'],text=True).strip()=='1.0.0'
    assert not list((prefix/'bin').glob('.viewbernetes.*'))
    print('Installer build, path with spaces, cleanup and installed binary checks passed')

    # Simulate missing headers and package installation: no real system changes.
    compiler=shutil.which('cc')
    marker=base/'dependencies-ready'
    package_log=base/'package-calls'
    (base/'cc').write_text('#!/bin/sh\n[ -f "$READY" ] || exit 1\nexec "$REAL_CC" "$@"\n')
    (base/'apt-get').write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$PACKAGE_LOG"\nif [ "$1" = install ]; then touch "$READY"; fi\n')
    (base/'id').write_text('#!/bin/sh\necho 1000\n')
    (base/'sudo').write_text('#!/bin/sh\nexec "$@"\n')
    for name in ['cc','apt-get','id','sudo']: (base/name).chmod(0o755)
    prompt_env=dict(env,PREFIX=str(prefix),ARCHIVE=str(archive),READY=str(marker),REAL_CC=compiler,PACKAGE_LOG=str(package_log))
    no_tty=subprocess.run(['sh',str(ROOT/'install-source.sh')],env=prompt_env,start_new_session=True,text=True,capture_output=True,timeout=10)
    assert no_tty.returncode==1 and 'No interactive terminal' in no_tty.stderr
    assert not package_log.exists()

    def install_prompt(answer):
        master,slave=pty.openpty()
        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(slave,termios.TIOCSCTTY,0)
        p=subprocess.Popen(['sh'],stdin=subprocess.PIPE,stdout=slave,stderr=slave,env=prompt_env,preexec_fn=controlling_terminal,pass_fds=(slave,))
        os.close(slave)
        p.stdin.write((ROOT/'install-source.sh').read_bytes())
        p.stdin.close()
        output=b''
        sent=False
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            if select.select([master],[],[],.1)[0]:
                try: output+=os.read(master,65536)
                except OSError as e:
                    if e.errno==errno.EIO: break
                    raise
            if b'[y/N]' in output and not sent:
                os.write(master,answer+b'\n'); sent=True
            if p.poll() is not None: break
        p.wait(timeout=2); os.close(master)
        assert sent, output
        return p.returncode,output

    for answer in [b'n',b'']:
        code,output=install_prompt(answer)
        assert code==1 and b'declined' in output, output
        assert not package_log.exists() and b'Downloading' not in output
    code,output=install_prompt(b'yes')
    assert code==0 and b'Installed' in output, output
    assert package_log.read_text().splitlines()==['update','install -y build-essential libncurses-dev curl tar']
    assert subprocess.check_output([str(prefix/'bin/viewbernetes'),'--version'],text=True).strip()=='1.0.0'
    # Existing dependencies must never prompt or invoke the package manager.
    package_log.unlink()
    subprocess.run(['sh',str(ROOT/'install-source.sh')],env=prompt_env,start_new_session=True,check=True,capture_output=True,timeout=30)
    assert not package_log.exists()
    print('Piped installer yes/no/default-no, no-terminal and dependency-present checks passed')

subprocess.run(['cc','-std=c11','-Wno-unused-function',str(ROOT/'tests/unit.c'),'-o',str(ROOT/'tests/unit'),'-lncurses','-lpthread','-lm'],check=True)
subprocess.run([str(ROOT/'tests/unit')],check=True)
