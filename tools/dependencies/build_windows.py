#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Windows (win-x64, win-arm64) build of the reviewed dependency snapshots.

    set PYTHONUTF8=1
    python tools/dependencies/build_windows.py --rid win-x64 --jobs 16

The Windows sibling of build_macos.py. It reuses that recipe's component profile
(libpng not built, every OIIO format plugin disabled: the Windows product reads
and writes no image files) and changes only what Windows needs: clang-cl and
lld-link from Visual Studio with Visual Studio's CMake and Ninja
(tools/windows_toolchain.py), the hybrid C runtime (no Visual C++
Redistributable), the JPEG library's SIMD off (no NASM, the verified scalar
build), its own work tree and prefix marker, and PE checks of every installed
DLL. build_macos.py and build.py stay unchanged because their hashes are part of
the osx-arm64 prefix identity and provenance. The snapshot is verified with
tools/windows_sources.py (the same checks as dependency_sources.py with '/'
paths).
"""
import argparse,importlib.util,json,re,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import windows_toolchain as wt
from windows_sources import verify_dependencies,DEST as SOURCE
sys.path.insert(0,str(ROOT/'tools/dependencies'))
spec=importlib.util.spec_from_file_location('macos_arm64_dependencies',ROOT/'tools/dependencies/build_macos.py')
macos=importlib.util.module_from_spec(spec);spec.loader.exec_module(macos)
SKIPPED=macos.SKIPPED
sha=wt.sha


def work(rid):return ROOT/'native/work/windows'/rid/'dependencies'
def marker_name(rid):return f'codebrix-{rid}-build.json'
def save(path,value):Path(path).write_text(json.dumps(value,indent=2,default=str)+'\n')


# CMake's own initial flags for MSVC-compatible compilers, which a -DCMAKE_*_FLAGS
# value replaces; /EHsc is C++ exception handling.
DEFAULT_FLAGS={'CMAKE_C_FLAGS':'/DWIN32 /D_WINDOWS','CMAKE_CXX_FLAGS':'/DWIN32 /D_WINDOWS /EHsc'}


def cmake_value(value):
 # CMake reads '\' in a path as an escape; paths go to CMake with '/'.
 return value.as_posix() if isinstance(value,Path) else str(value)


def path_map():
 # Build paths never reach the objects (checkout-independent bytes, as on macOS).
 return f'/clang:-ffile-prefix-map={ROOT}=.'


def identity(toolchain):
 return dict(**toolchain.identity(),sourceManifestSha256=sha(ROOT/'third_party/dependency-imports.json'),
  profileRecipeSha256=sha(ROOT/'tools/dependencies/build.py'),macosProfileRecipeSha256=sha(ROOT/'tools/dependencies/build_macos.py'),
  windowsRecipeSha256=sha(Path(__file__)),toolchainRecipeSha256=sha(ROOT/'tools/windows_toolchain.py'),
  sourceVerifierSha256=sha(ROOT/'tools/windows_sources.py'),
  replacements={target:dict(replacement=original,sha256=sha(ROOT/original)) for target,original in REPLACEMENTS.items()},
  scope='Windows build of the reviewed snapshots for the WGSL/WebGPU host; no image format plugins; hybrid CRT')


def options(toolchain,prefix):
 slash=lambda p:str(p).replace('\\','/')
 settings=dict(CMAKE_BUILD_TYPE='Release',CMAKE_EXPORT_COMPILE_COMMANDS='ON',CMAKE_INSTALL_PREFIX=slash(prefix),CMAKE_PREFIX_PATH=slash(prefix),
  # CMake 4 rejects cmake_minimum_required below 3.5 in the jpeg, robin-map and
  # yaml-cpp snapshots; this only selects policy defaults for those projects.
  CMAKE_POLICY_VERSION_MINIMUM='3.5',
  BUILD_SHARED_LIBS='ON',BUILD_TESTING='OFF',FETCHCONTENT_FULLY_DISCONNECTED='ON',FETCHCONTENT_UPDATES_DISCONNECTED='ON')
 settings['CMAKE_INSTALL_LIBDIR:STRING']='lib'
 settings.update(toolchain.cmake_options())
 return settings


# Independent replacements (tools/dependencies/windows/) for Windows-only build
# helpers the reviewed profile excludes. They are placed into the work copy of the
# snapshot, never into third_party/, and recorded in the prefix provenance.
REPLACEMENTS={'zstd/build/VS2010/libzstd-dll/libzstd-dll.rc':'tools/dependencies/windows/zstd/libzstd-dll.rc',
 'expat/expat/win32/version.rc.cmake':'tools/dependencies/windows/expat/version.rc.cmake',
 'robin-map/tsl-robin-map.natvis':'tools/dependencies/windows/robin-map/tsl-robin-map.natvis'}


def place_replacements(source):
 placed={}
 for target,original in REPLACEMENTS.items():
  destination=source/target;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/original,destination)
  placed[target]=dict(replacement=original,sha256=sha(ROOT/original))
 return placed


def profile(source):
 rows=[]
 for name,path,settings in macos.profile(source):
  settings=dict(settings)
  # No NASM: the verified build uses the scalar JPEG fallback on every RID.
  if name=='jpeg':
   settings['WITH_SIMD']='OFF'
   # libjpeg-turbo's DLL target forces /NODEFAULTLIB:LIBCMT (it assumes the DLL
   # C runtime), which the hybrid CRT cannot link. Nothing in the product decodes
   # JPEG (OIIO's plugins are off; the macOS payload has no libjpeg either), so
   # the configure-time requirement is met by the static library.
   settings.update(ENABLE_SHARED='OFF',ENABLE_STATIC='ON')
  # OpenEXRCore passes size_t* where zlib takes uLongf* (unsigned long: 64-bit on
  # Linux and macOS, 32-bit on Windows); Clang 22 makes that an error by default.
  # Those paths decode EXR files, which the Windows product never reads (OIIO's
  # format plugins are disabled; OpenEXR is a configure-time requirement of OIIO).
  # expat sets CMAKE_MSVC_RUNTIME_LIBRARY itself; its static-CRT switch selects /MT.
  if name=='expat':settings['EXPAT_MSVC_STATIC_CRT']='ON'
  if name=='openexr':settings['CMAKE_C_FLAGS']=(settings.get('CMAKE_C_FLAGS','')+' /clang:-Wno-error=incompatible-pointer-types').strip()
  rows.append((name,path,settings))
 return rows


def profile_for(rid,source):
 rows=[]
 for name,path,settings in profile(source):
  settings=dict(settings)
  # OpenEXR enables its SSE4.1 zip filter from the MSVC predefines; clang-cl then
  # needs the target feature. x86-64 Cycles requires SSE4.2 already (its CPU
  # kernels and the fixed-baseline oracle are SSE4.2).
  if name=='openexr' and rid=='win-x64':
   for flags in ['CMAKE_C_FLAGS','CMAKE_CXX_FLAGS']:settings[flags]=(settings.get(flags,'')+' /clang:-msse4.2').strip()
  rows.append((name,path,settings))
 return rows


def libraries(prefix):
 return {p.relative_to(prefix).as_posix():sha(p) for d in ['bin','lib'] for p in sorted((prefix/d).rglob('*'))
  if p.is_file() and p.suffix.lower() in ('.dll','.lib')}


def registered_plugins(toolchain,prefix):
 """Format plugins export <name>_input/output_imageio_create factories."""
 names=set()
 for library in (prefix/'bin').glob('OpenImageIO*.dll'):
  for match in re.finditer(r'([A-Za-z0-9]+)_(?:input|output)_imageio_create',toolchain.tool(toolchain.readobj,'--coff-exports',str(library))):
   names.add(match.group(1))
 return sorted(names)


def check_build_inputs(toolchain,build):
 """Headers the compiler actually read (Ninja's dependency log)."""
 result=subprocess.run([str(toolchain.ninja),'-C',str(build),'-t','deps'],capture_output=True,text=True,env=toolchain.environment())
 bad=sorted({line.strip() for line in result.stdout.splitlines() if wt.excluded(line)})
 if bad:raise RuntimeError(f'Excluded distribution read by {build.name}: {bad[:5]}')
 return sum(1 for line in result.stdout.splitlines() if line.startswith(' '))


def prefix(rid):
 """Reuse only the complete dependency prefix matching current source/tool inputs."""
 verify_dependencies();toolchain=wt.Toolchain(rid);installed=work(rid)/'install';marker=installed/marker_name(rid)
 if not marker.exists():raise RuntimeError(f'Build {rid} dependencies first: python tools/dependencies/build_windows.py --rid {rid}')
 report=json.loads(marker.read_text())
 if report['identity']!=identity(toolchain):raise RuntimeError(f'{rid} dependency inputs/toolchain changed; rebuild the prefix')
 for name,digest in report['libraries'].items():
  if sha(installed/name)!=digest:raise RuntimeError(f'Changed {rid} dependency library: '+name)
 return installed


def main():
 parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 parser.add_argument('--rid',choices=sorted(wt.TARGETS),default='win-x64');parser.add_argument('--jobs',type=int,default=8)
 parser.add_argument('--only',nargs='+',help='Development: build only these components (no prefix marker is written)')
 args=parser.parse_args()
 wt.require_windows();toolchain=wt.Toolchain(args.rid);WORK=work(args.rid)
 manifest=verify_dependencies();WORK.mkdir(parents=True,exist_ok=True);source=WORK/'sources';installed=WORK/'install';installed.mkdir(exist_ok=True)
 expected={c['id']+'/'+f['path']:f['sha256'] for c in manifest['components'] for f in c['files']}
 if not source.exists():shutil.copytree(SOURCE,source)
 def verify_copy():
  changed=[name for name,digest in expected.items() if sha(source/name)!=digest]
  if changed:raise RuntimeError(f'{args.rid} dependency source copy changed: {changed[:5]}')
 verify_copy();place_replacements(source);pin=identity(toolchain);save(WORK/'provenance.json',pin)
 env=toolchain.environment(args.jobs);completed=[];state=dict(complete=False,completedComponents=completed,identity=pin,skipped=SKIPPED)
 try:
  depfiles={}
  for name,path,settings in profile_for(args.rid,source):
   if args.only and name not in args.only:continue
   build=WORK/name;build.mkdir(exist_ok=True)
   values=options(toolchain,installed);values.update(settings)
   for flags in ['CMAKE_C_FLAGS','CMAKE_CXX_FLAGS']:values[flags]=(DEFAULT_FLAGS[flags]+' '+path_map()+' '+values.get(flags,'')).strip()
   commands=[[str(toolchain.cmake),'-G','Ninja','-S',str(path),'-B',str(build),*['-D'+k+'='+cmake_value(v) for k,v in values.items()]],
    [str(toolchain.cmake),'--build',str(build),'--parallel',str(args.jobs)],[str(toolchain.cmake),'--install',str(build)]]
   save(build/'commands.json',commands)
   for step,command in zip(['configure','build','install'],commands):
    state.update(component=name,step=step);save(WORK/'status.json',state)
    with (build/(step+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
    if step=='configure':wt.check_isolation(build)
    if step=='install':
     outside=[p for p in (build/'install_manifest.txt').read_text().splitlines() if not Path(p).resolve().is_relative_to(installed.resolve())]
     if outside:raise RuntimeError(f'{name} installed outside the prefix: {outside[:3]}')
   wt.check_isolation(build);depfiles[name]=check_build_inputs(toolchain,build);completed.append(name);print(args.rid,name,'built and installed',flush=True)
  if args.only:state.update(complete=True,passed=None,note='partial development build; no prefix marker');return
  verify_dependencies();verify_copy()
  if pin!=identity(toolchain):raise RuntimeError('The toolchain or recipe changed during the build')
  dlls=sorted((installed/'bin').glob('*.dll'));names=[d.name for d in dlls]
  for dll in dlls:wt.check_binary(toolchain,dll,names)
  plugins=registered_plugins(toolchain,installed)
  if plugins:raise RuntimeError('OIIO still registers format plugins: '+', '.join(plugins))
  found=libraries(installed)
  save(installed/marker_name(args.rid),dict(identity=pin,libraries=found,skippedComponents=SKIPPED,
   oiioDisabledPlugins=macos.oiio_plugins(source),oiioRegisteredFormatPlugins=plugins,compilerDependencyFiles=depfiles))
  state.update(complete=True,passed=True,libraries=found,compilerDependencyFiles=depfiles)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(WORK/'status.json',state)


if __name__=='__main__':main()
