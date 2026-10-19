#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the reviewed native dependency profile entirely from local snapshots.

Compiler, CMake, Ninja/Make, NASM and platform SDKs are external build tools.
No implementation sources are fetched by this script or its configure steps.
The initial executable recipe is Linux; other RID recipes need native proof.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/licensing'))
from dependency_sources import verify
SOURCE=ROOT/'third_party/deps'
WORK=ROOT/'native/work/dependencies'
PREFIX=WORK/'install'


def profile(source):
    def off(*names):return {name:'OFF' for name in names}
    return [
      ('zlib',source/'zlib',off('ZLIB_BUILD_EXAMPLES')),
      ('zstd',source/'zstd/build/cmake',off('ZSTD_BUILD_PROGRAMS','ZSTD_BUILD_TESTS','ZSTD_BUILD_CONTRIB','ZSTD_BUILD_STATIC')),
      ('imath',source/'imath',dict(BUILD_TESTING='OFF',IMATH_BUILD_TESTS='OFF',PYTHON='OFF',IMATH_INSTALL_PKG_CONFIG='ON')),
      ('openexr',source/'openexr',dict(OPENEXR_BUILD_TOOLS='OFF',OPENEXR_INSTALL_EXAMPLES='OFF',OPENEXR_INSTALL_TOOLS='OFF',OPENEXR_BUILD_PYTHON='OFF',OPENEXR_FORCE_INTERNAL_IMATH='OFF')),
      ('png',source/'png',dict(PNG_TESTS='OFF',PNG_TOOLS='OFF',PNG_STATIC='OFF',PNG_EXECUTABLES='OFF')),
      ('jpeg',source/'jpeg',dict(ENABLE_STATIC='OFF',WITH_TURBOJPEG='OFF',WITH_JAVA='OFF')),
      ('tiff',source/'tiff',dict(**{'tiff-tools':'OFF','tiff-tests':'OFF','tiff-contrib':'OFF','tiff-docs':'OFF','lzw':'OFF','jbig':'OFF','lzma':'OFF','webp':'OFF','lerc':'OFF','libdeflate':'OFF','CMAKE_DISABLE_FIND_PACKAGE_JBIG':'ON','CMAKE_DISABLE_FIND_PACKAGE_LibLZMA':'ON','CMAKE_DISABLE_FIND_PACKAGE_WebP':'ON','CMAKE_DISABLE_FIND_PACKAGE_LERC':'ON','CMAKE_DISABLE_FIND_PACKAGE_Deflate':'ON'})),
      ('expat',source/'expat/expat',dict(EXPAT_BUILD_TOOLS='OFF',EXPAT_BUILD_EXAMPLES='OFF',EXPAT_BUILD_TESTS='OFF',EXPAT_BUILD_DOCS='OFF',EXPAT_SHARED_LIBS='ON')),
      ('tbb',source/'tbb',dict(TBB_TEST='OFF',TBB_DISABLE_HWLOC_AUTOMATIC_SEARCH='ON',TBB_EXAMPLES='OFF',TBB_STRICT='OFF',TBBMALLOC_BUILD='OFF',TBBMALLOC_PROXY_BUILD='OFF',TBB_ENABLE_IPO='OFF')),
      ('pugixml',source/'pugixml',dict(PUGIXML_BUILD_TESTS='OFF')),
      ('boost',ROOT/'tools/dependencies/boost',dict(BOOST_SOURCE_ROOT=source/'boost')),
      ('fmt',source/'fmt',dict(FMT_DOC='OFF',FMT_TEST='OFF',FMT_INSTALL='ON',BUILD_SHARED_LIBS='OFF')),
      ('robin-map',source/'robin-map',dict(TSL_ROBIN_MAP_ENABLE_TESTS='OFF')),
      ('pystring',ROOT/'tools/dependencies/pystring',dict(PYSTRING_SOURCE_ROOT=source/'pystring',BUILD_SHARED_LIBS='OFF')),
      ('yaml-cpp',source/'yaml-cpp',dict(YAML_CPP_BUILD_TESTS='OFF',YAML_CPP_BUILD_TOOLS='OFF',YAML_CPP_BUILD_CONTRIB='OFF',YAML_BUILD_SHARED_LIBS='OFF',BUILD_SHARED_LIBS='OFF')),
      ('minizip',source/'minizip',dict(BUILD_SHARED_LIBS='OFF',MZ_ZLIB='ON',**off('MZ_BZIP2','MZ_LZMA','MZ_ZSTD','MZ_LIBCOMP','MZ_FETCH_LIBS','MZ_FORCE_FETCH_LIBS','MZ_PKCRYPT','MZ_WZAES','MZ_OPENSSL','MZ_LIBBSD','MZ_SIGNING','MZ_ICONV','MZ_BUILD_TESTS','MZ_BUILD_UNIT_TESTS','MZ_BUILD_FUZZ_TESTS','MZ_COMPAT'))),
      ('ocio',source/'ocio',dict(OCIO_INSTALL_EXT_PACKAGES='NONE',OCIO_USE_SIMD='OFF',**off('OCIO_BUILD_APPS','OCIO_BUILD_TESTS','OCIO_BUILD_GPU_TESTS','OCIO_BUILD_PYTHON','OCIO_BUILD_DOCS','OCIO_BUILD_JAVA','OCIO_BUILD_NUKE','OCIO_BUILD_OPENFX'))),
      # The filtered tree omits tiff.imageio entirely. Upstream normally adds
      # DISABLE_TIFF while visiting that directory; explicitly retain that
      # definition so the global plugin registry has no missing TIFF symbols.
      ('oiio',source/'oiio',dict(CMAKE_CXX_FLAGS='-DDISABLE_TIFF',BUILD_FMT_FORCE='OFF',BUILD_ROBINMAP_FORCE='OFF',BUILD_MISSING_DEPS='OFF',INSTALL_FONTS='OFF',**off('OIIO_BUILD_TESTS','OIIO_BUILD_TOOLS','BUILD_DOCS','USE_PYTHON','USE_QT','USE_FFmpeg','USE_OpenCV','USE_LibRaw','USE_Libheif','USE_DCMTK','USE_OpenVDB','USE_OpenColorIO','USE_Ptex','USE_Freetype','USE_OpenJPEG','USE_WebP','USE_GIF','USE_TBB','USE_R3DSDK','USE_Nuke','STOP_ON_WARNING','ENABLE_tiff'))),
    ]


def run(source=SOURCE,work=WORK,jobs=6,only=None):
    if source==SOURCE:verify()
    prefix=work/'install';prefix.mkdir(parents=True,exist_ok=True)
    commands=[]
    env=dict(os.environ)
    env['CMAKE_BUILD_PARALLEL_LEVEL']=str(jobs)
    env['PKG_CONFIG_PATH']=str(prefix/'lib/pkgconfig')
    env['LD_LIBRARY_PATH']=str(prefix/'lib')
    for name,path,options in profile(source):
        if only and name not in only:continue
        common=dict(CMAKE_BUILD_TYPE='Release',CMAKE_POSITION_INDEPENDENT_CODE='ON',
                    CMAKE_EXPORT_COMPILE_COMMANDS='ON',CMAKE_INSTALL_PREFIX=prefix,
                    CMAKE_PREFIX_PATH=prefix,
                    CMAKE_INSTALL_RPATH='$ORIGIN',CMAKE_BUILD_RPATH=str(prefix/'lib'),
                    BUILD_SHARED_LIBS='ON',BUILD_TESTING='OFF',
                    FETCHCONTENT_FULLY_DISCONNECTED='ON',FETCHCONTENT_UPDATES_DISCONNECTED='ON')
        common['CMAKE_INSTALL_LIBDIR:STRING']='lib'
        common.update(options)
        build=work/name;build.mkdir(parents=True,exist_ok=True)
        steps=[['cmake','-S',path,'-B',build,*['-D'+k+'='+str(v) for k,v in common.items()]],
               ['cmake','--build',build,'--parallel',str(jobs)],['cmake','--install',build]]
        with (work/(name+'.log')).open('w') as log:
            for command in steps:
                command=list(map(str,command));commands.append(command)
                subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
        print(name,'built and installed',flush=True)
        (work/(name+'-commands.json')).write_text(json.dumps(steps,default=str,indent=2)+'\n')
    if source==SOURCE and all((work/name/'install_manifest.txt').exists() for name,_,_ in profile(source)):
        marker={'sourceManifestSha256':hashlib.sha256((ROOT/'third_party/dependency-imports.json').read_bytes()).hexdigest(),
                'recipeSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'libraries':{str(p.relative_to(prefix)):hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in (prefix/'lib').rglob('*') if p.is_file() and not p.is_symlink() and ('.so' in p.name or p.suffix=='.a')}}
        (prefix/'codebrix-source-build.json').write_text(json.dumps(marker,indent=2)+'\n')
    return prefix


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--jobs',type=int,default=6)
    p.add_argument('--only',nargs='+');p.add_argument('--staging',type=Path,
        help='License-review development only: outside-repo staged sources, never a release build')
    a=p.parse_args()
    if a.staging:
        if ROOT in a.staging.resolve().parents:raise RuntimeError('Staging must be outside the repository')
        print(run(a.staging,a.staging.parent/'staging-build',a.jobs,a.only))
    else:print(run(jobs=a.jobs,only=a.only))
