#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Stage and verify filtered dependency snapshots; never fetch during a build.

Initial staging is outside the repository. Review the scan before importing.
The committed manifest records every original and modified file hash.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MIGRATION = Path.home()/'ClaudeHome/cycles-license-migration-20261018'
FETCHED = MIGRATION/'dependency-source'
STAGE = MIGRATION/'dependency-stage'
DEST = ROOT/'third_party/deps'
MANIFEST = ROOT/'third_party/dependency-imports.json'
EXCLUDED = {
    'boost/boost/process/v2/ext/detail/proc_info.hpp': 'CC-BY-4.0 code; policy permits this license only for non-code.',
    'boost/boost/python/detail/python22_fixed.h': 'Unused legacy Python 2 implementation; not included in the reviewed Boost profile.',
    'boost/boost/outcome/outcome.gdb.py': 'Unlicensed optional debugger helper.',
    'tiff/libtiff/tif_lzw.c': 'Legacy Berkeley advertising condition; unused TIFF decoder omitted from this metadata-header dependency profile.',
    'zstd/COPYING': 'Unselected GPL alternative text; BSD alternative retained in LICENSE and original dual-license headers.',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def policy():
    old = json.loads((ROOT/'third_party/provenance.json').read_text())
    sources = {s['name']:s for s in old['sources']}
    roots = {
        'oiio': (Path.home()/'ClaudeHome/oiio-cycles-webgpu', 'OpenImageIO',
                 ['src/include/','src/cmake/','src/libOpenImageIO/','src/libutil/',
                  'src/libtexture/','src/build-scripts/hidesymbols.map','src/build-scripts/hidesymbols_osx.map','src/fonts/CMakeLists.txt','src/build-scripts/OpenImageIO.pc.in'],
                 'Apache-2.0 AND BSD-3-Clause AND MIT'),
        'ocio': (Path.home()/'ClaudeHome/ocio-cycles-webgpu', 'OpenColorIO',
                 ['include/','src/','share/cmake/','share/ocio/','ext/',
                  'vendor/CMakeLists.txt','tests/CMakeLists.txt'], 'BSD-3-Clause'),
        'fmt': (Path.home()/'ClaudeHome/oiio-cycles-webgpu/ext/fmt','fmt',
                ['include/','src/','support/cmake/'], 'MIT'),
        'robin-map': (Path.home()/'ClaudeHome/oiio-cycles-webgpu/ext/robin-map','robin-map',
                      ['include/','cmake/'], 'MIT'),
        'pystring': (ROOT/'prototype/work/ocio-build/ext/build/pystring/src/pystring_install','pystring',[], 'BSD-3-Clause'),
        'yaml-cpp': (ROOT/'prototype/work/ocio-build/ext/build/yaml-cpp/src/yaml-cpp_install','yaml-cpp',
                     ['include/','src/','cmake/'], 'MIT'),
        'minizip': (ROOT/'prototype/work/ocio-build/ext/build/libminizip-ng/src/minizip-ng_install','minizip-ng',
                    ['cmake/'], 'Zlib'),
    }
    for id,(path,key,includes,license) in roots.items():
        s=sources[key]
        yield id,dict(path=path,source=s['url'],revision=s['revision'],version=s['version'],
                      includes=includes,license=license)
    selected = {
        'openexr':(['src/bin/CMakeLists.txt','src/lib/','src/bindings/','cmake/','share/'], 'BSD-3-Clause'),
        'imath':(['src/Imath/','src/python/','src/CMakeLists.txt','cmake/','config/','share/'], 'BSD-3-Clause'),
        'tbb':(['integration/cmake/','integration/linux/env/','integration/windows/env/','integration/mac/env/','integration/pkg-config/tbb.pc.in','include/','src/','cmake/'], 'Apache-2.0'),
        'zlib':(['win32/zlib1.rc'], 'Zlib'),
        'zstd':(['lib/','build/cmake/'], 'BSD-3-Clause'),
        'png':(['scripts/','arm/','intel/','powerpc/','mips/','loongarch/'], 'libpng-2.0'),
        'jpeg':(['release/','md5/','cmakescripts/','simd/','win/','sharedlib/','doc/'], 'IJG AND BSD-3-Clause AND Zlib'),
        'expat':(['expat/lib/','expat/cmake/'], 'MIT'),
        'tiff':(['libtiff/','port/','cmake/','build/CMakeLists.txt'], 'libtiff'),
        'pugixml':(['src/','scripts/pugixml-config.cmake.in','scripts/pugixml.pc.in','scripts/pugixml_dll.rc'], 'MIT'),
    }
    for s in json.loads((FETCHED/'fetch.json').read_text()):
        includes,license=selected[s['name']]
        yield s['name'],dict(path=FETCHED/s['name'],source=s['url'],revision=s['revision'],
                             version=s['tag'],includes=includes,license=license)
    yield 'boost',dict(path=FETCHED/'boost_1_83_0',source='https://archives.boost.io/release/1.83.0/source/boost_1_83_0.tar.bz2',
                       archiveSha256=digest(FETCHED/'boost_1_83_0.tar.bz2'),version='1.83.0',
                       includes=['boost/']+['libs/'+n+'/src/' for n in
                           ['atomic','chrono','date_time','filesystem','iostreams','regex','system','thread']],license='BSL-1.0')


def selected(id,name,includes):
    path=Path(name)
    # No repository metadata, tests, docs with separate asset licenses, binaries,
    # maintenance scripts, bootstrap/autotools, or unrelated plugins are imported.
    if any(p.startswith('.') for p in path.parts): return False
    if id+'/'+name in EXCLUDED: return False
    if id=='jpeg' and name.startswith(('md5/','doc/html/')): return False
    if path.suffix.lower() in {'.png','.jpg','.jpeg','.gif','.svg','.pdf','.ttf','.exr','.dpx','.html'}: return False
    if id=='png' and (name=='pngusr.dfa' or path.suffix in {'.3','.5'}): return True
    if id=='png' and name.startswith('scripts/autoconf/'): return False
    if id=='zlib' and name=='zconf.h': return False  # CMake generates this; never mutate vendored input.
    if id=='zlib' and name in {'zconf.h.cmakein','zlib.pc.cmakein','zlib.3'}: return True
    if id=='jpeg' and path.suffix=='.1': return True
    if id=='jpeg' and name=='README.ijg': return True
    if id=='expat' and name=='README.md': return False  # Root alias; actual expat/README.md is retained.
    if id=='tiff' and name in {'VERSION','RELEASE-DATE','configure.ac'}: return True
    if id=='boost': return name=='LICENSE_1_0.txt' or any(name.startswith(n) for n in includes)
    if id=='oiio' and len(path.parts)>1 and path.parts[0]=='src' and path.parts[1].endswith('.imageio'):
        return path.parts[1] not in {'ffmpeg.imageio','raw.imageio','heif.imageio','dicom.imageio','openvdb.imageio','tiff.imageio','webp.imageio','jpeg2000.imageio','gif.imageio','ptex.imageio','r3d.imageio'}
    if id=='ocio' and name.startswith(('src/apps/','src/bindings/','src/libutils/')):
        return name.count('/')==2 and name.endswith('/CMakeLists.txt')
    if id=='ocio' and name.startswith('ext/'):
        return not any(p in ['patches','tests'] for p in path.parts)
    if id=='expat' and path.parts[0]=='expat' and len(path.parts)==2:
        return path.suffix in {'.txt','.cmake','.in'} or path.name in {'COPYING','Changes','README.md','AUTHORS'}
    if len(path.parts)==1:
        return path.suffix in {'.c','.h','.cpp','.in','.txt','.cmake','.md','.rst','.def','.map','.cmakein'} or path.name in {'LICENSE','COPYING','README','AUTHORS','NEWS'}
    return any(name==p or (p.endswith('/') and name.startswith(p)) for p in includes)


def transform(id,name,data):
    """Small reviewed adaptations to permissively licensed build files only."""
    changes = {
        ('oiio','CMakeLists.txt'): [
            ('if (PROJECT_IS_TOP_LEVEL)\n    include (testing)',
             'if (PROJECT_IS_TOP_LEVEL AND OIIO_BUILD_TESTS)\n    include (testing)'),
            ('include (packaging)', '# CodeBrix builds the dependency library; standalone CPack installers are excluded.')],
        ('tiff','libtiff/CMakeLists.txt'): [('        tif_lzw.c\n','')],
        ('jpeg','CMakeLists.txt'): [('add_subdirectory(md5)',
            '# CodeBrix: omit the unused standalone checksum test utility.')],
    }
    if (id,name) not in changes: return data
    content=data.decode()
    for before,after in changes[id,name]:
        if content.count(before)!=1:raise RuntimeError('Upstream build adaptation changed: '+id+'/'+name)
        content=content.replace(before,after)
    description={'oiio':'omit disabled test scaffolding and standalone CPack installers.',
                 'tiff':'omit the disabled legacy LZW decoder from the source list.',
                 'jpeg':'omit the unused checksum test program; image codecs are unchanged.'}[id]
    return ('# Modified by CodeBrix: '+description+'\n# Copyright (c) 2026 Jeremy Ellis and contributors\n'+content).encode()


def prepare(destination):
    """Prepare a deterministic candidate outside Git for review and scanning."""
    if destination.exists():raise RuntimeError('Refusing to replace an existing candidate')
    if destination.is_relative_to(ROOT):raise RuntimeError('Review candidates must be outside the repository')
    components=[]
    for id,p in policy():
        source=p['path']
        if (source/'.git').exists():
            if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()!=p['revision']:
                raise RuntimeError('Changed upstream revision: '+id)
            names=subprocess.check_output(['git','-C',str(source),'ls-files','-z'],text=True).rstrip('\0').split('\0')
        else:names=[str(q.relative_to(source)) for q in source.rglob('*') if q.is_file()]
        rows=[];excluded=[]
        for name in sorted(names):
            if not selected(id,name,p['includes']):
                excluded.append(dict(path=name,reason=EXCLUDED.get(id+'/'+name,'Outside the reviewed source/build profile.')));continue
            src=source/name
            if src.is_symlink():raise RuntimeError('Unreviewed source symlink: '+str(src))
            original=src.read_bytes();data=transform(id,name,original)
            dest=destination/id/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
            rows.append(dict(path=name,sourceSha256=hashlib.sha256(original).hexdigest(),sha256=digest(dest),modified=data!=original))
        entry={k:v for k,v in p.items() if k not in ['path','includes']}
        entry.update(id=id,files=rows,excluded=excluded);components.append(entry)
        print(id,len(rows),'candidate files',flush=True)
    manifest=dict(schemaVersion=2,importerSha256=digest(Path(__file__)),components=components)
    destination.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')


def import_reviewed(candidate,review):
    """Copy only an exactly reviewed candidate. The review is a separate input."""
    if DEST.exists() or MANIFEST.exists():raise RuntimeError('Existing dependency import requires a deliberate update')
    manifest=json.loads(candidate.with_suffix('.json').read_text())
    approval=json.loads(review.read_text())
    if approval.get('approved') is not True or approval['manifestSha256']!=digest(candidate.with_suffix('.json')):
        raise RuntimeError('Candidate has no matching completed license review')
    expected={c['id']+'/'+f['path']:f['sha256'] for c in manifest['components'] for f in c['files']}
    actual={str(p.relative_to(candidate)):digest(p) for p in candidate.rglob('*') if p.is_file()}
    if actual!=expected:raise RuntimeError('Candidate changed after review')
    for c in manifest['components']:
        for f in c['files']:
            row=approval['files'][c['id']+'/'+f['path']]
            if not row.get('license') or not row.get('basis'):raise RuntimeError('Incomplete per-file license review')
            f.update(license=row['license'],licenseBasis=row['basis'])
    manifest['reviewSha256']=digest(review)
    shutil.copytree(candidate,DEST)
    MANIFEST.write_text(json.dumps(manifest,indent=2)+'\n')
    verify()


def stage():
    if STAGE.exists(): raise RuntimeError('Staging directory exists; review it before another import')
    components=[]
    for id,p in policy():
        source=p['path']
        if (source/'.git').exists():
            actual=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
            if actual!=p['revision']: raise RuntimeError('Changed upstream pin: '+id)
            names=subprocess.check_output(['git','-C',str(source),'ls-files','-z'],text=True).strip('\0').split('\0')
        else: names=[str(q.relative_to(source)) for q in source.rglob('*') if q.is_file()]
        rows=[];excluded=[]
        for name in sorted(names):
            if not selected(id,name,p['includes']): excluded.append(name);continue
            src=source/name
            if src.is_symlink(): raise RuntimeError('Review source symlink: '+str(src))
            dest=STAGE/id/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest)
            rows.append(dict(path=name,sourceSha256=digest(src),sha256=digest(dest)))
        entry={k:v for k,v in p.items() if k not in ['path','includes']}
        entry.update(id=id,files=rows,excluded=excluded)
        components.append(entry)
        print(id,len(rows),'files',flush=True)
    (MIGRATION/'dependency-stage.json').write_text(json.dumps(dict(schemaVersion=1,components=components),indent=2)+'\n')


def verify():
    m=json.loads(MANIFEST.read_text())
    if m['importerSha256']!=digest(Path(__file__)):raise RuntimeError('Dependency import policy changed without review')
    expected=set()
    for c in m['components']:
        for f in c['files']:
            name=c['id']+'/'+f['path'];expected.add(name);path=DEST/name
            if path.is_symlink() or not path.is_file() or digest(path)!=f['sha256']:
                raise RuntimeError('Unreviewed dependency source change: '+name)
    actual={str(p.relative_to(DEST)) for p in DEST.rglob('*') if p.is_file()}
    if actual!=expected: raise RuntimeError('Unreviewed dependency files: '+str(sorted(actual^expected)))
    return m


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('action',choices=['stage','prepare','import','verify'])
    p.add_argument('--candidate',type=Path);p.add_argument('--review',type=Path);a=p.parse_args()
    if a.action=='stage':stage()
    elif a.action=='prepare':prepare(a.candidate.resolve())
    elif a.action=='import':import_reviewed(a.candidate.resolve(),a.review.resolve())
    else:print(len(verify()['components']),'dependency snapshots verified')
