#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Prove a fresh publication-candidate build in private network/mount namespaces.

No Git mutation is performed. The copy includes exactly the current tracked
and non-ignored untracked files, without existing work directories or .git.
Only child-namespace mounts are made; the user's mounts are never changed.
"""
import argparse
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--destination',type=Path)
    parser.add_argument('--inside',action='store_true')
    parser.add_argument('--hide',type=Path,action='append',default=[])
    parser.add_argument('--jobs',type=int,default=6)
    args=parser.parse_args()
    if args.inside:
        subprocess.run(['mount','--make-rprivate','/'],check=True)
        hidden=[]
        for path in args.hide:
            if path.exists():
                if ROOT.is_relative_to(path):raise RuntimeError('Cannot hide the fresh checkout')
                subprocess.run(['mount','-t','tmpfs','-o','size=4096,mode=000,ro','tmpfs',str(path)],check=True)
                hidden.append(str(path))
        env=dict(os.environ)
        for name in ['CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','LD_LIBRARY_PATH','CMAKE_PREFIX_PATH','PKG_CONFIG_PATH','PYTHONPATH']:
            env.pop(name,None)
        env['CCACHE_DISABLE']='1'
        report={'networkNamespace':os.readlink('/proc/self/ns/net'),
                'mountNamespace':os.readlink('/proc/self/ns/mnt'),'hiddenSources':hidden,
                'networkInterfaces':sorted(name for _,name in socket.if_nameindex()),
                'command':[sys.executable,'tools/build.py','--jobs',str(args.jobs)]}
        if report['networkInterfaces']!=['lo']:raise RuntimeError('Expected isolated network namespace')
        (ROOT/'offline-environment.json').write_text(json.dumps(report,indent=2)+'\n')
        subprocess.run(report['command'],cwd=ROOT,env=env,check=True)
        before=json.loads((ROOT/'publication-inputs.json').read_text())
        changed=[name for name,digest in before.items() if not (ROOT/name).is_file() or sha(ROOT/name)!=digest]
        report.update(passed=not changed,changedSources=changed)
        (ROOT/'offline-build-result.json').write_text(json.dumps(report,indent=2)+'\n')
        if changed:raise RuntimeError('Build mutated publication sources: '+str(changed))
        return
    destination=args.destination.resolve()
    if destination.exists():raise RuntimeError('Fresh proof destination already exists')
    if destination.is_relative_to(ROOT):raise RuntimeError('Place the proof outside the source checkout')
    names=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=ROOT,text=True)
    hashes={}
    for name in sorted(set(names.rstrip('\0').split('\0'))):
        source=ROOT/name
        if not source.exists():continue
        if source.is_symlink():raise RuntimeError('Review source symlink before copying: '+name)
        target=destination/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target);hashes[name]=sha(target)
    (destination/'publication-inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
    command=['unshare','-Urnm','--',sys.executable,str(destination/'tools/licensing/offline_build.py'),
             '--inside','--jobs',str(args.jobs),'--hide',str(ROOT)]
    for path in args.hide:command+=['--hide',str(path.resolve())]
    subprocess.run(command,check=True,cwd=destination)


if __name__=='__main__':main()
