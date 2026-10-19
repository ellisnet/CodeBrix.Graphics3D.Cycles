#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the Linux scene-preparation experiment; no system installation."""
import argparse
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'work'
sys.path.insert(0,str(ROOT.parent/'tools'))
from cycles_source import verify as verify_cycles
from native_dependencies import prefix as dependency_prefix
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
    if args.dependencies:
        run([sys.executable,ROOT.parent/'tools/dependencies/build.py','--jobs',args.jobs])
    deps=dependency_prefix()
    options={'CMAKE_BUILD_TYPE':'Release','CMAKE_POSITION_INDEPENDENT_CODE':'ON',
        'CMAKE_EXPORT_COMPILE_COMMANDS':'ON','CMAKE_PREFIX_PATH':deps,
        'WITH_LIBS_PRECOMPILED':'OFF','WITH_LEGACY_LIBRARIES':'ON','WITH_CYCLES_NATIVE_ONLY':'ON',
        'OpenImageIO_DIR':deps/'lib/cmake/OpenImageIO',
        'OpenColorIO_DIR':deps/'lib/cmake/OpenColorIO',
        'CMAKE_PROJECT_Cycles_INCLUDE':ROOT/'tools/inject.cmake'}
    options.update({'WITH_CYCLES_'+key:'OFF' for key in ['ALEMBIC','EMBREE','OPENIMAGEDENOISE',
        'OPENSUBDIV','OPENVDB','NANOVDB','OSL','USD','HYDRA_RENDER_DELEGATE','DEVICE_CUDA',
        'DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI']})
    configure(verify_cycles(),WORK/'cycles-build',options)
    run(['cmake','--build',WORK/'cycles-build','--target','codebrix_cycles_host_probe','--parallel',args.jobs])
    (WORK/'host-build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if __name__=='__main__': main()
