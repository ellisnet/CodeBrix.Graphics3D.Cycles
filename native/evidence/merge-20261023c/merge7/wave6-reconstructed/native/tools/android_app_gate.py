#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Install a reviewed local validation APK and independently check its output."""
import argparse,concurrent.futures,io,json,math,struct,subprocess,sys,tarfile,time,traceback
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'))
from device_gate import sha,save
from render_gate import inputs
from android_toolchain import TARGETS
APP_ID='com.codebrix.cycles.hostvalidation'
ACTIVITY='com.codebrix.cycles.hostvalidation.MainActivity'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-output',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--device',action='append',required=True,metavar='LABEL,RID,SERIAL')
    parser.add_argument('--adb',default=str(Path.home()/'Android/Sdk/platform-tools/adb'))
    parser.add_argument('--mode',choices=['cpu','webgpu','bundle'],default='cpu',
                        help='cpu: the CPU reference suite; webgpu: GPU versus CPU reference films rendered in the app; '
                        'bundle: a cbcOracleScene bundle (--bundle) rendered in the app through the library bundle loader')
    parser.add_argument('--bundle',type=Path,help='bundle mode: the bundle directory (scene.json and the raw files it names)')
    parser.add_argument('--reference-film',type=Path,help='bundle mode: film-0.f32 of the same bundle rendered on Linux x64 (glibc) by '
                        'codebrix_cycles_oracle --backend cpu (or the glTF tool); the device GPU film is judged against it and the '
                        'in-app CPU film is a diagnostic. The app then also selects that oracle\'s arithmetic adapters '
                        '(CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2), as the native gates do on reference import')
    parser.add_argument('--linux-reference',type=Path,help='webgpu mode: folder of <scene>.cpu.rgba.bin films rendered on Linux x64 '
                        '(glibc) by WebGpuRenderTests with CODEBRIX_CYCLES_REFERENCE_EXPORT; the device GPU film is judged '
                        'against them and the in-app CPU film becomes a diagnostic (native/DEVICE.md, "Validation reference policy")')
    parser.add_argument('--result-timeout',type=int,default=600,help='Seconds to wait for the app result')
    parser.add_argument('--uninstall',action='store_true',help='Uninstall the app after its evidence is extracted')
    args=parser.parse_args();root=args.build_output.resolve();out=args.output.resolve();app=root/'app'
    out.mkdir(parents=True,exist_ok=False)
    status=json.loads((root/'status.json').read_text());inspection=json.loads((app/'apk-inspection.json').read_text())
    assert status['complete'] and status['passed'] and inspection['passed']
    apk=Path(inspection['apk']);assert sha(apk)==inspection['apkSha256']==status['apkSha256']
    app_id=status.get('applicationId',APP_ID)
    payload=json.loads((app/'payload.json').read_text());build_pin=json.loads((root/'invocation.json').read_text())
    assert all(sha(Path(p))==digest for p,digest in {**build_pin['inputSha256'],**build_pin['stagedSha256']}.items())
    if args.mode=='bundle':
        assert args.bundle and (args.bundle/'scene.json').is_file(),'bundle mode needs --bundle DIR with scene.json'
        bundle_scene=json.loads((args.bundle/'scene.json').read_text());assert bundle_scene['cbcOracleScene']==1
        bundle_files=['scene.json']+sorted({p.name for p in args.bundle.iterdir() if p.is_file() and p.name!='scene.json'})
    sources=inputs();devices=[tuple(d.split(',')) for d in args.device]
    assert all(len(d)==3 and d[1] in TARGETS and d[0].isalnum() for d in devices)
    assert len({d[0] for d in devices})==len({d[2] for d in devices})==len(devices)
    save(out/'invocation.json',dict(sourceSha256=sources,runnerSha256=sha(Path(__file__)),apkSha256=sha(apk),
        inspectionSha256=sha(app/'apk-inspection.json'),devices=devices,mode=args.mode,resultTimeoutSeconds=args.result_timeout,
        linuxReferenceSha256={p.name:sha(p) for p in sorted(args.linux_reference.glob('*.cpu.rgba.bin'))} if args.linux_reference else None,
        bundleSha256={name:sha(args.bundle/name) for name in bundle_files} if args.mode=='bundle' else None,
        referenceFilmSha256=sha(args.reference_film) if args.reference_film else None))
    save(out/'status.json',dict(complete=False,phase='devices'))

    def run(label,rid,serial):
        folder=out/label;folder.mkdir();state=dict(complete=False,phase='install',rid=rid,serial=serial)
        save(folder/'status.json',state);commands=[];installed=False
        def call(arguments,name,check=True,timeout=120):
            command=[args.adb,'-s',serial,*arguments];commands.append(dict(command=command,timeoutSeconds=timeout));save(folder/'commands.json',commands)
            with (folder/(name+'.stdout')).open('wb') as stdout,(folder/(name+'.stderr')).open('wb') as stderr:
                result=subprocess.run(command,stdout=stdout,stderr=stderr,timeout=timeout)
            if check:result.check_returncode()
            return result.returncode,(folder/(name+'.stdout')).read_bytes()
        try:
            processes=call(['shell','ps','-A','-o','PID,ARGS'],'prior-processes')[1].decode()
            assert not any('/data/local/tmp/codebrix-' in line for line in processes.splitlines()),'Device native test still running; do not overlap workloads'
            existing=call(['shell','pm','path',app_id],'prior-app',check=False)[1]
            assert b'package:' not in existing,'Validation app already installed; preserve it until its evidence is reviewed'
            call(['install',str(apk)],'install',timeout=300);installed=True
            path=call(['shell','pm','path',app_id],'installed-path')[1].decode().strip().removeprefix('package:')
            assert call(['shell','sha256sum',path],'installed-hash')[1].decode().split()[0]==sha(apk)
            extras=['--es','mode','webgpu'] if args.mode=='webgpu' else []
            if args.mode=='bundle':
                # Pushed to a staging folder named after the application id's last part, copied into the app's private files/bundle with
                # run-as and verified there; the staging folder is removed before the app starts.
                staging='/data/local/tmp/codebrix-'+app_id.rsplit('.',1)[-1]+'-bundle'
                call(['shell','rm','-rf',staging],'bundle-staging-clean')
                call(['shell','mkdir','-p',staging],'bundle-staging')
                call(['shell','run-as',app_id,'mkdir','-p','files/bundle'],'bundle-mkdir')
                for name in bundle_files:
                    call(['push',str(args.bundle/name),staging+'/'+name],'push-'+name,timeout=600)
                    call(['shell','run-as',app_id,'cp',staging+'/'+name,'files/bundle/'+name],'copy-'+name)
                    remote=call(['shell','run-as',app_id,'sha256sum','files/bundle/'+name],'bundle-hash')[1].decode().split()[0]
                    assert remote==sha(args.bundle/name),'Bundle file differs on the device: '+name
                call(['shell','rm','-rf',staging],'bundle-staging-remove')
                extras=['--es','mode','bundle']+(['--es','oracle','linux-x64-sse4.2'] if args.reference_film else [])
            call(['shell','am','start','-W','-n',app_id+'/'+ACTIVITY,*extras],'launch')
            state['phase']={'webgpu':'managed WebGPU versus CPU reference films','bundle':'managed bundle render'}.get(args.mode,'managed CPU lifecycle and configuration reloads')
            save(folder/'status.json',state);started=time.monotonic()
            report=None;deadline=time.monotonic()+args.result_timeout;attempt=0
            while time.monotonic()<deadline:
                code,data=call(['shell','run-as',app_id,'cat','files/result.json'],'result-'+str(attempt),check=False)
                if code==0:
                    try:report=json.loads(data);break
                    except json.JSONDecodeError:pass
                attempt+=1;time.sleep(2)
            assert report is not None,'Android application result timeout'
            state['appSeconds']=round(time.monotonic()-started,1)
            save(folder/'app-result.json',report)
            # Preserve complete output before checking reported acceptance.
            _,archive=call(['exec-out','run-as',app_id,'tar','-cf','-','files/output','files/result.json','files/native-maps.txt'],
                           'outputs',check=False,timeout=240)
            extracted=folder/'extracted';extracted.mkdir()
            with tarfile.open(fileobj=io.BytesIO(archive)) as bundle:
                for member in bundle:
                    path=Path(member.name)
                    assert not path.is_absolute() and '..' not in path.parts and path.parts[0]=='files'
                    target=extracted/path
                    if member.isdir():target.mkdir(parents=True,exist_ok=True)
                    else:
                        assert member.isfile(),'Unexpected output archive member'
                        target.parent.mkdir(parents=True,exist_ok=True)
                        with bundle.extractfile(member) as data:target.write_bytes(data.read())
            if args.mode=='webgpu':
                webgpu(report,extracted,folder,rid,state);print(label+': '+json.dumps(state),flush=True);return True
            if args.mode=='bundle':
                judge_bundle(report,extracted,folder,rid,label,state);print(label+': '+json.dumps(state),flush=True);return True
            assert report['passed'],report
            assert all(report[field] for field in ['actualAndroidApp','explicitCpuReference','cancellationAndReuse','disposeDuringRender','propertyReadbackChecks'])
            assert report['architecture']==('Arm64' if rid=='android-arm64' else 'X64')
            api=int(call(['shell','getprop','ro.build.version.sdk'],'api')[1]);assert report['apiLevel']==api and api>=33
            assert report['configurationRounds']==6 and report['imageFilms']==1560 and report['imageFormats']==8
            assert report['registryNodeTypes']==184 and report['registrySockets']==1277
            assert report['nativeDirectory'].startswith('/data/app/') and report['libraries']==payload['native'][TARGETS[rid][0]]
            cases=[('lifecycle/run.'+str(i)+'.rgba.bin','reference/cpu/run.'+str(i)+'.rgba.bin',True) for i in range(3)]
            cases += [(f'round.{i}/case.{j}.rgba.bin',f'reference/{chr(ord("a")+i%3)}/output/case.{j}.rgba.bin',False) for i in range(6) for j in range(260)]
            reported={row['Film'].split('/files/output/',1)[1]:row for row in report['comparisons']}
            assert len(reported)==len(cases)==1563
            comparisons=[]
            for name,reference_name,strict_required in cases:
                actual=extracted/'files/output'/name;reference=app/reference_name
                assert sha(reference)==payload['references'][reference_name]
                a,b=struct.unpack('<3072f',actual.read_bytes()),struct.unpack('<3072f',reference.read_bytes())
                assert all(math.isfinite(x) for x in a+b)
                errors=[abs(x-y) for x,y in zip(a,b)]
                row=dict(film=name,filmSha256=sha(actual),bitwiseEqual=actual.read_bytes()==reference.read_bytes(),
                    maximumAbsoluteError=max(errors),practicalFailures=sum(e>5e-5+2e-4*abs(y) for e,y in zip(errors,b)),
                    strictDifferences=sum(e>1e-7+1e-7*abs(y) for e,y in zip(errors,b)),strictRequired=strict_required)
                comparisons.append(row)
                assert row['filmSha256']==reported[name]['Sha256']
                assert row['practicalFailures']==reported[name]['PracticalFailures'] and row['strictDifferences']==reported[name]['StrictDifferences']
                # Mono JSON formatting can round this diagnostic by one double ULP.
                # Film comparisons above retain the exact original criteria.
                maximum_reported=reported[name]['MaximumAbsoluteError']
                assert math.isfinite(maximum_reported) and abs(row['maximumAbsoluteError']-maximum_reported)<=math.ulp(row['maximumAbsoluteError'])
                row['reportedMaximumAbsoluteError']=maximum_reported
                assert row['strictRequired']==reported[name]['StrictRequired']
            save(folder/'comparisons.json',comparisons)
            assert all(row['practicalFailures']==0 and (not row['strictRequired'] or row['strictDifferences']==0) for row in comparisons)
            assert inputs()==sources and sha(apk)==inspection['apkSha256']
            state.update(complete=True,passed=True,phase='complete',independentlyComparedFilms=len(comparisons),
                practicalFailures=sum(row['practicalFailures'] for row in comparisons),
                strictDifferences=sum(row['strictDifferences'] for row in comparisons),
                maximumAbsoluteError=max(row['maximumAbsoluteError'] for row in comparisons),
                apkSha256=sha(apk),installedNativePayloads=len(report['libraries']))
            print(label+': '+json.dumps(state),flush=True);return True
        except Exception as error:
            (folder/'failure-traceback.txt').write_text(traceback.format_exc())
            state.update(complete=True,passed=False,error=repr(error));print(label+': '+repr(error),flush=True);return False
        finally:
            if installed:call(['shell','am','force-stop',app_id],'force-stop',check=False)
            if installed and args.uninstall:
                state['uninstalled']=call(['uninstall',app_id],'uninstall',check=False)[0]==0
            save(folder/'status.json',state)
    def webgpu(report,extracted,folder,rid,state):
        # Independent recomputation of the in-app GPU versus CPU reference comparison.
        assert report['mode']=='webgpu' and report['actualAndroidApp'],report
        assert report['architecture']==('Arm64' if rid=='android-arm64' else 'X64')
        assert report['nativeDirectory'].startswith('/data/app/')
        rows=[]
        for scene in report['scenes']:
            gpu=extracted/'files/output/webgpu'/(scene['name']+'.gpu.rgba.bin');cpu=extracted/'files/output/webgpu'/(scene['name']+'.cpu.rgba.bin')
            count=scene['width']*scene['height']*4
            a,b=struct.unpack('<%df'%count,gpu.read_bytes()),struct.unpack('<%df'%count,cpu.read_bytes())
            assert all(math.isfinite(x) for x in a+b)
            errors=[abs(x-y) for x,y in zip(a,b)]
            row=dict(scene=scene['name'],gpuSha256=sha(gpu),cpuSha256=sha(cpu),bitwiseEqual=gpu.read_bytes()==cpu.read_bytes(),
                maximumAbsoluteError=max(errors),practicalFailures=sum(e>5e-5+2e-4*abs(y) for e,y in zip(errors,b)),
                strictDifferences=sum(e>1e-7+1e-7*abs(y) for e,y in zip(errors,b)),gpuSeconds=scene['gpuSeconds'],
                cpuSeconds=scene['cpuSeconds'],progressEvents=scene['progressEvents'],pipelineTier=scene['pipelineTier'],warnings=scene['warnings'])
            assert row['gpuSha256']==scene['gpuSha256'] and row['cpuSha256']==scene['cpuSha256']
            assert row['practicalFailures']==scene['practicalFailures'] and row['strictDifferences']==scene['strictDifferences']
            if args.linux_reference:
                # Authoritative: device GPU versus the Linux glibc CPU film. The in-app CPU film (this
                # device's C library) and the in-app GPU-versus-CPU numbers above are diagnostics.
                linux=args.linux_reference/(scene['name']+'.cpu.rgba.bin');c=struct.unpack('<%df'%count,linux.read_bytes())
                assert all(math.isfinite(x) for x in c)
                def judge(x,y):
                    e=[abs(p-q) for p,q in zip(x,y)]
                    return dict(maximumAbsoluteError=max(e),practicalFailures=sum(d>5e-5+2e-4*abs(q) for d,q in zip(e,y)),
                                strictDifferences=sum(d>1e-7+1e-7*abs(q) for d,q in zip(e,y)),bitwiseEqual=list(x)==list(y))
                row['diagnosticInAppGpuVersusInAppCpu']=dict(maximumAbsoluteError=row['maximumAbsoluteError'],
                    practicalFailures=row['practicalFailures'],strictDifferences=row['strictDifferences'],role='diagnostic')
                row['diagnosticInAppCpuVersusLinux']=dict(judge(b,c),role='diagnostic')
                row.update(judge(a,c),reference='linux-glibc',linuxReferenceSha256=sha(linux))
            else:
                row['reference']='in-app-cpu'
            rows.append(row)
        save(folder/'comparisons.json',rows)
        capability=report['capability']
        (folder/'capability-report.json').write_text(capability['json'])
        state.update(scenes=len(rows),practicalFailures=sum(r['practicalFailures'] for r in rows),
            strictDifferences=sum(r['strictDifferences'] for r in rows),maximumAbsoluteError=max(r['maximumAbsoluteError'] for r in rows),
            pipelineTier=capability['pipelineTier'],deviceDescription=capability['deviceDescription'],
            capabilityWarnings=capability['warnings'],cancellation=report['cancellation'],perScene=rows)
        if not args.linux_reference:assert report['passed'],report
        assert len(rows)==3 and state['practicalFailures']==0
        assert report['cancellation']['cancelled'] and not report['cancellation']['faultedAfterCancel']
        assert report['cancellation']['reRenderPracticalFailures']==0
        assert all(r['progressEvents']>0 for r in rows)
        assert inputs()==sources and sha(apk)==inspection['apkSha256']
        state.update(complete=True,passed=True,phase='complete')

    def judge_bundle(report,extracted,folder,rid,label,state):
        # Independent judgement of the bundle render. With --reference-film the device GPU film is judged
        # against the Linux glibc CPU film of the same bundle; the in-app CPU film is a diagnostic.
        assert report.get('mode')=='bundle' and report.get('actualAndroidApp'),report
        assert report['architecture']==('Arm64' if rid=='android-arm64' else 'X64')
        assert report['nativeDirectory'].startswith('/data/app/')
        # The app hashes the scene document and every file it names; extra files (bundle-manifest.json) are not inputs.
        assert 'scene.json' in report['inputs'] and set(report['inputs'])<=set(bundle_files),'The app rendered other bundle files'
        assert all(report['inputs'][name]==sha(args.bundle/name) for name in report['inputs']),'The app rendered other bundle bytes'
        count=report['width']*report['height']*4;films=extracted/'files/output/bundle'
        def load(path):
            values=struct.unpack('<%df'%count,path.read_bytes());assert all(math.isfinite(x) for x in values);return values
        def judge(x,y):
            e=[abs(p-q) for p,q in zip(x,y)]
            worst=max(range(len(e)),key=lambda i:e[i]/(5e-5+2e-4*abs(y[i])))
            return dict(maximumAbsoluteError=max(e),practicalFailures=sum(d>5e-5+2e-4*abs(q) for d,q in zip(e,y)),
                        strictDifferences=sum(d>1e-7+1e-7*abs(q) for d,q in zip(e,y)),bitwiseEqual=list(x)==list(y),
                        worst=dict(index=worst,pixel=worst//4,channel='RGBA'[worst%4],reference=y[worst],actual=x[worst]))
        gpu=[load(films/f'gpu-{i}.f32') for i in range(report['repeats'])];cpu=load(films/'cpu-0.f32')
        assert [sha(films/f'gpu-{i}.f32') for i in range(report['repeats'])]==report['gpuSha256'] and sha(films/'cpu-0.f32')==report['cpuSha256']
        row=dict(sceneId=report['sceneId'],gpuSeconds=report['gpuSeconds'],cpuSeconds=report['cpuSeconds'],applySeconds=report['applySeconds'],
                 progressEvents=report['progressEvents'],errorEvents=report['errorEvents'],referenceOracle=report['referenceOracle'],
                 diagnosticGpuVersusInAppCpu=dict(judge(gpu[0],cpu),role='diagnostic'),
                 gpuRepeatBitwiseIdentical=all(g==gpu[0] for g in gpu))
        if args.reference_film:
            linux=load(args.reference_film)
            row['diagnosticInAppCpuVersusLinux']=dict(judge(cpu,linux),role='diagnostic')
            row.update(judge(gpu[0],linux),reference='linux-glibc',referenceFilmSha256=sha(args.reference_film))
            row['gpuRepeats']=[judge(g,linux) for g in gpu]
        else:
            row.update(judge(gpu[0],cpu),reference='in-app-cpu')
        save(folder/'comparisons.json',[row])
        # The device films in the oracle output layout, for native/oracle/compare.py.
        scene_sha=sha(args.bundle/'scene.json');device=label.lower()
        for backend,names,hashes in [('wgsl-android-'+device,[f'gpu-{i}.f32' for i in range(report['repeats'])],report['gpuSha256']),
                                     ('cpu-'+('arm64' if rid=='android-arm64' else 'x64')+'-android-'+device,['cpu-0.f32'],[report['cpuSha256']])]:
            target=folder/'oracle'/backend;target.mkdir(parents=True)
            for i,name in enumerate(names):(target/f'film-{i}.f32').write_bytes((films/name).read_bytes())
            data=[(target/f'film-{i}.f32').read_bytes() for i in range(len(names))]
            save(target/'meta.json',dict(sceneId=report['sceneId'],sceneSha256=scene_sha,backend=backend,width=report['width'],height=report['height'],
                samples=report['samples'],repeats=len(names),repeatBitwiseIdentical=all(d==data[0] for d in data),
                repeatMaxAbsDifference=max([0.0]+[max(abs(p-q) for p,q in zip(struct.unpack('<%df'%count,d),struct.unpack('<%df'%count,data[0]))) for d in data[1:]]),
                renderSeconds=report['gpuSeconds'] if backend.startswith('wgsl') else [report['cpuSeconds']],filmSha256=hashes,
                deviceName=report['capability']['deviceDescription'],apiLevel=report['apiLevel'],
                referenceOracle=report['referenceOracle'],warnings=report['capability']['warnings'] if backend.startswith('wgsl') else [],
                filmLayout='float32 little-endian RGBA, width*height*4 values, lower-left origin'))
        (folder/'capability-report.json').write_text(report['capability']['json'])
        state.update(perScene=[row],practicalFailures=row['practicalFailures'],strictDifferences=row['strictDifferences'],
            maximumAbsoluteError=row['maximumAbsoluteError'],pipelineTier=report['capability']['pipelineTier'],
            deviceDescription=report['capability']['deviceDescription'],capabilityWarnings=report['capability']['warnings'])
        assert report['passed'],report
        assert inputs()==sources and sha(apk)==inspection['apkSha256']
        state.update(complete=True,passed=row['practicalFailures']==0,phase='complete')

    with concurrent.futures.ThreadPoolExecutor(max_workers=len(devices)) as pool:
        futures=[pool.submit(run,*device) for device in devices];passed=[f.result() for f in futures]
    save(out/'status.json',dict(complete=True,passed=all(passed),phase='complete'))
    return 0 if all(passed) else 1


if __name__=='__main__':raise SystemExit(main())
