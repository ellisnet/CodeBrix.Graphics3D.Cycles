#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Judge the Intel Mac's WGSL device gates against the Linux x64 glibc reference, in one command.

    python3 native/tools/macos_x64_reference_exchange.py --output native/work/macos-x64-exchange-<n> [--jobs 6]

The Intel Mac mini is a reference device (native/DEVICE.md, "Reference devices and the publish rule"). Its own
CPU reference uses Apple's libm, which is not correctly rounded, so three device gates that fail against it are
judged against Linux x64 glibc through the validation reference policy's exchanges (docs/TESTING-MACOS-X64.md,
step 7). This script runs all three end to end on this Mac, with the Linux side in a Docker container that has
the dedicated Linux machine's toolchain (native/tools/linux_reference/Dockerfile):

  light_tree         scene exchange: the Mac dumps its scene tables, Linux computes the glibc reference from them
                     (android_reference.py scene-reference), the Mac GPU runs against the imported reference
  microfacet_render  the same, plus the render stage exchange: the import run records every GPU stage, Linux judges
                     them (android_reference.py stage-judge)
  mnee               the MNEE stage-state exchange: the Mac records every lockstep check, Linux judges it

Before the exchanges it makes sure the image and the container exist (building or starting them), copies this
working tree's source into the container (git-visible files; native/evidence, native/binaries and native/work are
left out) and rebuilds the three Linux gates (the first run also builds the Linux dependency prefix, about 15
minutes). The Mac host must be built (python3 tools/build_macos_x64.py). Every gate result, the verdicts, the
binary hashes and the image identity go to OUTPUT/summary.json; the exit status is 0 only when all three pass.
"""
import argparse,datetime,hashlib,json,os,platform,shutil,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
TOOLS=REPO/'native/tools'
MAC_BIN=REPO/'native/work/macos/osx-x64/host/build/bin'
IMAGE='codebrix-cycles-linux-reference:trixie'
CONTAINER='codebrix-cycles-linux-reference'
VOLUME='codebrix-cycles-linux-reference'
LINUX_REPO='/work/repo'
LINUX_WORK=LINUX_REPO+'/native/work'
LINUX_BIN=LINUX_WORK+'/cycles-build/bin'
PACKAGE_ID='codebrix.webgpu.native.mitlicenseforever'
# The Linux passes: the RDTSCP shim (Docker Desktop's VM lacks the instruction) and lavapipe for device creation.
LINUX_ENV={'LD_PRELOAD':'/opt/codebrix/librdtscp_shim.so','CODEBRIX_CYCLES_WEBGPU_ADAPTER':'software'}
GATES=['light_tree','microfacet_render','mnee']


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def log(text):print(text,flush=True)


def docker_binary():
    found=shutil.which('docker') or str(Path.home()/'.docker/bin/docker')
    if not Path(found).exists():raise SystemExit('docker was not found (Docker Desktop: ~/.docker/bin/docker); start Docker Desktop first')
    return found


class Docker:
    def __init__(self,out):self.docker=docker_binary();self.out=out;self.commands=[]
    def run(self,args,name=None,check=True,input=None,timeout=None):
        command=[self.docker,*map(str,args)];self.commands.append(command)
        result=subprocess.run(command,capture_output=True,input=input,timeout=timeout)
        if name:
            (self.out/'logs').mkdir(exist_ok=True)
            (self.out/'logs'/(name+'.log')).write_bytes(result.stdout+b'\n--- stderr ---\n'+result.stderr)
        if check and result.returncode:raise RuntimeError(f'docker {args[0]} failed ({name or args}): '+result.stderr.decode(errors='replace')[-600:])
        return result
    def shell(self,script,name,env=None,check=True,timeout=None):
        envs=[v for k,value in (env or {}).items() for v in ('-e',f'{k}={value}')]
        return self.run(['exec',*envs,CONTAINER,'bash','-c',script],name,check=check,timeout=timeout)
    def copy_in(self,source,target):self.run(['exec',CONTAINER,'rm','-rf',target]);self.run(['cp',str(source),f'{CONTAINER}:{target}'])
    def copy_out(self,source,target):
        if Path(target).exists():shutil.rmtree(target)
        self.run(['cp',f'{CONTAINER}:{source}',str(target)])


def context_sha():
    # The image is rebuilt (and the container recreated; the build volume stays) when its sources change.
    digest=hashlib.sha256()
    for path in sorted((TOOLS/'linux_reference').iterdir()):digest.update(path.name.encode()+b'\0'+path.read_bytes())
    return digest.hexdigest()


def ensure_environment(docker,package):
    context=context_sha()
    built=docker.run(['image','inspect','-f','{{index .Config.Labels "codebrix.context"}}',IMAGE],check=False)
    if built.returncode or built.stdout.decode().strip()!=context:
        log(f'building the image {IMAGE} (native/tools/linux_reference/Dockerfile)')
        docker.run(['build','--label',f'codebrix.context={context}','-t',IMAGE,TOOLS/'linux_reference'],'image-build',timeout=3600)
        docker.run(['rm','-f',CONTAINER],check=False)
    state=docker.run(['container','inspect','-f','{{.State.Running}}',CONTAINER],check=False)
    if state.returncode:
        log(f'creating the container {CONTAINER} (volume {VOLUME})')
        docker.run(['run','-d','--name',CONTAINER,'-v',f'{VOLUME}:/work','-v',f'{package}:/root/.nuget/packages/{PACKAGE_ID}:ro',IMAGE,'sleep','infinity'],'container-create')
        docker.shell(f'mkdir -p {LINUX_REPO} && git config --global --add safe.directory {LINUX_REPO}','container-setup')
    elif state.stdout.strip()!=b'true':
        log(f'starting the container {CONTAINER}');docker.run(['start',CONTAINER],'container-start')
    image=docker.run(['image','inspect','-f','{{.Id}}',IMAGE]).stdout.decode().strip()
    toolchain=docker.shell('cat /etc/debian_version; c++ --version|head -1; clang++ --version|head -1; cmake --version|head -1; ldd --version|head -1; '
                           'dpkg-query -W mesa-vulkan-drivers libvulkan1','toolchain').stdout.decode().strip().splitlines()
    return dict(image=IMAGE,imageId=image,contextSha256=context,container=CONTAINER,volume=VOLUME,toolchain=toolchain)


def sync_source(docker):
    # Git-visible files of this working tree (tracked and untracked-but-not-ignored, so uncommitted work is
    # judged as it is), without the evidence, the staged binaries and the build trees.
    files=subprocess.run(['git','ls-files','-co','--exclude-standard','-z','--','.',':!native/evidence',':!native/binaries',':!native/work'],
                         cwd=REPO,capture_output=True,check=True).stdout
    archive=subprocess.run(['tar','--no-xattrs','--null','-T','-','-cf','-'],cwd=REPO,input=files,capture_output=True,check=True,
                           env={**os.environ,'COPYFILE_DISABLE':'1'}).stdout
    docker.run(['exec','-i',CONTAINER,'tar','-xf','-','-C',LINUX_REPO],'source-sync',input=archive)
    return dict(files=files.count(b'\0'),archiveBytes=len(archive),archiveSha256=hashlib.sha256(archive).hexdigest())


def build_linux(docker,jobs):
    deps=docker.shell(f'test -d {LINUX_WORK}/dependencies/install/lib && echo yes || echo no','dependencies-check').stdout.decode().strip()
    if deps!='yes':
        log('building the Linux dependency prefix (first run, about 5 minutes)')
        docker.shell(f'cd {LINUX_REPO} && python3 tools/dependencies/build.py --jobs {jobs}','linux-dependencies',timeout=7200)
    log('building the Linux gates')
    targets=' '.join(f'--target codebrix_cycles_{g}_gate' for g in GATES)
    docker.shell(f'cd {LINUX_REPO} && python3 native/tools/build_cycles.py --jobs {jobs} {targets}','linux-build',timeout=7200)
    hashes=docker.shell(f'cd {LINUX_BIN} && sha256sum '+' '.join(f'codebrix_cycles_{g}_gate' for g in GATES),'linux-hashes').stdout.decode()
    return {line.split()[1]:line.split()[0] for line in hashes.strip().splitlines()}


def mac_gate(gate,folder,env,name):
    folder.mkdir(parents=True,exist_ok=True);binary=MAC_BIN/f'codebrix_cycles_{gate}_gate'
    result=subprocess.run([str(binary),str(folder/'run')],capture_output=True,text=True,env={**os.environ,**env})
    (folder.parent/(name+'.stdout.txt')).write_text(result.stdout);(folder.parent/(name+'.stderr.txt')).write_text(result.stderr)
    last=[line for line in result.stdout.strip().splitlines() if line.startswith('{"passed"')]
    report=json.loads(last[-1]) if result.returncode==0 and last else None
    errors=[l for l in result.stderr.strip().splitlines() if l.strip() and not l.startswith(('[OpenColorIO','Passed','Rendered'))]
    return dict(exit=result.returncode,report=report,failure=None if result.returncode==0 else (errors[-1] if errors else f'exit {result.returncode}'))


def judge(docker,kind,gate,scenes,stages,linux_out):
    if kind=='scene-reference':
        script=(f'cd {LINUX_REPO} && rm -rf {linux_out} && python3 native/tools/android_reference.py scene-reference --bin {LINUX_BIN} '
                f'--scenes {scenes} --output {linux_out} --label {gate} --gate {gate}')
    else:
        script=(f'cd {LINUX_REPO} && mkdir -p {linux_out} && python3 native/tools/android_reference.py stage-judge --bin {LINUX_BIN} '
                f'--scenes {scenes} --stages {stages} --gate {gate} --output {linux_out}/{gate}.stage-judge.json')
    result=docker.shell(script,f'{gate}-linux-{kind}',env=LINUX_ENV,check=False,timeout=7200)
    return result.returncode


def exchange(docker,gate,out,run):
    folder=out/gate;folder.mkdir(parents=True);row=dict(gate=gate)
    remote=f'{LINUX_WORK}/mac-exchange/{run}/{gate}'
    docker.shell(f'rm -rf {remote} && mkdir -p {remote}','remote-'+gate)
    if gate=='mnee':
        # Device pass: record every lockstep check and the scene tables the GPU used.
        stages=folder/'mnee.stages.bin'
        row['macRecord']=mac_gate(gate,folder/'record',{'CODEBRIX_CYCLES_STAGE_RECORD':str(stages)},'mac-record')
        if row['macRecord']['exit']:row.update(passed=False,failure='Mac record pass: '+row['macRecord']['failure']);return row
        row['stagesSha256']=sha(stages)
        docker.copy_in(stages,f'{remote}/stages.bin');docker.copy_in(Path(str(stages)+'.scenes'),f'{remote}/scenes')
        code=judge(docker,'stage-judge',gate,f'{remote}/scenes',f'{remote}/stages.bin',f'{remote}/judge')
        docker.copy_out(f'{remote}/judge',folder/'linux-judge')
        verdict=json.loads((folder/'linux-judge'/f'{gate}.stage-judge.json').read_text()) if (folder/'linux-judge'/f'{gate}.stage-judge.json').exists() else None
        row['linuxStageJudge']=verdict
        row.update(passed=code==0 and bool(verdict and verdict.get('passed')),failure=None if code==0 else (verdict or {}).get('failure') or 'Linux stage judge failed')
        return row
    # light_tree and microfacet_render: the device-scene flow.
    scenes=folder/'mac-scenes'
    row['macDump']=mac_gate(gate,folder/'dump',{'CODEBRIX_CYCLES_SCENE_DUMP':str(scenes)},'mac-dump')
    if row['macDump']['exit']:row.update(passed=False,failure='Mac scene dump: '+row['macDump']['failure']);return row
    docker.copy_in(scenes,f'{remote}/scenes')
    code=judge(docker,'scene-reference',gate,f'{remote}/scenes',None,f'{remote}/reference')
    if code:row.update(passed=False,failure='Linux scene-reference failed (see logs/)');return row
    docker.copy_out(f'{remote}/reference',folder/'linux-reference')
    row['referenceProvenance']=json.loads((folder/'linux-reference'/f'{gate}.provenance.json').read_text())
    references=folder/'linux-reference'/'references'/gate
    row['macImport']=mac_gate(gate,folder/'import',{'CODEBRIX_CYCLES_REFERENCE_IMPORT':str(references),'CODEBRIX_CYCLES_SCENE_VERIFY':str(scenes)},'mac-import')
    if row['macImport']['exit']:row.update(passed=False,failure='Mac GPU against the glibc reference: '+row['macImport']['failure']);return row
    if gate=='microfacet_render':
        stages=folder/'import'/'run.stages.bin';row['stagesSha256']=sha(stages)
        docker.copy_in(stages,f'{remote}/stages.bin')
        code=judge(docker,'stage-judge',gate,f'{remote}/scenes',f'{remote}/stages.bin',f'{remote}/judge')
        docker.copy_out(f'{remote}/judge',folder/'linux-judge')
        verdict=json.loads((folder/'linux-judge'/f'{gate}.stage-judge.json').read_text()) if (folder/'linux-judge'/f'{gate}.stage-judge.json').exists() else None
        row['linuxStageJudge']=verdict
        if code or not (verdict and verdict.get('passed')):row.update(passed=False,failure=(verdict or {}).get('failure') or 'Linux stage judge failed');return row
    row.update(passed=True,failure=None);return row


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--jobs',type=int,default=6)
    parser.add_argument('--gate',action='append',choices=GATES,help='Run only these gates (default: all three)')
    args=parser.parse_args()
    if platform.system()!='Darwin' or platform.machine()!='x86_64':
        raise SystemExit(f'This runs on the Intel Mac reference device; this host is {platform.system()} {platform.machine()}')
    for gate in GATES:
        if not (MAC_BIN/f'codebrix_cycles_{gate}_gate').exists():raise SystemExit('Build the Mac host first: python3 tools/build_macos_x64.py')
    sys.path.insert(0,str(REPO/'research'));from native_runner import PACKAGE
    if not PACKAGE.is_dir():raise SystemExit(f'Restore the WebGPU runtime first (dotnet restore research/GpuProbe/GpuProbe.csproj): {PACKAGE}')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    docker=Docker(out);run=out.name;started=datetime.datetime.now()
    summary=dict(created=started.isoformat(timespec='seconds'),policy='native/DEVICE.md "Validation reference policy", "Reference devices and the publish rule"',
                 repoCommit=subprocess.run(['git','rev-parse','HEAD'],cwd=REPO,capture_output=True,text=True).stdout.strip(),
                 workingTreeChanges=subprocess.run(['git','status','--porcelain'],cwd=REPO,capture_output=True,text=True).stdout.splitlines(),
                 macGateSha256={f'codebrix_cycles_{g}_gate':sha(MAC_BIN/f'codebrix_cycles_{g}_gate') for g in GATES},
                 linuxEnvironment=LINUX_ENV,gates={},passed=False)
    try:
        summary['environment']=ensure_environment(docker,PACKAGE.parent)
        summary['sourceSync']=sync_source(docker)
        summary['linuxGateSha256']=build_linux(docker,args.jobs)
        for gate in args.gate or GATES:
            log(f'{gate}: exchanging');row=exchange(docker,gate,out,run);summary['gates'][gate]=row
            log(f'{gate}: {"PASSED" if row["passed"] else "FAILED: "+str(row["failure"])}')
        summary['passed']=all(row['passed'] for row in summary['gates'].values()) and len(summary['gates'])==len(args.gate or GATES)
    except Exception as error:
        summary['error']=str(error);log('ERROR: '+str(error))
    finally:
        summary['seconds']=round((datetime.datetime.now()-started).total_seconds());summary['dockerCommands']=docker.commands
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    log(f'{"PASSED" if summary["passed"] else "FAILED"}: '+', '.join(f'{g} {"pass" if r["passed"] else "FAIL"}' for g,r in summary['gates'].items())+f' ({summary["seconds"]} s; {out/"summary.json"})')
    return 0 if summary['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
