#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the film pass oracle scenes (scenes/passes/) byte for byte.

Each scene enables named passes ("pass" nodes); codebrix_cycles_oracle writes every pass
film beside Combined and compare.py judges each one against the CPU gold standard
(native/oracle/README.md, "Named passes"). Usage:
python3 native/oracle/scenes/author_passes.py [--check]."""
import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent/'passes'


def graph(nodes, connect):
    return {'nodes': nodes, 'connect': connect}


def shader(handle, nodes, connect, extra=None):
    return {'handle': handle, 'create': 'shader', 'graph': graph(nodes, connect), 'set': {'emission_sampling_method': 'none', **(extra or {})}}


def diffuse(handle, color, extra=None):
    return shader(handle, [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': color, 'roughness': 0.25}}],
                  [['d', 'BSDF', 'output', 'Surface']], extra)


def mixed(handle, extra=None):
    return shader(handle, [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.55, 0.32, 0.18], 'roughness': 0.31}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.2, 0.5, 0.8], 'roughness': 0.35}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.4}}],
                  [['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']], extra)


def glass(handle, extra=None):
    return shader(handle, [{'handle': 'g', 'type': 'glass_bsdf', 'set': {'distribution': 'ggx', 'color': [0.85, 0.9, 0.95], 'roughness': 0.15, 'IOR': 1.45}}],
                  [['g', 'BSDF', 'output', 'Surface']], extra)


def glossy(handle, extra=None):
    return shader(handle, [{'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.8, 0.7, 0.5], 'roughness': 0.2}}],
                  [['g', 'BSDF', 'output', 'Surface']], extra)


def emitter(handle, color, strength, extra=None):
    return shader(handle, [{'handle': 'e', 'type': 'emission', 'set': {'color': color, 'strength': strength}}],
                  [['e', 'Emission', 'output', 'Surface']], extra)


def semi_transparent(handle, extra=None):
    """A diffuse/transparent mix: alpha 0.3 (below the default pass alpha threshold 0.5)."""
    return shader(handle, [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.3, 0.7, 0.4], 'roughness': 0.0}},
                           {'handle': 't', 'type': 'transparent_bsdf', 'set': {'color': [1, 1, 1]}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.7}}],
                  [['d', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']], extra)


UV = [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0]


def quad(handle, material, corners, object_set=None, uv=False):
    mesh = {'positions': [c for p in corners for c in p], 'indices': [0, 1, 2, 0, 2, 3]}
    if uv:
        mesh['attributes'] = [{'name': 'uv', 'value': 'point2', 'domain': 'corner', 'flags': ['standard'], 'data': UV}]
    return [{'handle': handle, 'create': 'mesh', 'mesh': mesh, 'set': {'used_shaders': [material]}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle, **(object_set or {})}}]


def passes(entries):
    return [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name, **extra}} for kind, name, extra in entries]


def scene(scene_id, description, materials, objects, pass_list, film=None, world=None, lamp=None, render=None, integrator=None):
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': {
                 'sampling_pattern': 'tabulated_sobol', 'seed': 0, 'use_guiding': False, 'use_light_tree': False,
                 'use_adaptive_sampling': False, 'use_denoise': False, 'min_bounce': 2, 'max_bounce': 4, 'max_diffuse_bounce': 3,
                 'max_glossy_bounce': 3, 'max_transmission_bounce': 4, 'transparent_max_bounce': 4, 'sample_clamp_direct': 0.0,
                 'sample_clamp_indirect': 0.0, 'caustics_reflective': True, 'caustics_refractive': True, 'filter_glossy': 0.0, **(integrator or {})}},
             # A film value None leaves the socket out (film use_sample_count is Cycles' session state; the host refuses a changing set).
             {'handle': 'film', 'scene': 'film', 'set': {k: v for k, v in {'use_sample_count': False, 'exposure': 1.0, 'filter_type': 'box', 'filter_width': 1.0, **(film or {})}.items() if v is not None}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'camera_type': 'perspective', 'fov': 0.9, 'nearclip': 0.1, 'farclip': 100.0,
                                                               'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}},
             *materials, *objects,
             shader('world', [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}],
                    [['b', 'Background', 'output', 'Surface']]),
             {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world', **(world or {})}},
             emitter('lamp-emission', [1, 1, 1], 1),
             {'handle': 'lamp', 'create': 'pointlight', 'set': {'radius': 0.45, 'is_sphere': True, 'use_mis': True, 'strength': [110, 90, 70], 'used_shaders': ['lamp-emission']}},
             {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0, -2.0], **(lamp or {})}},
             *passes(pass_list)]
    return {'cbcOracleScene': 1, 'id': scene_id, 'description': description, 'sourceFixture': 'native/oracle/scenes/author_passes.py',
            'render': render or {'width': 48, 'height': 36, 'samples': 8, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


BACK = [(-4, -3, 3), (4, -3, 3), (4, 3, 3), (-4, 3, 3)]
FLOOR = [(-4, -2, -2), (4, -2, -2), (4, -2, 3), (-4, -2, 3)]
CENTER = [(-0.8, -2, 0), (0.8, -2, 0), (0.8, 1.1, 0), (-0.8, 1.1, 0)]
LEFT = [(-3.2, -2, -1), (-1.4, -2, 0.5), (-1.4, 1.5, 0.5), (-3.2, 1.5, -1)]
RIGHT = [(1.4, -2, 0.5), (3.2, -2, -1), (3.2, 1.5, -1), (1.4, 1.5, 0.5)]
PANEL = [(-0.6, 1.6, 1.5), (0.6, 1.6, 1.5), (0.6, 2.4, 1.5), (-0.6, 2.4, 1.5)]
FRONT = [(-2.2, -1.9, -1.6), (-0.9, -1.9, -1.6), (-0.9, -0.6, -1.6), (-2.2, -0.6, -1.6)]
SIDE = [(2.0, -1.0, -1.5), (3.4, -1.0, -1.5), (3.4, 0.8, -1.5), (2.0, 0.8, -1.5)]

LIGHT_PASSES = [('diffuse_direct', 'DiffDir', {}), ('diffuse_indirect', 'DiffInd', {}), ('diffuse_color', 'DiffCol', {}),
                ('glossy_direct', 'GlossDir', {}), ('glossy_indirect', 'GlossInd', {}), ('glossy_color', 'GlossCol', {}),
                ('transmission_direct', 'TransDir', {}), ('transmission_indirect', 'TransInd', {}), ('transmission_color', 'TransCol', {}),
                ('emission', 'Emit', {}), ('background', 'Env', {}), ('diffuse', 'Diffuse', {}), ('glossy', 'Glossy', {}),
                ('transmission', 'Transmission', {})]
DATA_PASSES = [('depth', 'Depth', {}), ('position', 'Position', {}), ('normal', 'Normal', {}), ('roughness', 'Roughness', {}),
               ('uv', 'UV', {}), ('object_id', 'IndexOB', {}), ('material_id', 'IndexMA', {}), ('mist', 'Mist', {}),
               ('render_time', 'RenderTime', {})]


def scenes():
    room = [diffuse('wall', [0.6, 0.62, 0.65]), mixed('floor-material'), glass('glass-material'), glossy('metal'),
            emitter('panel-emission', [1.0, 0.8, 0.5], 6.0)]
    room_objects = (quad('back', 'wall', BACK)+quad('floor', 'floor-material', FLOOR)+quad('center', 'glass-material', CENTER)+
                    quad('left', 'metal', LEFT)+quad('right', 'wall', RIGHT)+quad('panel', 'panel-emission', PANEL))
    light = scene('passes/light-passes', 'light passes: diffuse, glossy and transmission direct, indirect and colour, emission and '
                  'background (and the combined diffuse, glossy and transmission passes) over diffuse, glossy, glass and emissive surfaces',
                  room, room_objects, LIGHT_PASSES)
    data_materials = [diffuse('wall', [0.6, 0.62, 0.65], {'pass_id': 3}), mixed('floor-material', {'pass_id': 5}),
                      glass('glass-material', {'pass_id': 7}), glossy('metal', {'pass_id': 9}), semi_transparent('veil', {'pass_id': 11})]
    data_objects = (quad('back', 'wall', BACK, {'pass_id': 1}, uv=True)+quad('floor', 'floor-material', FLOOR, {'pass_id': 2}, uv=True)+
                    quad('center', 'glass-material', CENTER, {'pass_id': 4})+quad('left', 'metal', LEFT, {'pass_id': 6}, uv=True)+
                    quad('right', 'wall', RIGHT, {'pass_id': 8})+quad('front', 'veil', FRONT, {'pass_id': 10}, uv=True))
    data = scene('passes/data-passes', 'data passes: depth, position, normal, roughness, UV, object and material index, mist (falloff 1.5) '
                 'and render time, with a semi-transparent surface below the pass alpha threshold in front',
                 data_materials, data_objects, DATA_PASSES, film={'mist_start': 2.0, 'mist_depth': 9.0, 'mist_falloff': 1.5})
    groups_materials = room[:4]+[emitter('panel-emission', [1.0, 0.8, 0.5], 6.0)]
    groups_objects = (quad('back', 'wall', BACK)+quad('floor', 'floor-material', FLOOR)+quad('center', 'glass-material', CENTER)+
                      quad('left', 'metal', LEFT)+quad('right', 'wall', RIGHT)+quad('panel', 'panel-emission', PANEL, {'lightgroup': 'panel'}))
    groups = scene('passes/light-groups', 'light groups: the point light, the emissive panel and the world each in their own group, '
                   'with the diffuse and glossy direct passes', groups_materials, groups_objects,
                   [('combined', 'LG lamp', {'lightgroup': 'lamp'}), ('combined', 'LG panel', {'lightgroup': 'panel'}),
                    ('combined', 'LG world', {'lightgroup': 'world'}), ('diffuse_direct', 'DiffDir', {}), ('glossy_direct', 'GlossDir', {})],
                   world={'lightgroup': 'world'}, lamp={'lightgroup': 'lamp'})
    # AOVs: colour and value outputs from surface and world shaders, behind a transparent veil and a
    # texture-coordinate driven value (stack inputs), plus a constant colour on a second material.
    aov_wall = shader('wall', [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.6, 0.62, 0.65], 'roughness': 0.25}},
                               {'handle': 'tc', 'type': 'texture_coordinate'},
                               {'handle': 'sep', 'type': 'separate_xyz'},
                               {'handle': 'ac', 'type': 'aov_output', 'set': {'name': 'Coords'}},
                               {'handle': 'av', 'type': 'aov_output', 'set': {'name': 'Height'}}],
                      [['d', 'BSDF', 'output', 'Surface'], ['tc', 'Generated', 'ac', 'Color'], ['tc', 'Object', 'sep', 'Vector'],
                       ['sep', 'Y', 'av', 'Value']])
    aov_metal = shader('metal', [{'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.8, 0.7, 0.5], 'roughness': 0.2}},
                                 {'handle': 'ac', 'type': 'aov_output', 'set': {'name': 'Coords', 'color': [0.25, 0.5, 0.75]}},
                                 {'handle': 'av', 'type': 'aov_output', 'set': {'name': 'Height', 'value': -2.5}}],
                       [['g', 'BSDF', 'output', 'Surface']])
    aov_materials = [aov_wall, mixed('floor-material'), aov_metal, semi_transparent('veil')]
    aov_objects = (quad('back', 'wall', BACK)+quad('floor', 'floor-material', FLOOR)+quad('left', 'metal', LEFT)+
                   quad('right', 'wall', RIGHT)+quad('front', 'veil', FRONT))
    aov = scene('passes/aovs', 'AOV outputs: a colour AOV from texture coordinates and a value AOV from object Y on the walls, '
                'constants on the metal, seen directly, behind a semi-transparent veil and around the world',
                aov_materials, aov_objects, [('aov_color', 'Coords', {}), ('aov_value', 'Height', {}), ('object_id', 'IndexOB', {})])
    # Cryptomatte: object, material and asset ids (named objects and shaders; two objects share an asset),
    # depth 2 (two passes of two id/weight slots per type), through a semi-transparent veil and a glass panel.
    crypto_materials = [diffuse('wall', [0.6, 0.62, 0.65], {'name': 'wall-material'}), mixed('floor-material', {'name': 'floor-material'}),
                        glass('glass-material', {'name': 'glass-material'}), glossy('metal', {'name': 'metal-material'}),
                        semi_transparent('veil', {'name': 'veil-material'})]
    crypto_objects = (quad('back', 'wall', BACK, {'name': 'back', 'asset_name': 'room'})+quad('floor', 'floor-material', FLOOR, {'name': 'floor', 'asset_name': 'room'})+
                      quad('center', 'glass-material', CENTER, {'name': 'pane', 'asset_name': 'pane'})+quad('left', 'metal', LEFT, {'name': 'left', 'asset_name': 'props'})+
                      quad('right', 'wall', RIGHT, {'name': 'right', 'asset_name': 'props'})+quad('front', 'veil', FRONT, {'name': 'veil', 'asset_name': 'veil'}))
    crypto = scene('passes/cryptomatte', 'cryptomatte: object, material and asset id/weight slots (depth 2), sorted by the '
                   'post-processing kernel, with a semi-transparent veil and a glass panel', crypto_materials, crypto_objects,
                   [('cryptomatte', name, {}) for name in ('CryptoObject00', 'CryptoObject01', 'CryptoMaterial00', 'CryptoMaterial01',
                                                            'CryptoAsset00', 'CryptoAsset01')],
                   film={'cryptomatte_passes': 7, 'cryptomatte_depth': 2})
    # Denoising features (no denoiser runs): albedo, normal, specular albedo, roughness and depth, with the
    # default film options (follow reflections) and with reflections not followed and albedo-roughness weighting.
    denoise_passes = [('denoising_albedo', 'Denoising Albedo', {}), ('denoising_normal', 'Denoising Normal', {}),
                      ('denoising_specular_albedo', 'Denoising Specular Albedo', {}), ('denoising_roughness', 'Denoising Roughness', {}),
                      ('denoising_depth', 'Denoising Depth', {})]
    denoise_materials = room+[semi_transparent('veil')]
    denoise_objects = room_objects+quad('front', 'veil', FRONT)
    denoise = scene('passes/denoising-features', 'denoising feature passes (albedo, normal, specular albedo, roughness, depth) '
                    'following reflections, over diffuse, glossy, glass, emissive and semi-transparent surfaces',
                    denoise_materials, denoise_objects, denoise_passes)
    denoise_weighted = scene('passes/denoising-features-weighted', 'denoising feature passes without following reflections and '
                             'with albedo roughness weighting', denoise_materials, denoise_objects, denoise_passes,
                             film={'denoising_pass_follow_reflections': False, 'denoising_pass_use_albedo_roughness_weighting': True})
    # Motion pass without motion blur (Scene::MOTION_PASS): moving and rotating objects, a moving camera with a
    # changing field of view; the world behind them writes no motion vector (pinned film_write_data_passes_background).
    motion_objects = (quad('back', 'wall', BACK)+quad('floor', 'floor-material', FLOOR)+
                      quad('center', 'glass-material', CENTER, {'motion': [[0.92, 1.98e-05, -9.12e-05, -0.34, -1.56e-05, 0.945, 2.46e-05, -0.11, 0.0001039, -3.56e-05, 1.164, -0.07],
                                                                           [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0],
                                                                           [1.08, -1.79e-05, 9.69e-05, 0.34, 1.83e-05, 0.855, -2.62e-05, 0.11, -0.0001219, 3.22e-05, 1.236, 0.07]]})+
                      quad('left', 'metal', LEFT, {'motion': [[0.9950042, 0.0, 0.0998334, -0.25, 0.0, 1.0, 0.0, 0.05, -0.0998334, 0.0, 0.9950042, 0.1],
                                                             [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0],
                                                             [0.9950042, 0.0, -0.0998334, 0.25, 0.0, 1.0, 0.0, -0.05, 0.0998334, 0.0, 0.9950042, -0.1]]})+
                      quad('right', 'wall', RIGHT)+quad('panel', 'panel-emission', PANEL))
    motion = scene('passes/motion', 'motion and motion weight passes without motion blur: moving and rotating objects, '
                   'a moving camera with a changing field of view; the background writes no motion vector', room, motion_objects,
                   [('motion', 'Vector', {}), ('motion_weight', 'Vector Weight', {}), ('depth', 'Depth', {})],
                   integrator={'motion_blur': False})
    for node in motion['nodes']:
        if node.get('scene') == 'camera':
            node['set'].update({'motion': [[1.0, 0.0, 0.0, -0.15, 0.0, 1.0, 0.0, 0.05, 0.0, 0.0, 1.0, -7.2],
                                           [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0],
                                           [1.0, 0.0, 0.0, 0.15, 0.0, 1.0, 0.0, -0.05, 0.0, 0.0, 1.0, -6.8]],
                                'use_perspective_motion': True, 'fov_pre': 0.88, 'fov_post': 0.92, 'shuttertime': 0.5})
    # Denoising motion features: backward motion (first bounce) and specular motion (after a reflection),
    # with the motion pass's transforms, over the moving scene with its glossy surfaces.
    import copy
    denoise_motion = copy.deepcopy(motion)
    denoise_motion['id'] = 'passes/denoising-motion'
    denoise_motion['description'] = ('denoising motion features (backward motion and specular motion) without motion blur, with moving objects '
                                     'and a moving camera, beside the denoising albedo and normal')
    denoise_motion['nodes'] = [n for n in denoise_motion['nodes'] if n.get('create') != 'pass']+passes(
        [('denoising_backward_motion', 'Denoising Backward Motion', {}), ('denoising_specular_motion', 'Denoising Specular Motion', {}),
         ('denoising_albedo', 'Denoising Albedo', {}), ('denoising_normal', 'Denoising Normal', {})])
    # Denoising backward motion and depth through transparent surfaces, with no data pass in the film (the data pass
    # stage runs only for its single-pass bookkeeping): a veil below the pass alpha threshold (alpha 0.3: the motion
    # is written by what lies behind it, the wall or the background) reaching over the wall's edge onto the
    # background, and a moving veil above the threshold (alpha 0.8: the veil's own motion, once per path). The
    # depth takes the near clip distance on a path's first segment only and the far clip depth at the background.
    veil_solid = shader('veil-solid', [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.7, 0.3, 0.4], 'roughness': 0.0}},
                                       {'handle': 't', 'type': 'transparent_bsdf', 'set': {'color': [1, 1, 1]}},
                                       {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.2}}],
                        [['d', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])
    denoise_motion_transparent = copy.deepcopy(denoise_motion)
    denoise_motion_transparent['id'] = 'passes/denoising-motion-transparent'
    denoise_motion_transparent['description'] = ('denoising backward motion and depth without motion blur and without data passes, through a '
                                                 'semi-transparent veil below the pass alpha threshold (over the wall and the background) and a '
                                                 'moving one above it')
    veils = (quad('side', 'veil', SIDE)+
             quad('front', 'veil-solid', FRONT, {'motion': [[1.0, 0.0, 0.0, -0.2, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.05],
                                                            [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0],
                                                            [1.0, 0.0, 0.0, 0.2, 0.0, 1.0, 0.0, -0.1, 0.0, 0.0, 1.0, -0.05]]}))
    kept = [n for n in denoise_motion_transparent['nodes'] if n.get('create') != 'pass']
    world = next(i for i, n in enumerate(kept) if n['handle'] == 'world')
    denoise_motion_transparent['nodes'] = (kept[:world]+[semi_transparent('veil'), veil_solid]+veils+kept[world:]+
                                           passes([('denoising_backward_motion', 'Denoising Backward Motion', {}), ('denoising_depth', 'Denoising Depth', {})]))
    # Deforming geometry in the motion pass (no motion blur): the right wall and the floor panel deform (three
    # motion steps, the centre in the middle; plain triangles with SD_OBJECT_HAS_VERTEX_MOTION), beside the moving
    # objects and the moving camera; the denoising motion features over the same scene.
    def deforming(nodes):
        out = copy.deepcopy(nodes)
        for node in out:
            if node.get('create') == 'mesh' and node['handle'] in ('right', 'panel'):
                pos = node['mesh']['positions']
                shift = (0.18, -0.06, 0.12) if node['handle'] == 'right' else (-0.1, 0.07, -0.15)
                pre = [round(v-shift[i % 3]+0.03*(i % 4), 4) for i, v in enumerate(pos)]
                post = [round(v+1.4*shift[i % 3]-0.02*(i % 5), 4) for i, v in enumerate(pos)]
                node['mesh']['motion'] = {'steps': 3, 'positions': pre+pos+post}
        return out
    motion_deform = copy.deepcopy(motion)
    motion_deform['id'] = 'passes/motion-deforming'
    motion_deform['description'] = ('motion and motion weight passes without motion blur with deforming geometry (vertex motion of plain '
                                    'triangles), moving objects and a moving camera')
    motion_deform['nodes'] = deforming(motion_deform['nodes'])
    denoise_motion_deform = copy.deepcopy(denoise_motion)
    denoise_motion_deform['id'] = 'passes/denoising-motion-deforming'
    denoise_motion_deform['description'] = ('denoising motion features (backward and specular motion) without motion blur with deforming '
                                            'geometry, moving objects and a moving camera')
    denoise_motion_deform['nodes'] = deforming(denoise_motion_deform['nodes'])
    # The sample count pass: the camera stage's atomic sample counter.
    count = scene('passes/sample-count', 'the sample count pass (film_write_sample in the camera stage) with the light and data passes',
                  room, room_objects, [('sample_count', 'Sample Count', {}), ('diffuse_direct', 'DiffDir', {}), ('depth', 'Depth', {}),
                                       ('normal', 'Normal', {}), ('mist', 'Mist', {})],
                  film={'use_sample_count': None, 'mist_start': 2.0, 'mist_depth': 9.0, 'mist_falloff': 2.0})
    # Holdouts and a transparent film: a holdout object (is_holdout), a holdout/diffuse mix, a semi-transparent veil,
    # the world transparent; mist with falloff 0.5 and a zero pass alpha threshold.
    holdout_materials = room+[semi_transparent('veil', {'pass_id': 11}),
                              shader('partial-holdout', [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.7, 0.4, 0.2], 'roughness': 0.1}},
                                                         {'handle': 'h', 'type': 'holdout'}, {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.35}}],
                                     [['d', 'BSDF', 'm', 'Closure1'], ['h', 'Holdout', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']], {'pass_id': 13})]
    holdout_objects = (quad('back', 'wall', BACK, {'pass_id': 1}, uv=True)+quad('floor', 'floor-material', FLOOR, {'pass_id': 2})+
                       quad('center', 'glass-material', CENTER, {'pass_id': 4})+quad('left', 'partial-holdout', LEFT, {'pass_id': 6})+
                       quad('right', 'wall', RIGHT, {'pass_id': 8, 'use_holdout': True})+quad('front', 'veil', FRONT, {'pass_id': 10})+
                       quad('panel', 'panel-emission', PANEL))
    holdout = scene('passes/holdout-transparent-film', 'holdouts (an object holdout and a holdout closure mix) and a transparent film '
                    'with the light, data, denoising feature passes; mist falloff 0.5 and a zero pass alpha threshold',
                    holdout_materials, holdout_objects, LIGHT_PASSES+[p for p in DATA_PASSES if p[0] != 'render_time']+
                    [('denoising_albedo', 'Denoising Albedo', {}), ('denoising_normal', 'Denoising Normal', {}), ('denoising_depth', 'Denoising Depth', {})],
                    film={'mist_start': 1.0, 'mist_depth': 12.0, 'mist_falloff': 0.5, 'pass_alpha_threshold': 0.0},
                    world={'transparent': True})
    return {'holdout-transparent-film.json': holdout, 'sample-count.json': count, 'denoising-motion.json': denoise_motion, 'motion.json': motion, 'motion-deforming.json': motion_deform,
            'denoising-motion-deforming.json': denoise_motion_deform, 'denoising-motion-transparent.json': denoise_motion_transparent, 'light-passes.json': light, 'data-passes.json': data, 'light-groups.json': groups, 'aovs.json': aov, 'cryptomatte.json': crypto,
            'denoising-features.json': denoise, 'denoising-features-weighted.json': denoise_weighted}


# Pass variants of committed scenes: the same scene with every light, data and denoising
# feature pass the device writes, so each feature family (closures, lights, light tree and
# linking, shadow catcher, MNEE, AO, displacement, textures, motion blur) is judged per pass.
VARIANT_PASSES = LIGHT_PASSES+[p for p in DATA_PASSES if p[0] != 'render_time']+[
    ('ao', 'AO', {}), ('denoising_albedo', 'Denoising Albedo', {}), ('denoising_normal', 'Denoising Normal', {}),
    ('denoising_specular_albedo', 'Denoising Specular Albedo', {}), ('denoising_roughness', 'Denoising Roughness', {}),
    ('denoising_depth', 'Denoising Depth', {})]
VARIANT_SOURCES = ['bsdf/metallic', 'bsdf/beckmann', 'bsdf/ashikhmin-shirley', 'bsdf/velvet', 'bsdf/toon', 'bsdf/glossy-filter',
                   'surface-nodes/bevel', 'surface-nodes/radial-tiling', 'surface-nodes/raycast',
                   'displacement/both', 'displacement/true-vector', 'mnee/glass-sphere-caustic', 'mnee/glass-sphere-object-motion',
                   'ao/ao-node', 'ao/fast-gi-additive', 'shadow-catcher/catcher-point-light', 'shadow-catcher/catcher-approximate',
                   'light-tree/lamps-and-mesh-lights', 'light-tree/light-linking', 'starter/textured-plane', 'starter/glass-and-area-light',
                   'combinations/tree-principled', 'combinations/tree-textured-emitter', 'combinations/enclosure-emissive-panel-mix',
                   'combinations/tree-transparent-lighting-3', 'combinations/link-transparent-lighting-4', 'combinations/glass-fast-gi-additive',
                   'linux-gates/broad-lighting-0', 'linux-gates/broad-lighting-5', 'linux-gates/triangle-lighting-1',
                   'linux-gates/glass-render', 'linux-gates/object-motion-0', 'linux-gates/deforming-motion-0',
                   'light-motion/linking-hdri', 'light-motion/motion-hdri-emitter', 'light-motion/clamps-filters-exclude']


def variant(source):
    family = source.split('/')[0]
    document = json.loads((HERE.parent/(source+'.json')).read_text())
    document['id'] = 'passes/with-'+source.replace('/', '-')
    document['description'] = 'pass variant of '+source+' (every light, data, AO and denoising feature pass): '+document['description']
    document['sourceFixture'] = 'native/oracle/scenes/author_passes.py (from scenes/'+source+'.json)'
    # Raw files stay beside the source scene.
    for image in document.get('images', []):
        image['file'] = '../'+family+'/'+image['file']
    handles = {n['handle'] for n in document['nodes']}
    for kind, name, extra in VARIANT_PASSES:
        if 'pass-'+name in handles or any(n.get('create') == 'pass' and n.get('set', {}).get('type') == kind for n in document['nodes']):
            continue
        document['nodes'].append({'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name, **extra}})
    return document


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    documents = scenes()
    documents.update({'with-'+source.replace('/', '-')+'.json': variant(source) for source in VARIANT_SOURCES})
    for name, document in documents.items():
        text = json.dumps(document, indent=1)+'\n'
        path = HERE/name
        if args.check:
            if path.read_text() != text:
                raise SystemExit('Scene differs: '+str(path))
        else:
            path.write_text(text)
        print(name, 'ok')


if __name__ == '__main__':
    main()
