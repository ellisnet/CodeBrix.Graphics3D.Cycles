# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Pinned external Android toolchain settings shared by maintained build recipes."""
import hashlib
import os
import platform
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = 33
NDK_REVISION = '29.0.14206865'
TARGETS = {'android-arm64': ('arm64-v8a', 'aarch64-linux-android'),
           'android-x64': ('x86_64', 'x86_64-linux-android')}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class AndroidToolchain:
    def __init__(self, rid):
        if platform.system() != 'Linux' or platform.machine() != 'x86_64':
            raise RuntimeError('This Android cross-build recipe currently requires a Linux x64 build host')
        self.rid = rid
        self.abi, self.triple = TARGETS[rid]
        self.root = Path(os.environ.get('CODEBRIX_ANDROID_NDK',
            str(Path.home()/'Android/Sdk/ndk'/NDK_REVISION))).resolve()
        self.properties = (self.root/'source.properties').read_text()
        properties = dict(line.split('=', 1) for line in self.properties.splitlines() if '=' in line)
        if not any(k.strip() == 'Pkg.Revision' and v.strip() == NDK_REVISION for k, v in properties.items()):
            raise RuntimeError('Unexpected Android NDK revision; deliberate toolchain review required')
        self.toolchain = self.root/'build/cmake/android.toolchain.cmake'
        self.prebuilt = self.root/'toolchains/llvm/prebuilt/linux-x86_64'
        self.bin = self.prebuilt/'bin'
        self.runtime = self.prebuilt/'sysroot/usr/lib'/self.triple/'libc++_shared.so'

    def identity(self):
        return dict(rid=self.rid, minimumAndroidApi=API, abi=self.abi,
                    ndkRevision=NDK_REVISION, ndkSourceProperties=self.properties,
                    toolchainSha256=sha(self.toolchain), compilerSha256=sha(self.bin/'clang'),
                    cxxRuntimeSha256=sha(self.runtime))

    def options(self, prefix):
        return dict(CMAKE_TOOLCHAIN_FILE=self.toolchain, ANDROID_ABI=self.abi,
                    ANDROID_PLATFORM=f'android-{API}', ANDROID_STL='c++_shared',
                    CMAKE_BUILD_TYPE='Release', CMAKE_POSITION_INDEPENDENT_CODE='ON',
                    CMAKE_EXPORT_COMPILE_COMMANDS='ON', CMAKE_PREFIX_PATH=prefix,
                    CMAKE_FIND_ROOT_PATH=prefix, CMAKE_FIND_ROOT_PATH_MODE_PROGRAM='NEVER',
                    CMAKE_FIND_ROOT_PATH_MODE_LIBRARY='ONLY', CMAKE_FIND_ROOT_PATH_MODE_INCLUDE='ONLY',
                    CMAKE_FIND_ROOT_PATH_MODE_PACKAGE='ONLY',
                    FETCHCONTENT_FULLY_DISCONNECTED='ON', FETCHCONTENT_UPDATES_DISCONNECTED='ON',
                    CMAKE_SHARED_LINKER_FLAGS='-Wl,-z,max-page-size=16384',
                    CMAKE_EXE_LINKER_FLAGS='-Wl,-z,max-page-size=16384')


def environment(prefix, jobs):
    env = dict(os.environ)
    for name in ['CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH',
                 'LD_LIBRARY_PATH', 'CMAKE_PREFIX_PATH', 'PKG_CONFIG_PATH', 'PYTHONPATH']:
        env.pop(name, None)
    env['PKG_CONFIG_PATH'] = env['PKG_CONFIG_LIBDIR'] = str(prefix/'lib/pkgconfig')
    env['CMAKE_BUILD_PARALLEL_LEVEL'] = str(jobs)
    return env
