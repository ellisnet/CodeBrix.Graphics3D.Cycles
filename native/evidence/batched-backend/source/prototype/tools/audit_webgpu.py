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
def audit():
    package=PACKAGE/(PACKAGE.parent.name+'.'+PACKAGE.name+'.nupkg')
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
            same=repopath.read_bytes()==original
            items.append({'path':name,'sha256':sha(original),'matchesNuGetArchive':True,'matchesRepositoryFile':same})
        result={'packageId':'CodeBrix.WebGpu.Native.MitLicenseForever','version':PACKAGE.name,
            'archiveSha256':sha(package.read_bytes()),
            'repositoryHead':subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip(),
            'allRepositoryInputsIdentical':all(i['matchesRepositoryFile'] for i in items),'files':items,
            'policy':'Use shipped binaries and headers unchanged; no WebGPU rebuild, patches or experimental feature requirements.'}
        if not result['allRepositoryInputsIdentical']: raise RuntimeError('Pinned WebGPU inputs differ from repository; investigate before testing')
        return result
if __name__=='__main__':
    result=audit(); path=ROOT/'work/webgpu-compatibility.json'
    path.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
