#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build a registered Cycles device with a reviewable upstream overlay."""
import argparse
import difflib
import hashlib
import json
import subprocess
from pathlib import Path
from build import ROOT,REPO,WORK,REVISION,PACKAGE,audit
UPSTREAM=Path.home()/'ClaudeHome/cycles-webgpu-upstream'

def overlay():
    if subprocess.check_output(['git','-C',str(UPSTREAM),'rev-parse','HEAD'],text=True).strip()!=REVISION:raise RuntimeError('Cycles revision mismatch')
    if subprocess.check_output(['git','-C',str(UPSTREAM),'status','--porcelain'],text=True).strip():raise RuntimeError('Reference checkout is modified')
    out=WORK/'cycles-overlay';(out/'device').mkdir(parents=True,exist_ok=True)
    before={name:(UPSTREAM/'src'/name).read_text() for name in ['device/device.h','device/device.cpp','integrator/path_trace.cpp']};after=dict(before)
    def replace(name,old,new):
        if after[name].count(old)!=1:raise RuntimeError('Upstream overlay anchor changed: '+old)
        after[name]=after[name].replace(old,new)
    replace('device/device.h','  DEVICE_DUMMY,','  DEVICE_DUMMY,\n  DEVICE_WEBGPU,')
    replace('device/device.h','  DEVICE_MASK_ALL = ~0','  DEVICE_MASK_WEBGPU = (1 << DEVICE_WEBGPU),\n  DEVICE_MASK_ALL = ~0')
    replace('device/device.cpp','#include "device/device.h"','#include "device/device.h"\n#ifdef WITH_WEBGPU\n#  include "codebrix_webgpu_device.h"\n#endif')
    replace('device/device.cpp','    case DEVICE_CPU:\n      device =',
            '#ifdef WITH_WEBGPU\n    case DEVICE_WEBGPU:\n      device = device_webgpu_create(info, stats, profiler, headless);\n      break;\n#endif\n    case DEVICE_CPU:\n      device =')
    replace('device/device.cpp','  if (strcmp(name, "ONEAPI") == 0) {',
            '  if (strcmp(name, "WEBGPU") == 0) {\n    return DEVICE_WEBGPU;\n  }\n  if (strcmp(name, "ONEAPI") == 0) {')
    replace('device/device.cpp','  if (type == DEVICE_ONEAPI) {',
            '  if (type == DEVICE_WEBGPU) {\n    return "WEBGPU";\n  }\n  if (type == DEVICE_ONEAPI) {')
    replace('device/device.cpp','  types.push_back(DEVICE_CPU);','  types.push_back(DEVICE_CPU);\n#ifdef WITH_WEBGPU\n  types.push_back(DEVICE_WEBGPU);\n#endif')
    replace('device/device.cpp','  if (mask & DEVICE_MASK_CPU) {\n    if (!(devices_initialized_mask',
            '#ifdef WITH_WEBGPU\n  if (mask & DEVICE_MASK_WEBGPU) {\n    device_webgpu_info(devices);\n  }\n#endif\n\n  if (mask & DEVICE_MASK_CPU) {\n    if (!(devices_initialized_mask')
    replace('integrator/path_trace.cpp','    case DEVICE_METAL:\n      return "Metal";',
            '    case DEVICE_METAL:\n      return "Metal";\n    case DEVICE_WEBGPU:\n      return "WebGPU";')
    for name in after:
        after[name]='// Copyright (c) 2026 Jeremy Ellis and contributors\n// CodeBrix WebGPU registration additions; upstream notices follow.\n'+after[name]
    patch=''.join(''.join(difflib.unified_diff(before[name].splitlines(True),after[name].splitlines(True),
                     fromfile='a/src/'+name,tofile='b/src/'+name)) for name in before)
    (WORK/'cycles-registration.patch').write_text(patch)
    for name,source in after.items():
        path=out/name;path.parent.mkdir(parents=True,exist_ok=True)
        if not path.exists() or path.read_text()!=source:path.write_text(source)
    (out/'codebrix_webgpu_device.h').write_text('#include "'+str(ROOT/'cycles/device.h')+'"\n')
    shader=(ROOT/'shaders/controls.wgsl').read_text();(out/'controls_shader.inc').write_text('R"CBWGSL('+shader+')CBWGSL"\n')
    # Compile the untouched upstream scalar prefix/sorted helpers and state-copy
    # function bodies as the oracle. The GPU-specific wrapper uses native pointers
    # only in this test translation unit; production keeps virtual addresses.
    source=(UPSTREAM/'src/kernel/integrator/state_util.h').read_text()
    source=source[source.index('ccl_device_inline void integrator_state_copy_only('):source.index('/* NOTE: Leaves kernel scheduling information untouched.')]
    source=source[:source.rfind('#endif')]
    (out/'state_copy_reference.inc').write_text('// SPDX-FileCopyrightText: 2011-2022 Blender Foundation\n// SPDX-License-Identifier: Apache-2.0\n'+source)
    return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--jobs',default='6');args=p.parse_args()
    WORK.mkdir(exist_ok=True)
    (WORK/'cycles-webgpu-compatibility.json').write_text(json.dumps(audit(allow_rust_documentation_change=True),indent=2)+'\n')
    generated=overlay();deps=REPO/'prototype/work';build=WORK/'cycles-build'
    options={'CMAKE_BUILD_TYPE':'Release','CMAKE_POSITION_INDEPENDENT_CODE':'ON','CMAKE_EXPORT_COMPILE_COMMANDS':'ON',
        'CMAKE_PREFIX_PATH':deps/'deps/usr','WITH_LIBS_PRECOMPILED':'OFF','WITH_LEGACY_LIBRARIES':'ON','WITH_CYCLES_NATIVE_ONLY':'ON',
        'OpenImageIO_DIR':deps/'minimal-deps/lib/cmake/OpenImageIO','OpenColorIO_DIR':deps/'minimal-deps/lib/cmake/OpenColorIO',
        'CMAKE_PROJECT_Cycles_INCLUDE':ROOT/'tools/inject_cycles.cmake','CODEBRIX_CYCLES_OVERLAY':generated,
        'CODEBRIX_WEBGPU_PACKAGE':PACKAGE}
    options.update({'WITH_CYCLES_'+name:'OFF' for name in ['ALEMBIC','EMBREE','OPENIMAGEDENOISE','OPENSUBDIV','OPENVDB','NANOVDB',
                   'OSL','USD','HYDRA_RENDER_DELEGATE','DEVICE_CUDA','DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI']})
    commands=[['cmake','-S',str(UPSTREAM),'-B',str(build),*['-D'+k+'='+str(v) for k,v in options.items()]],
              ['cmake','--build',str(build),'--target','codebrix_cycles_device_gate','--parallel',args.jobs]]
    for command in commands:subprocess.run(command,check=True)
    (WORK/'cycles-build.json').write_text(json.dumps({'commands':commands,'cyclesRevision':REVISION,
        'registrationPatchSha256':hashlib.sha256((WORK/'cycles-registration.patch').read_bytes()).hexdigest(),
        'shaderSha256':hashlib.sha256((ROOT/'shaders/controls.wgsl').read_bytes()).hexdigest()},indent=2)+'\n')
if __name__=='__main__':main()
