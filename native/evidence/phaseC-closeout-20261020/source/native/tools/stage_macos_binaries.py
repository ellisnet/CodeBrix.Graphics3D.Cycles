#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Stage the reviewed osx-arm64 deliverable in native/binaries/osx-arm64.

    python3 native/tools/stage_macos_binaries.py --gate native/work/<passed gate> --oracles native/work/oracles/<run>

runtime/    stripped host and dependency dylibs plus the Metal kernel sources
unstripped/ the same dylibs before stripping (symbol tables retained)
symbols/    dSYM for the host library (line tables; dependencies carry no DWARF)
provenance.json records inputs, toolchain, options, hashes, UUID mapping and the
exact approved payload that tools/licensing/audit.py accepts. The runtime payload
is then copied to a temporary directory and rendered with on CPU and Metal.
"""
import argparse,importlib.util,json,re,shutil,subprocess,sys,tempfile,time
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'tools/dependencies'))
from macos_checks import linked,tool
spec=importlib.util.spec_from_file_location('macos_dependencies',REPO/'tools/dependencies/build_macos.py')
deps=importlib.util.module_from_spec(spec);spec.loader.exec_module(deps)
sha,save=deps.sha,deps.save
BASE=REPO/'native/work/macos/osx-arm64';BUILD=BASE/'host/build';PREFIX=BASE/'dependencies/install'
TARGET=REPO/'native/binaries/osx-arm64';HOST='libcodebrix_cycles_host.dylib'
# Shipped dylib name prefix -> third_party/dependencies.json component.
COMPONENTS=[('libcodebrix_cycles_host','cycles'),('libOpenImageIO','oiio'),('libOpenColorIO','ocio'),('libOpenEXR','openexr'),
 ('libIex','openexr'),('libIlmThread','openexr'),('libImath','imath'),('libboost_','boost'),('libexpat','expat'),
 ('libpugixml','pugixml'),('libtbb','tbb'),('libzstd','zstd'),('libz.','zlib')]
STATIC={'cycles':['Cycles (with mikktspace and sky)','CodeBrix host'],'ocio':['yaml-cpp','pystring','minizip','ocio-embedded'],
 'oiio':['fmt','robin-map','oiio-embedded']}


def uuid(path):return re.findall(r'UUID: (\S+)',tool('dwarfdump','--uuid',str(path)))


def sign(path):subprocess.run(['codesign','-s','-','-f',str(path)],check=True,capture_output=True)


def rpaths(path):return re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (\S+)',tool('otool','-l',str(path)))


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


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--gate',type=Path,required=True);p.add_argument('--oracles',type=Path,required=True);a=p.parse_args();a.gate,a.oracles=a.gate.resolve(),a.oracles.resolve()
 status=json.loads((BASE/'host/status.json').read_text());invocation=json.loads((BASE/'host/invocation.json').read_text())
 gate=json.loads((a.gate/'status.json').read_text());oracle_run=json.loads((a.oracles/'run.json').read_text())
 if not (status['passed'] and gate['passed']):raise RuntimeError('Stage only a passed build and gate')
 if any(sha(BUILD/n)!=d for n,d in status['artifacts'].items()):raise RuntimeError('Build artifacts changed since the build recorded them')
 host_sha=sha(BUILD/'lib'/HOST)
 if oracle_run['common']['hostLibrarySha256']!=host_sha:raise RuntimeError('Oracle run used a different host library')
 if TARGET.exists() and any(p.name not in ('README.md',) for p in TARGET.iterdir()):raise RuntimeError(f'{TARGET} already holds a payload; review and remove it first')
 kernel=json.loads((BASE/'host/kernel-sources.json').read_text())
 runtime,unstripped,symbols=TARGET/'runtime',TARGET/'unstripped',TARGET/'symbols'
 for d in (runtime,unstripped,symbols):d.mkdir(parents=True)
 binaries={HOST:BUILD/'lib'/HOST,**{leaf:PREFIX/rel for leaf,rel in status['dependencyClosure'].items()}}
 files={};mapping={}
 for leaf,source in binaries.items():
  prepare(source,unstripped/leaf,leaf==HOST)
  shutil.copyfile(unstripped/leaf,runtime/leaf);(runtime/leaf).chmod(0o755)
  subprocess.run(['strip','-x','-S',str(runtime/leaf)],check=True,capture_output=True);sign(runtime/leaf)
  subprocess.run(['codesign','--verify',str(runtime/leaf)],check=True)
  entry=dict(runtime=f'runtime/{leaf}',unstripped=f'unstripped/{leaf}',uuid=uuid(runtime/leaf),buildSource=str(source.relative_to(REPO)))
  if uuid(unstripped/leaf)!=entry['uuid']:raise RuntimeError(leaf+' UUID changed between unstripped and runtime copies')
  if leaf==HOST:
   bundle=symbols/(leaf+'.dSYM')
   # Object paths in the debug map are repository-relative (-oso_prefix).
   subprocess.run(['dsymutil','--oso-prepend-path',str(REPO),str(source),'-o',str(bundle)],check=True,capture_output=True)
   if uuid(bundle)!=entry['uuid']:raise RuntimeError('dSYM UUID does not match the host library')
   entry['symbols']=f'symbols/{leaf}.dSYM'
  else:entry['symbols']=None
  mapping[leaf]=entry
 copies={}
 for name,digest in kernel['files'].items():
  target=runtime/kernel['root']/name;target.parent.mkdir(parents=True,exist_ok=True)
  shutil.copyfile(REPO/'third_party/cycles/src'/name,target)
  if sha(target)!=digest:raise RuntimeError('Kernel source differs from the staged build closure: '+name)
  copies[str(target.relative_to(TARGET))]=name
 payload={str(f.relative_to(TARGET)):sha(f) for f in sorted(TARGET.rglob('*')) if f.is_file() and f.name!='README.md'}
 for leaf,entry in mapping.items():
  entry['runtimeSha256']=payload[entry['runtime']];entry['unstrippedSha256']=payload[entry['unstripped']]
  entry['linked']=linked(runtime/leaf)
 manifest=json.loads((REPO/'third_party/dependencies.json').read_text());by_id={c['id']:c for c in manifest['components']}
 notices={}
 for leaf in mapping:
  component=next(c for prefix,c in COMPONENTS if leaf.startswith(prefix))
  notices[leaf]=dict(component=component,license=by_id[component]['license'],notices=by_id[component]['notices'],
   staticallyIncluded=STATIC.get(component,[]))
 record=dict(
  rid='osx-arm64',created=time.strftime('%Y-%m-%dT%H:%M:%S%z'),deploymentTarget=deps.DEPLOYMENT_TARGET,codeSigning='ad-hoc (codesign -s -)',
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
   oracles=dict(directory=str(a.oracles.relative_to(REPO)),runSha256=sha(a.oracles/'run.json'),comparisonsPassed=oracle_run['comparisonsPassed'])),
  approvedPayload=payload,snapshotCopies=copies)
 # Repository paths are recorded relative to the checkout root.
 text=json.dumps(record,indent=2).replace(str(REPO)+'/','').replace(str(REPO),'.')
 (TARGET/'provenance.json').write_text(text+'\n')
 # The runtime payload alone must load and render: copy it, point a runner at it.
 with tempfile.TemporaryDirectory() as temporary:
  root=Path(temporary)/'payload';shutil.copytree(runtime,root)
  runner=root/'codebrix_cycles_oracle';shutil.copyfile(BASE/'oracle-build/codebrix_cycles_oracle',runner);runner.chmod(0o755)
  for old in rpaths(runner):subprocess.run(['install_name_tool','-delete_rpath',old,str(runner)],check=True)
  subprocess.run(['install_name_tool','-add_rpath','@executable_path',str(runner)],check=True);sign(runner)
  scene=REPO/'native/oracle/scenes/starter/diffuse-triangle.json';results={}
  for backend,env in [('cpu',{}),('metal',{'CYCLES_METALRT':'0','CYCLES_METAL_SPECIALIZATION_LEVEL':'0'})]:
   output=Path(temporary)/backend
   result=subprocess.run([str(runner),'--scene',str(scene),'--backend',backend,'--output',str(output)],capture_output=True,text=True,
    env={'PATH':'/usr/bin:/bin','HOME':str(Path.home()),'XDG_CACHE_HOME':str(Path(temporary)/'cache'),**env})
   if result.returncode:raise RuntimeError(f'Standalone payload failed on {backend}: {result.stderr[-500:]}')
   results[backend]=json.loads((output/'meta.json').read_text())['filmSha256'][0]
  expected=json.loads((a.oracles/'oracle/starter/diffuse-triangle/cpu-arm64-macos/meta.json').read_text())
  if results['cpu']!=expected['filmSha256'][0]:
   raise RuntimeError('Standalone CPU film differs from the oracle film')
 print(json.dumps(dict(staged=str(TARGET.relative_to(REPO)),files=len(payload),binaries=len(mapping),standaloneRender=results),indent=2))


if __name__=='__main__':main()
