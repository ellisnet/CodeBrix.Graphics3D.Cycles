#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Run registered shadow stages and affected traversal/render regressions."""
import argparse,json,subprocess
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
    status=dict(complete=False,phase='prepare');save(out/'status.json',status)
    sources=inputs();results={};binaries={}
    runtime=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so';runtime_sha=sha(runtime)
    save(out/'invocation.json',dict(sourceSha256=sources,webGpuSha256=runtime_sha))
    kinds=['shadow','closest','microfacet_render']
    try:
        if not args.skip_build:
            status['phase']='build';save(out/'status.json',status)
            command=['python3',str(REPO/'native/tools/build_cycles.py'),'--jobs','4']
            for kind in kinds:command+=['--target','codebrix_cycles_'+kind+'_gate']
            with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
            render_replay.build('linux-x64')
        binaries={str(WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')):sha(WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')) for kind in kinds}
        replay=render_replay.BASE/'linux-x64/render_replay';binaries[str(replay)]=sha(replay)
        save(out/'invocation.json',dict(sourceSha256=sources,binarySha256=binaries,webGpuSha256=runtime_sha))
        for kind in kinds:
            status['phase']=kind;save(out/'status.json',status);folder=out/kind;folder.mkdir()
            executable=WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')
            with (folder/'stdout.txt').open('w') as stdout,(folder/'stderr.txt').open('w') as stderr:
                subprocess.run([str(executable),str(folder/'run')],stdout=stdout,stderr=stderr,check=True,timeout=600)
            report=json.loads((folder/'stdout.txt').read_text());assert report['passed'] and report['cyclesAllocationBytesAfterCleanup']==0
            assert Path((folder/'run.library.txt').read_text().strip()).resolve()==runtime.resolve()
            if kind=='shadow':
                assert report['originalCpuShadowKernels'] and report['originalGpuScheduler']
                assert report['cases']==60 and report['paths']==3846 and report['directDispatchCases']==30 and report['invalidCasesRejected']==18
                assert report['blocked']>100 and report['unoccluded']>100 and report['selfOrLightExclusions']>50
            elif kind=='closest':assert report['cases']==22 and report['invalidCasesRejected']==16 and report['originalCameraToIntersectionScheduler']
            else:assert report['nativeSceneAndScheduler'] and report['independentCpuImage'] and report['repeatBitwiseIdentical']
            results[kind]=report;save(out/'results.json',results)
        status['phase']='shadow-replay';save(out/'status.json',status)
        for mode in ['direct','indexed']:
            capture=out/'shadow'/('run.capture-'+mode);destination=out/('replay-'+mode)
            render_replay.run('linux-x64',capture,destination)
            report=json.loads((destination/'validation.json').read_text())
            assert report['workload']=='shadow-stages' and report['terminalShadowPathSlots']>0
            assert report['results']['pipelineDispatches'][6:8]==[1,1] and not any(report['results']['pipelineDispatches'][8:])
            results['replay-'+mode]=report;save(out/'results.json',results)
        assert inputs()==sources and sha(runtime)==runtime_sha and all(sha(Path(name))==digest for name,digest in binaries.items())
        status.update(complete=True,passed=True,phase='complete',inputsUnchanged=True,
          scope='Opaque static-triangle registered shadow stages and surface light-pass film output; original CPU references. Full direct lighting, transparent shadows and MIS are not yet proven.')
        print(json.dumps(status),flush=True)
    except Exception as error:
        status.update(complete=True,passed=False,error=repr(error));raise
    finally:save(out/'status.json',status)

if __name__=='__main__':main()
