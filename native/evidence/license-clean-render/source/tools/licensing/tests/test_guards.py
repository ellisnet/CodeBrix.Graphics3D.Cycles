#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Negative checks for the publication guards, using isolated fixture files."""
import hashlib
import gzip
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import dependency_sources
import audit


def digest(data):return hashlib.sha256(data).hexdigest()


class DependencyGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.dest=self.root/'deps';(self.dest/'example').mkdir(parents=True)
        self.source=self.dest/'example/input.h';self.source.write_bytes(b'// reviewed input\n')
        self.manifest=self.root/'manifest.json'
        self.manifest.write_text(json.dumps({'importerSha256':dependency_sources.digest(Path(dependency_sources.__file__)),
            'components':[{'id':'example','files':[{'path':'input.h','sha256':digest(self.source.read_bytes())}]}]}))
        for name,value in [('DEST',self.dest),('MANIFEST',self.manifest)]:
            mock=patch.object(dependency_sources,name,value);mock.start();self.addCleanup(mock.stop)

    def test_reviewed_fixture_passes(self):
        self.assertEqual(len(dependency_sources.verify()['components']),1)

    def test_modified_source_rejected(self):
        self.source.write_bytes(b'// unreviewed replacement\n')
        with self.assertRaisesRegex(RuntimeError,'Unreviewed dependency source change'):dependency_sources.verify()

    def test_added_source_rejected(self):
        (self.dest/'example/unreviewed.h').write_bytes(b'// additional source\n')
        with self.assertRaisesRegex(RuntimeError,'Unreviewed dependency files'):dependency_sources.verify()

    def test_external_source_symlink_rejected(self):
        outside=self.root/'outside.h';outside.write_bytes(self.source.read_bytes())
        self.source.unlink();self.source.symlink_to(outside)
        with self.assertRaisesRegex(RuntimeError,'Unreviewed dependency source change'):dependency_sources.verify()


class PackageGuardTests(unittest.TestCase):
    def test_changed_payload_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);package=root/'fixture.nupkg';manifest=root/'manifest.json'
            with zipfile.ZipFile(package,'w') as archive:
                for name in ['LICENSE','NOTICE','THIRD-PARTY-NOTICES.txt']:archive.writestr(name,'expected')
                archive.writestr('runtimes/linux-x64/native/unreviewed.so','additional payload')
            manifest.write_text(json.dumps({'approvedForPackaging':True,'files':{name:digest(b'expected') for name in ['LICENSE','NOTICE','THIRD-PARTY-NOTICES.txt']}}))
            with self.assertRaisesRegex(RuntimeError,'differs from reviewed exact payload'):audit.package_audit(package,manifest)

    def test_unapproved_manifest_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);manifest=root/'manifest.json';manifest.write_text('{"approvedForPackaging":false}')
            with self.assertRaisesRegex(RuntimeError,'not release-reviewed'):audit.package_audit(root/'unused.nupkg',manifest)


class SourceArtifactGuardTests(unittest.TestCase):
    def test_compiled_files_rejected_even_inside_gzip(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'third_party').mkdir()
            (root/audit.EXCEPTIONS).write_text('{"files":{}}')
            with patch.object(audit,'ROOT',root),patch.object(audit,'verify_cycles'),\
                 patch.object(audit,'verify_dependencies'),patch.object(audit,'verify_notices'),\
                 patch.object(audit,'candidates',return_value=[audit.EXCEPTIONS,'payload.dat']),\
                 patch.object(audit.subprocess,'check_output',return_value=''):
                for payload in [b'\x7fELFbinary',b'MZbinary',b'!<arch>\narchive',
                                b'\xcf\xfa\xed\xfemacho',b'\xca\xfe\xba\xbeuniversal',b'\x00asmwasm']:
                    for compressed in [False,True]:
                        with self.subTest(payload=payload,compressed=compressed):
                            (root/'payload.dat').write_bytes(gzip.compress(payload) if compressed else payload)
                            result=audit.source_audit()
                            self.assertFalse(result['passed'])
                            self.assertIn('Compiled artifact in source publication candidate: payload.dat',result['errors'])


if __name__=='__main__':unittest.main()
