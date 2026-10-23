#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native triangle instance motion: original state/closure and moving-image gates."""
import argparse
import array
import json
import math
import subprocess
import sys
from pathlib import Path
from build import REPO, WORK, PACKAGE, REVISION
from device_gate import sha, save
from render_gate import inputs
import render_replay


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--skip-build',action='store_true')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=inputs();save(out/'source-pins.json',source)
    names=['closest','shader_data','shadow','normal','image','object_motion_render']
    if not args.skip_build:
        command=[sys.executable,str(REPO/'native/tools/build_cycles.py'),'--jobs','2']
        for name in names:command+=['--target','codebrix_cycles_'+name+'_gate']
        with (out/'build.stdout.txt').open('w') as stdout,(out/'build.stderr.txt').open('w') as stderr:
            subprocess.run(command,stdout=stdout,stderr=stderr,check=True)
    executables={name:WORK/'cycles-build/bin'/('codebrix_cycles_'+name+'_gate') for name in names}
    render_replay.build('linux-x64')
    executables['replay']=render_replay.BASE/'linux-x64/render_replay'
    runtime=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins={str(p):sha(p) for p in [*executables.values(),runtime]};save(out/'binary-pins.json',pins)
    results={}
    def run(label,name,flags):
        folder=out/label;folder.mkdir();render=name=='object_motion_render'
        prefix=folder/'images' if render else folder/'run'
        command=[str(executables[name]),str(prefix),*flags];save(folder/'invocation.json',{'command':command})
        print(label,flush=True)
        with (folder/'stdout.txt').open('w') as stdout,(folder/'stderr.txt').open('w') as stderr:
            subprocess.run(command,stdout=stdout,stderr=stderr,check=True,timeout=900)
        report=json.loads((folder/'stdout.txt').read_text());assert report['passed']
        loaded=Path((prefix/'library.txt' if render else folder/'run.library.txt').read_text()).resolve()
        assert loaded==runtime.resolve() and sha(loaded)==pins[str(runtime)]
        assert report['cyclesAllocationBytesAfterCleanup']==0
        results[label]=report;save(out/'partial-results.json',results)
        return report
    try:
        for motion in [True,False]:
            for name in names[:-1]:
                report=run(name+('-motion' if motion else '-static'),name,['--motion'] if motion else [])
                if name!='image':assert report['cases']=={'closest':22,'shader_data':11,'shadow':60,'normal':399}[name]
                assert report['invalidCasesRejected']=={'closest':21,'shader_data':12,'shadow':18,'normal':10,'image':14}[name]
                if name=='closest':assert report['nativeInstanceLeaves']>=3 and report['hitObjects']==5
                if name=='shader_data':assert report['hitObjects']==9 and all(report['projectionAxes'])
                if name=='shadow':assert report['blocked']>100 and report['unoccluded']>100 and report['selfOrLightExclusions']>50
                if name=='normal':assert report['nativeGraphs']==48 and report['typedPrograms']==398 and report['paths']==10704
                if name=='image':assert report['nativeGraphs']==128 and report['typedPrograms']==176 and report['graphPaths']==1824 and report['samplingCases']==115650
        for label,flags in [('moving-images',[]),('stationary-images',['--stationary'])]:
            report=run(label,'object_motion_render',flags)
            assert report['cases']==8 and report['independentOriginalCpuImage'] and report['pointSunMisEnabled']
            assert report['shadowPaths']>100 and report['forwardLightPaths']>100
        def film(path):
            data=array.array('f');data.frombytes(path.read_bytes());return data
        repeat_comparisons={}
        for label in ['moving-images','stationary-images']:
            repeated=[]
            for mode in range(8):
                capture=out/label/'images'/('case-'+str(mode))
                a=film(capture/'run.gpu.film.bin');b=film(capture/'run.repeat.film.bin')
                errors=[abs(x-y) for x,y in zip(a,b,strict=True)]
                assert all(math.isfinite(x) and math.isfinite(y) and e<=5e-5+2e-4*abs(x) for x,y,e in zip(a,b,errors,strict=True))
                native=json.loads((capture/'result.json').read_text())
                repeated.append({'case':mode,'bitwiseDifferences':native['repeatBitwiseDifferences'],
                    'strictDifferences':sum(e>1e-7+1e-7*abs(x) for x,e in zip(a,errors)),
                    'maximumAbsoluteError':max(errors)})
            repeat_comparisons[label]=repeated
        differences={}
        for mode in range(8):
            differences[str(mode)]={}
            for device in ['cpu','gpu']:
                leaf=Path('images')/('case-'+str(mode))/('run.'+device+'.film.bin')
                moving=film(out/'moving-images'/leaf);stationary=film(out/'stationary-images'/leaf)
                assert len(moving)==len(stationary)==32*24*4
                changed=sum(a!=b for a,b in zip(moving,stationary));maximum=max(abs(a-b) for a,b in zip(moving,stationary))
                assert changed>100 and maximum>.01,'Motion did not measurably change image'
                differences[str(mode)][device]={'differentComponentsFromStationary':changed,'maximumFilmDifference':maximum}
        for mode in range(8):
            capture=out/'moving-images/images'/('case-'+str(mode))
            meta=json.loads((capture/'run.capture.json').read_text())
            assert meta['format'] in ('native-command-stream-v5','native-command-stream-v6','native-command-stream-v7','native-command-stream-v8') and meta['pipelineDispatches'][9]>0 and (meta['format']=='native-command-stream-v5' or meta['pipelineDispatches'][15]>0)
            replay=out/('replay-motion-'+str(mode))
            render_replay.run('linux-x64',capture,replay,None,timeout=900)
            results['replay-motion-'+str(mode)]=json.loads((replay/'validation.json').read_text())
            save(out/'partial-results.json',results)
        assert source==inputs(),'Source changed during validation'
        assert all(sha(Path(p))==digest for p,digest in pins.items()),'Binary/runtime changed during validation'
        save(out/'validation.json',{'passed':True,'cyclesRevision':REVISION,'results':results,
            'stationaryComparison':differences,'repeatComparisons':repeat_comparisons,'unchangedSourceBinaryAndRuntime':True,
            'absoluteTolerance':5e-5,'relativeTolerance':2e-4,'strictAbsoluteTolerance':1e-7,'strictRelativeTolerance':1e-7,
            'scope':'Native transformed triangle instances with object motion at ray time, closest/opaque shadows, cached shader transforms, object texture coordinates, box image projection, normals/tangents and motion-aware ray offset. Deforming geometry, animated emitters, curves, points and other unsupported features remain guarded.'})
        save(out/'status.json',{'complete':True,'passed':True})
    except BaseException as error:
        save(out/'status.json',{'complete':True,'passed':False,'error':str(error)})
        raise


if __name__=='__main__':main()
