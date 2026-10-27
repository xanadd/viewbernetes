#!/usr/bin/env python3
"""Release installer tests use local artifacts, never network or package managers."""
import hashlib
import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    base=Path(tmp)
    tools=base/'tools'; tools.mkdir()
    fixtures=base/'fixtures'; fixtures.mkdir()
    calls=base/'calls'
    prefix=base/'install with spaces'
    (tools/'curl').write_text('''#!/usr/bin/env python3
import os, shutil, sys
from pathlib import Path
args=sys.argv[1:]
url=next(a for a in args if a.startswith('https://'))
with open(os.environ['CALLS'],'a') as f: f.write(url+'\\n')
if os.environ.get('FAIL_DOWNLOAD'): sys.exit(22)
source=Path(os.environ['FIXTURES'])/url.rsplit('/',1)[-1]
shutil.copyfile(source,args[args.index('-o')+1])
''')
    (tools/'uname').write_text('#!/bin/sh\ncase "$1" in -s) echo "$TEST_OS";; -m) echo "$TEST_ARCH";; esac\n')
    for f in tools.iterdir(): f.chmod(0o755)
    # These must never be invoked by a binary installation.
    for name in ['cc','make','sudo','apt-get']:
        f=tools/name
        f.write_text('#!/bin/sh\necho "Unexpected dependency command" >&2\nexit 99\n'); f.chmod(0o755)
    env=dict(os.environ,PATH=str(tools)+os.pathsep+os.environ['PATH'],PREFIX=str(prefix),CALLS=str(calls),FIXTURES=str(fixtures),TEST_OS='Linux',TEST_ARCH='x86_64')
    for platform in ['linux','darwin']:
        for arch in ['amd64','arm64']:
            with tarfile.open(fixtures/f'viewbernetes-{platform}-{arch}.tar.gz','w:gz') as tar:
                tar.add(ROOT/'viewbernetes',arcname='viewbernetes')
    def checksums():
        (fixtures/'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\n' for f in sorted(fixtures.glob('*.tar.gz'))))
    checksums()
    def run(extra=None,code=0):
        # Match curl | sh: script on stdin, no controlling terminal.
        p=subprocess.run(['sh'],input=(ROOT/'install.sh').read_bytes(),env=dict(env,**(extra or {})),start_new_session=True,capture_output=True,timeout=15)
        assert p.returncode==code,(p.returncode,p.stdout,p.stderr)
        return p.stdout+p.stderr
    for osname,arch,target in [('Linux','x86_64','linux-amd64'),('Linux','aarch64','linux-arm64'),('Darwin','x86_64','darwin-amd64'),('Darwin','arm64','darwin-arm64')]:
        output=run({'TEST_OS':osname,'TEST_ARCH':arch})
        assert f'viewbernetes-{target}.tar.gz' in calls.read_text()
        assert b'Installed' in output
    binary=prefix/'bin/viewbernetes'
    assert subprocess.check_output([str(binary),'--version'],text=True).strip()=='1.0.0'
    original=binary.read_bytes()
    run({'VIEWBERNETES_VERSION':'v1.0.0'})
    assert '/releases/download/v1.0.0/' in calls.read_text()
    asset=fixtures/'viewbernetes-linux-amd64.tar.gz'
    good=asset.read_bytes()
    asset.write_bytes(b'corrupted archive')
    assert b'Checksum mismatch' in run(code=1)
    assert binary.read_bytes()==original
    asset.write_bytes(good)
    (fixtures/'SHA256SUMS').write_text('')
    assert b'checksum' in run(code=1)
    checksums()
    sums=(fixtures/'SHA256SUMS').read_text()
    (fixtures/'SHA256SUMS').write_text(sums+sums)
    assert b'checksum' in run(code=1)
    checksums()
    run({'FAIL_DOWNLOAD':'1'},code=22)
    assert binary.read_bytes()==original
    for extra in [{'TEST_OS':'FreeBSD'},{'TEST_ARCH':'riscv64'},{'VIEWBERNETES_VERSION':'../../secret'},{'PREFIX':'relative'}]:
        before=calls.read_text()
        run(extra,code=1)
        assert calls.read_text()==before
    with tarfile.open(asset,'w:gz') as tar:
        info=tarfile.TarInfo('viewbernetes'); info.type=tarfile.SYMTYPE; info.linkname='/bin/sh'; tar.addfile(info)
    checksums()
    assert b'Invalid release executable' in run(code=1)
    assert binary.read_bytes()==original
    assert not list((prefix/'bin').glob('.viewbernetes.*'))
    print('Release installer: all targets, piped stdin, version pinning, checksums, failures and atomic replacement passed')
