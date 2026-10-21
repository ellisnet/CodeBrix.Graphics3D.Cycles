#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E2: build the diagnostic probe_replay (work folder only) like render_replay.build()."""
import subprocess, sys, shutil
from pathlib import Path
HERE = Path(__file__).resolve().parent; REPO = HERE.parents[3]
sys.path.insert(0, str(REPO/'native/tools'))
from build import ROOT, PACKAGE, NDK
rid = sys.argv[1]; out = HERE/rid; out.mkdir(exist_ok=True)
compiler = {'linux-x64': 'clang++', 'android-arm64': str(NDK/'aarch64-linux-android33-clang++')}[rid]
cmd = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'src'), '-I', str(ROOT/'include'), '-I', str(PACKAGE/'include'),
       '-I', str(REPO/'third_party/deps/tbb/include'), str(HERE/'probe_replay.cpp'), str(ROOT/'src/webgpu_device.cpp'),
       '-L', str(PACKAGE/'runtimes'/rid/'native'), '-lcodebrix_wgpu', '-ldl', '-pthread', '-Wl,-rpath,$ORIGIN', '-o', str(out/'probe_replay')]
if rid.startswith('android'): cmd += ['-static-libstdc++']
subprocess.run(cmd, check=True)
shutil.copy2(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so', out/'libcodebrix_wgpu.so')
print(out/'probe_replay')
