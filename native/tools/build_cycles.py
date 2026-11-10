#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build a registered Cycles device with a reviewable upstream overlay."""
import argparse
import difflib
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from build import ROOT,REPO,WORK,REVISION,PACKAGE,audit,generate, SOURCE, verify_cycles
from native_dependencies import prefix as dependency_prefix
from arm_math_source import verify as verify_arm_math
from cycles_overlay import registration_overlay
from rebase_build_tree import rebase, verify as verify_rebase
UPSTREAM=SOURCE
LINK_FLAGS='-Wl,--as-needed -Wl,--exclude-libs,libstdc++_nonshared.a'

def _write(path,text):
    # Generated build inputs are rewritten only when their content changes, so an
    # unchanged overlay never makes Make recompile its dependants.
    path=Path(path)
    if not path.exists() or path.read_text()!=text:path.write_text(text)

# The Linux x64 build runs in the manylinux container (native/manylinux): the checkout is mounted
# at /src there, and build.sh sets this variable. Outside it, a build is handed over to the container.
IN_CONTAINER=bool(os.environ.get('CODEBRIX_CYCLES_CONTAINER'))
CONTAINER_BUILD=REPO/'native/manylinux/build.sh'
STALE_TRANSLATION=('The WGSL translation in the overlay is missing or stale, and it needs Clang: regenerate the overlay on '
    'the build host first (python3 native/tools/build_cycles.py --overlay-only)')

def overlay(out=None,translate=True,located=None):
    """Generate the overlay into OUT. TRANSLATE=False refuses instead of running the Clang AST
    translation when its cached output is not current. LOCATED is the directory the overlay is
    used from (default OUT): the device header names native/cycles relative to it, so the overlay
    holds no checkout path and is the same in every checkout and in the build container."""
    verify_cycles()
    verify_arm_math()
    out=Path(out) if out is not None else WORK/'cycles-overlay'
    registration_overlay(out)
    device=Path(os.path.relpath(ROOT/'cycles/device.h',Path(located) if located is not None else out)).as_posix()
    _write(out/'codebrix_webgpu_device.h','#include "'+device+'"\n')
    shader=(ROOT/'shaders/controls.wgsl').read_text();_write(out/'controls_shader.inc','R"CBWGSL('+shader+')CBWGSL"\n')
    # Reuse the already checked pure integer hash/shuffle functions; native
    # camera accessors below read original KernelData and scene arrays directly.
    sampling=(REPO/'parity/shaders/sampling.wgsl').read_text()
    sampling=sampling[sampling.index('fn wang_hash'):sampling.index('fn sobol_index')]
    common=shader+sampling+(ROOT/'shaders/wavefront_common.wgsl').read_text()+(ROOT/'shaders/wavefront_float.wgsl').read_text()+(ROOT/'shaders/wavefront_trig.wgsl').read_text()+(ROOT/'shaders/wavefront_motion.wgsl').read_text()+(ROOT/'shaders/wavefront_motion_triangle.wgsl').read_text()+(ROOT/'shaders/wavefront_curve_math.wgsl').read_text()+(ROOT/'shaders/wavefront_point.wgsl').read_text()
    # Orthographic and panoramic cameras and depth of field (CAMERA_PROJECTIONS) use the disk-angle sine and cosine.
    camera=common+'//#if CAMERA_PROJECTIONS\n'+(ROOT/'shaders/wavefront_native_trig.wgsl').read_text()+'//#endif\n'+(ROOT/'shaders/wavefront_camera.wgsl').read_text()
    _write(out/'camera_shader.inc','R"CBWGSL('+camera+')CBWGSL"\n')
    shader_data=common+(ROOT/'shaders/wavefront_shader_data.wgsl').read_text()+(ROOT/'shaders/wavefront_curve_shader_data.wgsl').read_text()+(ROOT/'shaders/wavefront_point_shader_data.wgsl').read_text()+(ROOT/'shaders/wavefront_shader_data_cache.wgsl').read_text()
    _write(out/'shader_data_shader.inc','R"CBWGSL('+shader_data+')CBWGSL"\n')
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
        if not translate:raise RuntimeError(STALE_TRANSLATION)
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
    # c_asin/c_acos (the translator's float series): glibc's asinf/acosf are correctly rounded, so the renderer
    # modules take the correctly rounded double-float adapters (wavefront_trig.wgsl trig_asin/trig_acos).
    for name in ('c_asin','c_acos'):
        head='fn '+name+'(x: f32) -> f32 {'
        if translated.count(head)!=1:raise RuntimeError('Arcsine/arccosine translation anchor changed')
        start=translated.index(head);end=translated.index('\n}\n',start)+3
        translated=translated[:start]+head+' return trig_'+name[2:]+'(x); }\n'+translated[end:]
    # The Math node's arcsine and arccosine share one inlined adapter body (compiler pressure: the same operations),
    math_cases='case 7u: {\nreturn c_safe_asinf(v_a);\n}\ncase 8u: {\nreturn c_safe_acosf(v_a);\n}'
    if translated.count(math_cases)!=1:raise RuntimeError('Math node arcsine/arccosine translation anchor changed')
    # Compiled only when a reachable Math node uses them (census bit SVM_MATH_ARC, pipelines.cpp); otherwise the
    # case reports error bit 2 (a missed node is refused, never rendered differently).
    translated=translated.replace(math_cases,'case 7u, 8u: {\n//#if SVM_MATH_ARC else atomicOr(&heap[p(17u)/4u],2u);return 0.0;\n'
                                  'return trig_arc(clamp(v_a, (-1.0f), 1.0f), v_type == 8u);\n//#endif\n}')
    svm=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_svm.wgsl','wavefront_attributes.wgsl','wavefront_images.wgsl','wavefront_normals.wgsl','wavefront_svm_nodes.wgsl','wavefront_svm_derivatives.wgsl'))
    closures=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_svm_closures.wgsl','wavefront_svm_microfacet.wgsl','wavefront_svm_hair.wgsl','wavefront_svm_hair_huang.wgsl'))
    enum_pattern=r'\b(?:NODE_[A-Za-z0-9_]+|ATTR_[A-Za-z0-9_]+|IMAGE_[A-Za-z0-9_]+|INTERPOLATION_[A-Za-z0-9_]+|EXTENSION_[A-Za-z0-9_]+|KERNEL_(?:IMAGE_NONE|TILE_LOAD_NONE)|SVM_(?:STACK_SIZE|STACK_INVALID|INPUT_STACK_OFFSET_MASK)|CAMERA_(?:PERSPECTIVE|ORTHOGRAPHIC))\b'
    # Enum constants used only by the texture/colour/utility node regions are
    # emitted inside their SVM_NODES_ANY layout region (svm_nodes_enums.inc).
    nodes_text=(ROOT/'shaders/wavefront_svm_nodes.wgsl').read_text()
    node_features=set(re.findall(r'^//#if (\w+)',nodes_text,re.M))
    def without_node_regions(text):
        kept,stack=[],[]
        for line in text.split('\n'):
            m=re.match(r'//#if !?(\w+)',line)  # negated regions (//#if !NAME) included
            if m:stack.append(m.group(1) in node_features);continue
            if line.startswith('//#endif'):stack.pop();continue
            if not any(stack):kept.append(line)
        return '\n'.join(kept)
    base_names=set(re.findall(enum_pattern,without_node_regions(svm.replace(nodes_text,''))+closures))
    enum_names=sorted(base_names)
    node_enum_names=sorted(set(re.findall(enum_pattern,svm+closures))-base_names)
    _write(out/'svm_enums.inc',''.join('  ENUM('+name+');\n' for name in enum_names))
    _write(out/'svm_nodes_enums.inc',''.join('  ENUM('+name+');\n' for name in node_enum_names))
    _write(out/'svm_shader.inc','R"CBWGSL('+translated+closures+svm+')CBWGSL"\n')
    emitter_math=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_native_trig.wgsl','wavefront_emitters.wgsl'))
    light_tree=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_light_link.wgsl','wavefront_light_tree.wgsl'))
    background=shader_data+translated+closures+svm+emitter_math+(ROOT/'shaders/wavefront_background_light.wgsl').read_text()+(ROOT/'shaders/wavefront_shadow_catcher.wgsl').read_text()+(ROOT/'shaders/wavefront_background.wgsl').read_text()+(ROOT/'shaders/wavefront_light_eval.wgsl').read_text()+light_tree+(ROOT/'shaders/wavefront_shadow_link_lamp.wgsl').read_text()
    _write(out/'background_shader.inc','R"CBWGSL('+background+')CBWGSL"\n')
    film=(ROOT/'shaders/wavefront_background.wgsl').read_text().split('fn background_visible')[0]+(ROOT/'shaders/wavefront_shadow_catcher.wgsl').read_text()
    sampling=(REPO/'parity/shaders/closures.wgsl').read_text()
    sampling=sampling[:sampling.index('struct Closure')]+sampling[sampling.index('// Original numerical adapter:'):sampling.index('fn oren_g')]
    # MNEE (wavefront_mnee.wgsl) needs the closest traversal inside the surface
    # module; both sit in MNEE regions, so other scenes' surface WGSL is unchanged.
    # The curve intersection (CURVES regions) goes with every copy of the traversal text.
    closest_text=(ROOT/'shaders/wavefront_curve_intersect.wgsl').read_text()+(ROOT/'shaders/wavefront_closest.wgsl').read_text()
    mnee_traversal=closest_text.split('// Point emitters use')[0]
    # Its traversal stack lives in the MNEE record (MR_STACK, mn_stack_at), not in a private
    # array: the same pushes and pops in the same order, in storage memory (the Adreno 730 fails
    # to link the MNEE entries with motion blur when the 192-entry stack is private).
    for old,new,count in (('var stack:array<i32,192>;stack[0]=0x76543210i;var sp=0i;','if(mn_stack_at==0u){fail();return result;}store_word(mn_stack_at,0x76543210u);var sp=0i;',1),
                          ('stack[u32(sp)]=other;','store_word(mn_stack_at+u32(sp)*4u,bitcast<u32>(other));',1),
                          ('stack[u32(sp)]=0x76543210i;','store_word(mn_stack_at+u32(sp)*4u,0x76543210u);',1),
                          ('node=stack[u32(sp)];','node=bitcast<i32>(load_word(mn_stack_at+u32(sp)*4u));',3)):
        assert mnee_traversal.count(old)==count,old
        mnee_traversal=mnee_traversal.replace(old,new)
    assert 'stack[' not in mnee_traversal
    # The helpers before NativeHit (node tests, transforms, curve intersection) also serve the raytrace traversals' curve
    # hits (CURVE_RAYTRACE), with or without MNEE; the same text in the same order in MNEE scenes.
    traversal_helpers,traversal_rest=mnee_traversal.split('struct NativeHit {',1);traversal_rest='struct NativeHit {'+traversal_rest
    surface=film+sampling+''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_native_trig.wgsl','wavefront_surface_stage.wgsl','wavefront_terminator.wgsl','wavefront_surface.wgsl','wavefront_film_passes.wgsl','wavefront_microfacet.wgsl','wavefront_hair.wgsl','wavefront_hair_huang.wgsl','wavefront_emitters.wgsl','wavefront_triangle_light.wgsl','wavefront_background_light.wgsl','wavefront_lighting.wgsl','wavefront_light_nee.wgsl','wavefront_ao.wgsl','wavefront_volume_light.wgsl','wavefront_subsurface.wgsl','wavefront_shadow_link_shade.wgsl'))+light_tree+'//#if TRAVERSAL_HELPERS\n'+traversal_helpers+'//#endif\n//#if MNEE\n'+traversal_rest+'//#endif\n'+(ROOT/'shaders/wavefront_mnee.wgsl').read_text()
    _write(out/'surface_shader.inc','R"CBWGSL('+surface+')CBWGSL"\n')
    volume=''.join((ROOT/'shaders'/name).read_text() for name in ('wavefront_native_trig.wgsl','wavefront_shadow_catcher.wgsl','wavefront_volume.wgsl','wavefront_volume_octree.wgsl','wavefront_volume_heterogeneous.wgsl'))
    _write(out/'volume_shader.inc','R"CBWGSL('+volume+')CBWGSL"\n')
    closest=common+translated+(ROOT/'shaders/wavefront_shadow_catcher.wgsl').read_text()+closest_text+(ROOT/'shaders/wavefront_light_link.wgsl').read_text()+(ROOT/'shaders/wavefront_shadow_link_intersect.wgsl').read_text()
    _write(out/'film_post_shader.inc','R"CBWGSL('+(ROOT/'shaders/wavefront_film_post.wgsl').read_text()+')CBWGSL"\n')
    # Adaptive sampling (film_passes.cpp): the contraction-proof binary32 helpers and the three kernels.
    _write(out/'film_adaptive_shader.inc','R"CBWGSL('+(ROOT/'shaders/wavefront_float.wgsl').read_text()+(ROOT/'shaders/wavefront_film_adaptive.wgsl').read_text()+')CBWGSL"\n')
    _write(out/'film_volume_guiding_shader.inc','R"CBWGSL('+(ROOT/'shaders/wavefront_float.wgsl').read_text()+(ROOT/'shaders/wavefront_film_volume_guiding.wgsl').read_text()+')CBWGSL"\n')
    _write(out/'shadow_catcher_shader.inc','R"CBWGSL('+(ROOT/'shaders/wavefront_shadow_catcher_split.wgsl').read_text()+')CBWGSL"\n')
    _write(out/'closest_shader.inc','R"CBWGSL('+closest+')CBWGSL"\n')
    traversal=closest_text.split('// Point emitters use')[0]
    volume_stack=shader_data+translated+traversal+(ROOT/'shaders/wavefront_volume_stack.wgsl').read_text()
    _write(out/'volume_stack_shader.inc','R"CBWGSL('+volume_stack+')CBWGSL"\n')
    shadow=traversal+film+(ROOT/'shaders/wavefront_shadow_traversal.wgsl').read_text()+(ROOT/'shaders/wavefront_volume_octree.wgsl').read_text()+(ROOT/'shaders/wavefront_shadow.wgsl').read_text()
    _write(out/'shadow_shader.inc','R"CBWGSL('+shadow+')CBWGSL"\n')
    # Compile the untouched upstream scalar prefix/sorted helpers and state-copy
    # function bodies as the oracle. The GPU-specific wrapper uses native pointers
    # only in this test translation unit; production keeps virtual addresses.
    source=(UPSTREAM/'src/kernel/integrator/state_util.h').read_text()
    source=source[source.index('ccl_device_inline void integrator_state_copy_only('):source.index('/* NOTE: Leaves kernel scheduling information untouched.')]
    source=source[:source.rfind('#endif')]
    _write(out/'state_copy_reference.inc','// SPDX-FileCopyrightText: 2011-2022 Blender Foundation\n// SPDX-License-Identifier: Apache-2.0\n'+source)
    return out

def overlay_check(out=None):
    """The build container's overlay step: the handed-in overlay must be exactly what the current
    sources generate. It is regenerated into a scratch directory from its own cached translation
    (no Clang) and compared file by file; nothing in OUT is written. Stale: refused."""
    out=Path(out) if out is not None else WORK/'cycles-overlay'
    with tempfile.TemporaryDirectory() as scratch:
        fresh=Path(scratch)/out.name;fresh.mkdir()
        for name in ('translated.wgsl','translation.json','translation-cache.json'):
            if (out/name).exists():shutil.copy2(out/name,fresh/name)
        overlay(fresh,translate=False,located=out)
        expected={p.relative_to(fresh).as_posix():p.read_bytes() for p in fresh.rglob('*') if p.is_file()}
        patch=(Path(scratch)/'cycles-registration.patch').read_bytes()
    stale=sorted(name for name,data in expected.items() if not (out/name).is_file() or (out/name).read_bytes()!=data)
    if not (out.parent/'cycles-registration.patch').is_file() or (out.parent/'cycles-registration.patch').read_bytes()!=patch:
        stale.append('../cycles-registration.patch')
    if stale:
        raise RuntimeError('The WGSL overlay '+str(out)+' is stale ('+', '.join(stale[:12])+(' ...' if len(stale)>12 else '')+
            '): regenerate it on the build host (python3 native/tools/build_cycles.py --overlay-only)')
    return dict(overlay=str(out),files=len(expected),identical=True)

def build_machine():
    """The container build's toolchain, recorded in cycles-build.json (and from there in provenance)."""
    def first(*command):
        try:return subprocess.run(command,capture_output=True,text=True,check=True).stdout.splitlines()[0].strip()
        except (OSError,subprocess.CalledProcessError,IndexError):return None
    return dict(kind='manylinux container' if IN_CONTAINER else 'host',image=os.environ.get('CODEBRIX_BUILD_IMAGE'),
        imageId=os.environ.get('CODEBRIX_BUILD_IMAGE_ID'),baseImage=os.environ.get('CODEBRIX_BUILD_BASE_IMAGE'),
        baseDigest=os.environ.get('CODEBRIX_BUILD_BASE_DIGEST'),checkoutRoot=str(REPO),
        os=next((l.split('=',1)[1].strip().strip('"') for l in Path('/etc/os-release').read_text().splitlines() if l.startswith('PRETTY_NAME=')),None),
        compiler=first('c++','--version'),linker=first('ld','--version'),libc=first('ldd','--version'),cmake=first('cmake','--version'),
        make=first('make','--version'),python=sys.version.split()[0],
        ispc=first(os.environ['CODEBRIX_ISPC'],'--version') if os.environ.get('CODEBRIX_ISPC') else None)

def hand_over():
    """On the Linux x64 build host: make the overlay here (Clang), then run this script with the
    same arguments in the build container (native/manylinux/build.sh cycles ...)."""
    overlay()
    environment=dict(os.environ,CODEBRIX_CYCLES_OVERLAY_DONE='1')
    result=subprocess.run(['bash',str(CONTAINER_BUILD),'cycles',*sys.argv[1:]],env=environment)
    if result.returncode:raise SystemExit(result.returncode)

def cpu_kernel_flags(build):
    """The CPU kernel compile line of the fixed-baseline oracle, checked: SSE4.2,
    no -march=native/AVX/FMA, contraction and reciprocal math switched off last."""
    rows=[row for row in json.loads((build/'compile_commands.json').read_text())
          if row['file'].endswith('src/kernel/device/cpu/kernel.cpp')]
    if len(rows)!=1:raise RuntimeError('CPU kernel compile command not found')
    words=rows[0]['command'].split()
    flags=[w for w in words if re.match(r'-(O\d|march|mtune|mss?se|mavx|mfma|mf16c|ffp-contract|f(no-)?reciprocal-math|ffast-math|mfpmath)',w)]
    contract=[w for w in flags if w.startswith('-ffp-contract')]
    reciprocal=[w for w in flags if w.endswith('reciprocal-math') or w=='-ffast-math']
    problems=[w for w in flags if w.startswith(('-march','-mavx','-mfma','-mf16c'))]
    if problems or '-msse4.2' not in flags or not contract or contract[-1]!='-ffp-contract=off' \
       or not reciprocal or reciprocal[-1]!='-fno-reciprocal-math':
        raise RuntimeError('CPU kernels are not at the fixed oracle baseline: '+' '.join(flags))
    return dict(file='src/kernel/device/cpu/kernel.cpp',flags=flags,isa='x86-64 SSE4.2',fpContract='off',reciprocalMath='off')

def main():
    p=argparse.ArgumentParser();p.add_argument('--jobs',default='6')
    p.add_argument('--target', action='append', help='Build selected CMake targets instead of the default host and gates')
    p.add_argument('--rebase-build-tree', action='store_true', help='Re-point a build tree copied from another checkout at this checkout first (tools/rebase_build_tree.py)')
    p.add_argument('--overlay-only', action='store_true', help='Only regenerate the overlay (on the build host; needs Clang)')
    p.add_argument('--overlay-check', action='store_true', help='Only check that the overlay is current (the container build tree runs this before compiling it)')
    args=p.parse_args()
    if args.overlay_check:
        print(json.dumps(overlay_check()));return
    if args.overlay_only:
        if IN_CONTAINER:raise RuntimeError('The overlay is generated on the build host, never in the build container: '
            'run python3 native/tools/build_cycles.py --overlay-only there')
        overlay();return
    if not IN_CONTAINER and sys.platform=='linux' and platform.machine()=='x86_64':
        hand_over();return
    WORK.mkdir(exist_ok=True)
    rebased=rebase(WORK/'cycles-build',REPO) if args.rebase_build_tree and (WORK/'cycles-build/CMakeCache.txt').exists() else None
    (WORK/'cycles-webgpu-compatibility.json').write_text(json.dumps(audit(allow_rust_documentation_change=True),indent=2)+'\n')
    if IN_CONTAINER:
        overlay_check();generated=WORK/'cycles-overlay'
    else:
        generated=overlay()
    deps=dependency_prefix();build=WORK/'cycles-build'
    runtime=Path(PACKAGE)/'runtimes/linux-x64/native'
    options={'CMAKE_BUILD_TYPE':'Release','CMAKE_POSITION_INDEPENDENT_CODE':'ON','CMAKE_EXPORT_COMPILE_COMMANDS':'ON',
        'CMAKE_PREFIX_PATH':deps,'WITH_LIBS_PRECOMPILED':'OFF','WITH_LEGACY_LIBRARIES':'ON',
        # Fixed-baseline CPU oracle (native/DEVICE.md): Cycles' SSE4.2 kernels only,
        # never -march=native (which tied the oracle to the build machine) and no
        # AVX2 kernel; inject_cycles.cmake removes contraction and reciprocal math.
        'WITH_CYCLES_NATIVE_ONLY':'OFF','CXX_HAS_AVX2:BOOL':'OFF','CXX_HAS_F16C:BOOL':'OFF',
        'OpenImageIO_DIR':deps/'lib/cmake/OpenImageIO','OpenColorIO_DIR':deps/'lib/cmake/OpenColorIO',
        'CMAKE_PROJECT_Cycles_INCLUDE':ROOT/'tools/inject_cycles.cmake','CODEBRIX_CYCLES_OVERLAY':generated,
        # Every build of the tree regenerates the overlay first, so a plain
        # cmake --build never compiles shader text older than native/shaders.
        # In the build container that step only checks the overlay handed in from the build host.
        'CODEBRIX_CYCLES_OVERLAY_COMMAND':sys.executable+';'+str(Path(__file__).resolve())+(';--overlay-check' if IN_CONTAINER else ';--overlay-only'),
        'CODEBRIX_WEBGPU_PACKAGE':PACKAGE,
        # Run paths: the build tree's own libraries and the dependency prefix relative to $ORIGIN (bin/ and
        # lib/ are both two levels below native/work), so the trees run outside the container and from a
        # copy in another checkout; the unchanged WebGPU runtime from its package path (the container
        # mounts the package at the same path). Staging rewrites every shipped run path to $ORIGIN.
        # glibc 2.28 (the build container) keeps pthread out of libc: FindThreads' result then depends on
        # which find_package(Threads) runs first, and only the second configure of a fresh tree gives every
        # target -pthread, so the first rebuild would recompile everything. Preferring the flag from the
        # start makes the first configure equal every later one.
        'THREADS_PREFER_PTHREAD_FLAG:BOOL':'ON',
        'CMAKE_BUILD_WITH_INSTALL_RPATH':'ON',
        # Only the libraries a binary uses become NEEDED (Debian's GCC default, not the manylinux GCC's), and
        # the static libstdc++_nonshared.a parts stay private to each binary (tools/dependencies/build.py).
        'CMAKE_SHARED_LINKER_FLAGS':LINK_FLAGS,'CMAKE_MODULE_LINKER_FLAGS':LINK_FLAGS,'CMAKE_EXE_LINKER_FLAGS':LINK_FLAGS,
        # Inline functions are hidden (-fvisibility-inlines-hidden). The host exports only its C API, but
        # the container's binutils 2.41 keeps a LOCAL .dynsym entry for every inline libstdc++ member the
        # host defines that libstdc++.so.6 also defines and references (std::string::reserve and others
        # from C++20 translation units): the version script makes them local, yet they stay in the dynamic
        # symbol table. Hidden at compile time they never get there, as with Debian's binutils 2.44.
        'CMAKE_VISIBILITY_INLINES_HIDDEN:BOOL':'ON',
        'CMAKE_INSTALL_RPATH':'$ORIGIN/../lib;$ORIGIN/../../dependencies/install/lib;'+str(runtime)}
    # Open Image Denoise (CPU device) from the reviewed snapshot built into the
    # dependency prefix; path guiding stays off explicitly.
    options.update({'WITH_CYCLES_OPENIMAGEDENOISE':'ON','OPENIMAGEDENOISE_ROOT_DIR':deps,'WITH_CYCLES_PATH_GUIDING':'OFF'})
    # Embree (static, from the reviewed snapshot built into the dependency prefix): the product CPU device's BVH;
    # the host keeps BVH2 for the CPU reference backend and the oracle (docs/RELEASE-LICENSE-PLAN.md, Embree).
    options.update({'WITH_CYCLES_EMBREE':'ON','EMBREE_ROOT_DIR':deps})
    options.update({'WITH_CYCLES_'+name:'OFF' for name in ['ALEMBIC','OPENSUBDIV','OPENVDB','NANOVDB',
                   'OSL','USD','HYDRA_RENDER_DELEGATE','DEVICE_CUDA','DEVICE_OPTIX','DEVICE_HIP','DEVICE_HIPRT','DEVICE_ONEAPI']})
    commands=[['cmake','-S',str(UPSTREAM),'-B',str(build),*['-D'+k+'='+str(v) for k,v in options.items()]],
              ['cmake','--build',str(build),'--target','codebrix_cycles_device_gate','codebrix_cycles_camera_gate','codebrix_cycles_closest_gate','codebrix_cycles_curve_gate','codebrix_cycles_curve_shadow_gate','codebrix_cycles_curve_render_gate','codebrix_cycles_point_gate','codebrix_cycles_point_render_gate','codebrix_cycles_hair_huang_gate','codebrix_cycles_hair_huang_render_gate','codebrix_cycles_background_gate','codebrix_cycles_svm_world_gate','codebrix_cycles_shader_data_gate','codebrix_cycles_svm_surface_gate','codebrix_cycles_attribute_gate','codebrix_cycles_normal_gate','codebrix_cycles_image_gate','codebrix_cycles_image_file_gate','codebrix_cycles_surface_gate','codebrix_cycles_microfacet_svm_gate','codebrix_cycles_microfacet_bounce_gate','codebrix_cycles_microfacet_render_gate','codebrix_cycles_render_gate','codebrix_cycles_atomic_film_gate','--parallel',args.jobs]]
    if args.target:
        commands[1]=['cmake','--build',str(build),'--target',*args.target,'--parallel',args.jobs]
    else:
        commands[1][4:4]=['codebrix_cycles_host','codebrix_host_cpu_test','codebrix_host_gpu_test',
                         'codebrix_host_reload_test','codebrix_host_concurrency_test','codebrix_host_tier_test','codebrix_host_pipeline_cache_test',
                         'codebrix_host_session_reference','codebrix_host_session64_reference',
                         'codebrix_host_image_reference']
    for command in commands:subprocess.run(command,check=True)
    if rebased and rebased['rebased']:verify_rebase(build,REPO,rebased['oldRoot'])
    (WORK/'cycles-build.json').write_text(json.dumps({'commands':commands,'cyclesRevision':REVISION,
        'buildMachine':build_machine(),
        'cpuKernelCompile':cpu_kernel_flags(build),
        'registrationPatchSha256':hashlib.sha256((WORK/'cycles-registration.patch').read_bytes()).hexdigest(),
        'shaderSha256':hashlib.sha256((ROOT/'shaders/controls.wgsl').read_bytes()).hexdigest(),
        'cameraShaderSha256':hashlib.sha256((ROOT/'shaders/wavefront_camera.wgsl').read_bytes()).hexdigest(),
        'combinedCameraIncludeSha256':hashlib.sha256((generated/'camera_shader.inc').read_bytes()).hexdigest(),
        'combinedBackgroundIncludeSha256':hashlib.sha256((generated/'background_shader.inc').read_bytes()).hexdigest(),
        'combinedClosestIncludeSha256':hashlib.sha256((generated/'closest_shader.inc').read_bytes()).hexdigest()},indent=2)+'\n')
if __name__=='__main__':main()
