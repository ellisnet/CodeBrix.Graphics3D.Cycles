#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""macOS ARM64 (Apple Silicon) build of the reviewed dependency snapshots.

Xcode's Apple Clang, macOS SDK, CMake and Python are external build tools. No
source is fetched, and Homebrew or /usr/local content is excluded from every
find and rejected in configured caches, compile commands and linked libraries.
OIIO is configured without any image format plugin; libraries it needs only to
configure are built here but must not be loaded by the shipped host.
"""
import argparse,importlib.util,json,os,platform,re,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
from android_toolchain import environment,sha
sys.path.insert(0,str(ROOT/'tools/dependencies'))
from macos_checks import EXCLUDED_PREFIXES,tool,check_isolation,linked,check_binary as check_macho
spec=importlib.util.spec_from_file_location('dependency_profile',ROOT/'tools/dependencies/build.py')
recipe=importlib.util.module_from_spec(spec);spec.loader.exec_module(recipe)
RID='osx-arm64';DEPLOYMENT_TARGET='15.0'
WORK=ROOT/'native/work/macos/osx-arm64/dependencies'
# libpng is optional to OIIO and is not built. zlib, libtiff, OpenEXR/Imath and
# libjpeg are configure-time requirements of the pinned OIIO.
SKIPPED=['png']
PATH_MAP=f'-ffile-prefix-map={ROOT}=.'


def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n')


def identity():
 clang=Path(tool('xcrun','-f','clang'));clangxx=Path(tool('xcrun','-f','clang++'));cmake=Path(shutil.which('cmake')).resolve()
 return dict(rid=RID,buildHost=platform.machine()+'-'+platform.system(),deploymentTarget=DEPLOYMENT_TARGET,
  sourceManifestSha256=sha(ROOT/'third_party/dependency-imports.json'),profileRecipeSha256=sha(ROOT/'tools/dependencies/build.py'),
  macosRecipeSha256=sha(Path(__file__)),environmentRecipeSha256=sha(ROOT/'tools/android_toolchain.py'),
  xcode=tool('xcodebuild','-version'),developerDir=tool('xcode-select','-p'),sdkVersion=tool('xcrun','--show-sdk-version'),
  sdkPath=tool('xcrun','--show-sdk-path'),compilerVersion=tool(str(clangxx),'--version'),cCompilerSha256=sha(clang),cxxCompilerSha256=sha(clangxx),
  cmakeVersion=tool(str(cmake),'--version').splitlines()[0],cmakeSha256=sha(cmake),
  scope='Native Apple Silicon build of the reviewed snapshots for the osx-arm64 Metal host; no image format plugins')


def options(prefix):
 settings=dict(CMAKE_BUILD_TYPE='Release',CMAKE_POSITION_INDEPENDENT_CODE='ON',CMAKE_EXPORT_COMPILE_COMMANDS='ON',
  CMAKE_INSTALL_PREFIX=prefix,CMAKE_PREFIX_PATH=prefix,CMAKE_OSX_ARCHITECTURES='arm64',CMAKE_OSX_DEPLOYMENT_TARGET=DEPLOYMENT_TARGET,
  # Link with the final relative rpath: a build-tree rpath would size the Mach-O
  # header by checkout path, shifting every section in the installed library.
  CMAKE_MACOSX_RPATH='ON',CMAKE_INSTALL_NAME_DIR='@rpath',CMAKE_INSTALL_RPATH='@loader_path',CMAKE_BUILD_WITH_INSTALL_RPATH='ON',
  CMAKE_FIND_FRAMEWORK='LAST',CMAKE_FIND_APPBUNDLE='NEVER',
  CMAKE_IGNORE_PREFIX_PATH=';'.join(EXCLUDED_PREFIXES),
  CMAKE_IGNORE_PATH=';'.join(p+'/'+d for p in EXCLUDED_PREFIXES for d in ['bin','lib','include','opt']),
  # CMake 4 rejects cmake_minimum_required below 3.5 in the jpeg, robin-map and
  # yaml-cpp snapshots; this only selects policy defaults for those projects.
  CMAKE_POLICY_VERSION_MINIMUM='3.5',
  BUILD_SHARED_LIBS='ON',BUILD_TESTING='OFF',FETCHCONTENT_FULLY_DISCONNECTED='ON',FETCHCONTENT_UPDATES_DISCONNECTED='ON')
 settings['CMAKE_INSTALL_LIBDIR:STRING']='lib'
 return settings


def oiio_plugins(source):
 return sorted(p.name[:-len('.imageio')] for p in (source/'oiio/src').glob('*.imageio') if p.is_dir())


def profile(source):
 rows=[]
 for name,path,settings in recipe.profile(source):
  if name in SKIPPED:continue
  settings=dict(settings)
  if name=='oiio':
   # Every format plugin is disabled; OIIO adds DISABLE_<NAME> for each one.
   settings.update({'ENABLE_'+plugin:'OFF' for plugin in oiio_plugins(source)})
   settings['USE_PNG']='OFF'
  if name in ['imath','openexr','oiio']:settings['CLANG_FORMAT_EXE:FILEPATH']='OFF'
  rows.append((name,path,settings))
 return rows


def environment_for(prefix,jobs):
 env=environment(prefix,jobs)
 for name in ['SDKROOT','MACOSX_DEPLOYMENT_TARGET','DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','LDFLAGS','CFLAGS','CXXFLAGS','CPPFLAGS']:
  env.pop(name,None)
 env['MACOSX_DEPLOYMENT_TARGET']=DEPLOYMENT_TARGET
 return env


def check_binary(path):check_macho(path,DEPLOYMENT_TARGET)


def libraries(prefix):
 return {str(p.relative_to(prefix)):sha(p) for p in sorted((prefix/'lib').rglob('*'))
  if p.is_file() and not p.is_symlink() and (p.suffix in ('.dylib','.a'))}


def registered_plugins(prefix):
 """Format plugins register <name>_input/output_imageio_create factories."""
 names=set()
 for library in (prefix/'lib').glob('libOpenImageIO*.dylib'):
  if library.is_symlink():continue
  for line in tool('nm','-gU',str(library)).splitlines():
   match=re.search(r'_([A-Za-z0-9]+)_(?:input|output)_imageio_create',line)
   if match:names.add(match.group(1))
 return sorted(names)


def prefix():
 """Reuse only the complete dependency prefix matching current source/tool inputs."""
 recipe.verify();installed=WORK/'install';marker=installed/'codebrix-osx-arm64-build.json'
 if not marker.exists():raise RuntimeError('Build macOS ARM64 dependencies first: python3 tools/dependencies/build_macos.py')
 report=json.loads(marker.read_text())
 if report['identity']!=identity():raise RuntimeError('macOS ARM64 dependency inputs/toolchain changed; rebuild the prefix')
 for name,digest in report['libraries'].items():
  if sha(installed/name)!=digest:raise RuntimeError('Changed macOS ARM64 dependency library: '+name)
 return installed


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--jobs',type=int,default=8);args=parser.parse_args()
 if platform.system()!='Darwin' or platform.machine()!='arm64':raise RuntimeError('This recipe requires an Apple Silicon macOS build host')
 manifest=recipe.verify();WORK.mkdir(parents=True,exist_ok=True);source=WORK/'sources';prefix=WORK/'install';prefix.mkdir(exist_ok=True)
 expected={str(Path(c['id'])/f['path']):f['sha256'] for c in manifest['components'] for f in c['files']}
 if not source.exists():shutil.copytree(recipe.SOURCE,source)
 def verify_copy():
  assert all(sha(source/name)==digest for name,digest in expected.items()),'macOS dependency source copy changed'
 verify_copy();pin=identity();save(WORK/'provenance.json',pin)
 env=environment_for(prefix,args.jobs);completed=[];state=dict(complete=False,completedComponents=completed,identity=pin,skipped=SKIPPED)
 try:
  for name,path,settings in profile(source):
   build=WORK/name;build.mkdir(exist_ok=True)
   values=options(prefix);values.update(settings)
   # Build paths never reach the binaries, so any checkout location gives the same bytes.
   for flags in ['CMAKE_C_FLAGS','CMAKE_CXX_FLAGS']:values[flags]=(PATH_MAP+' '+values.get(flags,'')).strip()
   commands=[['cmake','-S',str(path),'-B',str(build),*['-D'+k+'='+str(v) for k,v in values.items()]],
    ['cmake','--build',str(build),'--parallel',str(args.jobs)],['cmake','--install',str(build)]]
   save(build/'commands.json',commands)
   for step,command in zip(['configure','build','install'],commands):
    state.update(component=name,step=step);save(WORK/'status.json',state)
    with (build/(step+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
    if step=='configure':check_isolation(build)
    if step=='install':assert all(Path(p).resolve().is_relative_to(prefix.resolve()) for p in (build/'install_manifest.txt').read_text().splitlines())
   check_isolation(build);completed.append(name);print(RID,name,'built and installed',flush=True)
  recipe.verify();verify_copy();assert pin==identity()
  for library in (prefix/'lib').glob('*.dylib'):
   if not library.is_symlink():check_binary(library)
  plugins=registered_plugins(prefix)
  if plugins:raise RuntimeError('OIIO still registers format plugins: '+', '.join(plugins))
  found=libraries(prefix)
  save(prefix/'codebrix-osx-arm64-build.json',dict(identity=pin,libraries=found,skippedComponents=SKIPPED,
   oiioDisabledPlugins=oiio_plugins(source),oiioRegisteredFormatPlugins=plugins))
  state.update(complete=True,passed=True,libraries=found)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(WORK/'status.json',state)


if __name__=='__main__':main()
