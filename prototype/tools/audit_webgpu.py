#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Fail if the experiment's WebGPU inputs differ from the pinned NuGet archive."""
import hashlib
import json
import subprocess
import sys
import zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'research'))
from native_runner import PACKAGE
REPO=Path.home()/'GitHome/CodeBrix.WebGpu'
def sha(data): return hashlib.sha256(data).hexdigest()
def audit(allow_rust_documentation_change=False, compare_repository=False):
    package=PACKAGE/(PACKAGE.parent.name+'.'+PACKAGE.name+'.nupkg')
    expected='ddfd92f8e9ce0f4348b0b079b856158608552184d3255c26a3e35a87b73b8b7b'
    if sha(package.read_bytes()) != expected:
        raise RuntimeError('WebGPU archive differs from the reviewed shipped package')
    with zipfile.ZipFile(package) as archive:
        names=[n for n in archive.namelist() if n.startswith('runtimes/') and n.endswith(('.so','.dll','.dylib'))]
        names += [n for n in archive.namelist() if n.startswith('include/') and n.endswith(('.h','.rs'))]
        items=[]
        header_paths={
            'webgpu.h':'wgpu-native-tools/wgpu-native/ffi/webgpu-headers/webgpu.h',
            'wgpu.h':'wgpu-native-tools/wgpu-native/ffi/wgpu.h',
            'codebrix_wgpu.h':'wgpu-native-tools/codebrix-wgpu/include/codebrix_wgpu.h'}
        for name in sorted(names):
            original=archive.read(name); cached=(PACKAGE/name).read_bytes()
            if cached!=original: raise RuntimeError('Modified package input: '+name)
            repopath=REPO/header_paths.get(Path(name).name,'src/CodeBrix.WebGpu.Native/'+name)
            if not compare_repository:
                items.append({'path':name,'sha256':sha(original),'matchesNuGetArchive':True})
                continue
            repository_bytes=repopath.read_bytes();same=repository_bytes==original
            # The native C++ build does not consume this Rust helper. Permit an
            # explicitly requested, strictly comment-only repository difference;
            # all package cache bytes, native binaries and C headers stay exact.
            def without_line_comments(data):
                return b'\n'.join(line for line in data.splitlines() if not line.lstrip().startswith(b'//'))
            documentation_only=(name=='include/codebrix_wgpu_procs.rs' and not same and
                                without_line_comments(repository_bytes)==without_line_comments(original))
            items.append({'path':name,'sha256':sha(original),'matchesNuGetArchive':True,'matchesRepositoryFile':same,
                          'repositorySha256':sha(repository_bytes),'repositoryRustCommentsOnly':documentation_only})
        result={'packageId':'CodeBrix.WebGpu.Native.MitLicenseForever','version':PACKAGE.name,
            'archiveSha256':sha(package.read_bytes()),
            'repositoryHead':subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip() if compare_repository else None,
            'allPackageInputsIdentical':True,
            'repositoryComparisonRequested':compare_repository,
            'allRepositoryInputsIdentical':all(i['matchesRepositoryFile'] for i in items) if compare_repository else None,
            'allNativeBinariesAndCHeadersIdentical':all(i['matchesRepositoryFile'] for i in items if not i['path'].endswith('.rs')) if compare_repository else None,
            'rustDocumentationExceptionEnabled':allow_rust_documentation_change,'files':items,
            'policy':'Use shipped binaries and headers unchanged; no WebGPU rebuild, patches or experimental feature requirements.'}
        if compare_repository and not all(i['matchesRepositoryFile'] or (allow_rust_documentation_change and i['repositoryRustCommentsOnly']) for i in items):
            raise RuntimeError('Pinned WebGPU inputs differ from repository; investigate before testing')
        return result
if __name__=='__main__':
    result=audit(); path=ROOT/'work/webgpu-compatibility.json'
    path.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
