#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native GGX refraction/glass closure, path-state and independent image gates."""
import argparse
import json
import subprocess
from pathlib import Path
from build import REPO,WORK,PACKAGE
from device_gate import sha,save
from render_gate import inputs
import specialization_check

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--off-mode',choices=['superset','unspecialized','none'],default='superset',
                        help='specialization-off identity check of glass_render (default: superset mode)')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    status={'complete':False,'phase':'prepare'};save(out/'status.json',status)
    sources=inputs();runtime=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins={str(runtime):sha(runtime)};reports={}
    kinds=['glass_svm','glass_bounce','glass_render','microfacet_svm','microfacet_bounce','microfacet_render']
    save(out/'invocation.json',{'productionSourceSha256':sources,'fileSha256':pins})
    try:
        if not args.skip_build:
            status['phase']='build';save(out/'status.json',status)
            command=['python3',str(REPO/'native/tools/build_cycles.py'),'--jobs','2']
            for kind in kinds:command+=['--target','codebrix_cycles_'+kind+'_gate']
            with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
        for kind in kinds:
            binary=WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate');pins[str(binary)]=sha(binary)
        save(out/'invocation.json',{'productionSourceSha256':sources,'fileSha256':pins})
        for kind in kinds:
            status['phase']=kind;save(out/'status.json',status)
            folder=out/kind;folder.mkdir();prefix=folder/'run'
            with (folder/'stdout.txt').open('w') as stdout,(folder/'stderr.txt').open('w') as stderr:
                subprocess.run([str(WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')),str(prefix)],stdout=stdout,stderr=stderr,check=True,timeout=900)
            report=json.loads((folder/'stdout.txt').read_text());assert report['passed'] and report['cyclesAllocationBytesAfterCleanup']==0
            assert Path((folder/'run.library.txt').read_text().strip()).resolve()==runtime.resolve()
            if kind=='glass_svm':assert report['originalCpuSurfaceSvm'] and report['compiledGraphs']==20 and report['closureFamilies']==7 and report['invalidCasesRejected']==17
            if kind=='glass_bounce':assert report['originalCpuSurfaceIntegrator'] and report['cases']==29 and report['invalidCasesRejected']==21
            reports[kind]=report;save(out/'results.json',reports)
        if args.off_mode!='none':
            status['phase']='glass_render-'+args.off_mode;save(out/'status.json',status)
            reports['glass_render-'+args.off_mode]=specialization_check.check(WORK/'cycles-build/bin'/'codebrix_cycles_glass_render_gate',
                out/'glass_render',out/('glass_render-'+args.off_mode),args.off_mode);save(out/'results.json',reports)
        assert sources==inputs() and all(sha(Path(name))==digest for name,digest in pins.items())
        status.update(complete=True,passed=True,phase='complete',inputsUnchanged=True,
                      scope='Native single-scatter GGX refraction and glass, Fresnel reflection/transmission, TIR, mixtures and independent original-Cycles images. Thin film, multi-GGX glass and Principled remain unsupported.')
    except Exception as error:
        status.update(complete=True,passed=False,error=repr(error));raise
    finally:save(out/'status.json',status)
    print(json.dumps(status),flush=True)

if __name__=='__main__':main()
