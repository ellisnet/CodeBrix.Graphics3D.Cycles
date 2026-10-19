#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Audit native build source/header provenance and the resolved ELF closure.

Use only on binaries built locally from reviewed inputs: ldd is not a safe
inspector for arbitrary untrusted executables. No artifact is published here.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(builds,executables,prefix):
    m=json.loads((ROOT/'third_party/dependencies.json').read_text())
    libraries={n:c for c in m['components'] for n in c.get('sonames',[])}
    errors=[];compiled=[];headers={};archives=[];elf=[];links=[]
    for build in builds:
        command_file=build/'compile_commands.json'
        if not command_file.exists():errors.append('No compilation database: '+str(build));continue
        for c in json.loads(command_file.read_text()):
            source=Path(c['file']).resolve()
            if not source.is_relative_to(ROOT):errors.append('External implementation source: '+str(source))
            if any(x in str(source) for x in ['/third_party/atomic/','cycles-webgpu-upstream','/deps/usr/']):
                errors.append('Excluded implementation source: '+str(source))
            compiled.append(dict(file=str(source),sha256=sha(source),command=c.get('command',c.get('arguments'))))
        for file in build.rglob('*.o.d'):
            content=file.read_text().replace('\\\n',' ')
            for name in content.split(':',1)[-1].split():
                path=Path(name)
                if not path.is_absolute():path=build/path
                if not path.is_file():continue
                path=path.resolve();s=str(path)
                if s in headers:continue
                if path.is_relative_to(ROOT/'third_party'):
                    basis='Reviewed in-repository dependency/source snapshot'
                elif path.is_relative_to(prefix):basis='Headers installed from reviewed dependency snapshots'
                elif path.is_relative_to(ROOT):basis='Project source or generated build header'
                elif s.startswith('/usr/include/c++/') or s.startswith('/usr/lib/gcc/'):
                    basis='GCC standard library/compiler runtime header exception'
                elif s.startswith('/usr/lib/linux/uapi/'):
                    basis='System Linux UAPI header; external platform SDK input under Linux-syscall-note exception, never vendored kernel implementation'
                elif s.startswith('/usr/include/'):
                    # A system codec/math/threading dependency would defeat the
                    # source-contained build. Platform C/POSIX/kernel headers
                    # are the allowed SDK/runtime exception, not arbitrary libs.
                    forbidden=['/boost/','/OpenEXR/','/Imath/','/OpenImageIO/','/oneapi/',
                               '/tbb/','/yaml-cpp/','/fmt/','/tsl/','/expat','/png','/jpeglib',
                               '/zstd','/zlib','/OpenColorIO/','/pugixml','/tiff']
                    if any(x in s for x in forbidden):errors.append('Unvendored dependency header: '+s)
                    basis='System C/POSIX/Linux SDK header; explicit platform exception'
                elif '/.nuget/packages/codebrix.webgpu.native.' in s:
                    basis='Pinned shipped CodeBrix.WebGpu.Native header'
                else:
                    errors.append('Unreviewed external header: '+s);basis='UNREVIEWED'
                if '/third_party/atomic/' in s or 'cycles-webgpu-upstream' in s:
                    errors.append('Excluded atomic/upstream header: '+s)
                headers[s]=dict(sha256=sha(path),basis=basis)
        for p in build.rglob('*.a'):
            archives.append(dict(path=str(p),sha256=sha(p),members=subprocess.check_output(['ar','t',str(p)],text=True).splitlines()))
        for p in build.rglob('link.txt'):
            links.append(dict(path=str(p),sha256=sha(p),command=p.read_text()))
    env=dict(os.environ);env['LD_LIBRARY_PATH']=str(prefix/'lib')
    for binary in executables:
        output=subprocess.check_output(['ldd',str(binary)],text=True,env=env)
        if 'not found' in output:errors.append('Unresolved runtime: '+str(binary))
        dependencies=[]
        for line in output.splitlines():
            fields=line.strip().split()
            if not fields or fields[0].startswith('linux-vdso'):continue
            soname=fields[0] if '=>' in fields else Path(fields[0]).name
            resolved=fields[2] if '=>' in fields else fields[0]
            if not resolved.startswith('/'):continue
            path=Path(resolved).resolve();component=libraries.get(soname)
            if component is None:errors.append('Unreviewed linked library: '+soname);continue
            allowed_system=component['id'] in ['glibc','libgcc','libstdcxx']
            if not allowed_system and component['id']!='webgpu' and not path.is_relative_to(prefix):
                errors.append('Library escaped source-built prefix: '+str(path))
            dependencies.append(dict(soname=soname,path=str(path),sha256=sha(path),component=component['id'],license=component['license']))
        program_headers=subprocess.check_output(['readelf','-l',str(binary)],text=True)
        # A shared library with no C runtime references (e.g. Boost's empty
        # compatibility libraries) legitimately has no libc dependency.
        # Dynamically loaded executables must still use the approved libc.
        if 'INTERP' in program_headers and not any(d['component']=='glibc' for d in dependencies):
            errors.append('Expected dynamically linked system glibc: '+str(binary))
        elf.append(dict(path=str(binary),sha256=sha(binary),dynamicSection=subprocess.check_output(['readelf','-d',str(binary)],text=True),dependencies=dependencies))
    return dict(passed=not errors,errors=sorted(set(errors)),compiledSources=compiled,
                compilerHeaders=headers,staticArchives=archives,linkCommands=links,elfClosure=elf,
                scope='Linux build inputs and resolved ELF dependencies; GPU driver/platform SDK implementation is external. Other RIDs and actual NuGet payloads require separate audits.')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,action='append',required=True)
    p.add_argument('--executable',type=Path,action='append',required=True)
    p.add_argument('--prefix',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=inspect([b.resolve() for b in a.build],[b.resolve() for b in a.executable],a.prefix.resolve())
    a.output.parent.mkdir(exist_ok=True,parents=True);a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps(dict(passed=r['passed'],errors=r['errors'],compiledSources=len(r['compiledSources']),headers=len(r['compilerHeaders']),staticArchives=len(r['staticArchives'])),indent=2))
    raise SystemExit(0 if r['passed'] else 1)
