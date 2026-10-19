#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Create a pinned, filtered Cycles import without copying excluded source.

Never edits the upstream checkout and never reads excluded implementation bodies
for transformation. New upstream versions require reviewing this policy first.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REVISION = 'a456b761034dda42c32eef9f4aae0fa5a5c9f604'
DEST = ROOT/'third_party/cycles'
MANIFEST = ROOT/'third_party/cycles-import.json'
REPLACEMENTS = Path(__file__).parent/'replacements'
EXCLUDED = {
    'src/cmake/dependency_targets.cmake':'Excluded GPL setup; independently implemented target adapters replace it.',
    'src/cmake/make_update.py':'No established file license; unnecessary maintenance helper.',
    'src/cmake/make_utils.py':'No established file license; unnecessary maintenance helper.',
    'src/cmake/make_format.py':'No established file license; unnecessary maintenance helper.',
}
PREFIXES = ('src/', 'third_party/sky/', 'third_party/libc_compat/', 'third_party/mikktspace/')
ROOT_FILES = {'CMakeLists.txt','LICENSE','AUTHORS','README.md'}
INHERITED = {'CMakeLists.txt','AUTHORS','README.md','third_party/mikktspace/README.md',
             'src/hydra/plugInfo.json','src/hydra/resources/apple_symbols.map',
             'src/hydra/resources/cycles.json.in','src/hydra/resources/linux_symbols.map',
             'src/hydra/resources/plugInfo.json'}
ALLOWED = {'Apache-2.0','BSD-3-Clause','MIT','Zlib'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def modified(text, description, cmake=False):
    marker = ('# ' if cmake else '// ')
    return marker+'Modified by CodeBrix: '+description+'\n'+marker+'Copyright (c) 2026 Jeremy Ellis and contributors\n'+text


def transform(name, text):
    if name == 'CMakeLists.txt':
        text = text.replace('include(dependency_targets)','include(codebrix_dependencies)')
        text = text.replace('\tthird_party/cuew/include\n','').replace('\tthird_party/hipew/include\n','')
        text = text.replace('project(Cycles)','project(Cycles)\nset(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_CXX_EXTENSIONS OFF)')
        option='option(WITH_LIBS_PRECOMPILED        "Use precompiled library dependencies" ON)'
        assert option in text
        text=text.replace(option,option.replace(' ON)',' OFF)')+'\nif(WITH_LIBS_PRECOMPILED)\n  message(FATAL_ERROR "Blender precompiled libraries are excluded from CodeBrix builds")\nendif()')
        return modified('# SPDX-License-Identifier: Apache-2.0\n# Original standalone Cycles build file; license inherited from upstream LICENSE.\n'+text,
                        'use reviewed dependency adapters, C++20, and no CUDA/HIP loader includes.',True)
    if name == 'src/CMakeLists.txt':
        block='if(CYCLES_STANDALONE_REPOSITORY)\n  include_directories(../third_party/atomic)\nelse()\n  include_directories(../atomic)\nendif()'
        assert block in text
        text=text.replace(block,'# Host atomics use the Apache-2.0 util/atomic_ops_codebrix.h replacement.')
        text=text.replace('add_subdirectory(app)','')
        return modified(text,'remove excluded atomic include roots and standalone application build.',True)
    if name == 'src/util/atomic.h':
        assert '#  include "atomic_ops.h"' in text
        text=text.replace('/* Using atomic ops header from Blender. */','/* Native host operations use the independent CodeBrix C++20 implementation. */')
        text=text.replace('#  include "atomic_ops.h"','#  include "util/atomic_ops_codebrix.h"')
        return modified(text,'use independent host atomics; device branches are unchanged.')
    if name == 'src/util/stats.h':
        text=text.replace('#include "util/atomic.h"','#include "util/atomic.h"\n#include <cassert>')
        text=text.replace('atomic_add_and_fetch_z(&mem_used, size);\n    atomic_fetch_and_update_max_z(&mem_peak, mem_used);',
                          'const size_t used = atomic_add_and_fetch_z(&mem_used, size);\n    atomic_fetch_and_update_max_z(&mem_peak, used);')
        text=text.replace('assert(mem_used >= size);','assert(atomic_load_z(&mem_used) >= size);')
        return modified(text,'use atomic return/load for concurrent memory accounting.')
    return text


def license_for(name, text):
    tags = re.findall(r'SPDX-License-Identifier:\s*([A-Za-z0-9.+-]+)',text)
    if tags:
        if not set(tags) <= ALLOWED:
            raise RuntimeError('Excluded/unreviewed file license: '+name+' '+str(tags))
        return sorted(set(tags)), 'Explicit per-file SPDX; preserved unchanged.'
    if 'Licensed under the Apache License, Version 2.0' in text[:3000]:
        return ['Apache-2.0'], 'Explicit legacy Apache header; preserved unchanged.'
    if name in INHERITED:
        return ['Apache-2.0'], 'Standalone Cycles first-party metadata/build file covered by pinned root LICENSE; reviewed inherited-license list.'
    if name == 'LICENSE' or name.startswith('src/doc/license/'):
        return ['LicenseRef-License-Notice-Text'], 'License/attribution text retained as documentation, not renderer implementation.'
    raise RuntimeError('No reviewed license for '+name)


def run(upstream):
    assert subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip()==REVISION
    assert not subprocess.check_output(['git','-C',str(upstream),'status','--porcelain'],text=True).strip()
    if DEST.exists() or MANIFEST.exists():
        raise RuntimeError('Import exists; verify it or explicitly prepare a separately reviewed upgrade')
    names=subprocess.check_output(['git','-C',str(upstream),'ls-files','-z'],text=True).split('\0')
    prepared={};records=[];excluded=[]
    for name in sorted(n for n in names if n):
        if name in EXCLUDED or not (name in ROOT_FILES or name.startswith(PREFIXES)):
            excluded.append({'path':name,'reason':EXCLUDED.get(name,'Outside reviewed source/build scope; not copied.')})
            continue
        source=upstream/name
        if not source.is_file() or source.is_symlink():
            raise RuntimeError('Unexpected nonregular source: '+name)
        data=source.read_bytes();text=data.decode('utf-8')
        licenses,basis=license_for(name,text)
        result=transform(name,text).encode()
        prepared[name]=result
        records.append({'path':name,'sourceSha256':digest(data),'sha256':digest(result),
                        'licenses':licenses,'licenseBasis':basis,'modified':result!=data})
    additions={'src/util/atomic_ops_codebrix.h':'atomic_ops_codebrix.h',
               'src/cmake/codebrix_dependencies.cmake':'codebrix_dependencies.cmake'}
    for name,replacement in additions.items():
        data=(REPLACEMENTS/replacement).read_bytes();prepared[name]=data
        records.append({'path':name,'sha256':digest(data),'licenses':['Apache-2.0'],
                        'licenseBasis':'Original CodeBrix implementation; excluded implementation not used.',
                        'replacementSource':str((REPLACEMENTS/replacement).relative_to(ROOT)),'modified':False})
    name='third_party/CMakeLists.txt'
    data=b'# Copyright (c) 2026 Jeremy Ellis and contributors\n# SPDX-License-Identifier: Apache-2.0\nadd_subdirectory(sky)\nif(UNIX AND NOT APPLE)\n  add_subdirectory(libc_compat)\nendif()\n'
    prepared[name]=data;records.append({'path':name,'sha256':digest(data),'licenses':['Apache-2.0'],
                                      'licenseBasis':'Original CodeBrix build definition.','modified':False})
    for name,data in prepared.items():
        dest=DEST/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    MANIFEST.write_text(json.dumps({'schemaVersion':1,'upstream':'https://github.com/blender/cycles',
        'revision':REVISION,'importerSha256':digest(Path(__file__).read_bytes()),
        'policy':'Filtered import; no upstream atomic implementation, GPL dependency setup, maintenance helpers, submodules, examples, cuew or hipew.',
        'files':records,'excluded':excluded},indent=2)+'\n')
    print(DEST, len(records),'files;',len(excluded),'excluded paths')


if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--upstream',type=Path,default=Path.home()/'ClaudeHome/cycles-webgpu-upstream')
    run(parser.parse_args().upstream)
