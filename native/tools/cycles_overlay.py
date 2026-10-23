#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Generated Cycles overlay shared by every RID: device registration names and
the color-cache correction. WGSL generation stays in build_cycles.py."""
import difflib
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from cycles_source import SOURCE as UPSTREAM


def registration_overlay(out):
    out=Path(out)
    (out/'device').mkdir(parents=True,exist_ok=True)
    before={name:(UPSTREAM/'src'/name).read_text() for name in ['device/device.h','device/device.cpp','integrator/path_trace.cpp','util/colorspace.cpp']};after=dict(before)
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
    # Configuration transforms may change while scene-linear names stay equal.
    replace('util/colorspace.cpp', '#include "util/colorspace.h"',
            '// Modifications: Copyright (c) 2026 Jeremy Ellis and contributors\n#include "util/colorspace.h"')
    replace('util/colorspace.cpp', '  static string cache_scene_linear_name;',
            '  static string cache_config_id;')
    replace('util/colorspace.cpp',
            '  /* Invalidate cached processors and colorspace, in case Blender changed it.\n'
            '   * Note this should not happen during rendering, all render should be stopped\n'
            '   * before it is changed. */',
            '  /* Configurations can change transforms, roles and aliases while retaining\n'
            '   * the same scene-linear name. Invalidate every derived cache when the full\n'
            '   * OCIO configuration identity changes. All renders must be stopped first. */')
    replace('util/colorspace.cpp',
            '  const OCIO::ConstColorSpaceRcPtr scene_linear_colorspace = config->getColorSpace("scene_linear");\n'
            '  if (scene_linear_colorspace && cache_scene_linear_name != scene_linear_colorspace->getName()) {\n'
            '    cache_scene_linear_name = scene_linear_colorspace->getName();',
            '  const string config_id = config->getCacheID();\n'
            '  if (cache_config_id != config_id) {\n'
            '    cache_config_id = config_id;')
    for name in after:
        if name != 'util/colorspace.cpp':
            after[name]='// Copyright (c) 2026 Jeremy Ellis and contributors\n// CodeBrix WebGPU registration additions; upstream notices follow.\n'+after[name]
    patch=''.join(''.join(difflib.unified_diff(before[name].splitlines(True),after[name].splitlines(True),
                     fromfile='a/src/'+name,tofile='b/src/'+name)) for name in before)
    if not (out.parent/'cycles-registration.patch').exists() or (out.parent/'cycles-registration.patch').read_text()!=patch:
        (out.parent/'cycles-registration.patch').write_text(patch)
    for name,source in after.items():
        path=out/name;path.parent.mkdir(parents=True,exist_ok=True)
        if not path.exists() or path.read_text()!=source:path.write_text(source)
    return out
