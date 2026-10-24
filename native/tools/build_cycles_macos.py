#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the osx-arm64 Cycles host with Cycles' Metal and CPU devices.

The pinned snapshot is used unchanged; only the shared registration/color
overlay is generated. Metal kernels are compiled by Cycles at run time, so the
include closure of kernel.metal is staged beside the host library.
"""
import argparse,difflib,importlib.util,json,re,shutil,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];sys.path.insert(0,str(REPO/'native/tools'));sys.path.insert(0,str(REPO/'tools'))
from cycles_source import SOURCE,verify as verify_cycles
from cycles_overlay import registration_overlay
spec=importlib.util.spec_from_file_location('macos_dependencies',REPO/'tools/dependencies/build_macos.py')
deps=importlib.util.module_from_spec(spec);spec.loader.exec_module(deps)
sha,save,tool=deps.sha,deps.save,deps.tool
WORK=REPO/'native/work/macos/osx-arm64/host'
HOST_TARGETS=['codebrix_cycles_host','codebrix_host_cpu_test','codebrix_host_gpu_test','codebrix_host_reload_test',
 'codebrix_host_concurrency_test','codebrix_host_session_reference','codebrix_host_session64_reference']
# Linux gates that test WGSL kernels, plus the image oracle that writes and
# reads EXR/PNG through OIIO, do not apply to this profile.
SKIPPED_TARGETS={'codebrix_host_image_reference':'writes and reads EXR/PNG through OIIO, whose format plugins are disabled',
 'WGSL device and render gates':'exercise the WebGPU device, which this profile does not contain'}
KERNEL_ROOT='codebrix-cycles/source'
KERNEL_ENTRY='kernel/device/metal/kernel.metal'
INCLUDE=re.compile(r'^\s*#\s*include\s+"([^"]+)"',re.M)


def kernel_closure(src):
 """Mirror Cycles' path_source_replace_includes: base first, then the includer.
 Like Cycles, leave unresolved includes (system or inactive #if branches) for
 the Metal compiler; the run-time Metal compile is the authoritative check."""
 pending=[KERNEL_ENTRY];found=set();unresolved=set()
 while pending:
  name=pending.pop()
  if name in found:continue
  found.add(name)
  for include in INCLUDE.findall((src/name).read_text()):
   for candidate in [Path(include),Path(name).parent/include]:
    if (src/candidate).is_file():pending.append(str(Path(*[p for p in candidate.parts if p!='.'])));break
   else:unresolved.add(f'{name}: {include}')
 return sorted(found),sorted(unresolved)


def stage_kernels(src,destination,overlay_names):
 names,unresolved=kernel_closure(src)
 shadowed=sorted(set(names)&set(overlay_names))
 if shadowed:raise RuntimeError('Metal kernel closure includes overlaid files: '+', '.join(shadowed))
 root=destination/KERNEL_ROOT
 if root.exists():shutil.rmtree(root)
 for name in names:
  target=root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src/name,target)
 return {name:sha(root/name) for name in names},unresolved


def exports(library):
 symbols=sorted(line.split()[-1] for line in tool('nm','-gU',str(library)).splitlines() if ' T ' in line)
 expected=sorted((REPO/'native/host/exports.macos').read_text().split())
 if symbols!=expected:raise RuntimeError(f'Unexpected host exports: {sorted(set(symbols)^set(expected))}')
 return symbols


def closure(library,prefix):
 """Libraries the host loads from the dependency prefix, by install name."""
 seen={};pending=[library]
 while pending:
  current=pending.pop()
  for name in deps.linked(current):
   if not name.startswith('@rpath/'):continue
   leaf=name[len('@rpath/'):]
   if leaf==library.name or leaf in seen:continue
   path=prefix/'lib'/leaf
   if not path.exists():raise RuntimeError(f'{current.name} needs {name}, absent from the dependency prefix')
   seen[leaf]=str(path.resolve().relative_to(prefix.resolve()));pending.append(path)
 return dict(sorted(seen.items()))


def platform_source():
 """Verified Cycles copy with the Apple ARM64 adaptation; the snapshot stays unchanged."""
 source=WORK/'cycles-source'
 if not source.exists():shutil.copytree(SOURCE,source)
 # Upstream's NEON half conversion relies on sse2neon to include arm_neon.h;
 # this profile has no sse2neon (same adaptation as the Linux ARM64 recipe).
 changed=['src/util/half.h'];before={name:(SOURCE/name).read_text() for name in changed};after=dict(before)
 old='#pragma once\n'
 if after[changed[0]].count(old)!=1:raise RuntimeError('half.h adaptation anchor changed')
 after[changed[0]]=after[changed[0]].replace(old,old+'\n#if defined(__ARM_NEON) && !defined(__KERNEL_GPU__)\n#  include <arm_neon.h>\n#endif\n')
 for name in changed:
  after[name]='// Copyright (c) 2026 Jeremy Ellis and contributors\n// Apple ARM64 adaptation; upstream notices follow.\n'+after[name]
  if (source/name).read_text()!=after[name]:(source/name).write_text(after[name])
 patch=''.join(''.join(difflib.unified_diff(before[n].splitlines(True),after[n].splitlines(True),fromfile='a/'+n,tofile='b/'+n)) for n in changed)
 (WORK/'platform.patch').write_text(patch)
 manifest=json.loads((REPO/'third_party/cycles-import.json').read_text())
 expected={row['path']:(sha(source/row['path']) if row['path'] in changed else row['sha256']) for row in manifest['files']}
 def verify():
  if {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}!=set(expected):raise RuntimeError('Cycles source copy has unexpected files')
  if any(sha(source/n)!=d for n,d in expected.items()):raise RuntimeError('Cycles source copy changed')
 verify();return source,changed,verify


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--jobs',type=int,default=8);p.add_argument('--target',action='append');a=p.parse_args()
 WORK.mkdir(parents=True,exist_ok=True);state=dict(complete=False,phase='prepare',rid=deps.RID);save(WORK/'status.json',state)
 try:
  prefix=deps.prefix();marker=prefix/'codebrix-osx-arm64-build.json';dependency=json.loads(marker.read_text())
  verify_cycles();generated=registration_overlay(WORK/'overlay');source,adapted,verify_source=platform_source()
  overlay_files={str(p.relative_to(generated)):sha(p) for p in sorted(generated.rglob('*')) if p.is_file()}
  values=deps.options(prefix)
  for name in ['CMAKE_INSTALL_PREFIX','CMAKE_INSTALL_NAME_DIR','CMAKE_POLICY_VERSION_MINIMUM','BUILD_SHARED_LIBS','BUILD_TESTING','CMAKE_INSTALL_LIBDIR:STRING']:
   values.pop(name)
  # Only checkout-independent rpaths: build/lib and build/bin reach the host
  # library and the dependency prefix relatively; staging keeps @loader_path.
  values['CMAKE_INSTALL_RPATH']='@loader_path;@loader_path/../lib;@loader_path/../../../dependencies/install/lib'
  values.update(WITH_LIBS_PRECOMPILED='OFF',WITH_LEGACY_LIBRARIES='ON',
   # Native-only adds -march=native, which would tie the CPU kernels to the
   # build machine's Apple core; the shipped binary must run on every M-series.
   WITH_CYCLES_NATIVE_ONLY='OFF',WITH_CYCLES_DEVICE_METAL='ON',WITH_CYCLES_PATH_GUIDING='OFF',
   # Apple Clang accepts the -msse4.2 probe for arm64, so Cycles would add x86
   # SSE/AVX flags. Upstream avoids that branch only when sse2neon is present,
   # which this profile omits; record Cycles' own probe results as absent.
   **{'CXX_HAS_SSE42:BOOL':'OFF','CXX_HAS_AVX2:BOOL':'OFF','CXX_HAS_F16C:BOOL':'OFF'},
   OpenImageIO_DIR=prefix/'lib/cmake/OpenImageIO',OpenColorIO_DIR=prefix/'lib/cmake/OpenColorIO',
   CMAKE_PROJECT_Cycles_INCLUDE=REPO/'native/tools/inject_cycles_macos.cmake',CODEBRIX_CYCLES_OVERLAY=generated,
   # Line tables give symbolicated crash reports from the retained dSYM; Clang's
   # debug information does not change generated code.
   # Prefix maps keep build paths out of strings, line tables and the debug map.
   CMAKE_C_FLAGS='-gline-tables-only '+deps.PATH_MAP,CMAKE_CXX_FLAGS='-gline-tables-only '+deps.PATH_MAP,
   CMAKE_OBJCXX_FLAGS='-gline-tables-only '+deps.PATH_MAP,CODEBRIX_OSO_PREFIX=f'{REPO}/')
  values.update({'WITH_CYCLES_'+n:'OFF' for n in ['ALEMBIC','EMBREE','OPENIMAGEDENOISE','OPENSUBDIV','OPENVDB','NANOVDB','OSL','USD',
   'HYDRA_RENDER_DELEGATE','DEVICE_CUDA','DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI']})
  build=WORK/'build'
  commands=[['cmake','-S',str(source),'-B',str(build),*['-D'+k+'='+str(v) for k,v in values.items()]],
   ['cmake','--build',str(build),'--target',*(a.target or HOST_TARGETS),'--parallel',str(a.jobs)]]
  pin=dict(identity=deps.identity(),cyclesImportSha256=sha(REPO/'third_party/cycles-import.json'),dependencyMarkerSha256=sha(marker),
   recipeSha256=sha(Path(__file__)),injectionSha256=sha(REPO/'native/tools/inject_cycles_macos.cmake'),
   hostSourceSha256={str(p.relative_to(REPO)):sha(p) for p in sorted((REPO/'native/host').glob('*')) if p.is_file() and p.suffix in ('.cpp','.h','.inc','.cmake','.macos')},
   overlaySha256=overlay_files,platformPatchSha256=sha(WORK/'platform.patch'),adaptedFiles=adapted,commands=commands,skippedTargets=SKIPPED_TARGETS)
  save(WORK/'invocation.json',pin);env=deps.environment_for(prefix,a.jobs)
  for step,command in zip(['configure','build'],commands):
   state['phase']=step;save(WORK/'status.json',state)
   with (WORK/(step+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
   deps.check_isolation(build)
   if step=='configure':
    cache=(build/'CMakeCache.txt').read_text()
    for required in ['WITH_CYCLES_DEVICE_METAL:BOOL=ON','WITH_CYCLES_NATIVE_ONLY:BOOL=OFF']:
     if required not in cache:raise RuntimeError('Configured cache lacks '+required)
  rows=json.loads((build/'compile_commands.json').read_text())
  native=[row['file'] for row in rows if re.search(r'-m(arch|cpu|tune)=native|-msse|-mavx|-mf16c',row.get('command',''))]
  if native:raise RuntimeError(f'Host-specific or x86 CPU flags in {native[:3]}')
  # Fixed-baseline CPU oracle (native/DEVICE.md): the last -ffp-contract and
  # reciprocal-math options on the CPU kernel line must switch both off
  # (inject_cycles_macos.cmake). UNTESTED on a Mac so far.
  kernel=[row.get('command','').split() for row in rows if row['file'].endswith('src/kernel/device/cpu/kernel.cpp')]
  contract=[w for w in kernel[0] if w.startswith('-ffp-contract')] if kernel else [];reciprocal=[w for w in kernel[0] if w.endswith('reciprocal-math')] if kernel else []
  if len(kernel)!=1 or contract[-1:]!=['-ffp-contract=off'] or reciprocal[-1:]!=['-fno-reciprocal-math']:
   raise RuntimeError('CPU kernels are not at the fixed oracle baseline (no contraction, no reciprocal math)')
  if 'WITH_WEBGPU' in json.dumps(rows):raise RuntimeError('WebGPU code was configured into the Metal profile')
  host=build/'lib/libcodebrix_cycles_host.dylib';deps.check_binary(host)
  symbols=exports(host);loaded=closure(host,prefix)
  forbidden=[name for name in loaded if re.match(r'lib(jpeg|png|tiff|webp|gif|heif|raw|openjp|avif|jxl)',name)]
  if forbidden:raise RuntimeError('Image codec libraries are loaded by the host: '+', '.join(forbidden))
  # Shipped kernel sources are the pristine snapshot; the half.h adaptation is
  # CPU-only and excluded from GPU compilation by __KERNEL_GPU__.
  verify_source();verify_cycles();kernels,unresolved=stage_kernels(SOURCE/'src',build/'lib',overlay_files)
  save(WORK/'kernel-sources.json',dict(root=KERNEL_ROOT,entry=KERNEL_ENTRY,files=kernels,unresolvedIncludes=unresolved))
  artifacts={str(p.relative_to(build)):sha(p) for d in [build/'bin',build/'lib'] for p in sorted(d.glob('*'))
   if p.is_file() and (p.name.startswith('codebrix_') or p.name=='libcodebrix_cycles_host.dylib')}
  if not a.target:assert len(artifacts)==len(HOST_TARGETS),artifacts
  state.update(complete=True,passed=True,phase='complete',artifacts=artifacts,exports=len(symbols),dependencyClosure=loaded,
   kernelSourceFiles=len(kernels),executionProven=False,releaseCleared=False)
  print('osx-arm64: Metal host and lifecycle targets built',flush=True)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(WORK/'status.json',state)


if __name__=='__main__':main()
