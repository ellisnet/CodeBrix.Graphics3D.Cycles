#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Linux ARM64 bring-up using reviewed local sources and installed cross SDK."""
import argparse,hashlib,importlib.util,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
from android_toolchain import environment,sha
spec=importlib.util.spec_from_file_location('dependency_profile',ROOT/'tools/dependencies/build.py')
recipe=importlib.util.module_from_spec(spec);spec.loader.exec_module(recipe)
WORK=ROOT/'native/work/linux/linux-arm64/dependencies'
CC=Path('/usr/bin/aarch64-linux-gnu-gcc-14');CXX=Path('/usr/bin/aarch64-linux-gnu-g++-14');SDK=Path('/usr/aarch64-linux-gnu')


def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n')


def identity():
 return dict(rid='linux-arm64',buildHost='linux-x64',sourceManifestSha256=sha(ROOT/'third_party/dependency-imports.json'),
  profileRecipeSha256=sha(ROOT/'tools/dependencies/build.py'),crossRecipeSha256=sha(Path(__file__)),environmentRecipeSha256=sha(ROOT/'tools/android_toolchain.py'),
  compilerVersion=subprocess.check_output([str(CXX),'--version'],text=True),cCompilerSha256=sha(CC),cxxCompilerSha256=sha(CXX),
  sysroot=str(SDK),sdkRuntimeSha256={str(p):sha(p) for p in [SDK/'lib/ld-linux-aarch64.so.1',SDK/'lib/libc.so.6',SDK/'lib/libstdc++.so.6',SDK/'lib/libgcc_s.so.1']},
  emulatorSha256=sha(Path('/usr/bin/qemu-aarch64')),
  scope='Local cross-build against the installed GNU/Linux ARM64 SDK; not a release baseline or native ARM64 GPU execution claim')


def prefix():
 """Reuse only the complete dependency prefix matching current source/tool inputs."""
 recipe.verify();installed=WORK/'install';marker=installed/'codebrix-linux-arm64-build.json'
 if not marker.exists():raise RuntimeError('Build Linux ARM64 dependencies first: python3 tools/dependencies/build_linux_arm64.py')
 report=json.loads(marker.read_text())
 if report['identity']!=identity():raise RuntimeError('Linux ARM64 dependency inputs/toolchain changed; rebuild the prefix')
 if sha(WORK/'toolchain.cmake')!=report['toolchainSha256']:raise RuntimeError('Linux ARM64 toolchain file changed')
 for name,digest in report['libraries'].items():
  if sha(installed/name)!=digest:raise RuntimeError('Changed Linux ARM64 dependency library: '+name)
 return installed


def main():
 parser=argparse.ArgumentParser();parser.add_argument('--jobs',type=int,default=4);args=parser.parse_args()
 manifest=recipe.verify();WORK.mkdir(parents=True,exist_ok=True);source=WORK/'sources';prefix=WORK/'install';prefix.mkdir(exist_ok=True)
 expected={str(Path(c['id'])/f['path']):f['sha256'] for c in manifest['components'] for f in c['files']}
 if not source.exists():shutil.copytree(recipe.SOURCE,source)
 def verify_copy():
  assert all(sha(source/name)==digest for name,digest in expected.items()),'Cross dependency source copy changed'
 verify_copy();pin=identity();save(WORK/'provenance.json',pin)
 toolchain=WORK/'toolchain.cmake'
 toolchain.write_text('# Copyright (c) 2026 Jeremy Ellis and contributors\n# SPDX-License-Identifier: Apache-2.0\n'
  'set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR aarch64)\n'
  f'set(CMAKE_C_COMPILER "{CC}")\nset(CMAKE_CXX_COMPILER "{CXX}")\n'
  f'set(CMAKE_FIND_ROOT_PATH "{prefix}" "{SDK}")\n'
  'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\nset(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n'
  'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\nset(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)\n'
  f'set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/qemu-aarch64 -L "{SDK}")\n')
 env=environment(prefix,args.jobs);completed=[];state=dict(complete=False,completedComponents=completed,identity=pin)
 try:
  # Confirm compiler, target libc/libstdc++ and QEMU work before building dependencies.
  probe=WORK/'sdk_probe.cpp';probe.write_text('// Copyright (c) 2026 Jeremy Ellis and contributors\n// SPDX-License-Identifier: Apache-2.0\n#include <thread>\n#include <iostream>\nint main(){int n=0;std::thread t([&]{n=42;});t.join();std::cout<<n<<"\\n";return n!=42;}\n')
  subprocess.run([str(CXX),str(probe),'-pthread','-o',str(WORK/'sdk_probe')],env=env,check=True)
  output=subprocess.check_output(['/usr/bin/qemu-aarch64','-L',str(SDK),str(WORK/'sdk_probe')],text=True,env=env)
  assert output=='42\n';save(WORK/'sdk-probe.json',dict(passed=True,executableSha256=sha(WORK/'sdk_probe'),output=output,emulated=True))
  for name,path,options in recipe.profile(source):
   build=WORK/name;build.mkdir(exist_ok=True)
   settings=dict(CMAKE_TOOLCHAIN_FILE=toolchain,CMAKE_BUILD_TYPE='Release',CMAKE_POSITION_INDEPENDENT_CODE='ON',CMAKE_EXPORT_COMPILE_COMMANDS='ON',
    CMAKE_INSTALL_PREFIX=prefix,CMAKE_PREFIX_PATH=prefix,CMAKE_INSTALL_RPATH='$ORIGIN',CMAKE_BUILD_RPATH=str(prefix/'lib'),BUILD_SHARED_LIBS='ON',BUILD_TESTING='OFF',
    FETCHCONTENT_FULLY_DISCONNECTED='ON',FETCHCONTENT_UPDATES_DISCONNECTED='ON')
   settings['CMAKE_INSTALL_LIBDIR:STRING']='lib';settings.update(options)
   if name in ['imath','openexr','oiio']:settings['CLANG_FORMAT_EXE:FILEPATH']='OFF'
   commands=[['cmake','-S',str(path),'-B',str(build),*['-D'+k+'='+str(v) for k,v in settings.items()]],
    ['cmake','--build',str(build),'--parallel',str(args.jobs)],['cmake','--install',str(build)]]
   save(build/'commands.json',commands)
   for step,command in zip(['configure','build','install'],commands):
    state.update(component=name,step=step);save(WORK/'status.json',state)
    with (build/(step+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
    if step=='install':assert all(Path(p).resolve().is_relative_to(prefix) for p in (build/'install_manifest.txt').read_text().splitlines())
   completed.append(name);print('linux-arm64',name,'built and installed',flush=True)
  recipe.verify();verify_copy();assert pin==identity()
  libraries={str(p.relative_to(prefix)):sha(p) for p in (prefix/'lib').rglob('*') if p.is_file() and not p.is_symlink() and ('.so' in p.name or p.suffix=='.a')}
  save(prefix/'codebrix-linux-arm64-build.json',dict(identity=pin,libraries=libraries,toolchainSha256=sha(toolchain)))
  state.update(complete=True,passed=True,libraries=libraries)
 except Exception as error:
  state.update(complete=True,passed=False,error=repr(error));raise
 finally:save(WORK/'status.json',state)


if __name__=='__main__':main()
