#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Stage the reviewed osx-x64 deliverable in native/binaries/osx-x64.

    python3 native/tools/stage_macos_x64_binaries.py --gate native/work/<passed x64 gate> --oracles native/work/oracles/<run>

runtime/    stripped host and dependency dylibs
unstripped/ the same dylibs before stripping (symbol tables retained)
symbols/    dSYM for the host library (line tables; dependencies carry no DWARF)
provenance.json records inputs, toolchain, options, hashes, UUID mapping, the
Mach-O review and the exact approved payload that tools/licensing/audit.py accepts.

The shipped WebGPU runtime (libcodebrix_wgpu.dylib) is not part of this payload:
it is delivered by the CodeBrix.WebGpu.Native.MitLicenseForever package, and
provenance.json records its package, version and SHA-256. The runtime payload is
then copied to a temporary directory together with that runtime and rendered
with on CPU and WebGPU.

Mach-O review, for every staged dylib: otool -L names only /usr/lib,
/System/Library, @rpath libraries of this payload and @rpath/libcodebrix_wgpu.dylib;
lipo reports x86_64 alone; the minimum OS is 15.0; the only rpath is
@loader_path; codesign reports an ad-hoc signature (no team identifier) that
verifies. The host must not link Metal itself; only the runtime does.
"""
import argparse,importlib.util,json,platform,re,shutil,subprocess,sys,tempfile,time
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'tools/dependencies'))
from macos_checks import linked,tool
spec=importlib.util.spec_from_file_location('macos_x64_dependencies',REPO/'tools/dependencies/build_macos_x64.py')
deps=importlib.util.module_from_spec(spec);spec.loader.exec_module(deps)
sha,save=deps.sha,deps.save
BASE=REPO/'native/work/macos/osx-x64';BUILD=BASE/'host/build';PREFIX=BASE/'dependencies/install'
TARGET=REPO/'native/binaries/osx-x64';HOST='libcodebrix_cycles_host.dylib'
RUNTIME_NAME='libcodebrix_wgpu.dylib'
RUNTIME_SHA256='840a106d54fe479c255ecb98d51cee192a9044c3d0411fc1535f9ff0b8abbb82'
RUNTIME_PACKAGE=dict(id='CodeBrix.WebGpu.Native.MitLicenseForever',version='1.0.300.210',path='runtimes/osx-x64/native/'+RUNTIME_NAME,sha256=RUNTIME_SHA256)
# Shipped dylib name prefix -> third_party/dependencies.json component (as for osx-arm64).
COMPONENTS=[('libcodebrix_cycles_host','cycles'),('libOpenImageIO','oiio'),('libOpenColorIO','ocio'),('libOpenEXR','openexr'),
 ('libIex','openexr'),('libIlmThread','openexr'),('libImath','imath'),('libboost_','boost'),('libexpat','expat'),
 ('libpugixml','pugixml'),('libtbb','tbb'),('libzstd','zstd'),('libz.','zlib')]
STATIC={'cycles':['Cycles (with mikktspace and sky)','CodeBrix host','CodeBrix WGSL/WebGPU device'],'ocio':['yaml-cpp','pystring','minizip','ocio-embedded'],
 'oiio':['fmt','robin-map','oiio-embedded']}


def uuid(path):return re.findall(r'UUID: (\S+)',tool('dwarfdump','--uuid',str(path)))


def sign(path):subprocess.run(['codesign','-s','-','-f',str(path)],check=True,capture_output=True)


def rpaths(path):return re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (\S+)',tool('otool','-l',str(path)))


def macho_review(path,payload_names):
 """What must be true of every staged dylib (see the module docstring)."""
 names=linked(path)
 for name in names:
  if name.startswith(('/usr/lib/','/System/Library/')):continue
  if name.startswith('@rpath/') and (name[len('@rpath/'):] in payload_names or name=='@rpath/'+RUNTIME_NAME):continue
  raise RuntimeError(f'{path.name} links {name}, outside the OS, this payload and the WebGPU runtime')
 info=tool('lipo','-info',str(path))
 if tool('lipo','-archs',str(path)).split()!=['x86_64']:raise RuntimeError(f'{path.name}: {info}')
 minos=re.findall(r'minos (\S+)',tool('otool','-l',str(path)))
 if not minos or any(v!=deps.DEPLOYMENT_TARGET for v in minos):raise RuntimeError(f'{path.name} minimum OS {minos}')
 if set(rpaths(path))-{'@loader_path'}:raise RuntimeError(f'{path.name} rpaths are {rpaths(path)}')
 signature=subprocess.run(['codesign','-dv',str(path)],capture_output=True,text=True).stderr
 if 'Signature=adhoc' not in signature or re.search(r'TeamIdentifier=(?!not set)',signature):
  raise RuntimeError(f'{path.name} is not ad-hoc signed as expected: {signature}')
 subprocess.run(['codesign','--verify',str(path)],check=True)
 if path.name==HOST and any('/Metal.framework/' in n for n in names):raise RuntimeError('The host links Metal directly')
 return dict(linked=names,lipoInfo=info,minimumOs=minos,rpaths=rpaths(path),codesign=signature.strip().splitlines())


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
 p.add_argument('--gate',type=Path,required=True);p.add_argument('--oracles',type=Path,required=True);a=p.parse_args()
 if platform.system()!='Darwin' or platform.machine()!='x86_64':
  raise SystemExit(f'osx-x64: staging runs only on an Intel Mac; this host is {platform.system()} {platform.machine()}')
 a.gate,a.oracles=a.gate.resolve(),a.oracles.resolve()
 status=json.loads((BASE/'host/status.json').read_text());invocation=json.loads((BASE/'host/invocation.json').read_text())
 gate=json.loads((a.gate/'status.json').read_text());oracle_run=json.loads((a.oracles/'run.json').read_text())
 if not (status['passed'] and gate['passed'] and gate.get('rid')=='osx-x64'):raise RuntimeError('Stage only a passed osx-x64 build and gate')
 if any(sha(BUILD/n)!=d for n,d in status['artifacts'].items()):raise RuntimeError('Build artifacts changed since the build recorded them')
 host_sha=sha(BUILD/'lib'/HOST)
 if oracle_run['common']['hostLibrarySha256']!=host_sha:raise RuntimeError('Oracle run used a different host library')
 runtime_source=Path(invocation['runtimePath'])
 if sha(runtime_source)!=RUNTIME_SHA256:raise RuntimeError('The package runtime differs from the pinned shipped binary')
 if TARGET.exists() and any(p.name not in ('README.md',) for p in TARGET.iterdir()):raise RuntimeError(f'{TARGET} already holds a payload; review and remove it first')
 runtime,unstripped,symbols=TARGET/'runtime',TARGET/'unstripped',TARGET/'symbols'
 for d in (runtime,unstripped,symbols):d.mkdir(parents=True)
 binaries={HOST:BUILD/'lib'/HOST,**{leaf:PREFIX/rel for leaf,rel in status['dependencyClosure'].items()}}
 mapping={}
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
 names=set(mapping)
 review={leaf:macho_review(runtime/leaf,names) for leaf in mapping}
 payload={str(f.relative_to(TARGET)):sha(f) for f in sorted(TARGET.rglob('*')) if f.is_file() and f.name!='README.md'}
 for leaf,entry in mapping.items():
  entry['runtimeSha256']=payload[entry['runtime']];entry['unstrippedSha256']=payload[entry['unstripped']];entry['machoReview']=review[leaf]
 manifest=json.loads((REPO/'third_party/dependencies.json').read_text());by_id={c['id']:c for c in manifest['components']}
 notices={}
 for leaf in mapping:
  component=next(c for prefix,c in COMPONENTS if leaf.startswith(prefix))
  notices[leaf]=dict(component=component,license=by_id[component]['license'],notices=by_id[component]['notices'],staticallyIncluded=STATIC.get(component,[]))
 record=dict(
  rid='osx-x64',created=time.strftime('%Y-%m-%dT%H:%M:%S%z'),deploymentTarget=deps.DEPLOYMENT_TARGET,codeSigning='ad-hoc (codesign -s -)',
  scope='Reviewed osx-x64 product binaries: Cycles with the CodeBrix WGSL/WebGPU device and the CPU device behind the CodeBrix C API; no image file readers, no Metal device. The WebGPU runtime is an external package dependency.',
  sources=dict(cyclesRevision=json.loads((REPO/'third_party/cycles-import.json').read_text())['revision'],
   cyclesImportSha256=invocation['cyclesImportSha256'],dependencyImportsSha256=sha(REPO/'third_party/dependency-imports.json'),
   dependencyMarkerSha256=invocation['dependencyMarkerSha256'],adaptedFiles=invocation['adaptedFiles'],overlaySha256=invocation['overlaySha256'],
   overlayComparison=invocation['overlayComparison'],hostSourceSha256=invocation['hostSourceSha256'],recipeSha256=invocation['recipeSha256'],
   injectionSha256=invocation['injectionSha256'],sharedInjectionSha256=invocation['sharedInjectionSha256']),
  webGpuRuntime=dict(RUNTIME_PACKAGE,machoReview=dict(linked=linked(runtime_source),lipoInfo=tool('lipo','-info',str(runtime_source)),
   minimumOs=re.findall(r'minos (\S+)',tool('otool','-l',str(runtime_source))))),
  toolchain=invocation['identity'],buildCommands=invocation['commands'],skippedTargets=invocation['skippedTargets'],
  binaries=mapping,notices=dict(repositoryNotices='THIRD-PARTY-NOTICES.txt',libraries=notices),
  validation=dict(hostGate=dict(directory=str(a.gate.relative_to(REPO)),statusSha256=sha(a.gate/'status.json'),passed=gate['passed'],
    sameHostBitwiseFailures=gate['sameHostBitwiseFailures'],gpuPracticalFailures=gate['gpuPracticalFailures'],adapterReport=gate.get('adapterReport')),
   oracles=dict(directory=str(a.oracles.relative_to(REPO)),runSha256=sha(a.oracles/'run.json'),comparisonsPassed=oracle_run['comparisonsPassed'])),
  approvedPayload=payload,snapshotCopies={})
 # Repository paths are recorded relative to the checkout root.
 text=json.dumps(record,indent=2).replace(str(REPO)+'/','').replace(str(REPO),'.')
 (TARGET/'provenance.json').write_text(text+'\n')
 # The runtime payload plus the package runtime must load and render on their own.
 with tempfile.TemporaryDirectory() as temporary:
  root=Path(temporary)/'payload';shutil.copytree(runtime,root);shutil.copyfile(runtime_source,root/RUNTIME_NAME)
  runner=root/'codebrix_cycles_oracle';shutil.copyfile(BASE/'oracle-build/codebrix_cycles_oracle',runner);runner.chmod(0o755)
  for old in rpaths(runner):subprocess.run(['install_name_tool','-delete_rpath',old,str(runner)],check=True)
  subprocess.run(['install_name_tool','-add_rpath','@executable_path',str(runner)],check=True);sign(runner)
  scene=REPO/'native/oracle/scenes/starter/diffuse-triangle.json';results={}
  for backend in ['cpu','webgpu']:
   output=Path(temporary)/backend
   result=subprocess.run([str(runner),'--scene',str(scene),'--backend',backend,'--output',str(output)],capture_output=True,text=True,
    env={'PATH':'/usr/bin:/bin','HOME':str(Path.home())})
   if result.returncode:raise RuntimeError(f'Standalone payload failed on {backend}: {result.stderr[-500:]}')
   results[backend]=json.loads((output/'meta.json').read_text())['filmSha256'][0]
  expected=json.loads((a.oracles/'oracle/starter/diffuse-triangle/cpu-x64-macos/meta.json').read_text())
  if results['cpu']!=expected['filmSha256'][0]:raise RuntimeError('Standalone CPU film differs from the oracle film')
 print(json.dumps(dict(staged=str(TARGET.relative_to(REPO)),files=len(payload),binaries=len(mapping),standaloneRender=results),indent=2))


if __name__=='__main__':main()
