#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Stage the reviewed osx-arm64 deliverable in native/binaries/osx-arm64.

    python3 native/tools/stage_macos_binaries.py --gate native/work/<passed gate> --oracles native/work/oracles/<run>
    python3 native/tools/stage_macos_binaries.py --gate ... --oracles ... --output DIR   (scratch stage)

Stages the build in native/work/macos/osx-arm64 that the passed host gate and
oracle run validated, and never rebuilds: a rebuild is not byte-identical (object
timestamps and absolute paths in the debug map change the host's UUID and
signature), so only the validated files may be staged. The build itself records
the commit it was made from and the git status of the Cycles-with-Metal sources
(tools/licensing/metal_sources.py), when it starts (host/invocation.json) and when
it completes (host/status.json). A real stage refuses unless both records name the
same commit with a clean status, HEAD descends from that commit with no
Cycles-with-Metal source change since, and the working tree has none of its own;
provenance.json then names that commit as sourceCommit. So: commit, build, gate,
render the oracles, stage.

A scratch stage (--output, a folder outside the tracked tree: git-ignored under
the checkout, such as native/work/staging/osx-arm64, or outside it) leaves
native/binaries/ untouched. It may proceed with uncommitted changes or a build
that a real stage refuses (the reasons are recorded under buildSource), and then
records the SHA-256 of `git diff --binary` and the changed paths. It is never
approved: provenance.json says "approved": false and has an empty
approvedPayload, and the files it staged are listed in payloadFiles. The pack
driver packs .MacOSArm64 from it with -p:MacOSArm64Payload=DIR, and
tools/licensing/package_check.py --payload osx-arm64=DIR checks that package
against it and reports it as not publishable.

runtime/    stripped host and dependency dylibs, plus the Metal kernel sources in
            codebrix-cycles/source (byte-identical to third_party/cycles/src)
unstripped/ the same dylibs before stripping (symbol tables retained)
symbols/    dSYM for the host library (line tables; dependencies carry no DWARF)
provenance.json records inputs, toolchain, options, hashes, UUID mapping, the
standalone render and the exact approved payload that tools/licensing/audit.py
accepts. A file or folder whose name starts with '.' (such as Finder's .DS_Store)
is refused anywhere in the payload: git ignores it, so it could never be committed
with its approval. Before the record is written, the runtime payload is copied to
a temporary directory and rendered with on CPU and Metal; the CPU film must equal
the oracle run's film.
"""
import argparse,hashlib,importlib.util,json,platform,re,shutil,subprocess,sys,tempfile,time
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'tools/dependencies'))
from macos_checks import linked,tool
spec=importlib.util.spec_from_file_location('macos_dependencies',REPO/'tools/dependencies/build_macos.py')
deps=importlib.util.module_from_spec(spec);spec.loader.exec_module(deps)
sys.path.append(str(REPO/'tools/licensing'))
from metal_sources import METAL_SOURCES
sha,save=deps.sha,deps.save
RID='osx-arm64'
BASE=REPO/'native/work/macos/osx-arm64';BUILD=BASE/'host/build';PREFIX=BASE/'dependencies/install'
TARGET=REPO/'native/binaries'/RID;HOST='libcodebrix_cycles_host.dylib'
CHECK_SCENE='starter/diffuse-triangle'
# Shipped dylib name prefix -> third_party/dependencies.json component.
COMPONENTS=[('libcodebrix_cycles_host','cycles'),('libOpenImageDenoise','oidn'),('libOpenImageIO','oiio'),('libOpenColorIO','ocio'),
 ('libOpenEXR','openexr'),('libIex','openexr'),('libIlmThread','openexr'),('libImath','imath'),('libboost_','boost'),('libexpat','expat'),
 ('libpugixml','pugixml'),('libtbb','tbb'),('libzstd','zstd'),('libz.','zlib')]
# Embree is a static library linked into the host's CPU device (tools/dependencies/build.py, embree_options).
STATIC={'cycles':['Cycles (with mikktspace and sky)','CodeBrix host','embree'],'ocio':['yaml-cpp','pystring','minizip','ocio-embedded'],
 'oiio':['fmt','robin-map','oiio-embedded']}


def uuid(path):return re.findall(r'UUID: (\S+)',tool('dwarfdump','--uuid',str(path)))


def sign(path):subprocess.run(['codesign','-s','-','-f',str(path)],check=True,capture_output=True)


def rpaths(path):return re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (\S+)',tool('otool','-l',str(path)))


def git(*args):return subprocess.run(['git',*args],cwd=REPO,capture_output=True,text=True)


def prepare(source,destination,host):
 shutil.copyfile(source,destination);destination.chmod(0o755)
 # Build-tree rpaths (some dependency installs keep their prefix) never ship.
 for old in rpaths(destination):
  if old!='@loader_path':subprocess.run(['install_name_tool','-delete_rpath',old,str(destination)],check=True)
 if host and '@loader_path' not in rpaths(destination):
  subprocess.run(['install_name_tool','-add_rpath','@loader_path',str(destination)],check=True)
 identity=tool('otool','-D',str(destination)).splitlines()[-1]
 if identity!='@rpath/'+destination.name:raise RuntimeError(f'{destination.name} install name is {identity}')
 if not set(rpaths(destination))<={'@loader_path'} or (host and rpaths(destination)!=['@loader_path']):
  raise RuntimeError(f'{destination.name} rpaths are {rpaths(destination)}')
 sign(destination)


def build_source(invocation,status,dirty):
 """The commit the build recorded, and every reason a real stage of it is refused."""
 commit=invocation.get('sourceCommit');recorded=invocation.get('metalSourceStatus');problems=[]
 if not commit:
  problems.append('the build did not record its source commit; build again from a clean commit (tools/build_macos.py), then gate it and render the oracles')
 else:
  if recorded:problems.append('the build was made with uncommitted Cycles-with-Metal source changes: '+'; '.join(recorded))
  if status.get('sourceAtEnd')!=dict(sourceCommit=commit,metalSourceStatus=recorded):
   problems.append('the commit or the Cycles-with-Metal sources changed while the build ran')
  if git('merge-base','--is-ancestor',commit,'HEAD').returncode:problems.append(f'HEAD does not descend from the build commit {commit}')
  else:
   changed=git('diff','--name-only',commit,'HEAD','--',*METAL_SOURCES).stdout.split()
   if changed:problems.append(f'{len(changed)} Cycles-with-Metal source files changed since the build commit {commit}: '+', '.join(changed[:10]))
 if dirty:problems.append('Native sources have uncommitted changes; commit them first:\n'+dirty)
 return dict(sourceCommit=commit,metalSourceStatus=recorded,realStageRefusals=problems)


def approvable(payload,target):
 """The audit requires every approved file to be a tracked candidate, so a git-ignored path is
 recorded in `binaries` but not approved (none is: osx-arm64 commits all three folders in full)."""
 ignored=subprocess.run(['git','check-ignore','--stdin'],cwd=REPO,input='\n'.join(str(target.relative_to(REPO)/name) for name in payload),capture_output=True,text=True).stdout.split()
 return {name:digest for name,digest in payload.items() if str(target.relative_to(REPO)/name) not in ignored}


def scratch_target(path):
 """A scratch stage must not touch the tracked tree: outside the checkout, or a git-ignored folder in it."""
 path=path.resolve()
 if path==REPO or REPO in path.parents:
  relative=path.relative_to(REPO)
  if relative.parts[:2]==('native','binaries') or subprocess.run(['git','check-ignore','-q',str(relative/'provenance.json')],cwd=REPO).returncode!=0:
   raise RuntimeError(f'{path} is in the tracked tree; a scratch stage goes to a git-ignored folder (such as native/work/staging/{RID}) or outside the checkout')
 return path


def standalone_render(runtime,oracles):
 """The runtime payload alone must load and render: copy it, point a runner at it, clear the environment."""
 with tempfile.TemporaryDirectory() as temporary:
  root=Path(temporary)/'payload';shutil.copytree(runtime,root)
  runner=root/'codebrix_cycles_oracle';shutil.copyfile(BASE/'oracle-build/codebrix_cycles_oracle',runner);runner.chmod(0o755)
  for old in rpaths(runner):subprocess.run(['install_name_tool','-delete_rpath',old,str(runner)],check=True)
  subprocess.run(['install_name_tool','-add_rpath','@executable_path',str(runner)],check=True);sign(runner)
  scene=REPO/'native/oracle/scenes'/(CHECK_SCENE+'.json');results={}
  for backend,env in [('cpu',{}),('metal',{'CYCLES_METALRT':'0','CYCLES_METAL_SPECIALIZATION_LEVEL':'0'})]:
   output=Path(temporary)/backend
   result=subprocess.run([str(runner),'--scene',str(scene),'--backend',backend,'--output',str(output)],capture_output=True,text=True,
    env={'PATH':'/usr/bin:/bin','HOME':str(Path.home()),'XDG_CACHE_HOME':str(Path(temporary)/'cache'),**env})
   if result.returncode:raise RuntimeError(f'Standalone payload failed on {backend}: {result.stderr[-500:]}')
   results[backend]=json.loads((output/'meta.json').read_text())['filmSha256'][0]
 expected=json.loads((oracles/'oracle'/CHECK_SCENE/'cpu-arm64-macos/meta.json').read_text())['filmSha256'][0]
 if results['cpu']!=expected:raise RuntimeError(f'Standalone CPU film {results["cpu"]} differs from the oracle film {expected}')
 return dict(scene=CHECK_SCENE,filmSha256=results,cpuFilmEqualsOracleRun=True)


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--gate',type=Path,required=True);p.add_argument('--oracles',type=Path,required=True)
 p.add_argument('--output',type=Path,help=f'Stage into this scratch folder instead of native/binaries/{RID} (never approved)')
 a=p.parse_args();a.gate,a.oracles=a.gate.resolve(),a.oracles.resolve()
 if platform.system()!='Darwin' or platform.machine()!='arm64':
  raise SystemExit(f'{RID}: staging runs only on an Apple Silicon Mac; this host is {platform.system()} {platform.machine()}')
 scratch=a.output is not None
 target=scratch_target(a.output) if scratch else TARGET
 status=json.loads((BASE/'host/status.json').read_text());invocation=json.loads((BASE/'host/invocation.json').read_text())
 gate=json.loads((a.gate/'status.json').read_text());oracle_run=json.loads((a.oracles/'run.json').read_text())
 if not (status['passed'] and gate['passed']):raise RuntimeError('Stage only a passed build and gate')
 if any(sha(BUILD/n)!=d for n,d in status['artifacts'].items()):raise RuntimeError('Build artifacts changed since the build recorded them')
 host_sha=sha(BUILD/'lib'/HOST)
 if oracle_run['common']['hostLibrarySha256']!=host_sha:raise RuntimeError('Oracle run used a different host library')
 if target.exists() and any(target.iterdir()):raise RuntimeError(f'{target} already holds a payload; review and remove it first')
 dirty=git('status','--porcelain','--',*METAL_SOURCES).stdout.rstrip()
 source=build_source(invocation,status,dirty)
 if source['realStageRefusals'] and not scratch:raise RuntimeError('Refusing to stage: '+'\n'.join(source['realStageRefusals']))
 commit=git('rev-parse','HEAD').stdout.strip()
 diff=subprocess.run(['git','diff','--binary'],cwd=REPO,capture_output=True,check=True).stdout
 kernel=json.loads((BASE/'host/kernel-sources.json').read_text())
 runtime,unstripped,symbols=target/'runtime',target/'unstripped',target/'symbols'
 for d in (runtime,unstripped,symbols):d.mkdir(parents=True)
 binaries={HOST:BUILD/'lib'/HOST,**{leaf:PREFIX/rel for leaf,rel in status['dependencyClosure'].items()}}
 mapping={}
 for leaf,source_path in binaries.items():
  prepare(source_path,unstripped/leaf,leaf==HOST)
  shutil.copyfile(unstripped/leaf,runtime/leaf);(runtime/leaf).chmod(0o755)
  subprocess.run(['strip','-x','-S',str(runtime/leaf)],check=True,capture_output=True);sign(runtime/leaf)
  subprocess.run(['codesign','--verify',str(runtime/leaf)],check=True)
  entry=dict(runtime=f'runtime/{leaf}',unstripped=f'unstripped/{leaf}',uuid=uuid(runtime/leaf),buildSource=str(source_path.relative_to(REPO)))
  if uuid(unstripped/leaf)!=entry['uuid']:raise RuntimeError(leaf+' UUID changed between unstripped and runtime copies')
  if leaf==HOST:
   bundle=symbols/(leaf+'.dSYM')
   # Object paths in the debug map are repository-relative (-oso_prefix).
   subprocess.run(['dsymutil','--oso-prepend-path',str(REPO),str(source_path),'-o',str(bundle)],check=True,capture_output=True)
   if uuid(bundle)!=entry['uuid']:raise RuntimeError('dSYM UUID does not match the host library')
   entry['symbols']=f'symbols/{leaf}.dSYM'
  else:entry['symbols']=None
  mapping[leaf]=entry
 copies={}
 for name,digest in kernel['files'].items():
  copy=runtime/kernel['root']/name;copy.parent.mkdir(parents=True,exist_ok=True)
  shutil.copyfile(REPO/'third_party/cycles/src'/name,copy)
  if sha(copy)!=digest:raise RuntimeError('Kernel source differs from the staged build closure: '+name)
  copies[str(copy.relative_to(target))]=name
 hidden=[str(f.relative_to(target)) for f in sorted(target.rglob('*')) if any(part.startswith('.') for part in f.relative_to(target).parts)]
 if hidden:raise RuntimeError('Hidden files in the payload (git ignores them, so they cannot be approved); remove them and stage again: '+', '.join(hidden))
 payload={str(f.relative_to(target)):sha(f) for f in sorted(target.rglob('*')) if f.is_file()}
 for leaf,entry in mapping.items():
  entry['runtimeSha256']=payload[entry['runtime']];entry['unstrippedSha256']=payload[entry['unstripped']]
  entry['linked']=linked(runtime/leaf)
 manifest=json.loads((REPO/'third_party/dependencies.json').read_text());by_id={c['id']:c for c in manifest['components']}
 notices={}
 for leaf in mapping:
  component=next((c for prefix,c in COMPONENTS if leaf.startswith(prefix)),None)
  if component is None:raise RuntimeError(f'{leaf} has no component in COMPONENTS (third_party/dependencies.json)')
  notices[leaf]=dict(component=component,license=by_id[component]['license'],notices=by_id[component]['notices'],
   staticallyIncluded=STATIC.get(component,[]))
 rendered=standalone_render(runtime,a.oracles)
 record=dict(
  rid=RID,created=time.strftime('%Y-%m-%dT%H:%M:%S%z'),sourceCommit=source['sourceCommit'],stagedAtCommit=commit,
  deploymentTarget=deps.DEPLOYMENT_TARGET,codeSigning='ad-hoc (codesign -s -)',
  scope='Reviewed osx-arm64 product binaries: Cycles with its Metal and CPU devices behind the CodeBrix C API; no image file readers, no WebGPU device.',
  sources=dict(cyclesRevision=json.loads((REPO/'third_party/cycles-import.json').read_text())['revision'],
   cyclesImportSha256=invocation['cyclesImportSha256'],dependencyImportsSha256=sha(REPO/'third_party/dependency-imports.json'),
   dependencyMarkerSha256=invocation['dependencyMarkerSha256'],platformPatchSha256=invocation['platformPatchSha256'],
   adaptedFiles=invocation['adaptedFiles'],overlaySha256=invocation['overlaySha256'],hostSourceSha256=invocation['hostSourceSha256'],
   recipeSha256=invocation['recipeSha256'],injectionSha256=invocation['injectionSha256']),
  toolchain=invocation['identity'],buildCommands=invocation['commands'],skippedTargets=invocation['skippedTargets'],
  binaries=mapping,kernelSources=dict(root='runtime/'+kernel['root'],entry=kernel['entry'],files=len(copies),unresolvedIncludes=kernel['unresolvedIncludes']),
  notices=dict(repositoryNotices='THIRD-PARTY-NOTICES.txt',libraries=notices),
  validation=dict(hostGate=dict(directory=str(a.gate.relative_to(REPO)),statusSha256=sha(a.gate/'status.json'),passed=gate['passed'],
    sameHostBitwiseFailures=gate['sameHostBitwiseFailures'],metalPracticalFailures=gate['metalPracticalFailures']),
   oracles=dict(directory=str(a.oracles.relative_to(REPO)),runSha256=sha(a.oracles/'run.json'),comparisonsPassed=oracle_run['comparisonsPassed'],
    repoCommit=oracle_run['common']['machine'].get('repoCommit'),repoWorkingTreeClean=oracle_run['common']['machine'].get('repoWorkingTreeClean'))),
  standaloneRender=rendered,
  approvedPayload={} if scratch else approvable(payload,target),snapshotCopies={} if scratch else copies)
 if scratch:
  record.update(approved=False,scratchStage=True,payloadFiles=payload,buildSource=source,
   uncommittedChanges=dict(gitDiffBinarySha256=hashlib.sha256(diff).hexdigest(),gitDiffBytes=len(diff),
    nativeSourceStatus=dirty.splitlines()) if diff or dirty else None)
 # Repository paths are recorded relative to the checkout root.
 text=json.dumps(record,indent=2).replace(str(REPO)+'/','').replace(str(REPO),'.')
 (target/'provenance.json').write_text(text+'\n')
 print(json.dumps(dict(staged=str(target.relative_to(REPO)) if REPO in target.parents else str(target),commit=commit,sourceCommit=source['sourceCommit'],
  approved=not scratch,binaries=len(mapping),files=len(payload),standaloneRender=rendered,realStageRefusals=source['realStageRefusals']),indent=2))


if __name__=='__main__':main()
