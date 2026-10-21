#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build a registered Cycles device with a reviewable upstream overlay."""
import argparse
import difflib
import hashlib
import json
import re
import subprocess
from pathlib import Path
from build import ROOT,REPO,WORK,REVISION,PACKAGE,audit,generate, SOURCE, verify_cycles
from native_dependencies import prefix as dependency_prefix
from arm_math_source import verify as verify_arm_math
from cycles_overlay import registration_overlay
UPSTREAM=SOURCE

def overlay(out=None):
    verify_cycles()
    verify_arm_math()
    out=Path(out) if out is not None else WORK/'cycles-overlay'
    registration_overlay(out)
    (out/'codebrix_webgpu_device.h').write_text('#include "'+str(ROOT/'cycles/device.h')+'"\n')
    shader=(ROOT/'shaders/controls.wgsl').read_text();(out/'controls_shader.inc').write_text('R"CBWGSL('+shader+')CBWGSL"\n')
    # Reuse the already checked pure integer hash/shuffle functions; native
    # camera accessors below read original KernelData and scene arrays directly.
    sampling=(REPO/'parity/shaders/sampling.wgsl').read_text()
    sampling=sampling[sampling.index('fn wang_hash'):sampling.index('fn sobol_index')]
    common=shader+sampling+(ROOT/'shaders/wavefront_common.wgsl').read_text()+(ROOT/'shaders/wavefront_float.wgsl').read_text()+(ROOT/'shaders/wavefront_trig.wgsl').read_text()+(ROOT/'shaders/wavefront_motion.wgsl').read_text()+(ROOT/'shaders/wavefront_motion_triangle.wgsl').read_text()
    camera=common+(ROOT/'shaders/wavefront_camera.wgsl').read_text()
    (out/'camera_shader.inc').write_text('R"CBWGSL('+camera+')CBWGSL"\n')
    shader_data=common+(ROOT/'shaders/wavefront_shader_data.wgsl').read_text()+(ROOT/'shaders/wavefront_shader_data_cache.wgsl').read_text()
    (out/'shader_data_shader.inc').write_text('R"CBWGSL('+shader_data+')CBWGSL"\n')
    # Generate from the pinned upstream AST, retaining translation evidence.
    translation_inputs=[REPO/'research/translate.py',REPO/'research/native/reference.cpp']
    fingerprint=hashlib.sha256(REVISION.encode()+b''.join(p.read_bytes() for p in translation_inputs)).hexdigest()
    cache=out/'translation-cache.json'
    cached=json.loads(cache.read_text()) if cache.exists() else {}
    valid=cached.get('fingerprint')==fingerprint and all((out/name).exists() and
          hashlib.sha256((out/name).read_bytes()).hexdigest()==digest for name,digest in cached.get('outputs',{}).items())
    if valid and set(cached.get('outputs',{}))=={'translated.wgsl','translation.json'}:
        translated=(out/'translated.wgsl').read_text()
    else:
        generate(UPSTREAM,out,REPO/'research/native/reference.cpp')
        translated=(out/'translated.wgsl').read_text()
        cache.write_text(json.dumps({'fingerprint':fingerprint,'outputs':{name:hashlib.sha256((out/name).read_bytes()).hexdigest()
            for name in ['translated.wgsl','translation.json']}},indent=2)+'\n')
    # WGSL division may round a boundary hit below tmin (observed 4 -> 3.99999976).
    # Keep the original AST output, but use the reviewed binary32 adapter for the
    # depth decision in generated renderer modules; do not loosen ray bounds.
    depth_division='var v_t: f32 = (v_T / v_den);'
    if translated.count(depth_division)!=1:raise RuntimeError('Triangle depth translation anchor changed')
    translated=translated.replace(depth_division,'var v_t: f32 = rounded_divide(v_T, v_den);')
    # The AST intersection stays as reviewed translation evidence; renderer
    # modules call the contraction-proof, oracle-ordered adapter instead.
    if translated.count('fn c_ray_triangle_intersect(')!=1:raise RuntimeError('Triangle intersection translation anchor changed')
    translated=translated.replace('fn c_ray_triangle_intersect(', 'fn c_ray_triangle_intersect_ast(')
    translated=translated.replace('fn c_ray_triangle_dot(', 'fn c_ray_triangle_dot_scalar(')+(ROOT/'shaders/wavefront_triangle.wgsl').read_text()
    svm=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_svm.wgsl','wavefront_attributes.wgsl','wavefront_images.wgsl','wavefront_normals.wgsl'))
    closures=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_svm_closures.wgsl','wavefront_svm_microfacet.wgsl'))
    enum_names=sorted(set(re.findall(r'\b(?:NODE_[A-Za-z0-9_]+|ATTR_[A-Za-z0-9_]+|IMAGE_[A-Za-z0-9_]+|INTERPOLATION_[A-Za-z0-9_]+|EXTENSION_[A-Za-z0-9_]+|KERNEL_(?:IMAGE_NONE|TILE_LOAD_NONE)|SVM_(?:STACK_SIZE|STACK_INVALID|INPUT_STACK_OFFSET_MASK)|CAMERA_(?:PERSPECTIVE|ORTHOGRAPHIC))\b',svm+closures)))
    (out/'svm_enums.inc').write_text(''.join('  ENUM('+name+');\n' for name in enum_names))
    (out/'svm_shader.inc').write_text('R"CBWGSL('+translated+closures+svm+')CBWGSL"\n')
    emitter_math=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_native_trig.wgsl','wavefront_emitters.wgsl'))
    background=shader_data+translated+closures+svm+emitter_math+(ROOT/'shaders/wavefront_background.wgsl').read_text()+(ROOT/'shaders/wavefront_light_eval.wgsl').read_text()
    (out/'background_shader.inc').write_text('R"CBWGSL('+background+')CBWGSL"\n')
    film=(ROOT/'shaders/wavefront_background.wgsl').read_text().split('fn background_visible')[0]
    sampling=(REPO/'parity/shaders/closures.wgsl').read_text()
    sampling=sampling[:sampling.index('struct Closure')]+sampling[sampling.index('// Original numerical adapter:'):sampling.index('fn oren_g')]
    surface=film+sampling+''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_native_trig.wgsl','wavefront_surface_stage.wgsl','wavefront_surface.wgsl','wavefront_microfacet.wgsl','wavefront_emitters.wgsl','wavefront_triangle_light.wgsl','wavefront_lighting.wgsl'))
    (out/'surface_shader.inc').write_text('R"CBWGSL('+surface+')CBWGSL"\n')
    volume=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_native_trig.wgsl','wavefront_volume.wgsl'))
    (out/'volume_shader.inc').write_text('R"CBWGSL('+volume+')CBWGSL"\n')
    closest=common+translated+(ROOT/'shaders/wavefront_closest.wgsl').read_text()
    (out/'closest_shader.inc').write_text('R"CBWGSL('+closest+')CBWGSL"\n')
    traversal=(ROOT/'shaders/wavefront_closest.wgsl').read_text().split('// Point emitters use')[0]
    volume_stack=shader_data+translated+traversal+(ROOT/'shaders/wavefront_volume_stack.wgsl').read_text()
    (out/'volume_stack_shader.inc').write_text('R"CBWGSL('+volume_stack+')CBWGSL"\n')
    shadow=traversal+film+(ROOT/'shaders/wavefront_shadow_traversal.wgsl').read_text()+(ROOT/'shaders/wavefront_shadow.wgsl').read_text()
    (out/'shadow_shader.inc').write_text('R"CBWGSL('+shadow+')CBWGSL"\n')
    # Compile the untouched upstream scalar prefix/sorted helpers and state-copy
    # function bodies as the oracle. The GPU-specific wrapper uses native pointers
    # only in this test translation unit; production keeps virtual addresses.
    source=(UPSTREAM/'src/kernel/integrator/state_util.h').read_text()
    source=source[source.index('ccl_device_inline void integrator_state_copy_only('):source.index('/* NOTE: Leaves kernel scheduling information untouched.')]
    source=source[:source.rfind('#endif')]
    (out/'state_copy_reference.inc').write_text('// SPDX-FileCopyrightText: 2011-2022 Blender Foundation\n// SPDX-License-Identifier: Apache-2.0\n'+source)
    return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--jobs',default='6')
    p.add_argument('--target', action='append', help='Build selected CMake targets instead of the default host and gates')
    args=p.parse_args()
    WORK.mkdir(exist_ok=True)
    (WORK/'cycles-webgpu-compatibility.json').write_text(json.dumps(audit(allow_rust_documentation_change=True),indent=2)+'\n')
    generated=overlay();deps=dependency_prefix();build=WORK/'cycles-build'
    options={'CMAKE_BUILD_TYPE':'Release','CMAKE_POSITION_INDEPENDENT_CODE':'ON','CMAKE_EXPORT_COMPILE_COMMANDS':'ON',
        'CMAKE_PREFIX_PATH':deps,'WITH_LIBS_PRECOMPILED':'OFF','WITH_LEGACY_LIBRARIES':'ON','WITH_CYCLES_NATIVE_ONLY':'ON',
        'OpenImageIO_DIR':deps/'lib/cmake/OpenImageIO','OpenColorIO_DIR':deps/'lib/cmake/OpenColorIO',
        'CMAKE_PROJECT_Cycles_INCLUDE':ROOT/'tools/inject_cycles.cmake','CODEBRIX_CYCLES_OVERLAY':generated,
        'CODEBRIX_WEBGPU_PACKAGE':PACKAGE}
    options.update({'WITH_CYCLES_'+name:'OFF' for name in ['ALEMBIC','EMBREE','OPENIMAGEDENOISE','OPENSUBDIV','OPENVDB','NANOVDB',
                   'OSL','USD','HYDRA_RENDER_DELEGATE','DEVICE_CUDA','DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI']})
    commands=[['cmake','-S',str(UPSTREAM),'-B',str(build),*['-D'+k+'='+str(v) for k,v in options.items()]],
              ['cmake','--build',str(build),'--target','codebrix_cycles_device_gate','codebrix_cycles_camera_gate','codebrix_cycles_closest_gate','codebrix_cycles_background_gate','codebrix_cycles_svm_world_gate','codebrix_cycles_shader_data_gate','codebrix_cycles_svm_surface_gate','codebrix_cycles_attribute_gate','codebrix_cycles_normal_gate','codebrix_cycles_image_gate','codebrix_cycles_image_file_gate','codebrix_cycles_surface_gate','codebrix_cycles_microfacet_svm_gate','codebrix_cycles_microfacet_bounce_gate','codebrix_cycles_microfacet_render_gate','codebrix_cycles_render_gate','codebrix_cycles_atomic_film_gate','--parallel',args.jobs]]
    if args.target:
        commands[1]=['cmake','--build',str(build),'--target',*args.target,'--parallel',args.jobs]
    else:
        commands[1][4:4]=['codebrix_cycles_host','codebrix_host_cpu_test','codebrix_host_gpu_test',
                         'codebrix_host_reload_test','codebrix_host_concurrency_test','codebrix_host_tier_test',
                         'codebrix_host_session_reference','codebrix_host_session64_reference',
                         'codebrix_host_image_reference']
    for command in commands:subprocess.run(command,check=True)
    (WORK/'cycles-build.json').write_text(json.dumps({'commands':commands,'cyclesRevision':REVISION,
        'registrationPatchSha256':hashlib.sha256((WORK/'cycles-registration.patch').read_bytes()).hexdigest(),
        'shaderSha256':hashlib.sha256((ROOT/'shaders/controls.wgsl').read_bytes()).hexdigest(),
        'cameraShaderSha256':hashlib.sha256((ROOT/'shaders/wavefront_camera.wgsl').read_bytes()).hexdigest(),
        'combinedCameraIncludeSha256':hashlib.sha256((generated/'camera_shader.inc').read_bytes()).hexdigest(),
        'combinedBackgroundIncludeSha256':hashlib.sha256((generated/'background_shader.inc').read_bytes()).hexdigest(),
        'combinedClosestIncludeSha256':hashlib.sha256((generated/'closest_shader.inc').read_bytes()).hexdigest()},indent=2)+'\n')
if __name__=='__main__':main()
