#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Validate native point/sun NEE, complete films and affected registered stages."""
import argparse
import contextlib
import json
import subprocess
from pathlib import Path
from build import REPO,WORK,PACKAGE
from device_gate import sha,save
from render_gate import inputs
import render_replay

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--skip-build',action='store_true')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    status={'complete':False,'phase':'prepare'};save(out/'status.json',status)
    source=inputs();runtime=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins={str(runtime):sha(runtime)};reports={}
    kinds=['lighting','shadow','closest','surface','background','microfacet_render']
    save(out/'invocation.json',{'productionSourceSha256':source,'fileSha256':pins})
    try:
        if not args.skip_build:
            status['phase']='build';save(out/'status.json',status)
            command=['python3',str(REPO/'native/tools/build_cycles.py'),'--jobs','4']
            for kind in kinds:command+=['--target','codebrix_cycles_'+kind+'_gate']
            with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
            render_replay.build('linux-x64')
        for kind in kinds:
            path=WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate');pins[str(path)]=sha(path)
        path=render_replay.BASE/'linux-x64/render_replay';pins[str(path)]=sha(path)
        save(out/'invocation.json',{'productionSourceSha256':source,'fileSha256':pins})
        for kind in kinds:
            status['phase']=kind;save(out/'status.json',status)
            directory=out/kind;directory.mkdir()
            prefix=directory/'run'
            with (directory/'stdout.txt').open('w') as stdout,(directory/'stderr.txt').open('w') as stderr:
                subprocess.run([str(WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')),str(prefix)],stdout=stdout,stderr=stderr,check=True,timeout=900)
            report=json.loads((directory/'stdout.txt').read_text());assert report['passed'] and report['cyclesAllocationBytesAfterCleanup']==0
            library=prefix/'library.txt' if kind=='lighting' else directory/'run.library.txt'
            assert Path(library.read_text().strip()).resolve()==runtime.resolve()
            if kind=='lighting':assert report['cases']==8 and report['nativeSceneAndScheduler'] and report['independentOriginalCpuImage'] and report['shadowPaths']>1000
            reports[kind]=report;save(out/'results.json',reports)
        status['phase']='lighting-replay';save(out/'status.json',status)
        for case in range(8):
            destination=out/('replay-'+str(case));capture=out/'lighting/run'/('case-'+str(case))
            with (out/('replay-'+str(case)+'.log')).open('w') as log,contextlib.redirect_stdout(log):render_replay.run('linux-x64',capture,destination)
            reports['replay-'+str(case)]=json.loads((destination/'validation.json').read_text())
            assert reports['replay-'+str(case)]['terminalShadowPathSlots']>0
            save(out/'results.json',reports)
        assert source==inputs() and all(sha(Path(name))==digest for name,digest in pins.items())
        status.update(complete=True,passed=True,phase='complete',inputsUnchanged=True,
                      scope='Native point/sun constant-emitter NEE, opaque shadows and complete independent CPU films. Full lighting MIS, other emitter types and transparent shadows remain required.')
    except Exception as error:
        status.update(complete=True,passed=False,error=repr(error));raise
    finally:save(out/'status.json',status)
    print(json.dumps(status),flush=True)

if __name__=='__main__':main()
