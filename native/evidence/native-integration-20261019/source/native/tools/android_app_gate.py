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


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-output',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--device',action='append',required=True,metavar='LABEL,RID,SERIAL')
    parser.add_argument('--adb',default=str(Path.home()/'Android/Sdk/platform-tools/adb'))
    args=parser.parse_args();root=args.build_output.resolve();out=args.output.resolve();app=root/'app'
    out.mkdir(parents=True,exist_ok=False)
    status=json.loads((root/'status.json').read_text());inspection=json.loads((app/'apk-inspection.json').read_text())
    assert status['complete'] and status['passed'] and inspection['passed']
    apk=Path(inspection['apk']);assert sha(apk)==inspection['apkSha256']==status['apkSha256']
    payload=json.loads((app/'payload.json').read_text());build_pin=json.loads((root/'invocation.json').read_text())
    assert all(sha(Path(p))==digest for p,digest in {**build_pin['inputSha256'],**build_pin['stagedSha256']}.items())
    sources=inputs();devices=[tuple(d.split(',')) for d in args.device]
    assert all(len(d)==3 and d[1] in TARGETS and d[0].isalnum() for d in devices)
    assert len({d[0] for d in devices})==len({d[2] for d in devices})==len(devices)
    save(out/'invocation.json',dict(sourceSha256=sources,runnerSha256=sha(Path(__file__)),apkSha256=sha(apk),
        inspectionSha256=sha(app/'apk-inspection.json'),devices=devices))
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
            existing=call(['shell','pm','path',APP_ID],'prior-app',check=False)[1]
            assert b'package:' not in existing,'Validation app already installed; preserve it until its evidence is reviewed'
            call(['install',str(apk)],'install',timeout=300);installed=True
            path=call(['shell','pm','path',APP_ID],'installed-path')[1].decode().strip().removeprefix('package:')
            assert call(['shell','sha256sum',path],'installed-hash')[1].decode().split()[0]==sha(apk)
            call(['shell','am','start','-W','-n',APP_ID+'/.MainActivity'],'launch')
            state['phase']='managed CPU lifecycle and configuration reloads';save(folder/'status.json',state)
            report=None;deadline=time.monotonic()+600;attempt=0
            while time.monotonic()<deadline:
                code,data=call(['shell','run-as',APP_ID,'cat','files/result.json'],'result-'+str(attempt),check=False)
                if code==0:
                    try:report=json.loads(data);break
                    except json.JSONDecodeError:pass
                attempt+=1;time.sleep(2)
            assert report is not None,'Android application result timeout'
            save(folder/'app-result.json',report)
            # Preserve complete output before checking reported acceptance.
            _,archive=call(['exec-out','run-as',APP_ID,'tar','-cf','-','files/output','files/result.json','files/native-maps.txt'],
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
            if installed:call(['shell','am','force-stop',APP_ID],'force-stop',check=False)
            save(folder/'status.json',state)
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(devices)) as pool:
        futures=[pool.submit(run,*device) for device in devices];passed=[f.result() for f in futures]
    save(out/'status.json',dict(complete=True,passed=all(passed),phase='complete'))
    return 0 if all(passed) else 1


if __name__=='__main__':raise SystemExit(main())
