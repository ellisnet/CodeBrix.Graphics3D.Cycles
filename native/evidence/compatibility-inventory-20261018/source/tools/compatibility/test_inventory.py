# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Regression checks for omissions, stale evidence and inflated coverage claims."""
import json
from pathlib import Path
import tempfile
import unittest

import inventory as inv


class Declarations(unittest.TestCase):
    def test_derivatives_comments_and_separate_sentinel(self):
        header = '''enum ShaderNodeType : uint {
          #define SHADER_NODE_TYPE(name) name,
          #define SHADER_NODE_TYPE_DERIVATIVE(name) name, name##_DERIVATIVE,
          #include "node_types_template.h"
          NODE_NUM
        };'''
        template = '''// SHADER_NODE_TYPE(NODE_REMOVED)
SHADER_NODE_TYPE(NODE_END)
SHADER_NODE_TYPE_DERIVATIVE(NODE_ATTR)
'''
        entries = inv.enum_declarations(header, template)
        self.assertEqual(entries[0]['symbols'],
                         ['NODE_END', 'NODE_ATTR', 'NODE_ATTR_DERIVATIVE', 'NODE_NUM'])

    def test_film_macro_expansion(self):
        header = '''enum DeviceKernel {
          #define DECLARE_FILM_CONVERT_KERNEL(x) DEVICE_KERNEL_FILM_CONVERT_##x, \\
          DEVICE_KERNEL_FILM_CONVERT_##x##_HALF_RGBA
          DECLARE_FILM_CONVERT_KERNEL(COMBINED),
          DEVICE_KERNEL_NUM
        };'''
        self.assertEqual(inv.enum_declarations(header, '')[0]['symbols'],
                         ['DEVICE_KERNEL_FILM_CONVERT_COMBINED',
                          'DEVICE_KERNEL_FILM_CONVERT_COMBINED_HALF_RGBA', 'DEVICE_KERNEL_NUM'])

    def test_new_syntax_cannot_silently_drop_a_declaration(self):
        with self.assertRaisesRegex(ValueError, 'refusing partial'):
            inv.enum_declarations('enum A { OK }; enum B : unsigned int { MISSED };', '')

    def test_unknown_declaration_macro_fails(self):
        with self.assertRaisesRegex(ValueError, 'Unreviewed enum'):
            inv.enum_declarations('enum A { NEW_MACRO(ONE), TWO };', '')

    def test_compiler_membership_and_order_are_independent(self):
        entries = inv.enum_declarations('enum A { FIRST, SECOND };', '')
        for compiled in ({'A': ['FIRST']}, {'A': ['SECOND', 'FIRST']},
                         {'A': ['FIRST', 'SECOND', 'NEW']}, {}):
            with self.subTest(compiled=compiled), self.assertRaisesRegex(ValueError, 'mismatch'):
                inv.compare_declarations(entries, compiled)

    def test_concatenated_clang_json(self):
        enum = dict(kind='EnumDecl', name='A', inner=[dict(kind='EnumConstantDecl', name='X')])
        text = json.dumps(dict(kind='NamespaceDecl', inner=[enum])) + '\n' + json.dumps({})
        self.assertEqual(inv.ast_enum_sets(text), {'A': ['X']})


class Claims(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'report.json').write_text('{"passed": true}')
        (self.root / 'shader.wgsl').write_text('original')
        self.registry = {'gate': dict(report='report.json',
            sha256=inv.digest(self.root / 'report.json'),
            assertions=[dict(pointer='/passed', equals=True)],
            sourceSha256={'shader.wgsl': inv.digest(self.root / 'shader.wgsl')})}

    def test_valid_evidence_and_source_drift(self):
        inv.verify_evidence(self.registry, self.root)
        (self.root / 'shader.wgsl').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'source change'):
            inv.verify_evidence(self.registry, self.root)

    def test_changed_report_is_rejected(self):
        (self.root / 'report.json').write_text('{"passed": false}')
        with self.assertRaisesRegex(ValueError, 'report changed'):
            inv.verify_evidence(self.registry, self.root)

    def test_rehashed_failure_and_non_boolean_success_are_rejected(self):
        for value in (False, 1):
            (self.root / 'report.json').write_text(json.dumps(dict(passed=value)))
            self.registry['gate']['sha256'] = inv.digest(self.root / 'report.json')
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'assertion failed'):
                inv.verify_evidence(self.registry, self.root)

    def test_reviews_cannot_override_compiled_values_or_claim_full_coverage(self):
        for review in ({'nativeValue': 99}, {'fullRequirementVerified': True},
                       {'status': 'integrated-tested'}, {'coverage': 'complete'},
                       {'evidence': ['unknown']}):
            with self.subTest(review=review), self.assertRaises(ValueError):
                inv.make_row('opcodes', 'ShaderNodeType', 'NODE_END', 'source.h', 0,
                             {'ShaderNodeType:NODE_END': review}, self.registry)

    def test_stale_or_missing_generated_documents_fail(self):
        outputs = {'rows.json': {'symbols': ['FIRST', 'SECOND']}}
        inv.write_or_check(outputs, self.root, False)
        inv.write_or_check(outputs, self.root, True)
        (self.root / 'rows.json').write_text('{"symbols": ["FIRST"]}')
        with self.assertRaisesRegex(ValueError, 'stale'):
            inv.write_or_check(outputs, self.root, True)
        with self.assertRaisesRegex(ValueError, 'stale'):
            inv.write_or_check({'missing.json': {}}, self.root, True)

    def test_replay_does_not_certify_android_native_host(self):
        rows = inv.platforms({'pixel': {'kind': 'command-replay', 'rid': 'android-arm64'}})
        self.assertEqual({r['rid'] for r in rows}, {'win-x64', 'win-arm64', 'osx-x64',
            'osx-arm64', 'linux-x64', 'linux-arm64', 'linux-riscv64', 'android-arm64', 'android-x64'})
        android = next(r for r in rows if r['rid'] == 'android-arm64')
        self.assertEqual(android['replayEvidence'], ['pixel'])
        self.assertTrue(all(v == 'pending' for v in android['nativeHost'].values()))
        self.assertFalse(android['fullRequirementVerified'])


if __name__ == '__main__':
    unittest.main()
