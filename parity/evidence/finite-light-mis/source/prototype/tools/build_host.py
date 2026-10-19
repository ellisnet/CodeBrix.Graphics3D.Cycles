#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the Linux scene-preparation experiment; no system installation."""
import argparse
import json
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'work'
SOURCES=Path.home()/'ClaudeHome'
PINS={
 'cycles-webgpu-upstream':'a456b761034dda42c32eef9f4aae0fa5a5c9f604',
 'oiio-cycles-webgpu':'ec3d065c97302364c947579b77fa9a151f3e455e',
 'ocio-cycles-webgpu':'6918fad3f5d22ac3ef2397c754bf4268c2b58dd0'}
commands=[]
def run(command):
    command=list(map(str,command)); commands.append(command)
    subprocess.run(command,check=True)
def configure(source,build,options):
    run(['cmake','-S',source,'-B',build,*['-D'+k+'='+str(v) for k,v in options.items()]])
def build(source,folder,options,jobs,install=False):
    configure(source,WORK/folder,options)
    run(['cmake','--build',WORK/folder,'--parallel',jobs])
    if install: run(['cmake','--install',WORK/folder])
def main():
    p=argparse.ArgumentParser(); p.add_argument('--dependencies',action='store_true'); p.add_argument('--jobs',default='6')
    args=p.parse_args()
    for name,revision in PINS.items():
        found=subprocess.check_output(['git','-C',str(SOURCES/name),'rev-parse','HEAD'],text=True).strip()
        if found!=revision: raise RuntimeError('Source revision mismatch: '+name)
    common={'CMAKE_BUILD_TYPE':'Release','CMAKE_INSTALL_PREFIX':WORK/'minimal-deps',
            'CMAKE_PREFIX_PATH':WORK/'deps/usr'}
    if args.dependencies:
        oiio=dict(common)
        oiio.update({k:'OFF' for k in ['OIIO_BUILD_TESTS','OIIO_BUILD_TOOLS','BUILD_DOCS','USE_PYTHON',
          'USE_QT','USE_FFmpeg','USE_OpenCV','USE_LibRaw','USE_Libheif','USE_DCMTK','USE_OpenVDB',
          'USE_OpenColorIO','USE_Ptex','USE_Freetype','USE_OpenJPEG','USE_WebP','USE_GIF','USE_TBB',
          'STOP_ON_WARNING','ENABLE_tiff']})
        oiio.update({'BUILD_FMT_FORCE':'ON','BUILD_ROBINMAP_FORCE':'ON'})
        build(SOURCES/'oiio-cycles-webgpu','oiio-build',oiio,args.jobs,True)
        ocio=dict(common)
        ocio.update({k:'OFF' for k in ['OCIO_BUILD_APPS','OCIO_BUILD_TESTS','OCIO_BUILD_GPU_TESTS',
             'OCIO_BUILD_PYTHON','OCIO_BUILD_DOCS','OCIO_USE_SIMD']})
        ocio['OCIO_INSTALL_EXT_PACKAGES']='MISSING'
        build(SOURCES/'ocio-cycles-webgpu','ocio-build',ocio,args.jobs,True)
    options={'CMAKE_BUILD_TYPE':'Release','CMAKE_POSITION_INDEPENDENT_CODE':'ON',
        'CMAKE_EXPORT_COMPILE_COMMANDS':'ON','CMAKE_PREFIX_PATH':WORK/'deps/usr',
        'WITH_LIBS_PRECOMPILED':'OFF','WITH_LEGACY_LIBRARIES':'ON','WITH_CYCLES_NATIVE_ONLY':'ON',
        'OpenImageIO_DIR':WORK/'minimal-deps/lib/cmake/OpenImageIO',
        'OpenColorIO_DIR':WORK/'minimal-deps/lib/cmake/OpenColorIO',
        'CMAKE_PROJECT_Cycles_INCLUDE':ROOT/'tools/inject.cmake'}
    options.update({'WITH_CYCLES_'+key:'OFF' for key in ['ALEMBIC','EMBREE','OPENIMAGEDENOISE',
        'OPENSUBDIV','OPENVDB','NANOVDB','OSL','USD','HYDRA_RENDER_DELEGATE','DEVICE_CUDA',
        'DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI']})
    configure(SOURCES/'cycles-webgpu-upstream',WORK/'cycles-build',options)
    run(['cmake','--build',WORK/'cycles-build','--target','codebrix_cycles_host_probe','--parallel',args.jobs])
    (WORK/'host-build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if __name__=='__main__': main()
