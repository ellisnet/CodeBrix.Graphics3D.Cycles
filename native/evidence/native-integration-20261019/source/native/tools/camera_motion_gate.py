#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native perspective motion: original camera state and independent rendered images."""
import argparse
import array
import json
import subprocess
import sys
from pathlib import Path
from build import REPO, WORK, PACKAGE, REVISION
from device_gate import sha, save
from render_gate import inputs


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--skip-build',action='store_true')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=inputs();save(out/'source-pins.json',source)
    if not args.skip_build:
        with (out/'build.stdout.txt').open('w') as stdout,(out/'build.stderr.txt').open('w') as stderr:
            subprocess.run([sys.executable,str(REPO/'native/tools/build_cycles.py'),'--jobs','2',
                '--target','codebrix_cycles_camera_gate','--target','codebrix_cycles_render_gate'],
                stdout=stdout,stderr=stderr,check=True)
    executables={name:WORK/'cycles-build/bin'/('codebrix_cycles_'+name+'_gate') for name in ['camera','render']}
    runtime=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins={str(p):sha(p) for p in [*executables.values(),runtime]};save(out/'binary-pins.json',pins)
    results={}
    def run(label,name,flags):
        folder=out/label;folder.mkdir();prefix=folder/'run'
        command=[str(executables[name]),str(prefix),*flags]
        save(folder/'invocation.json',{'command':command})
        with (folder/'stdout.txt').open('w') as stdout,(folder/'stderr.txt').open('w') as stderr:
            subprocess.run(command,stdout=stdout,stderr=stderr,check=True,timeout=900)
        report=json.loads((folder/'stdout.txt').read_text());assert report['passed']
        loaded=Path((folder/'run.library.txt').read_text()).resolve()
        assert loaded==runtime.resolve() and sha(loaded)==pins[str(runtime)]
        results[label]=report;save(out/'partial-results.json',results)
    try:
        run('camera-static','camera',[]);run('camera-motion','camera',['--motion'])
        assert results['camera-static']['cases']==24 and results['camera-static']['invalidCasesRejected']==12
        assert results['camera-motion']['cameraMotionCases']==24 and results['camera-motion']['invalidCasesRejected']==19
        run('render-static','render',[])
        def film(path):
            data=array.array('f');data.frombytes(path.read_bytes());return data
        stationary_cpu=film(out/'render-static/run.cpu.film.bin')
        stationary_gpu=film(out/'render-static/run.gpu.film.bin')
        differences={}
        for mode in range(8):
            label='render-motion-'+str(mode);run(label,'render',['--motion='+str(mode)])
            assert results[label]['independentCpuImage'] and results[label]['cyclesAllocationBytesAfterCleanup']==0
            differences[label]={}
            for device,reference in [('cpu',stationary_cpu),('gpu',stationary_gpu)]:
                moving=film(out/label/('run.'+device+'.film.bin'));assert len(moving)==len(reference)
                changed=sum(a!=b for a,b in zip(moving,reference));maximum=max(abs(a-b) for a,b in zip(moving,reference))
                assert changed>100 and maximum>.01,'Motion did not measurably change rendered image'
                differences[label][device]={'differentComponentsFromStationary':changed,'maximumFilmDifference':maximum}
        assert source==inputs(),'Source changed during validation'
        assert all(sha(Path(p))==digest for p,digest in pins.items()),'Binary/runtime changed during validation'
        save(out/'validation.json',{'passed':True,'cyclesRevision':REVISION,'results':results,
            'stationaryComparison':differences,'unchangedSourceBinaryAndRuntime':True,
            'scope':'Perspective pinhole camera transform/FOV motion, shutter CDF and top rolling shutter; static triangle geometry. Object/deformation motion is validated separately by deforming_motion_gate.py. Other cameras and depth of field remain unsupported.'})
        save(out/'status.json',{'complete':True,'passed':True})
    except BaseException as error:
        save(out/'status.json',{'complete':True,'passed':False,'error':str(error)})
        raise


if __name__=='__main__':main()
