#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""macOS x64 (Intel) build of the reviewed dependency snapshots.

The osx-x64 sibling of build_macos.py. It reuses that recipe's component profile
(libpng not built, every OIIO format plugin disabled) and changes only what the
target needs: x86_64 objects, its own work tree and prefix marker, and a build-host
check for an Intel Mac. build_macos.py stays unchanged because its own hash is part
of the osx-arm64 prefix identity and provenance.

Homebrew lives in /usr/local on Intel Macs and in /opt/homebrew on Apple Silicon.
Both prefixes are excluded from every find and rejected in configured caches,
compile and link commands, compiler dependency files and linked libraries. On an
Intel Mac /usr/local also holds Homebrew's own CMake and Python; those programs
are tools, never inputs.
"""
import argparse,importlib.util,json,platform,re,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
from android_toolchain import sha
sys.path.insert(0,str(ROOT/'tools/dependencies'))
from macos_checks import EXCLUDED_PREFIXES,excluded,tool,check_isolation,linked
spec=importlib.util.spec_from_file_location('macos_arm64_dependencies',ROOT/'tools/dependencies/build_macos.py')
arm64=importlib.util.module_from_spec(spec);spec.loader.exec_module(arm64)
recipe=arm64.recipe
RID='osx-x64';ARCH='x86_64'
# The oldest macOS that .NET 10 supports, as for osx-arm64. The host and the .NET
# wrapper refuse older systems on every Apple RID; the shipped WebGPU runtime's
# osx-x64 dylib declares 11.0, which this target satisfies.
DEPLOYMENT_TARGET='15.0'
WORK=ROOT/'native/work/macos/osx-x64/dependencies'
MARKER='codebrix-osx-x64-build.json'
SKIPPED=arm64.SKIPPED
PATH_MAP=f'-ffile-prefix-map={ROOT}=.'
save=arm64.save


def require_build_host():
 """Darwin on an Intel CPU only. Under Rosetta, CMake's host processor and the
 x86 reference-option checks are not established; refuse rather than guess."""
 if platform.system()!='Darwin':raise SystemExit(f'{RID}: this recipe runs only on macOS (an Intel Mac); this host is {platform.system()}')
 if platform.machine()!='x86_64':raise SystemExit(f'{RID}: this recipe requires an Intel (x86_64) macOS build host; this host is {platform.machine()}')
 translated=subprocess.run(['sysctl','-n','sysctl.proc_translated'],capture_output=True,text=True).stdout.strip()
 if translated=='1':raise SystemExit(f'{RID}: refusing to build under Rosetta translation; use a native Intel Mac')


def xcode_version():
 """Xcode's version, or the Command Line Tools package version when only the CLT
 are installed (xcodebuild then refuses to run)."""
 try:return tool('xcodebuild','-version')
 except subprocess.CalledProcessError:
  info=tool('pkgutil','--pkg-info=com.apple.pkg.CLTools_Executables')
  return 'Command Line Tools '+re.search(r'^version: (\S+)',info,re.M).group(1)


def sdk_path():return tool('xcrun','--show-sdk-path')


def identity():
 clang=Path(tool('xcrun','-f','clang'));clangxx=Path(tool('xcrun','-f','clang++'));cmake=Path(shutil.which('cmake')).resolve()
 return dict(rid=RID,buildHost=platform.machine()+'-'+platform.system(),deploymentTarget=DEPLOYMENT_TARGET,
  sourceManifestSha256=sha(ROOT/'third_party/dependency-imports.json'),profileRecipeSha256=sha(ROOT/'tools/dependencies/build.py'),
  macosProfileRecipeSha256=sha(ROOT/'tools/dependencies/build_macos.py'),macosRecipeSha256=sha(Path(__file__)),
  environmentRecipeSha256=sha(ROOT/'tools/android_toolchain.py'),checksSha256=sha(ROOT/'tools/dependencies/macos_checks.py'),
  xcode=xcode_version(),developerDir=tool('xcode-select','-p'),sdkVersion=tool('xcrun','--show-sdk-version'),
  sdkPath=sdk_path(),compilerVersion=tool(str(clangxx),'--version'),cCompilerSha256=sha(clang),cxxCompilerSha256=sha(clangxx),
  cmakeVersion=tool(str(cmake),'--version').splitlines()[0],cmakeSha256=sha(cmake),cmakePath=str(cmake),
  homebrewPrefixesPresent=[p for p in EXCLUDED_PREFIXES if Path(p).is_dir()],
  scope='Native Intel build of the reviewed snapshots for the osx-x64 WGSL/WebGPU host; no image format plugins')


def options(prefix):
 settings=arm64.options(prefix)
 # CMake 4 no longer picks a sysroot on macOS; without one Apple Clang searches
 # /usr/local/include (Homebrew on an Intel Mac) before the SDK.
 settings.update(CMAKE_OSX_ARCHITECTURES=ARCH,CMAKE_OSX_DEPLOYMENT_TARGET=DEPLOYMENT_TARGET,CMAKE_OSX_SYSROOT=sdk_path())
 return settings


def profile(source):
 rows=[]
 for name,path,settings in arm64.profile(source):
  # No NASM, as on Windows: the scalar JPEG fallback. On an Intel Mac CMake would
  # otherwise find Homebrew's nasm in /usr/local/bin.
  if name=='jpeg':settings=dict(settings,WITH_SIMD='OFF')
  rows.append((name,path,settings))
 return rows


def environment_for(prefix,jobs):
 env=arm64.environment_for(prefix,jobs)
 env['MACOSX_DEPLOYMENT_TARGET']=DEPLOYMENT_TARGET
 # An explicit SDK drops /usr/local/include from Apple Clang's default search.
 env['SDKROOT']=sdk_path()
 # Homebrew's shell setup exports these on Intel Macs; none may steer a build.
 for name in ['HOMEBREW_PREFIX','HOMEBREW_CELLAR','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','OBJC_INCLUDE_PATH']:env.pop(name,None)
 return env


def check_compiler_search(env):
 """Apple Clang's default search list must not reach a Homebrew prefix. On an
 Intel Mac /usr/local/include would otherwise be searched before the SDK."""
 result=subprocess.run([tool('xcrun','-f','clang++'),'-x','c++','-arch',ARCH,'-v','-E','-'],input='',capture_output=True,text=True,env=env)
 lines=result.stderr.split('#include <...> search starts here:')[-1].split('End of search list.')[0].split('\n')
 paths=[line.strip() for line in lines if line.strip()]
 bad=[p for p in paths if excluded(p) and not p.startswith(sdk_path())]
 if bad:raise RuntimeError('The compiler searches an excluded prefix by default: '+', '.join(bad))
 return paths


def check_build_inputs(build):
 """Headers the compiler actually read (dependency files) and link commands."""
 problems=[]
 for depfile in list(build.rglob('*.o.d'))+list(build.rglob('*.d')):
  if not depfile.is_file():continue
  text=depfile.read_text(errors='replace')
  if excluded(text):problems.append(str(depfile.relative_to(build)))
 for link in build.rglob('link.txt'):
  if excluded(link.read_text(errors='replace')):problems.append(str(link.relative_to(build)))
 if problems:raise RuntimeError(f'Excluded prefix read or linked by {build.name}: {problems[:5]}')
 return len(list(build.rglob('*.o.d')))


def check_binary(path):
 for name in linked(path):
  if not (name.startswith('@rpath/') or name.startswith('/usr/lib/') or name.startswith('/System/Library/')):
   raise RuntimeError(f'{path.name} links outside the reviewed prefix and the OS: {name}')
 minos=re.findall(r'minos (\S+)',tool('otool','-l',str(path)))
 if minos and any(v!=DEPLOYMENT_TARGET for v in minos):raise RuntimeError(f'{path.name} has minimum OS {minos}')
 archs=tool('lipo','-archs',str(path)).split()
 if archs!=[ARCH]:raise RuntimeError(f'{path.name} architectures are {archs}, expected only {ARCH}')


def libraries(prefix):return arm64.libraries(prefix)


def prefix():
 """Reuse only the complete dependency prefix matching current source/tool inputs."""
 recipe.verify();installed=WORK/'install';marker=installed/MARKER
 if not marker.exists():raise RuntimeError('Build macOS x64 dependencies first: python3 tools/dependencies/build_macos_x64.py')
 report=json.loads(marker.read_text())
 if report['identity']!=identity():raise RuntimeError('macOS x64 dependency inputs/toolchain changed; rebuild the prefix')
 for name,digest in report['libraries'].items():
  if sha(installed/name)!=digest:raise RuntimeError('Changed macOS x64 dependency library: '+name)
 return installed


def main():
 parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 parser.add_argument('--jobs',type=int,default=8);args=parser.parse_args()
 require_build_host()
 manifest=recipe.verify();WORK.mkdir(parents=True,exist_ok=True);source=WORK/'sources';installed=WORK/'install';installed.mkdir(exist_ok=True)
 expected={str(Path(c['id'])/f['path']):f['sha256'] for c in manifest['components'] for f in c['files']}
 if not source.exists():shutil.copytree(recipe.SOURCE,source)
 def verify_copy():
  assert all(sha(source/name)==digest for name,digest in expected.items()),'macOS x64 dependency source copy changed'
 verify_copy();pin=identity();save(WORK/'provenance.json',pin)
 env=environment_for(installed,args.jobs);completed=[];state=dict(complete=False,completedComponents=completed,identity=pin,skipped=SKIPPED)
 try:
  state['compilerSearchPaths']=check_compiler_search(env);depfiles={}
  for name,path,settings in profile(source):
   build=WORK/name;build.mkdir(exist_ok=True)
   values=options(installed);values.update(settings)
   # Build paths never reach the binaries, so any checkout location gives the same bytes.
   for flags in ['CMAKE_C_FLAGS','CMAKE_CXX_FLAGS']:values[flags]=(PATH_MAP+' '+values.get(flags,'')).strip()
   commands=[['cmake','-S',str(path),'-B',str(build),*['-D'+k+'='+str(v) for k,v in values.items()]],
    ['cmake','--build',str(build),'--parallel',str(args.jobs)],['cmake','--install',str(build)]]
   save(build/'commands.json',commands)
   for step,command in zip(['configure','build','install'],commands):
    state.update(component=name,step=step);save(WORK/'status.json',state)
    with (build/(step+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
    if step=='configure':check_isolation(build)
    if step=='install':assert all(Path(p).resolve().is_relative_to(installed.resolve()) for p in (build/'install_manifest.txt').read_text().splitlines())
   check_isolation(build);depfiles[name]=check_build_inputs(build);completed.append(name);print(RID,name,'built and installed',flush=True)
  recipe.verify();verify_copy();assert pin==identity()
  for library in (installed/'lib').glob('*.dylib'):
   if not library.is_symlink():check_binary(library)
  plugins=arm64.registered_plugins(installed)
  if plugins:raise RuntimeError('OIIO still registers format plugins: '+', '.join(plugins))
  found=libraries(installed)
  save(installed/MARKER,dict(identity=pin,libraries=found,skippedComponents=SKIPPED,
   oiioDisabledPlugins=arm64.oiio_plugins(source),oiioRegisteredFormatPlugins=plugins,compilerDependencyFiles=depfiles))
  state.update(complete=True,passed=True,libraries=found,compilerDependencyFiles=depfiles)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(WORK/'status.json',state)


if __name__=='__main__':main()
