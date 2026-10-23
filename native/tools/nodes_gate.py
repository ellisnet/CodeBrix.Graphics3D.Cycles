#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Texture, colour and utility SVM node gates: original-CPU surface SVM
evaluation (compiled graphs, typed programs, rejections), compiled worlds and an
independent-CPU-image render, with scene-driven specialization on and, for the
kinds selected, off (films and world images must be bit-identical between the
two). "Off" is the superset mode by default (every handler except the bump and
derivative regions the scene does not reach); --off-mode unspecialized selects
the fully unspecialized diagnostic modules."""
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
from build import REPO,WORK,PACKAGE
from device_gate import sha,save
from render_gate import inputs
import specialization_check

KINDS=['nodes_svm','nodes_world','nodes_render']
# Specialization-off runs. The default off mode is the superset mode
# (CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset); the fully unspecialized modules
# (=0, --off-mode unspecialized) exceed what Intel Mesa 25.0.7 compiles correctly
# since the bump and derivative handlers (native/DEVICE.md, "Linux reference fixes on
# the updated Mesa"). nodes_svm is not in the default list: its typed programs reach
# every region, so its superset module is the unspecialized module.
UNSPECIALIZED_DEFAULT='nodes_world,nodes_render'
OFF_MODES={'superset':'superset','unspecialized':'0'}

def run(binary,folder,specialized,off_mode='superset'):
    folder.mkdir(parents=True)
    env=dict(os.environ,CODEBRIX_CYCLES_SHADER_SPECIALIZATION='1' if specialized else OFF_MODES[off_mode],
             CODEBRIX_CYCLES_PIPELINE_LOG=str(folder/'pipelines.jsonl'),MESA_SHADER_CACHE_DIR=str(folder/'shader-cache'))
    with (folder/'stdout.txt').open('w') as stdout,(folder/'stderr.txt').open('w') as stderr:
        subprocess.run([str(binary),str(folder/'run')],stdout=stdout,stderr=stderr,check=True,timeout=900,env=env)
    for path in (folder/'shader-cache').rglob('*'):
        if path.is_file():path.unlink()
    report=json.loads((folder/'stdout.txt').read_text())
    assert report['passed'] and report['cyclesAllocationBytesAfterCleanup']==0
    return report

def images(folder):
    names=[p for p in sorted(folder.iterdir()) if p.name.endswith(('.ppm','.film.bin'))]
    return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in names}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--jobs',default='6')
    parser.add_argument('--unspecialized',default=UNSPECIALIZED_DEFAULT,help='comma-separated kinds that also run with specialization off')
    parser.add_argument('--off-mode',choices=sorted(OFF_MODES),default='superset',help='specialization-off mode for those kinds')
    args=parser.parse_args();unspecialized=[k for k in args.unspecialized.split(',') if k];suffix='-'+args.off_mode
    assert all(k in KINDS for k in unspecialized),unspecialized
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    status={'complete':False,'phase':'prepare'};save(out/'status.json',status)
    sources=inputs();runtime=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins={str(runtime):sha(runtime)};reports={}
    try:
        if not args.skip_build:
            status['phase']='build';save(out/'status.json',status)
            command=['python3',str(REPO/'native/tools/build_cycles.py'),'--jobs',args.jobs]
            for kind in KINDS:command+=['--target','codebrix_cycles_'+kind+'_gate']
            with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
        for kind in KINDS:
            binary=WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate');pins[str(binary)]=sha(binary)
        save(out/'invocation.json',{'productionSourceSha256':sources,'fileSha256':pins})
        for kind in KINDS:
            binary=WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')
            for specialized in (True,):
                name=kind;status['phase']=name;save(out/'status.json',status)
                report=run(binary,out/name,specialized,args.off_mode);reports[name]=report;save(out/'results.json',reports)
                assert Path((out/name/'run.library.txt').read_text().strip()).resolve()==runtime.resolve()
                if kind=='nodes_svm':
                    assert report['originalCpuSurfaceSvm'] and report['compiledGraphs']==67 and report['closureFamilies']==5 and report['invalidCasesRejected']==32
                if kind=='nodes_world':assert report['compiledWorlds']==24
                if kind=='nodes_render':assert report['independentCpuImage'] and report['repeatBitwiseIdentical']
        # Specialization off (superset groups by default): every film/image must be
        # bit-identical to the specialized run (specialization_check.py).
        identical={}
        for kind in KINDS:
            if kind not in unspecialized:continue
            status['phase']=kind+suffix;save(out/'status.json',status)
            binary=WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')
            identical[kind]=specialization_check.check(binary,out/kind,out/(kind+suffix),args.off_mode,timeout=1800)
            reports[kind+suffix]=identical[kind];save(out/'results.json',reports)
        save(out/'specialization-identical.json',{'unspecializedKinds':unspecialized,'offMode':args.off_mode,'identical':identical})
        assert sources==inputs() and all(sha(Path(name))==digest for name,digest in pins.items())
        status.update(complete=True,passed=True,phase='complete',inputsUnchanged=True,
                      scope='Native texture, colour and utility SVM nodes against the unmodified CPU kernels: compiled graphs (including Gabor, wavelength and wireframe), typed programs and rejections; compiled worlds; and an independent-CPU-image render with repeat and capture. Specialized and specialization-off ('+args.off_mode+') films/images are bit-identical for the kinds run with specialization off ('+','.join(unspecialized)+').',unspecializedKinds=unspecialized,offMode=args.off_mode)
    except Exception as error:
        status.update(complete=True,passed=False,error=repr(error));raise
    finally:save(out/'status.json',status)
    print(json.dumps(status),flush=True)

if __name__=='__main__':main()
