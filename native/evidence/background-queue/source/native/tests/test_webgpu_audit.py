#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""The optional Rust documentation exception must never admit ABI/binary drift."""
import sys
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'prototype/tools'))
import audit_webgpu as subject

class AuditTests(unittest.TestCase):
    def setUp(self):
        package=subject.PACKAGE
        headers={'webgpu.h':'wgpu-native-tools/wgpu-native/ffi/webgpu-headers/webgpu.h',
                 'wgpu.h':'wgpu-native-tools/wgpu-native/ffi/wgpu.h',
                 'codebrix_wgpu.h':'wgpu-native-tools/codebrix-wgpu/include/codebrix_wgpu.h'}
        self.contents={}
        with zipfile.ZipFile(package/(package.parent.name+'.'+package.name+'.nupkg')) as archive:
            for name in archive.namelist():
                if (name.startswith('runtimes/') and name.endswith(('.so','.dll','.dylib'))) or (name.startswith('include/') and name.endswith(('.h','.rs'))):
                    self.contents[subject.REPO/headers.get(Path(name).name,'src/CodeBrix.WebGpu.Native/'+name)]=archive.read(name)
        self.rust=subject.REPO/'src/CodeBrix.WebGpu.Native/include/codebrix_wgpu_procs.rs'
        read=Path.read_bytes;contents=self.contents
        self.mock=patch.object(Path,'read_bytes',lambda path:contents[path] if path in contents else read(path));self.mock.start();self.addCleanup(self.mock.stop)
    def test_identical(self):self.assertTrue(subject.audit()['allRepositoryInputsIdentical'])
    def test_comments_require_explicit_option(self):
        self.contents[self.rust]+=b'// Reviewed documentation example.\n'
        with self.assertRaises(RuntimeError):subject.audit()
        report=subject.audit(allow_rust_documentation_change=True)
        self.assertFalse(report['allRepositoryInputsIdentical']);self.assertTrue(report['allNativeBinariesAndCHeadersIdentical'])
    def test_rust_code_rejected(self):
        self.contents[self.rust]+=b'\npub const CHANGED_ABI: u32 = 1;\n'
        with self.assertRaises(RuntimeError):subject.audit(allow_rust_documentation_change=True)
    def test_c_header_even_comment_rejected(self):
        target=subject.REPO/'wgpu-native-tools/codebrix-wgpu/include/codebrix_wgpu.h';self.contents[target]+=b'\n// Change\n'
        with self.assertRaises(RuntimeError):subject.audit(allow_rust_documentation_change=True)
    def test_native_change_rejected(self):
        target=subject.REPO/'src/CodeBrix.WebGpu.Native/runtimes/linux-x64/native/libcodebrix_wgpu.so'
        self.contents[target]=bytes([self.contents[target][0]^1])+self.contents[target][1:]
        with self.assertRaises(RuntimeError):subject.audit(allow_rust_documentation_change=True)
    def test_cache_change_rejected(self):
        target=subject.PACKAGE/'include/codebrix_wgpu_procs.rs';self.contents[target]=target.read_bytes()+b'\n// Cache still must be exact.\n'
        with self.assertRaises(RuntimeError):subject.audit(allow_rust_documentation_change=True)
if __name__=='__main__':unittest.main()
