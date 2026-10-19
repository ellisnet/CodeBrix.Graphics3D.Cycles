# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build/run the same native C++ host on Linux and Android, without a CLR."""
import hashlib
import json
import subprocess
from pathlib import Path

PACKAGE = Path.home()/'.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.290.1433'
ADB = Path.home()/'Android/Sdk/platform-tools/adb'
NDK = Path.home()/'Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin'

def build(root, work, rid):
    compiler = {'linux-x64': 'clang++', 'android-arm64': str(NDK/'aarch64-linux-android33-clang++'),
                'android-x64': str(NDK/'x86_64-linux-android33-clang++')}[rid]
    library = PACKAGE/'runtimes'/rid/'native'
    binary = work/('gpu_probe-' + rid)
    command = [compiler, '-std=c++20', '-O2', str(root/'native/gpu_probe.cpp'),
               '-I', str(PACKAGE/'include'), '-L', str(library), '-lcodebrix_wgpu',
               '-Wl,-rpath,' + (str(library) if rid == 'linux-x64' else '$ORIGIN'), '-o', str(binary)]
    if rid.startswith('android'): command += ['-static-libstdc++']
    subprocess.run(command, check=True)
    (work/('build-' + rid + '.json')).write_text(json.dumps({'command': command,
        'librarySha256': hashlib.sha256((library/'libcodebrix_wgpu.so').read_bytes()).hexdigest()}, indent=2)+'\n')
    return binary, library/'libcodebrix_wgpu.so'

def case_args(case):
    assert case['Output'] == 1 and len(case['Dispatches']) == 1
    assert case['Dispatches'][0]['Entry'] == 'main'
    return [case['Name'], str(case['Dispatches'][0]['Groups']), *case['Buffers']]

def linux(root, work):
    binary, _ = build(root, work, 'linux-x64')
    results = []
    for case in json.loads((work/'cases.json').read_text()):
        result = subprocess.run([str(binary), str(work), *case_args(case)],
                                text=True, capture_output=True, timeout=90)
        (work/(case['Name']+'.native-stderr.txt')).write_text(result.stderr)
        if result.returncode: raise RuntimeError(result.stderr)
        results.append(json.loads(result.stdout))
    (work/'native-gpu.json').write_text(json.dumps(results, indent=2)+'\n')

def android(root, work, serial):
    def adb(*args, **kwargs):
        return subprocess.run([str(ADB), '-s', serial, *map(str,args)], check=True, timeout=120, **kwargs)
    def prop(name): return adb('shell','getprop',name,capture_output=True,text=True).stdout.strip()
    metadata = {'serial':serial, 'api':int(prop('ro.build.version.sdk')), 'abi':prop('ro.product.cpu.abi'),
                'model':prop('ro.product.model'), 'manufacturer':prop('ro.product.manufacturer'),
                'minimumApi':33, 'executionContext':'adb shell native executable; application sandbox not tested'}
    if metadata['api'] < 33: raise RuntimeError('Device below required API 33')
    rid = {'arm64-v8a':'android-arm64','x86_64':'android-x64'}[metadata['abi']]
    binary, library = build(root, work, rid)
    destination = '/data/local/tmp/codebrix-cycles-webgpu-research'
    result_dir = work/('android-' + serial.replace(':','_'))
    result_dir.mkdir(exist_ok=True)
    adb('shell','mkdir','-p',destination)
    cases = json.loads((work/'cases.json').read_text())
    files = {binary, library}
    for case in cases:
        files.add(work/case['Shader'])
        files.update(work/p for p in case['Buffers'])
    for path in sorted(files): adb('push',path,destination+'/',capture_output=True)
    adb('shell','chmod','755',destination+'/'+binary.name)
    results = []
    for case in cases:
        result = adb('shell',destination+'/'+binary.name,destination,*case_args(case),capture_output=True,text=True)
        (result_dir/(case['Name']+'.stderr.txt')).write_text(result.stderr)
        results.append(json.loads(result.stdout))
        adb('pull',destination+'/'+case['Name']+'.gpu.bin',result_dir,capture_output=True)
    (result_dir/'device.json').write_text(json.dumps(metadata,indent=2)+'\n')
    (result_dir/'native-gpu.json').write_text(json.dumps(results,indent=2)+'\n')
    return result_dir
