#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Collect retained source comment notices; this does not approve any license.

The reviewed manifest supplies the allowed files. Complete component license
texts are separate inputs to notices.py. This supplement preserves embedded
copyright/permission blocks, including variants absent from a root LICENSE.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
COMMENT=re.compile(r'/\*.*?\*/|(?:(?:^[ \t]*//[^\n]*\n)+)',re.M|re.S)
HASH_COMMENT=re.compile(r'(?:(?:^[ \t]*\#[^\n]*\n)+)',re.M)
NOTICE=re.compile(r'copyright|permission|licensed|licen[sc]e|public domain',re.I)


def collect(check=False):
    manifest=json.loads((ROOT/'third_party/dependency-imports.json').read_text())
    cycles=json.loads((ROOT/'third_party/cycles-import.json').read_text())
    components=[dict(id='cycles',source=cycles['upstream'],revision=cycles['revision'],files=cycles['files']),
                *manifest['components']]
    for component in components:
        blocks={}
        for row in component['files']:
            prefix='third_party/cycles/' if component['id']=='cycles' else 'third_party/deps/'+component['id']+'/'
            name=prefix+row['path']
            path=ROOT/name
            if path.suffix not in {'.c','.h','.cc','.hh','.hpp','.cpp','.ipp','.inl','.cmake','.py','.pl','.sh','.S','.asm','.inc'} and path.name not in {'CMakeLists.txt','Makefile.in'}:continue
            content=path.read_text(errors='replace')
            pattern=HASH_COMMENT if path.suffix in {'.cmake','.py','.pl','.sh'} or path.name in {'CMakeLists.txt','Makefile.in'} else COMMENT
            for match in pattern.finditer(content):
                block=match.group().strip()
                if NOTICE.search(block):blocks.setdefault(block,[]).append(name)
        output=('Source notice supplement for '+component['id']+'\n'
                'Origin: '+component['source']+'\n'
                'Pin: '+component.get('revision',component.get('archiveSha256',''))+'\n'
                'Collected verbatim comment blocks from the reviewed snapshot.\n'
                'Identical notices are grouped; original source notices remain intact.\n'
                'Complete component license texts and selected licenses are in the root notices.\n\n')
        for block,paths in blocks.items():
            output+='Sources:\n'+'\n'.join(paths)+'\n\n'+block+'\n\n'
        dest=ROOT/'third_party/licenses'/('source-notices-'+component['id']+'.txt')
        if check:
            if not dest.exists() or dest.read_text()!=output:raise RuntimeError('Stale source notice supplement: '+str(dest))
        else:dest.write_text(output)
    return len(components)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true')
    print(collect(parser.parse_args().check),'source notice supplements')
