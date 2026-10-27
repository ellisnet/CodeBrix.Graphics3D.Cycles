#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Audit recorded direct compiler invocations for backend and replay binaries.

Replay each trusted build command in dependency-only mode to enumerate headers.
This supplements CMake's actual object dependency files, without rebuilding or
changing a binary. Android inspection uses readelf, never the host ELF loader.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PACKAGE=Path(os.environ.get('CODEBRIX_WEBGPU_PACKAGE',str(Path.home()/'.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210'))).resolve()
NDK=(Path.home()/'Android/Sdk/ndk/29.0.14206865').resolve()


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(records):
    errors=[];results=[]
    for record in records:
        data=json.loads(record.read_text())
        commands=data.get('commands') or [data['command']]
        for command in commands:
            compiler=Path(shutil.which(command[0]) or command[0]).resolve()
            android=compiler.is_relative_to(NDK)
            sources=[];flags=[];skip=False
            for arg in command[1:]:
                if skip:skip=False;continue
                if arg in ('-o','-L'):skip=True;continue
                if arg in ('-shared','-static-libstdc++') or arg.startswith(('-l','-Wl,','-L')):continue
                if Path(arg).suffix in ('.c','.cc','.cpp','.cxx'):sources.append(Path(arg).resolve())
                else:flags.append(arg)
            if not sources:errors.append('No recorded implementation sources: '+str(record))
            headers={};source_rows=[]
            for source in sources:
                if not source.is_relative_to(ROOT):errors.append('External implementation: '+str(source))
                source_rows.append(dict(path=str(source),sha256=sha(source)))
                dependency_command=[command[0],*flags,'-M','-MT','audit',str(source)]
                output=subprocess.check_output(dependency_command,text=True,cwd=ROOT)
                for name in output.replace('\\\n',' ').split(':',1)[1].split():
                    path=Path(name).resolve();s=str(path)
                    if s in headers:continue
                    if path.is_relative_to(ROOT):basis='Repository source or generated header'
                    elif path.is_relative_to(PACKAGE/'include'):basis='Pinned unchanged shipped WebGPU headers'
                    elif android and path.is_relative_to(NDK):basis='External Android API 33 platform/LLVM SDK'
                    elif not android and (s.startswith('/usr/lib/gcc/') or re.match(r'/usr/lib/llvm-[0-9]+/lib/clang/',s)):
                        basis='External GCC/LLVM toolchain header'
                    elif not android and s.startswith(('/usr/include/','/usr/lib/linux/uapi/')):
                        basis='External C/C++ platform SDK/runtime header'
                        if any(t in s for t in ('/boost/','/OpenImageIO/','/OpenEXR/','/Imath/','/tbb/','/oneapi/','/fmt/','/yaml-cpp/')):
                            errors.append('Unvendored dependency header: '+s)
                    else:errors.append('Unreviewed external header: '+s);basis='UNREVIEWED'
                    headers[s]=dict(sha256=sha(path),basis=basis)
            binary=Path(command[command.index('-o')+1]).resolve()
            dynamic=subprocess.check_output(['readelf','-d',str(binary)],text=True)
            needed=re.findall(r'\(NEEDED\).*\[(.*?)\]',dynamic)
            allowed={'libcodebrix_wgpu.so','libcodebrix_cycles_webgpu.so'}
            allowed.update({'libc.so','libm.so','libdl.so','liblog.so'} if android else
                           {'libc.so.6','libm.so.6','libdl.so.2','libpthread.so.0','libstdc++.so.6','libgcc_s.so.1'})
            errors += ['Unreviewed linked library: '+n for n in needed if n not in allowed]
            if android and data.get('minimumAndroidApi')!=33:
                errors.append('Missing recorded Android API 33: '+str(record))
            rid='android-arm64' if 'android-arm64' in record.parts else 'android-x64' if android else 'linux-x64'
            runtime=PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'
            if sha(runtime)!=data['webGpuSha256']:errors.append('Changed shipped runtime: '+str(runtime))
            staged=binary.parent/'libcodebrix_wgpu.so'
            if not staged.is_file() or sha(staged)!=sha(runtime):errors.append('Changed staged runtime: '+str(staged))
            resolved=None
            if not android:
                resolved=subprocess.check_output(['ldd',str(binary)],text=True)
                if 'not found' in resolved:errors.append('Unresolved runtime: '+str(binary))
                for line in resolved.splitlines():
                    if 'libcodebrix_wgpu.so =>' in line:
                        loaded=Path(line.split('=>',1)[1].strip().split()[0])
                        if sha(loaded)!=sha(runtime):errors.append('Loaded runtime differs from package')
            results.append(dict(record=str(record),recordSha256=sha(record),command=command,
                compiler=str(compiler),compilerSha256=sha(compiler),sources=source_rows,
                dependencyOnlyHeaders=headers,binary=str(binary),binarySha256=sha(binary),
                needed=needed,elfDynamicSection=dynamic,linuxResolvedDependencies=resolved,
                androidApi=33 if android else None,shippedRuntimeSha256=sha(runtime)))
    return dict(passed=not errors,errors=errors,builds=results,
        scope='Recorded direct builds, dependency-only compiler header replay and ELF dependencies; Android API 33 replay executables, not complete Android Cycles hosts.')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--record',type=Path,action='append',required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=inspect(a.record);a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(passed=result['passed'],errors=result['errors'],builds=len(result['builds'])),indent=2))
    raise SystemExit(not result['passed'])
