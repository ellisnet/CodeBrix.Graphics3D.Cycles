#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Judge a device's WGSL device gates against the Linux x64 glibc reference, in four hand-off steps.

    device:  python3 native/tools/reference_exchange_handoff.py device-dump      --output <x>/out-1
    Linux:   python3 native/tools/reference_exchange_handoff.py linux-reference  --dump <x>/out-1 --output <x>/in-1
    device:  python3 native/tools/reference_exchange_handoff.py device-import    --dump <x>/out-1 --reference <x>/in-1 --output <x>/out-2
    Linux:   python3 native/tools/reference_exchange_handoff.py linux-judge      --dump <x>/out-1 --reference <x>/in-1 --run <x>/out-2 --output <x>/in-2 [--evidence <dir>]

(On Windows run each as `python -I -X utf8 native/tools/...`.)

Some device gates are judged against Linux x64 glibc computed from the device's OWN scene tables (native/DEVICE.md,
"Validation reference policy"): the device's C library prepares the scene the GPU consumes, so a reference prepared
on Linux would differ from the GPU's inputs. The Intel Mac does both halves at once
(native/tools/macos_x64_reference_exchange.py). A device without a Linux x64 side (Windows, the Raspberry Pi, the
Intel Mac without Docker) hands the files to the Linux x64 machine instead:

  device-dump      light_tree, microfacet_render: one pass with CODEBRIX_CYCLES_SCENE_DUMP (no GPU work);
                   mnee: the record pass (CODEBRIX_CYCLES_STAGE_RECORD), which runs the GPU and records every
                   lockstep check with the scene tables it used
  linux-reference  the glibc reference of each dumped scene (android_reference.py scene-reference) and the
                   mnee stage judge (android_reference.py stage-judge); the mnee verdict is final here
  device-import    light_tree, microfacet_render against the imported reference (CODEBRIX_CYCLES_REFERENCE_IMPORT,
                   CODEBRIX_CYCLES_SCENE_VERIFY); microfacet_render records its GPU stages
  linux-judge      the stage judge of microfacet_render's recorded stages; the summary of all three gates

Every step writes manifest.json (commit, working-tree changes, binary and runtime SHA-256, `dotnet --version`,
host identity) and refuses inputs from another commit or with another runtime. The WebGPU runtime must be the
CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 file for the RID and the .NET SDK 10.0.403 (docs/TESTING-*.md,
"MANDATORY TEST RULES"). linux-judge --evidence writes a copy for native/evidence/ without the stage records
(mnee about 150 MB, microfacet_render about 25 MB) and the GPU arena dumps (about 17-27 MB per case), which are
identified by SHA-256 in transport-only.json and travel by hand.
"""
import argparse,datetime,hashlib,json,os,platform,shutil,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
TOOLS=REPO/'native/tools'
LINUX_BIN=REPO/'native/work/cycles-build/bin'
RUNTIME_VERSION='1.0.300.210'
SDK_VERSION='10.0.403'
SCENE_GATES=['light_tree','microfacet_render']
GATES=SCENE_GATES+['mnee']
RUNTIME_FILE={'linux-x64':'libcodebrix_wgpu.so','linux-arm64':'libcodebrix_wgpu.so','osx-x64':'libcodebrix_wgpu.dylib',
              'osx-arm64':'libcodebrix_wgpu.dylib','win-x64':'codebrix_wgpu.dll','win-arm64':'codebrix_wgpu.dll'}
# Large records stay out of the evidence copy (the stage records and the GPU arena dumps); transport-only.json
# keeps their SHA-256.
TRANSPORT_ONLY=('stages.bin','.arena.bin')


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def log(text):print(f'[{datetime.datetime.now():%H:%M:%S}] {text}',flush=True)
def save(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
def load(path):return json.loads(Path(path).read_text())
# The manifests summarise the provenance and judge records; the full records (with the per-file source hashes)
# stay in their own files.
def brief(record):return {k:v for k,v in record.items() if k!='sourceSha256'} if record else record


def rid():
    system,machine=platform.system(),platform.machine().lower()
    arch={'x86_64':'x64','amd64':'x64','aarch64':'arm64','arm64':'arm64'}.get(machine)
    os_name={'Linux':'linux','Darwin':'osx','Windows':'win'}.get(system)
    if not arch or not os_name:raise SystemExit(f'Unsupported host: {system} {machine}')
    return f'{os_name}-{arch}'


def default_bin(host):
    if host.startswith('win-'):return REPO/'native/work/windows'/host/'host/build/bin'
    if host=='osx-x64':return REPO/'native/work/macos/osx-x64/host/build/bin'
    if host=='linux-x64':return LINUX_BIN
    if host=='linux-arm64':return REPO/'native/work/linux/linux-arm64/host/build/bin'
    raise SystemExit(f'No WGSL device gates on {host}; pass --bin')


def gate_binary(folder,gate):
    name=f'codebrix_cycles_{gate}_gate'+('.exe' if os.name=='nt' else '')
    path=Path(folder)/name
    if not path.exists():raise SystemExit(f'Gate not built: {path}')
    return path


def package_runtime(host):
    sys.path.insert(0,str(REPO/'research'));from native_runner import PACKAGE
    if PACKAGE.name!=RUNTIME_VERSION:raise SystemExit(f'The WebGPU runtime must be {RUNTIME_VERSION}; native_runner.PACKAGE is {PACKAGE}')
    path=PACKAGE/'runtimes'/host/'native'/RUNTIME_FILE[host]
    if not path.is_file():raise SystemExit(f'Restore the WebGPU runtime first (dotnet restore research/GpuProbe/GpuProbe.csproj): {path}')
    return path


def loaded_runtime(host,folder):
    # Windows loads the runtime from the gates' folder (the build or payload run folder copies it there); the other
    # hosts link the package file. Either way it must be the package's file, byte for byte.
    package=package_runtime(host);beside=Path(folder)/RUNTIME_FILE[host]
    used=beside if host.startswith('win-') else package
    if not used.is_file():raise SystemExit(f'The WebGPU runtime is not beside the gates: {used}')
    if sha(used)!=sha(package):raise SystemExit(f'{used} is not the CodeBrix.WebGpu.Native.MitLicenseForever {RUNTIME_VERSION} file ({package})')
    return dict(path=str(used),package=str(package),version=RUNTIME_VERSION,sha256=sha(used))


def sdk_version():
    try:found=subprocess.run(['dotnet','--version'],capture_output=True,text=True,cwd=REPO).stdout.strip()
    except FileNotFoundError:found=None
    if found!=SDK_VERSION:raise SystemExit(f'The .NET SDK must be {SDK_VERSION} (dotnet --version: {found})')
    return found


def git(*args):return subprocess.run(['git',*args],cwd=REPO,capture_output=True,text=True).stdout


def manifest(step,host,folder,gates):
    return dict(step=step,created=datetime.datetime.now().isoformat(timespec='seconds'),
                policy='native/DEVICE.md "Validation reference policy"; docs/TESTING-*.md "MANDATORY TEST RULES"',
                repoCommit=git('rev-parse','HEAD').strip(),workingTreeChanges=git('status','--porcelain').splitlines(),
                host=dict(rid=host,system=platform.system(),release=platform.release(),machine=platform.machine(),node=platform.node()),
                dotnetSdk=sdk_version(),webgpuRuntime=loaded_runtime(host,folder),
                gateSha256={gate_binary(folder,g).name:sha(gate_binary(folder,g)) for g in gates})


def same_commit(previous,current,what):
    if previous['repoCommit']!=current['repoCommit']:
        raise SystemExit(f'{what} was made at {previous["repoCommit"]}, this tree is {current["repoCommit"]}: '
                         'every step of one exchange must use the same commit')


def run_gate(binary,folder,env,name,backend=None):
    folder.mkdir(parents=True,exist_ok=True)
    environment={k:v for k,v in os.environ.items() if not k.startswith(('CODEBRIX_CYCLES_SCENE_','CODEBRIX_CYCLES_REFERENCE_','CODEBRIX_CYCLES_STAGE_'))}
    environment.update(env)
    if backend:environment['CODEBRIX_CYCLES_WEBGPU_BACKEND']=backend
    if os.name=='nt':environment['PATH']=os.pathsep.join([str(binary.parent),environment.get('PATH','')])
    started=datetime.datetime.now()
    result=subprocess.run([str(binary),str(folder/'run')],capture_output=True,text=True,env=environment,cwd=REPO)
    (folder.parent/(name+'.stdout.txt')).write_text(result.stdout);(folder.parent/(name+'.stderr.txt')).write_text(result.stderr)
    rows=[json.loads(l) for l in result.stdout.splitlines() if l.strip().startswith('{"passed"')]
    errors=[l for l in result.stderr.strip().splitlines() if l.strip() and not l.startswith(('[OpenColorIO','Passed','Rendered'))]
    return dict(exit=result.returncode,seconds=round((datetime.datetime.now()-started).total_seconds()),
                environment={k:v for k,v in env.items()},report=rows[-1] if result.returncode==0 and rows else None,
                failure=None if result.returncode==0 else (errors[-1] if errors else f'exit {result.returncode}'))


def android_reference(kind,gate,scenes,output,stages=None):
    command=[sys.executable,str(TOOLS/'android_reference.py'),kind,'--bin',str(LINUX_BIN),'--scenes',str(scenes),'--gate',gate]
    command+=['--output',str(output),'--label',gate] if kind=='scene-reference' else ['--stages',str(stages),'--output',str(output)]
    result=subprocess.run(command,capture_output=True,text=True,cwd=REPO)
    name=Path(output).name.removesuffix('.json')
    (Path(output).parent/(name+'.'+kind+'.log')).write_text(result.stdout+'\n--- stderr ---\n'+result.stderr)
    return result.returncode


def finish(out,record):
    save(out/'manifest.json',record)
    for gate,row in record['gates'].items():log(f'{gate}: '+('ok' if row.get('ok') else 'FAILED: '+str(row.get('failure'))))
    return 0 if all(row.get('ok') for row in record['gates'].values()) else 1


def device_dump(args):
    host=rid();folder=(args.bin or default_bin(host)).resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=manifest('device-dump',host,folder,GATES);record['backend']=args.backend;record['gates']={}
    for gate in GATES:
        log(f'{gate}: device pass');binary=gate_binary(folder,gate)
        if gate=='mnee':
            stages=out/gate/'stages.bin';(out/gate).mkdir(parents=True)
            row=run_gate(binary,out/gate/'record',{'CODEBRIX_CYCLES_STAGE_RECORD':str(stages)},'record',args.backend)
            if not row['exit']:
                # The record pass writes the scene tables beside the record (PREFIX.scenes).
                row['stagesSha256']=sha(stages);(out/gate/'stages.bin.sha256').write_text(row['stagesSha256']+'  stages.bin\n')
        else:
            row=run_gate(binary,out/gate/'dump',{'CODEBRIX_CYCLES_SCENE_DUMP':str(out/gate/'scenes')},'dump',args.backend)
        row['ok']=row['exit']==0;record['gates'][gate]=row
    return finish(out,record)


def linux_reference(args):
    host=rid()
    if host!='linux-x64':raise SystemExit(f'The glibc reference is computed on the Linux x64 machine; this host is {host}')
    dump=args.dump.resolve();previous=load(dump/'manifest.json');out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=manifest('linux-reference',host,LINUX_BIN,GATES);same_commit(previous,record,dump);record['device']=previous['host'];record['gates']={}
    for gate,device in previous['gates'].items():
        if not device.get('ok'):record['gates'][gate]=dict(ok=False,failure='device pass failed: '+str(device.get('failure')));continue
        log(f'{gate}: Linux x64');(out/gate).mkdir(parents=True)
        if gate=='mnee':
            stages=dump/gate/'stages.bin'
            if sha(stages)!=device['stagesSha256']:raise SystemExit(f'{stages} differs from the record pass (SHA-256)')
            code=android_reference('stage-judge',gate,Path(str(stages)+'.scenes'),out/gate/'mnee.stage-judge.json',stages)
            verdict=load(out/gate/'mnee.stage-judge.json') if (out/gate/'mnee.stage-judge.json').exists() else None
            # The device's record pass already compared its GPU with its own (local) CPU; Linux decides.
            ok=code==0 and bool(verdict and verdict.get('passed'))
            record['gates'][gate]=dict(ok=ok,final=True,stagesSha256=device['stagesSha256'],linuxStageJudge=brief(verdict),
                                       failure=None if ok else (verdict or {}).get('failure') or f'stage judge exit {code}')
        else:
            code=android_reference('scene-reference',gate,dump/gate/'scenes',out/gate/'reference')
            provenance=out/gate/'reference'/f'{gate}.provenance.json'
            record['gates'][gate]=dict(ok=code==0 and provenance.exists(),referenceProvenance=brief(load(provenance)) if provenance.exists() else None,
                                       failure=None if code==0 else f'scene-reference exit {code} (see {gate}/reference.scene-reference.log)')
    return finish(out,record)


def device_import(args):
    host=rid();folder=(args.bin or default_bin(host)).resolve();dump=args.dump.resolve();reference=args.reference.resolve()
    previous,linux=load(dump/'manifest.json'),load(reference/'manifest.json')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=manifest('device-import',host,folder,SCENE_GATES);same_commit(previous,record,dump);same_commit(linux,record,reference)
    if record['gateSha256']!={k:v for k,v in previous['gateSha256'].items() if k in record['gateSha256']}:
        raise SystemExit('The gate binaries changed since device-dump')
    record['backend']=args.backend;record['gates']={}
    for gate in SCENE_GATES:
        if not linux['gates'].get(gate,{}).get('ok'):record['gates'][gate]=dict(ok=False,failure='no Linux reference: '+str(linux['gates'].get(gate,{}).get('failure')));continue
        log(f'{gate}: GPU against the glibc reference')
        env={'CODEBRIX_CYCLES_REFERENCE_IMPORT':str(reference/gate/'reference'/'references'/gate),'CODEBRIX_CYCLES_SCENE_VERIFY':str(dump/gate/'scenes')}
        row=run_gate(gate_binary(folder,gate),out/gate/'import',env,'import',args.backend)
        stages=out/gate/'import'/'run.stages.bin'
        if gate=='microfacet_render' and stages.exists():
            row['stagesSha256']=sha(stages);(out/gate/'import'/'run.stages.bin.sha256').write_text(row['stagesSha256']+'  run.stages.bin\n')
        row['ok']=row['exit']==0;record['gates'][gate]=row
    return finish(out,record)


def linux_judge(args):
    host=rid()
    if host!='linux-x64':raise SystemExit(f'The stage judge runs on the Linux x64 machine; this host is {host}')
    dump,reference,run=args.dump.resolve(),args.reference.resolve(),args.run.resolve()
    first,linux,device=load(dump/'manifest.json'),load(reference/'manifest.json'),load(run/'manifest.json')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=manifest('linux-judge',host,LINUX_BIN,GATES)
    for previous,where in [(first,dump),(linux,reference),(device,run)]:same_commit(previous,record,where)
    record['device']=first['host'];record['gates']={}
    record['gates']['mnee']=dict(linux['gates'].get('mnee',dict(ok=False,failure='not in the Linux reference step')))
    for gate in SCENE_GATES:
        row=dict(device['gates'].get(gate,dict(ok=False,failure='not in the device import step')))
        if row.get('ok') and gate=='microfacet_render':
            stages=run/gate/'import'/'run.stages.bin'
            if sha(stages)!=row.get('stagesSha256'):raise SystemExit(f'{stages} differs from the import pass (SHA-256)')
            log(f'{gate}: Linux stage judge');(out/gate).mkdir(parents=True)
            code=android_reference('stage-judge',gate,dump/gate/'scenes',out/gate/f'{gate}.stage-judge.json',stages)
            verdict=load(out/gate/f'{gate}.stage-judge.json') if (out/gate/f'{gate}.stage-judge.json').exists() else None
            row=dict(row,linuxStageJudge=brief(verdict),ok=code==0 and bool(verdict and verdict.get('passed')))
            if not row['ok']:row['failure']=(verdict or {}).get('failure') or f'stage judge exit {code}'
        record['gates'][gate]=row
    record['passed']=all(record['gates'][g].get('ok') for g in GATES)
    code=finish(out,record)
    if args.evidence:
        evidence=args.evidence.resolve();evidence.mkdir(parents=True,exist_ok=False);skipped={}
        for name,source in [('out-1',dump),('in-1',reference),('out-2',run),('in-2',out)]:
            def ignore(directory,names):
                large=[n for n in names if n.endswith(TRANSPORT_ONLY)]
                for n in large:skipped[str(Path(name)/Path(directory).relative_to(source)/n)]=sha(Path(directory)/n)
                return large
            shutil.copytree(source,evidence/name,ignore=ignore)
        save(evidence/'transport-only.json',dict(note='Stage records left out of the repository; identified by SHA-256',files=skipped))
        log(f'evidence copy: {evidence}')
    log(('PASSED' if record['passed'] else 'FAILED')+f' ({out/"manifest.json"})')
    return code


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    steps=parser.add_subparsers(dest='step',required=True)
    for name in ['device-dump','device-import']:
        step=steps.add_parser(name);step.add_argument('--output',type=Path,required=True)
        step.add_argument('--bin',type=Path,help='Folder of the gate executables (default: this host\'s build; on Windows a payload run folder also works)')
        step.add_argument('--backend',help='CODEBRIX_CYCLES_WEBGPU_BACKEND for the device passes (Windows: d3d12 or vulkan)')
        if name=='device-import':step.add_argument('--dump',type=Path,required=True);step.add_argument('--reference',type=Path,required=True)
    step=steps.add_parser('linux-reference');step.add_argument('--dump',type=Path,required=True);step.add_argument('--output',type=Path,required=True)
    step=steps.add_parser('linux-judge')
    for name in ['--dump','--reference','--run','--output']:step.add_argument(name,type=Path,required=True)
    step.add_argument('--evidence',type=Path,help='Also write a copy for native/evidence/ without the stage records')
    args=parser.parse_args()
    if args.step in ('device-dump','device-import') and args.backend is None and rid().startswith('win-'):args.backend='d3d12'
    return dict(zip(['device-dump','linux-reference','device-import','linux-judge'],[device_dump,linux_reference,device_import,linux_judge]))[args.step](args)


if __name__=='__main__':raise SystemExit(main())
