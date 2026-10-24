#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Stage the reviewed Windows deliverable in native/binaries/<rid>.

    python -X utf8 native/tools/stage_windows_binaries.py --rid win-x64 --gate native/work/<passed gate> [--gate <second backend>] [--oracles <run>]
    python -X utf8 native/tools/stage_windows_binaries.py --rid win-arm64

runtime/    the host DLL, every DLL it loads from the dependency prefix, and the
            pinned DXC compiler with its license text (third_party/dxc-import.json)
symbols/    PDBs of the host and the oracle runner (line tables)
tests/      the executables the host gate runs, so a device can run the gate from
            this payload without building anything (win-arm64 is cross-built)
provenance.json records inputs, toolchain, options, hashes, the PE review, the
validation it was staged from and the exact approved payload that
tools/licensing/audit.py accepts.

Windows binaries carry no symbol tables (debug information is in the PDBs), so
there is no separate unstripped copy: the runtime files are the unstripped
binaries. The shipped WebGPU runtime (codebrix_wgpu.dll) is not part of the
payload; it is delivered by the CodeBrix.WebGpu.Native.MitLicenseForever package,
and provenance.json records its package, version and SHA-256. On a machine that
can run the RID, the runtime payload is copied to a temporary directory with that
runtime and renders the example scene on the CPU and GPU backends.

A win-x64 payload requires a passed host gate of the same build (--gate, one per
backend run). A win-arm64 payload is cross-built: it is staged unvalidated
(validation.status "pending-device") and the ARM64 device runs the gate from it
(native/tools/windows_host_gate.py --payload).
"""
import argparse,datetime,json,os,platform,re,shutil,subprocess,sys,tempfile
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'));sys.path.insert(0,str(REPO/'tools'))
import windows_toolchain as wt
import windows_dxc
sys.path.insert(0,str(REPO/'tools/dependencies'))
import build_windows as deps
import build_cycles_windows as recipe
sha,save=wt.sha,deps.save
HOST='codebrix_cycles_host.dll'
# The gate list's executables (windows_host_gate.py) and the oracle runner.
TESTS=['codebrix_host_'+n for n in ['cpu_test','gpu_test','concurrency_test','abi_test','example_test','session_reference','session64_reference']]+['codebrix_cycles_oracle']
SYMBOLS=['codebrix_cycles_host','codebrix_cycles_oracle']
# Shipped DLL name prefix -> third_party/dependencies.json component.
COMPONENTS=[('codebrix_cycles_host','cycles'),('OpenImageIO','oiio'),('OpenColorIO','ocio'),('OpenEXR','openexr'),('Iex','openexr'),
 ('IlmThread','openexr'),('Imath','imath'),('boost_','boost'),('libexpat','expat'),('pugixml','pugixml'),('tbb','tbb'),('zstd','zstd'),
 ('zlib','zlib'),('dxcompiler','dxc')]
STATIC={'cycles':['Cycles (with mikktspace and sky)','CodeBrix host','CodeBrix WGSL/WebGPU device'],'ocio':['yaml-cpp','pystring','minizip','ocio-embedded'],
 'oiio':['fmt','robin-map','oiio-embedded']}


def component(name):return next((c for prefix,c in COMPONENTS if name.lower().startswith(prefix.lower())),None)


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--rid',choices=sorted(wt.TARGETS),required=True)
 p.add_argument('--gate',type=Path,action='append',default=[],help='A passed windows_host_gate.py output of this build (repeat per backend)')
 p.add_argument('--oracles',type=Path,help='A completed oracle run of this build (render_oracles.py output)')
 a=p.parse_args()
 wt.require_windows();toolchain=wt.Toolchain(a.rid)
 host=recipe.work(a.rid);build=host/'build';binary=build/'bin';prefix=deps.work(a.rid)/'install'
 status=json.loads((host/'status.json').read_text());invocation=json.loads((host/'invocation.json').read_text())
 assert status['complete'] and status['passed'],'Build the host first: python -X utf8 native/tools/build_cycles_windows.py --rid '+a.rid
 for name,digest in status['artifacts'].items():assert sha(build/name)==digest,'Build artifact changed since the build: '+name
 native={'AMD64':'win-x64','ARM64':'win-arm64'}.get(platform.machine())==a.rid
 gates=[]
 for gate in a.gate:
  record=json.loads((gate/'status.json').read_text());used=json.loads((gate/'invocation.json').read_text())
  assert record['complete'] and record['passed'] and record['rid']==a.rid and record['source']=='build',f'{gate} is not a passed {a.rid} gate of the build tree'
  for path,digest in used['binarySha256'].items():
   if Path(path).is_relative_to(build):assert sha(Path(path))==digest,f'{gate} validated different binaries: {path}'
  gates.append(dict(output=str(gate),backend=record['backend'],adapterBackend=record.get('adapterBackend'),filmsCompared=record['filmsCompared'],
   gpuPracticalFailures=record['gpuPracticalFailures'],gpuStrictDifferences=record['gpuStrictDifferences'],
   gpuMaximumAbsoluteError=record['gpuMaximumAbsoluteError'],crossPlatformCompared=record['crossPlatformCompared'],statusSha256=sha(gate/'status.json')))
 if a.rid=='win-x64':assert gates,'A win-x64 payload needs a passed host gate (--gate)'
 dxc=windows_dxc.files(a.rid)
 target=REPO/'native/binaries'/a.rid;stage=Path(tempfile.mkdtemp(prefix=f'stage-{a.rid}-',dir=REPO/'native/work/windows'))
 for d in ['runtime','symbols','tests']:(stage/d).mkdir()
 loaded=[name for name in status['dependencyClosure'].values() if name.lower()!=recipe.RUNTIME_NAME]
 runtime_files=[HOST,*sorted(loaded)]
 for name in runtime_files:shutil.copy2(binary/name,stage/'runtime'/name)
 for source in [dxc['dll'],dxc['license']]:shutil.copy2(source,stage/'runtime'/source.name)
 for name in SYMBOLS:
  pdb=binary/(name+'.pdb');assert pdb.exists(),'Missing PDB: '+pdb.name;shutil.copy2(pdb,stage/'symbols'/pdb.name)
 for name in TESTS:shutil.copy2(binary/(name+'.exe'),stage/'tests'/(name+'.exe'))
 # PE review of every staged binary: target machine, Windows/payload/runtime imports
 # only, no Visual C++ runtime DLL; the host exports exactly the C ABI.
 payload_dlls=[f.name for f in (stage/'runtime').glob('*.dll')]
 review={}
 for f in sorted([*(stage/'runtime').glob('*.dll'),*(stage/'tests').glob('*.exe')]):
  if f.name=='dxcompiler.dll':review[f.name]=dict(machine=wt.machine(toolchain,f),imports=wt.imports(toolchain,f),unchanged='Microsoft-signed DXC');continue
  wt.check_binary(toolchain,f,[*payload_dlls,recipe.RUNTIME_NAME])
  review[f.relative_to(stage).as_posix()]=dict(sha256=sha(f),machine=wt.machine(toolchain,f),imports=wt.imports(toolchain,f),
   pdb=re.findall(r'PDBFileName:\s*(\S+)',toolchain.tool(toolchain.readobj,'--coff-debug-directory',str(f))))
 exports=recipe.exports(toolchain,stage/'runtime'/HOST)
 for name,row in review.items():
  if row.get('pdb') and any(('\\' in x or '/' in x) for x in row['pdb']):raise RuntimeError(f'{name} names a PDB by path: {row["pdb"]}')
 codecs=[n for n in payload_dlls if re.match(r'(lib)?(jpeg|png|webp|gif|heif|raw|openjp|avif|jxl)',n,re.I)]
 if codecs:raise RuntimeError('Image codec DLLs in the payload: '+', '.join(codecs))
 smoke=None
 if native:
  # Render from a temporary copy of the payload plus the shipped runtime only.
  with tempfile.TemporaryDirectory(prefix=f'payload-{a.rid}-') as run:
   run=Path(run)
   for f in [*(stage/'runtime').iterdir(),*(stage/'tests').iterdir()]:shutil.copy2(f,run/f.name)
   shutil.copy2(Path(invocation['runtimePath']),run/recipe.RUNTIME_NAME)
   env={k:v for k,v in os.environ.items() if not k.startswith('CODEBRIX_')};env['PATH']=str(run)+os.pathsep+os.environ.get('SystemRoot',r'C:\Windows')+r'\System32'
   smoke={}
   for mode in ['cpu','gpu']:
    out=subprocess.run([str(run/'codebrix_host_example_test.exe'),mode],capture_output=True,text=True,timeout=1800,env=env,cwd=run)
    rows=[json.loads(l) for l in out.stdout.splitlines() if l.strip().startswith('{')]
    if out.returncode or not rows or not rows[-1]['passed']:raise RuntimeError(f'Payload smoke render ({mode}) failed: {out.stdout[-400:]} {out.stderr[-400:]}')
    smoke[mode]=rows[-1]
 approved={f.relative_to(stage).as_posix():sha(f) for f in sorted(stage.rglob('*')) if f.is_file()}
 identity=invocation['identity']
 provenance=dict(rid=a.rid,created=datetime.datetime.now().astimezone().strftime('%Y-%m-%dT%H:%M:%S%z'),
  scope=f'Reviewed {a.rid} product binaries: Cycles with the CodeBrix WGSL/WebGPU device (Direct3D 12 with DXC by default, Vulkan on request) '
   'and the CPU device behind the CodeBrix C API; no image file readers; hybrid C runtime (no Visual C++ Redistributable).',
  sources=dict(cyclesRevision=json.loads((REPO/'third_party/cycles-import.json').read_text())['revision'],cyclesImportSha256=invocation['cyclesImportSha256'],
   dependencyImportsSha256=sha(REPO/'third_party/dependency-imports.json'),dependencyMarkerSha256=invocation['dependencyMarkerSha256'],
   dependencyReplacements=identity.get('replacements'),overlaySha256=invocation['overlaySha256'],overlayComparison=invocation['overlayComparison'],
   hostSourceSha256=invocation['hostSourceSha256'],recipeSha256=invocation['recipeSha256'],injectionSha256=invocation['injectionSha256'],
   sharedInjectionSha256=invocation['sharedInjectionSha256'],stagerSha256=sha(Path(__file__)),adaptedFiles=invocation['adaptedFiles']),
  toolchain=identity,buildCommands=invocation['commands'],cpuKernelCompile=status.get('cpuKernelCompile'),skippedTargets=invocation['skippedTargets'],
  webgpuRuntime=dict(id='CodeBrix.WebGpu.Native.MitLicenseForever',version='1.0.290.1433',path=f'runtimes/{a.rid}/native/'+recipe.RUNTIME_NAME,
   sha256=recipe.RUNTIME_SHA256[a.rid],note='Not part of this payload; delivered by its own package and placed beside the host.'),
  thirdPartyRuntime={'dxcompiler.dll':dict(component='dxc',version=dxc['version'],import_='third_party/dxc-import.json',sha256=dxc['sha256'],
   license='runtime/'+dxc['license'].name,signed='Microsoft Corporation (Authenticode)',unchanged=True)},
  binaries=review,exports=len(exports),unstripped='Windows binaries carry no symbol tables; debug information (line tables) is in symbols/*.pdb.',
  components={name:dict(component=component(name),static=STATIC.get(component(name),[])) for name in [*runtime_files,'dxcompiler.dll']},
  notices=dict(repositoryNotices='THIRD-PARTY-NOTICES.txt',dxc='runtime/'+dxc['license'].name),
  validation=dict(status='validated' if gates else 'pending-device',hostGates=gates,
   oracles=str(a.oracles) if a.oracles else None,payloadSmoke=smoke,
   note=None if gates else 'Cross-built on win-x64; the ARM64 device runs native/tools/windows_host_gate.py --payload native/binaries/'+a.rid),
  approvedPayload=approved,snapshotCopies={})
 provenance['thirdPartyRuntime']['dxcompiler.dll']['import']=provenance['thirdPartyRuntime']['dxcompiler.dll'].pop('import_')
 save(stage/'provenance.json',provenance)
 # Replace the payload as a whole (the DXC files are re-staged from the same pins).
 if target.exists():shutil.rmtree(target)
 shutil.copytree(stage,target);shutil.rmtree(stage)
 assert all(sha(target/name)==digest for name,digest in approved.items())
 print(json.dumps(dict(rid=a.rid,files=len(approved),validation=provenance['validation']['status'],smoke=bool(smoke)),indent=1))


if __name__=='__main__':main()
