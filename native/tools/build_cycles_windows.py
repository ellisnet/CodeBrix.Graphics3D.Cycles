#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the Windows Cycles host with the CodeBrix WGSL/WebGPU and CPU devices.

    python -X utf8 native/tools/build_cycles_windows.py --rid win-x64 --jobs 16 [--reference-overlay linux-overlay.json]

The Windows sibling of build_cycles_macos_x64.py. win-x64 and win-arm64 run the
same WGSL device as Linux, Android and Intel Macs, on the unchanged shipped
CodeBrix.WebGpu.Native runtime (Direct3D 12 by default, Vulkan on request:
native/src/webgpu_device.cpp). The pinned snapshot is built unchanged with
clang-cl (tools/windows_toolchain.py); the WGSL overlay is generated exactly as on
Linux. win-arm64 is cross-compiled on an x64 Windows machine; its executables
are built but run only on an ARM64 Windows device.

The runtime DLL is linked through an import library generated from its exports,
and copied with the pinned DXC compiler (tools/windows_dxc.py) and the dependency
DLLs beside the host library, where the loader and the WebGPU device find them.
"""
import argparse,json,os,pathlib,re,shutil,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'));sys.path.insert(0,str(REPO/'tools'))
import windows_toolchain as wt
import windows_dxc
from windows_sources import verify_dependencies
sys.path.insert(0,str(REPO/'tools/dependencies'))
import build_windows as deps
sha,save=wt.sha,deps.save
# CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433, runtimes/<rid>/native/codebrix_wgpu.dll.
RUNTIME_NAME='codebrix_wgpu.dll'
RUNTIME_SHA256={'win-x64':'61c6f4d8d529240ab531e17f9aab7845b9dda04a45848674c1af463f346b6afa',
                'win-arm64':'5c93319aaefae2d531922f98283f0d3eda6336b8380d245c11aad63263bcf531'}
# The Linux host-gate list (host_gate.py), plus the WGSL device gates.
HOST_TARGETS=['codebrix_cycles_host',*['codebrix_host_'+n for n in ['cpu_test','gpu_test','reload_test','concurrency_test','tier_test',
 'abi_test','example_test','scene_edit_test','session_reference','session64_reference']],'codebrix_cycles_oracle']
GATE_TARGETS=['codebrix_cycles_'+n+'_gate' for n in ['device','camera','closest','background','svm_world','shader_data','svm_surface',
 'attribute','normal','image','surface','microfacet_svm','microfacet_bounce','microfacet_render','render','atomic_film',
 'ao_render','ao_state','light_tree','mnee','mnee_render','shadow_catcher']]
SKIPPED_TARGETS={'codebrix_host_image_reference':'writes and reads EXR/PNG through OIIO, whose format plugins are disabled',
 'codebrix_cycles_image_file_gate':'loads texture files through OIIO, whose format plugins are disabled'}
OVERLAY_PATTERNS=['*.inc','translated.wgsl','device/*','integrator/*','util/*']


def work(rid):return REPO/'native/work/windows'/rid/'host'


def lf_text_files():
 """Generated build inputs are written with '\\n' as on Linux: Path.write_text
 translates newlines on Windows, which would change the overlay's bytes. The
 shared generators (build_cycles.py, cycles_overlay.py, research/translate.py)
 stay unchanged; this process-local default applies only to this recipe."""
 original=pathlib.Path.write_text
 def write_text(self,data,encoding=None,errors=None,newline=None):
  return original(self,data,encoding or 'utf-8',errors,'\n' if newline is None else newline)
 pathlib.Path.write_text=write_text


def overlay_hashes(directory):
 directory=Path(directory)
 return {p.relative_to(directory).as_posix():sha(p) for pattern in OVERLAY_PATTERNS for p in sorted(directory.glob(pattern)) if p.is_file()}


def compare_overlay(generated,reference):
 expected=json.loads(Path(reference).read_text());actual=overlay_hashes(generated)
 different=sorted(n for n in set(expected)|set(actual) if expected.get(n)!=actual.get(n))
 if different:raise RuntimeError('Generated WGSL overlay differs from the reference build: '+', '.join(different[:10]))
 return dict(reference=str(reference),files=len(actual),identical=True)


def import_library(toolchain,runtime,out):
 """codebrix_wgpu.lib from the shipped DLL's export table (the package has no .lib)."""
 text=toolchain.tool(toolchain.readobj,'--coff-exports',str(runtime))
 names=sorted(set(re.findall(r'^\s*Name:\s*(\S+)\s*$',text,re.M)))
 if not names:raise RuntimeError('No exports found in '+str(runtime))
 definition=out.with_suffix('.def');definition.write_text('LIBRARY '+runtime.name+'\nEXPORTS\n'+''.join('  '+n+'\n' for n in names))
 machine={'win-x64':'x64','win-arm64':'arm64'}[toolchain.rid]
 toolchain.tool(toolchain.llvm_lib,'/nologo','/def:'+str(definition),'/machine:'+machine,'/out:'+str(out))
 return dict(definition=str(definition),exports=len(names),sha256=sha(out))


def exports(toolchain,library):
 text=toolchain.tool(toolchain.readobj,'--coff-exports',str(library))
 symbols=sorted(set(re.findall(r'^\s*Name:\s*(\S+)\s*$',text,re.M)))
 expected=sorted(w for w in (REPO/'native/host/exports.def').read_text().split() if w!='EXPORTS')
 if symbols!=expected:raise RuntimeError(f'Unexpected host exports: {sorted(set(symbols)^set(expected))}')
 return symbols


# Windows DLLs the shipped WebGPU runtime imports beyond the common system list:
# Rust's standard library takes its random numbers from bcryptprimitives.dll.
RUNTIME_SYSTEM_DLL=re.compile(r'^(bcryptprimitives|ntdll)\.dll$',re.I)


def closure(toolchain,library,search):
 """DLLs the host loads that are not part of Windows (resolved in SEARCH)."""
 seen={};pending=[Path(library)]
 while pending:
  current=pending.pop()
  for name in wt.imports(toolchain,current):
   if wt.SYSTEM_DLL.match(name) or name.lower() in seen:continue
   if current.name.lower()==RUNTIME_NAME and RUNTIME_SYSTEM_DLL.match(name):continue
   path=next((d/name for d in search if (d/name).exists()),None)
   if path is None:raise RuntimeError(f'{current.name} needs {name}, found in neither the dependency prefix nor the runtime')
   seen[name.lower()]=path;pending.append(path)
 return dict(sorted(seen.items()))


def cpu_kernel_flags(build,rid):
 """The CPU kernel compile line of the fixed-baseline oracle, checked: the last
 contraction/reciprocal options switch both off, the C runtime is the static one,
 x64 kernels are SSE4.2 without AVX/FMA."""
 rows=[r for r in json.loads((build/'compile_commands.json').read_text()) if r['file'].replace('\\','/').endswith('src/kernel/device/cpu/kernel.cpp')]
 if len(rows)!=1:raise RuntimeError('CPU kernel compile command not found')
 words=rows[0]['command'].split()
 contract=[w for w in words if 'ffp-contract' in w];reciprocal=[w for w in words if 'reciprocal-math' in w or 'fast-math' in w or w.startswith('/fp:')]
 runtime=[w for w in words if re.fullmatch(r'[/-]M[DT]d?',w)]
 problems=[]
 if contract[-1:]!=['/clang:-ffp-contract=off']:problems.append('contraction '+str(contract))
 if reciprocal[-1:]!=['/clang:-fno-reciprocal-math']:problems.append('reciprocal '+str(reciprocal))
 if runtime[-1:]!=['/MT']:problems.append('runtime '+str(runtime))
 if rid=='win-x64' and ('-msse4.2' not in words or any(w.startswith(('-mavx','-mfma','/arch:AVX')) for w in words)):problems.append('ISA not SSE4.2')
 if problems:raise RuntimeError('CPU kernels are not at the fixed oracle baseline: '+'; '.join(problems))
 return dict(file='src/kernel/device/cpu/kernel.cpp',fpContract=contract,reciprocal=reciprocal,runtime=runtime[-1],
  isa='x86-64 SSE4.2' if rid=='win-x64' else 'ARM64')


def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--rid',choices=sorted(wt.TARGETS),default='win-x64');p.add_argument('--jobs',type=int,default=8)
 p.add_argument('--target',action='append',help='Build selected CMake targets only')
 p.add_argument('--reference-overlay',type=Path,help='Overlay hashes written on Linux; the generated overlay must match')
 a=p.parse_args()
 wt.require_windows();toolchain=wt.Toolchain(a.rid);WORK=work(a.rid)
 # The overlay generator runs clang++ for the AST translation; it inherits this
 # process's environment, which becomes the isolated MSVC one.
 environment=toolchain.environment(a.jobs);os.environ.clear();os.environ.update(environment);lf_text_files()
 # native/tools/build.py, not tools/dependencies/build.py (also on the import path).
 sys.path.insert(0,str(REPO/'native/tools'));sys.modules.pop('build',None)
 from build import PACKAGE,verify_cycles,audit
 from build_cycles import overlay
 from cycles_source import SOURCE
 WORK.mkdir(parents=True,exist_ok=True);state=dict(complete=False,phase='prepare',rid=a.rid);save(WORK/'status.json',state)
 try:
  prefix=deps.prefix(a.rid);marker=prefix/deps.marker_name(a.rid)
  verify_cycles();save(WORK/'webgpu-compatibility.json',audit(allow_rust_documentation_change=True))
  runtime=PACKAGE/'runtimes'/a.rid/'native'/RUNTIME_NAME
  if a.rid not in RUNTIME_SHA256:raise RuntimeError(f'No pinned {a.rid} WebGPU runtime hash (record it from the reviewed package first)')
  if sha(runtime)!=RUNTIME_SHA256[a.rid]:raise RuntimeError(f'The {a.rid} WebGPU runtime differs from the pinned shipped binary')
  dxc=windows_dxc.files(a.rid)
  implib=import_library(toolchain,runtime,WORK/'codebrix_wgpu.lib')
  generated=overlay(WORK/'overlay');generated_hashes=overlay_hashes(generated)
  overlay_check=compare_overlay(generated,a.reference_overlay) if a.reference_overlay else dict(reference=None,identical=None,
   note='not compared; pass --reference-overlay with hashes written on Linux (build_cycles_macos_x64.py --write-overlay-hashes)')
  slash=lambda v:v.as_posix() if isinstance(v,Path) else str(v)
  values=dict(CMAKE_BUILD_TYPE='Release',CMAKE_EXPORT_COMPILE_COMMANDS='ON',CMAKE_PREFIX_PATH=prefix,
   CMAKE_POLICY_VERSION_MINIMUM='3.5',**toolchain.cmake_options(),
   WITH_LIBS_PRECOMPILED='OFF',WITH_LEGACY_LIBRARIES='ON',
   # Fixed-baseline CPU oracle (native/DEVICE.md): Cycles' SSE4.2 kernels only on
   # x64 (no AVX2/FMA kernel, never -march=native); the shared injection switches
   # contraction and reciprocal math off in clang-cl's /clang: form.
   WITH_CYCLES_NATIVE_ONLY='OFF',WITH_CYCLES_PATH_GUIDING='OFF',
   **{'CXX_HAS_AVX2:BOOL':'OFF','CXX_HAS_F16C:BOOL':'OFF'},
   # ARM64: the ARMv8-A base level and no x86 flag probe (clang-cl may accept
   # -msse4.2 for an ARM64 target with only a warning), as in the Linux ARM64 recipe.
   **({'CXX_HAS_SSE42:BOOL':'OFF'} if a.rid=='win-arm64' else {}),
   OpenImageIO_DIR=prefix/'lib/cmake/OpenImageIO',OpenColorIO_DIR=prefix/'lib/cmake/OpenColorIO',
   CMAKE_PROJECT_Cycles_INCLUDE=REPO/'native/tools/inject_cycles_windows.cmake',CODEBRIX_CYCLES_OVERLAY=generated,
   CODEBRIX_WEBGPU_PACKAGE=PACKAGE,CODEBRIX_WEBGPU_RID=a.rid,CODEBRIX_WEBGPU_IMPLIB=WORK/'codebrix_wgpu.lib',
   # Cycles' configure_build.cmake replaces CMAKE_<LANG>_FLAGS on MSVC-compatible
   # compilers, so the injection adds these as directory options: line tables (as
   # on macOS, -gline-tables-only) for a PDB per binary, and build paths mapped
   # out of the objects. The binaries name only their PDB's file.
   CODEBRIX_WINDOWS_COMPILE_OPTIONS='-gline-tables-only;'+deps.path_map())
  # Headers of the prefix (TBB) name their import libraries with #pragma comment(lib);
  # the linker finds those in the prefix, never anywhere else.
  for name in ['CMAKE_EXE_LINKER_FLAGS','CMAKE_SHARED_LINKER_FLAGS','CMAKE_MODULE_LINKER_FLAGS']:
   values[name]=values[name]+' /LIBPATH:'+(prefix/'lib').as_posix()+' /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:%_PDB%'
  values.update({'WITH_CYCLES_'+n:'OFF' for n in ['ALEMBIC','EMBREE','OPENIMAGEDENOISE','OPENSUBDIV','OPENVDB','NANOVDB','OSL','USD',
   'HYDRA_RENDER_DELEGATE','DEVICE_CUDA','DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI','DEVICE_METAL']})
  build=WORK/'build'
  commands=[[str(toolchain.cmake),'-G','Ninja','-S',str(SOURCE),'-B',str(build),*['-D'+k+'='+slash(v) for k,v in values.items()]],
   [str(toolchain.cmake),'--build',str(build),'--target',*(a.target or HOST_TARGETS+GATE_TARGETS),'--parallel',str(a.jobs)]]
  pin=dict(identity=deps.identity(toolchain),cyclesImportSha256=sha(REPO/'third_party/cycles-import.json'),dependencyMarkerSha256=sha(marker),
   recipeSha256=sha(Path(__file__)),injectionSha256=sha(REPO/'native/tools/inject_cycles_windows.cmake'),
   sharedInjectionSha256=sha(REPO/'native/tools/inject_cycles.cmake'),runtimeSha256=sha(runtime),runtimePath=str(runtime),
   runtimeImportLibrary=implib,dxc=dict(version=dxc['version'],sha256=dxc['sha256']),
   hostSourceSha256={p.relative_to(REPO).as_posix():sha(p) for p in sorted((REPO/'native/host').glob('*')) if p.is_file() and p.suffix in ('.cpp','.h','.inc','.cmake','.def')},
   overlaySha256=generated_hashes,overlayComparison=overlay_check,adaptedFiles=[],commands=commands,skippedTargets=SKIPPED_TARGETS)
  save(WORK/'invocation.json',pin);env=toolchain.environment(a.jobs)
  for step,command in zip(['configure','build'],commands):
   state['phase']=step;save(WORK/'status.json',state)
   with (WORK/(step+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
   wt.check_isolation(build)
   if step=='configure':
    cache=(build/'CMakeCache.txt').read_text()
    # Cycles declares the Metal option only on Apple platforms (UNINITIALIZED here).
    for required in [r'^WITH_CYCLES_DEVICE_METAL:\w+=OFF$',r'^WITH_CYCLES_NATIVE_ONLY:BOOL=OFF$']:
     if not re.search(required,cache,re.M):raise RuntimeError('Configured cache lacks '+required)
  state['compilerDependencyFiles']=deps.check_build_inputs(toolchain,build)
  rows=json.loads((build/'compile_commands.json').read_text())
  native=[row['file'] for row in rows if re.search(r'-m(arch|cpu|tune)=native|/arch:AVX',row.get('command',''))]
  if native:raise RuntimeError(f'Host-specific CPU flags in {native[:3]}')
  state['cpuKernelCompile']=cpu_kernel_flags(build,a.rid)
  if not any('-DWITH_WEBGPU' in row.get('command','') for row in rows):raise RuntimeError('The WebGPU device was not configured')
  if any('-DWITH_METAL' in row.get('command','') for row in rows):raise RuntimeError('Cycles Metal code was configured into the WGSL profile')
  bin_dir=build/'bin';host=bin_dir/'codebrix_cycles_host.dll'
  # Everything the host loads goes beside it: the dependency DLLs, the runtime
  # and DXC. Executables in bin/ find them there too.
  copied={}
  for dll in sorted((prefix/'bin').glob('*.dll')):shutil.copy2(dll,bin_dir/dll.name);copied[dll.name]=sha(dll)
  for source in [runtime,dxc['dll'],dxc['license']]:shutil.copy2(source,bin_dir/source.name);copied[source.name]=sha(source)
  if sha(bin_dir/RUNTIME_NAME)!=RUNTIME_SHA256[a.rid] or sha(bin_dir/'dxcompiler.dll')!=dxc['sha256']:raise RuntimeError('Copied runtime or DXC differs from the pinned binary')
  loaded=closure(toolchain,host,[prefix/'bin',runtime.parent])
  allowed=[p.name for p in loaded.values()]
  wt.check_binary(toolchain,host,allowed)
  for dll in loaded.values():
   if dll.parent==prefix/'bin':wt.check_binary(toolchain,dll,allowed)
  if RUNTIME_NAME.lower() not in loaded:raise RuntimeError('The host does not load the WebGPU runtime')
  symbols=exports(toolchain,host)
  forbidden=[name for name in loaded if re.match(r'(lib)?(jpeg|png|tiff|webp|gif|heif|raw|openjp|avif|jxl)',name)]
  if forbidden:raise RuntimeError('Image codec libraries are loaded by the host: '+', '.join(forbidden))
  verify_cycles();verify_dependencies()
  if sha(runtime)!=pin['runtimeSha256']:raise RuntimeError('The runtime changed during the build')
  artifacts={p.relative_to(build).as_posix():sha(p) for p in sorted(bin_dir.glob('*')) if p.is_file() and p.suffix.lower() in ('.dll','.exe')}
  state.update(complete=True,passed=True,phase='complete',artifacts=artifacts,exports=len(symbols),
   dependencyClosure={k:v.name for k,v in loaded.items()},copiedBesideHost=copied,
   runtime=dict(path='bin/'+RUNTIME_NAME,sha256=RUNTIME_SHA256[a.rid]),dxc=dict(path='bin/dxcompiler.dll',sha256=dxc['sha256']),
   overlayComparison=overlay_check,executionProven=False,releaseCleared=False)
  print(f'{a.rid}: WGSL/WebGPU host, lifecycle targets and device gates built',flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(WORK/'status.json',state)


if __name__=='__main__':main()
