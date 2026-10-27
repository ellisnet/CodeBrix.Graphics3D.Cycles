#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the osx-x64 Cycles host with the CodeBrix WGSL/WebGPU and CPU devices.

    python3 native/tools/build_cycles_macos_x64.py --jobs 8 [--reference-overlay linux-overlay.json]
    python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay --output linux-overlay.json

The osx-x64 sibling of build_cycles_macos.py (osx-arm64, Cycles' Metal device).
Intel Macs run the same WGSL device as Linux and Android on the unchanged shipped
CodeBrix.WebGpu.Native osx-x64 runtime, which reaches the GPU through Metal. The
pinned snapshot is built unchanged (no platform adaptation is needed on x86_64);
the WGSL overlay is generated exactly as on Linux. The runtime dylib is linked
like libcodebrix_wgpu.so on Linux and copied beside the host library.

--write-overlay-hashes runs on any host: on Linux it records the hashes of the
generated WGSL overlay so the Mac build can prove it generated the same kernels.
"""
import argparse,importlib.util,json,platform,re,shutil,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'));sys.path.insert(0,str(REPO/'tools'))
spec=importlib.util.spec_from_file_location('macos_x64_dependencies',REPO/'tools/dependencies/build_macos_x64.py')
deps=importlib.util.module_from_spec(spec);spec.loader.exec_module(deps)
sha,save,tool=deps.sha,deps.save,deps.tool
WORK=REPO/'native/work/macos/osx-x64/host'
RUNTIME_NAME='libcodebrix_wgpu.dylib'
# CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210, runtimes/osx-x64/native.
RUNTIME_SHA256='840a106d54fe479c255ecb98d51cee192a9044c3d0411fc1535f9ff0b8abbb82'
# The Linux host-gate list (host_gate.py), plus the WGSL device gates.
HOST_TARGETS=['codebrix_cycles_host',*['codebrix_host_'+n for n in ['cpu_test','gpu_test','reload_test','concurrency_test','tier_test',
 'abi_test','example_test','scene_edit_test','session_reference','session64_reference']]]
GATE_TARGETS=['codebrix_cycles_'+n+'_gate' for n in ['device','camera','closest','background','svm_world','shader_data','svm_surface',
 'attribute','normal','image','surface','microfacet_svm','microfacet_bounce','microfacet_render','render','atomic_film']]
SKIPPED_TARGETS={'codebrix_host_image_reference':'writes and reads EXR/PNG through OIIO, whose format plugins are disabled',
 'codebrix_cycles_image_file_gate':'loads texture files through OIIO, whose format plugins are disabled'}
# Overlay files that must be byte-identical to the Linux build's: every embedded
# WGSL include, the translated kernels and the registration overlay. AST dumps,
# caches and the device header (which names an absolute path) are excluded.
OVERLAY_PATTERNS=['*.inc','translated.wgsl','device/*','integrator/*','util/*']


def overlay_hashes(directory):
 directory=Path(directory)
 return {str(p.relative_to(directory)):sha(p) for pattern in OVERLAY_PATTERNS for p in sorted(directory.glob(pattern)) if p.is_file()}


def compare_overlay(generated,reference):
 expected=json.loads(Path(reference).read_text());actual=overlay_hashes(generated)
 different=sorted(n for n in set(expected)|set(actual) if expected.get(n)!=actual.get(n))
 if different:raise RuntimeError('Generated WGSL overlay differs from the reference build: '+', '.join(different[:10]))
 return dict(reference=str(reference),files=len(actual),identical=True)


def exports(library):
 symbols=sorted(line.split()[-1] for line in tool('nm','-gU',str(library)).splitlines() if ' T ' in line)
 expected=sorted((REPO/'native/host/exports.macos').read_text().split())
 if symbols!=expected:raise RuntimeError(f'Unexpected host exports: {sorted(set(symbols)^set(expected))}')
 return symbols


def closure(library,prefix):
 """Libraries the host loads by @rpath: the dependency prefix plus the runtime."""
 seen={};pending=[library]
 while pending:
  current=pending.pop()
  for name in deps.linked(current):
   if not name.startswith('@rpath/'):continue
   leaf=name[len('@rpath/'):]
   if leaf==library.name or leaf in seen or leaf==RUNTIME_NAME:continue
   path=prefix/'lib'/leaf
   if not path.exists():raise RuntimeError(f'{current.name} needs {name}, absent from the dependency prefix')
   seen[leaf]=str(path.resolve().relative_to(prefix.resolve()));pending.append(path)
 return dict(sorted(seen.items()))


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--jobs',type=int,default=8);p.add_argument('--target',action='append',help='Build selected CMake targets only')
 p.add_argument('--reference-overlay',type=Path,help='Overlay hashes written on Linux; the generated overlay must match')
 p.add_argument('--write-overlay-hashes',type=Path,metavar='OVERLAY_DIR',help='Write the hashes of an existing overlay directory and exit (any host)')
 p.add_argument('--output',type=Path,help='With --write-overlay-hashes: the JSON file to write')
 a=p.parse_args()
 if a.write_overlay_hashes:
  if not a.output:p.error('--write-overlay-hashes needs --output')
  hashes=overlay_hashes(a.write_overlay_hashes)
  if not hashes:raise SystemExit(f'No overlay files under {a.write_overlay_hashes}')
  save(a.output,hashes);print(f'{len(hashes)} overlay hashes written to {a.output}');return
 deps.require_build_host()
 # native/tools/build.py, not the tools/build.py coordinator (the dependency
 # recipes put tools/ first on the import path).
 sys.path.insert(0,str(REPO/'native/tools'))
 from build import PACKAGE,verify_cycles,audit
 from build_cycles import overlay
 from cycles_source import SOURCE
 WORK.mkdir(parents=True,exist_ok=True);state=dict(complete=False,phase='prepare',rid=deps.RID);save(WORK/'status.json',state)
 try:
  prefix=deps.prefix();marker=prefix/deps.MARKER
  verify_cycles();save(WORK/'webgpu-compatibility.json',audit(allow_rust_documentation_change=True))
  runtime=PACKAGE/'runtimes/osx-x64/native'/RUNTIME_NAME
  if sha(runtime)!=RUNTIME_SHA256:raise RuntimeError('The osx-x64 WebGPU runtime differs from the pinned shipped binary')
  # Same generator as Linux; translation runs Apple Clang's AST dump here, so
  # the result is compared with the Linux overlay when a reference is given.
  generated=overlay(WORK/'overlay');generated_hashes=overlay_hashes(generated)
  overlay_check=compare_overlay(generated,a.reference_overlay) if a.reference_overlay else dict(reference=None,identical=None,
   note='not compared; pass --reference-overlay with hashes written on Linux')
  values=deps.options(prefix)
  for name in ['CMAKE_INSTALL_PREFIX','CMAKE_INSTALL_NAME_DIR','CMAKE_POLICY_VERSION_MINIMUM','BUILD_SHARED_LIBS','BUILD_TESTING','CMAKE_INSTALL_LIBDIR:STRING']:
   values.pop(name)
  # Only checkout-independent rpaths: build/lib and build/bin reach the host,
  # the runtime copied beside it and the dependency prefix relatively.
  values['CMAKE_INSTALL_RPATH']='@loader_path;@loader_path/../lib;@loader_path/../../../dependencies/install/lib'
  values.update(WITH_LIBS_PRECOMPILED='OFF',WITH_LEGACY_LIBRARIES='ON',
   # Fixed-baseline CPU oracle (native/DEVICE.md): Cycles' SSE4.2 kernels only.
   # Native-only would add -march=native (tying the kernels to the build machine)
   # and the AVX2 kernel would use FMA; neither is built. inject_cycles.cmake
   # removes contraction and reciprocal math. UNTESTED on an Intel Mac so far.
   WITH_CYCLES_NATIVE_ONLY='OFF',WITH_CYCLES_DEVICE_METAL='OFF',WITH_CYCLES_PATH_GUIDING='OFF',
   **{'CXX_HAS_AVX2:BOOL':'OFF','CXX_HAS_F16C:BOOL':'OFF'},
   OpenImageIO_DIR=prefix/'lib/cmake/OpenImageIO',OpenColorIO_DIR=prefix/'lib/cmake/OpenColorIO',
   CMAKE_PROJECT_Cycles_INCLUDE=REPO/'native/tools/inject_cycles_macos_x64.cmake',CODEBRIX_CYCLES_OVERLAY=generated,
   CODEBRIX_WEBGPU_PACKAGE=PACKAGE,CODEBRIX_WEBGPU_RID='osx-x64',
   CMAKE_C_FLAGS='-gline-tables-only '+deps.PATH_MAP,CMAKE_CXX_FLAGS='-gline-tables-only '+deps.PATH_MAP,
   CMAKE_OBJCXX_FLAGS='-gline-tables-only '+deps.PATH_MAP,CODEBRIX_OSO_PREFIX=f'{REPO}/')
  values.update({'WITH_CYCLES_'+n:'OFF' for n in ['ALEMBIC','EMBREE','OPENIMAGEDENOISE','OPENSUBDIV','OPENVDB','NANOVDB','OSL','USD',
   'HYDRA_RENDER_DELEGATE','DEVICE_CUDA','DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI']})
  build=WORK/'build'
  commands=[['cmake','-S',str(SOURCE),'-B',str(build),*['-D'+k+'='+str(v) for k,v in values.items()]],
   ['cmake','--build',str(build),'--target',*(a.target or HOST_TARGETS+GATE_TARGETS),'--parallel',str(a.jobs)]]
  pin=dict(identity=deps.identity(),cyclesImportSha256=sha(REPO/'third_party/cycles-import.json'),dependencyMarkerSha256=sha(marker),
   recipeSha256=sha(Path(__file__)),injectionSha256=sha(REPO/'native/tools/inject_cycles_macos_x64.cmake'),
   sharedInjectionSha256=sha(REPO/'native/tools/inject_cycles.cmake'),runtimeSha256=sha(runtime),runtimePath=str(runtime),
   hostSourceSha256={str(p.relative_to(REPO)):sha(p) for p in sorted((REPO/'native/host').glob('*')) if p.is_file() and p.suffix in ('.cpp','.h','.inc','.cmake','.macos')},
   overlaySha256=generated_hashes,overlayComparison=overlay_check,adaptedFiles=[],commands=commands,skippedTargets=SKIPPED_TARGETS)
  save(WORK/'invocation.json',pin);env=deps.environment_for(prefix,a.jobs)
  for step,command in zip(['configure','build'],commands):
   state['phase']=step;save(WORK/'status.json',state)
   with (WORK/(step+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
   deps.check_isolation(build)
   if step=='configure':
    cache=(build/'CMakeCache.txt').read_text()
    for required in ['WITH_CYCLES_DEVICE_METAL:BOOL=OFF','WITH_CYCLES_NATIVE_ONLY:BOOL=OFF','CMAKE_OSX_ARCHITECTURES:STRING=x86_64']:
     if required not in cache:raise RuntimeError('Configured cache lacks '+required)
  state['compilerDependencyFiles']=deps.check_build_inputs(build)
  rows=json.loads((build/'compile_commands.json').read_text())
  native=[row['file'] for row in rows if re.search(r'-m(arch|cpu|tune)=native',row.get('command',''))]
  if native:raise RuntimeError(f'Host-specific CPU flags in {native[:3]}')
  kernel=[row.get('command','').split() for row in rows if row['file'].endswith('src/kernel/device/cpu/kernel.cpp')]
  contract=[w for w in kernel[0] if w.startswith('-ffp-contract')] if kernel else [];reciprocal=[w for w in kernel[0] if w.endswith('reciprocal-math')] if kernel else []
  if len(kernel)!=1 or contract[-1:]!=['-ffp-contract=off'] or reciprocal[-1:]!=['-fno-reciprocal-math'] or '-msse4.2' not in kernel[0] \
     or any(w.startswith(('-mavx','-mfma')) for w in kernel[0]):
   raise RuntimeError('CPU kernels are not at the fixed oracle baseline (SSE4.2, no contraction, no reciprocal math)')
  if not any('-DWITH_WEBGPU' in row.get('command','') for row in rows):raise RuntimeError('The WebGPU device was not configured')
  if any('-DWITH_METAL' in row.get('command','') for row in rows):raise RuntimeError('Cycles Metal code was configured into the WGSL profile')
  host=build/'lib/libcodebrix_cycles_host.dylib';deps.check_binary(host)
  if '@rpath/'+RUNTIME_NAME not in deps.linked(host):raise RuntimeError('The host does not load the WebGPU runtime by @rpath')
  if any('/Metal.framework/' in n for n in deps.linked(host)):raise RuntimeError('The host links Metal directly; only the runtime may')
  symbols=exports(host);loaded=closure(host,prefix)
  forbidden=[name for name in loaded if re.match(r'lib(jpeg|png|tiff|webp|gif|heif|raw|openjp|avif|jxl)',name)]
  if forbidden:raise RuntimeError('Image codec libraries are loaded by the host: '+', '.join(forbidden))
  # The runtime sits beside the host (found by @loader_path from lib and
  # @loader_path/../lib from bin), as libcodebrix_wgpu.so does on Linux.
  copied=build/'lib'/RUNTIME_NAME;shutil.copy2(runtime,copied)
  if sha(copied)!=RUNTIME_SHA256:raise RuntimeError('Copied runtime differs from the shipped binary')
  verify_cycles()
  if sha(runtime)!=pin['runtimeSha256']:raise RuntimeError('The runtime changed during the build')
  artifacts={str(p.relative_to(build)):sha(p) for d in [build/'bin',build/'lib'] for p in sorted(d.glob('*'))
   if p.is_file() and (p.name.startswith('codebrix_') or p.name in ('libcodebrix_cycles_host.dylib',RUNTIME_NAME))}
  if not a.target:assert len(artifacts)==len(HOST_TARGETS+GATE_TARGETS)+1,artifacts
  state.update(complete=True,passed=True,phase='complete',artifacts=artifacts,exports=len(symbols),dependencyClosure=loaded,
   runtime=dict(path=str(copied.relative_to(build)),sha256=RUNTIME_SHA256),overlayComparison=overlay_check,
   executionProven=False,releaseCleared=False)
  print('osx-x64: WGSL/WebGPU host, lifecycle targets and device gates built',flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(WORK/'status.json',state)


if __name__=='__main__':main()
