#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the current native Cycles host/gates for the installed Linux ARM64 SDK."""
import argparse,difflib,importlib.util,json,shutil,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'))
from build import ROOT,SOURCE,PACKAGE,verify_cycles,audit
from build_cycles import overlay
from build_cycles_android import HOST_TARGETS,GATE_TARGETS
from android_toolchain import environment
from device_gate import sha,save
from render_gate import inputs
base=REPO/'native/work/linux/linux-arm64'
spec=importlib.util.spec_from_file_location('arm64_dependencies',REPO/'tools/dependencies/build_linux_arm64.py');deps=importlib.util.module_from_spec(spec);spec.loader.exec_module(deps)


def main():
 p=argparse.ArgumentParser();p.add_argument('--jobs',type=int,default=4);p.add_argument('--target',action='append');a=p.parse_args()
 work=base/'host';work.mkdir(parents=True,exist_ok=True);state=dict(complete=False,phase='prepare',rid='linux-arm64');save(work/'status.json',state)
 try:
  prefix=deps.prefix();marker=prefix/'codebrix-linux-arm64-build.json';dependency=json.loads(marker.read_text());assert dependency['identity']==deps.identity()
  assert all(sha(prefix/name)==digest for name,digest in dependency['libraries'].items())
  assert sha(deps.WORK/'toolchain.cmake')==dependency['toolchainSha256']
  verify_cycles();source=work/'cycles-source'
  if not source.exists():shutil.copytree(SOURCE,source)
  sources=inputs()
  changed=['src/CMakeLists.txt','src/util/half.h'];before={name:(SOURCE/name).read_text() for name in changed};after=dict(before)
  def replace(name,old,new):
   assert after[name].count(old)==1,('Cross source adaptation anchor changed',name)
   after[name]=after[name].replace(old,new)
  replace(changed[0],'  if(NOT MSVC)\n    add_check_cxx_compiler_flags(','  if(NOT MSVC AND NOT CMAKE_CROSSCOMPILING)\n    add_check_cxx_compiler_flags(')
  replace(changed[0],'  else()\n    if(NOT MSVC_NATIVE_ARCH_FLAGS)','  elseif(MSVC)\n    if(NOT MSVC_NATIVE_ARCH_FLAGS)')
  replace(changed[1],'#pragma once\n','#pragma once\n\n#if defined(__ARM_NEON) && !defined(__KERNEL_GPU__)\n#  include <arm_neon.h>\n#endif\n')
  for name in changed:
   comment='//' if name.endswith('.h') else '#'
   after[name]=comment+' Copyright (c) 2026 Jeremy Ellis and contributors\n'+comment+' Linux ARM64 cross-target adaptation; upstream notices follow.\n'+after[name]
   if (source/name).read_text()!=after[name]:(source/name).write_text(after[name])
  patch=''.join(''.join(difflib.unified_diff(before[name].splitlines(True),after[name].splitlines(True),fromfile='a/'+name,tofile='b/'+name)) for name in changed)
  (work/'platform.patch').write_text(patch)
  manifest=json.loads((REPO/'third_party/cycles-import.json').read_text());copy={row['path']:sha(source/row['path']) if row['path'] in changed else row['sha256'] for row in manifest['files']}
  def verify_copy():
   assert {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}==set(copy)
   assert all(sha(source/name)==digest for name,digest in copy.items())
  verify_copy();generated=overlay(work/'overlay');save(work/'webgpu-compatibility.json',audit(allow_rust_documentation_change=True))
  options=dict(CMAKE_TOOLCHAIN_FILE=deps.WORK/'toolchain.cmake',CMAKE_BUILD_TYPE='Release',CMAKE_POSITION_INDEPENDENT_CODE='ON',CMAKE_EXPORT_COMPILE_COMMANDS='ON',
   CMAKE_PREFIX_PATH=prefix,WITH_LIBS_PRECOMPILED='OFF',WITH_LEGACY_LIBRARIES='ON',
   # Fixed-baseline CPU oracle (native/DEVICE.md): the ARMv8-A base level, never
   # -march/-mcpu=native (also when this recipe runs on an ARM64 machine), no x86
   # flag probes; inject_cycles.cmake removes contraction and reciprocal math.
   WITH_CYCLES_NATIVE_ONLY='OFF',**{'CXX_HAS_SSE42:BOOL':'OFF','CXX_HAS_AVX2:BOOL':'OFF','CXX_HAS_F16C:BOOL':'OFF'},
   OpenImageIO_DIR=prefix/'lib/cmake/OpenImageIO',OpenColorIO_DIR=prefix/'lib/cmake/OpenColorIO',
   CMAKE_PROJECT_Cycles_INCLUDE=ROOT/'tools/inject_cycles.cmake',CODEBRIX_CYCLES_OVERLAY=generated,CODEBRIX_WEBGPU_PACKAGE=PACKAGE,CODEBRIX_WEBGPU_RID='linux-arm64',
   FETCHCONTENT_FULLY_DISCONNECTED='ON',FETCHCONTENT_UPDATES_DISCONNECTED='ON')
  options.update({'WITH_CYCLES_'+n:'OFF' for n in ['ALEMBIC','EMBREE','OPENIMAGEDENOISE','OPENSUBDIV','OPENVDB','NANOVDB','OSL','USD','HYDRA_RENDER_DELEGATE','DEVICE_CUDA','DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI']})
  build=work/'build';runtime=PACKAGE/'runtimes/linux-arm64/native/libcodebrix_wgpu.so'
  commands=[['cmake','-S',str(source),'-B',str(build),*['-D'+k+'='+str(v) for k,v in options.items()]],
    ['cmake','--build',str(build),'--target',*(a.target or HOST_TARGETS+GATE_TARGETS),'--parallel',str(a.jobs)]]
  pin=dict(sourceSha256=sources,sourceCopySha256=copy,dependencyMarkerSha256=sha(marker),crossRecipeSha256=sha(Path(__file__)),runtimeSha256=sha(runtime),commands=commands,
    platformPatchSha256=sha(work/'platform.patch'),overlaySha256={str(p.relative_to(generated)):sha(p) for p in generated.rglob('*') if p.is_file()})
  save(work/'invocation.json',pin);env=environment(prefix,a.jobs)
  for step,command in zip(['configure','build'],commands):
   state['phase']=step;save(work/'status.json',state)
   with (work/(step+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
   if step=='configure':
    rows=json.loads((build/'compile_commands.json').read_text());assert all('-msse' not in row['command'] and '-march=native' not in row['command'] for row in rows)
    kernel=[row['command'].split() for row in rows if row['file'].endswith('src/kernel/device/cpu/kernel.cpp')]
    contract=[w for w in kernel[0] if w.startswith('-ffp-contract')];reciprocal=[w for w in kernel[0] if w.endswith('reciprocal-math')]
    assert len(kernel)==1 and contract[-1:]==['-ffp-contract=off'] and reciprocal[-1:]==['-fno-reciprocal-math'] \
       and not any(w.startswith(('-march','-mcpu')) for w in kernel[0]),'CPU kernels are not at the fixed oracle baseline'
    state['cpuKernelCompile']=[w for w in kernel[0] if w.startswith(('-O','-ffp-contract','-freciprocal','-fno-reciprocal','-march','-mcpu'))]
  assert sources==inputs() and sha(marker)==pin['dependencyMarkerSha256'] and deps.identity()==dependency['identity']
  verify_copy();assert sha(runtime)==pin['runtimeSha256']
  artifacts=[p for d in [build/'bin',build/'lib'] for p in d.glob('*') if p.is_file() and (p.name.startswith('codebrix_') or p.name=='libcodebrix_cycles_host.so')]
  if not a.target:assert len(artifacts)==len(HOST_TARGETS)+len(GATE_TARGETS)  # 34: 8 host outputs + 26 default gates
  state.update(complete=True,passed=True,phase='complete',artifacts={str(p.relative_to(build)):sha(p) for p in artifacts},executionProven=False,releaseCleared=False)
  print('linux-arm64: maintained native host and selected gates built',flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(work/'status.json',state)


if __name__=='__main__':main()
