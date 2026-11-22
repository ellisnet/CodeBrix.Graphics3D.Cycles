#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the BSDF family oracle scenes (scenes/bsdf/) byte for byte.

Smooth spheres with the Metallic, Beckmann (glossy, anisotropic, refraction,
glass, metallic), Ashikhmin-Shirley, Ashikhmin velvet, microfiber Sheen and Toon BSDFs, the
anisotropic Glass BSDF, the Glass BSDF with the multiscatter GGX distribution and with a thin film, and the Principled BSDF's anisotropic
transmission, its
dispersion (smooth, rough, with linked inputs and with a linked scale that is 0 at run time) and its coat, sheen, emission and thin-wall layers, on a diffuse floor and wall, lit by a sphere lamp with MIS (BSDF evaluation through
next-event estimation) and a constant world, through the public C API.
Usage: python3 native/oracle/scenes/author_bsdf.py [--check]."""
import argparse
import json
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent/'bsdf'


def f32(v):
    return round(struct.unpack('<f', struct.pack('<f', v))[0], 7)


def shader(handle, nodes, connect):
    return {'handle': handle, 'create': 'shader', 'graph': {'nodes': nodes, 'connect': connect}, 'set': {'emission_sampling_method': 'none'}}


def single(handle, node_type, values, output='BSDF'):
    return shader(handle, [{'handle': 'n', 'type': node_type, 'set': values}], [['n', output, 'output', 'Surface']])


def mixed(handle, first, second, fac):
    """Mix of two (type, values) nodes."""
    return shader(handle, [{'handle': 'a', 'type': first[0], 'set': first[1]}, {'handle': 'b', 'type': second[0], 'set': second[1]},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': fac}}],
                  [['a', 'BSDF', 'm', 'Closure1'], ['b', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def tangent_shader(handle, node_type, values):
    """An anisotropic node with a constant tangent (as the native fixture)."""
    return shader(handle, [{'handle': 'n', 'type': node_type, 'set': values}, {'handle': 't', 'type': 'color', 'set': {'value': [0.8, 0.3, 0.2]}}],
                  [['t', 'Color', 'n', 'Tangent'], ['n', 'BSDF', 'output', 'Surface']])


def zero_scale(handle, texture, texture_set, comparison, bound, values):
    """A Principled BSDF whose Transmission Dispersion Scale is a comparison of a texture's Fac that is 0 everywhere."""
    return shader(handle, [{'handle': 'n', 'type': 'principled_bsdf', 'set': values}, {'handle': 'g', 'type': 'geometry', 'set': {}},
                           {'handle': 't', 'type': texture, 'set': texture_set}, {'handle': 'z', 'type': 'math', 'set': {'math_type': comparison, 'value2': bound}}],
                  [['g', 'Position', 't', 'Vector'], ['t', 'Fac', 'z', 'Value1'], ['z', 'Value', 'n', 'Transmission Dispersion Scale'], ['n', 'BSDF', 'output', 'Surface']])


def sphere(handle, material, center, radius, stacks=10, slices=14):
    positions, indices = [], []
    for i in range(stacks+1):
        for j in range(slices+1):
            th, ph = math.pi*i/stacks, 2*math.pi*j/slices
            p = (math.sin(th)*math.cos(ph), math.cos(th), math.sin(th)*math.sin(ph))
            positions += [f32(center[k]+radius*p[k]) for k in range(3)]
    for i in range(stacks):
        for j in range(slices):
            a = i*(slices+1)+j
            b = a+slices+1
            indices += [a, b, a+1, a+1, b, b+1]
    faces = len(indices)//3
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices},
             'set': {'used_shaders': [material], 'smooth': [True]*faces}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


def quad(handle, material, corners):
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': [c for p in corners for c in p], 'indices': [0, 1, 2, 0, 2, 3]},
             'set': {'used_shaders': [material]}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


BACK = [(-4, -3, 3), (4, -3, 3), (4, 3, 3), (-4, 3, 3)]
FLOOR = [(-4, -2, -2), (4, -2, -2), (4, -2, 3), (-4, -2, 3)]


def scene(scene_id, description, materials, filter_glossy=0.0):
    """Three spheres (left to right) with materials[0..2]."""
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': {
                 'use_guiding': False, 'use_light_tree': False, 'use_adaptive_sampling': False, 'use_denoise': False,
                 'min_bounce': 2, 'max_bounce': 4, 'filter_glossy': filter_glossy, 'caustics_reflective': True, 'caustics_refractive': True}},
             {'handle': 'film', 'scene': 'film', 'set': {'use_sample_count': False}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}},
             single('wall', 'diffuse_bsdf', {'color': [0.6, 0.62, 0.65], 'roughness': 0.25}),
             single('floor-material', 'diffuse_bsdf', {'color': [0.5, 0.45, 0.35], 'roughness': 0.0}),
             *materials,
             *quad('back', 'wall', BACK), *quad('floor', 'floor-material', FLOOR)]
    for k, m in enumerate(materials):
        nodes += sphere('sphere-'+str(k), m['handle'], (-2.3+2.3*k, -0.9, 0.4), 1.0)
    nodes += [shader('world', [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}],
                     [['b', 'Background', 'output', 'Surface']]),
              {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world'}},
              shader('emitter', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
              {'handle': 'lamp', 'create': 'pointlight', 'set': {'radius': 0.45, 'is_sphere': True, 'use_mis': True, 'strength': [110, 90, 70], 'used_shaders': ['emitter']}},
              {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0, -2.0]}}]
    return {'cbcOracleScene': 1, 'id': scene_id, 'description': description, 'sourceFixture': 'native/cycles/bsdf_fixture.h (the same closure families)',
            'render': {'width': 32, 'height': 24, 'samples': 8, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


def metallic(fresnel, distribution, roughness, **extra):
    values = {'fresnel_type': fresnel, 'distribution': distribution, 'roughness': roughness, 'color': [0.9, 0.62, 0.31],
              'edge_tint': [0.95, 0.86, 0.7], 'ior': [0.18, 0.42, 1.37], 'k': [3.42, 2.35, 1.77]}
    values.update(extra)
    return values


def scenes():
    out = {}
    out['metallic.json'] = scene('bsdf/metallic', 'Metallic BSDF: F82 tint (GGX), F82 tint with thin film (multiscatter GGX), physical conductor anisotropic (multiscatter GGX)', [
        single('m-f82', 'metallic_bsdf', metallic('f82', 'ggx', 0.35)),
        single('m-film', 'metallic_bsdf', metallic('f82', 'multi_ggx', 0.3, thin_film_thickness=320.0, thin_film_ior=1.45)),
        tangent_shader('m-physical', 'metallic_bsdf', metallic('physical_conductor', 'multi_ggx', 0.45, anisotropy=0.6, rotation=0.2))])
    out['metallic-film.json'] = scene('bsdf/metallic-film', 'Metallic BSDF thin film over the physical conductor Fresnel: GGX, a sub-nanometre film (multiscatter GGX), and anisotropic with a high-IOR film (multiscatter GGX)', [
        single('c-film', 'metallic_bsdf', metallic('physical_conductor', 'ggx', 0.25, thin_film_thickness=320.0, thin_film_ior=1.45)),
        single('c-thin', 'metallic_bsdf', metallic('physical_conductor', 'multi_ggx', 0.3, thin_film_thickness=0.6, thin_film_ior=1.6)),
        tangent_shader('c-aniso', 'metallic_bsdf', metallic('physical_conductor', 'multi_ggx', 0.4, anisotropy=0.5, rotation=0.1, thin_film_thickness=900.0, thin_film_ior=2.0))])
    out['beckmann.json'] = scene('bsdf/beckmann', 'Beckmann distribution: anisotropic glossy, glass, and a physical-conductor Metallic BSDF mixed with rough refraction', [
        tangent_shader('b-glossy', 'glossy_bsdf', {'distribution': 'beckmann', 'roughness': 0.4, 'anisotropy': -0.5, 'rotation': 0.15, 'color': [0.3, 0.6, 0.85]}),
        single('b-glass', 'glass_bsdf', {'distribution': 'beckmann', 'roughness': 0.3, 'IOR': 1.5, 'color': [0.8, 0.9, 0.95]}),
        mixed('b-mix', ('metallic_bsdf', metallic('physical_conductor', 'beckmann', 0.35)),
              ('refraction_bsdf', {'distribution': 'beckmann', 'roughness': 0.25, 'IOR': 1.33, 'color': [0.9, 0.95, 1.0]}), 0.5)])
    out['ashikhmin-shirley.json'] = scene('bsdf/ashikhmin-shirley', 'Ashikhmin-Shirley glossy: isotropic, anisotropic with a tangent, and mixed with diffuse', [
        single('as-iso', 'glossy_bsdf', {'distribution': 'ashikhmin_shirley', 'roughness': 0.3, 'color': [0.85, 0.7, 0.4]}),
        tangent_shader('as-aniso', 'glossy_bsdf', {'distribution': 'ashikhmin_shirley', 'roughness': 0.45, 'anisotropy': 0.6, 'rotation': 0.1, 'color': [0.4, 0.7, 0.85]}),
        mixed('as-mix', ('glossy_bsdf', {'distribution': 'ashikhmin_shirley', 'roughness': 0.2, 'color': [0.9, 0.9, 0.9]}),
              ('diffuse_bsdf', {'color': [0.7, 0.4, 0.2], 'roughness': 0.43}), 0.35)])
    out['velvet.json'] = scene('bsdf/velvet', 'Sheen BSDF with the Ashikhmin velvet distribution: two sigmas and a mix with diffuse', [
        single('v-low', 'sheen_bsdf', {'distribution': 'ashikhmin', 'roughness': 0.3, 'color': [0.8, 0.3, 0.5]}),
        single('v-high', 'sheen_bsdf', {'distribution': 'ashikhmin', 'roughness': 1.0, 'color': [0.4, 0.7, 0.3]}),
        mixed('v-mix', ('sheen_bsdf', {'distribution': 'ashikhmin', 'roughness': 0.6, 'color': [0.6, 0.5, 0.9]}),
              ('diffuse_bsdf', {'color': [0.7, 0.4, 0.2], 'roughness': 0.43}), 0.5)])
    out['toon.json'] = scene('bsdf/toon', 'Toon BSDF: diffuse, glossy, and the two components mixed', [
        single('t-diffuse', 'toon_bsdf', {'component': 'diffuse', 'size': 0.5, 'smooth': 0.1, 'color': [0.9, 0.5, 0.2]}),
        single('t-glossy', 'toon_bsdf', {'component': 'glossy', 'size': 0.3, 'smooth': 0.2, 'color': [0.6, 0.8, 0.9]}),
        mixed('t-mix', ('toon_bsdf', {'component': 'diffuse', 'size': 0.8, 'smooth': 0.0, 'color': [0.5, 0.6, 0.7]}),
              ('toon_bsdf', {'component': 'glossy', 'size': 0.1, 'smooth': 0.05, 'color': [0.9, 0.9, 0.8]}), 0.3)])
    out['glossy-filter.json'] = scene('bsdf/glossy-filter', 'Glossy filtering (filter_glossy 1): sharp GGX glossy, sharp Beckmann glass and sharp Ashikhmin-Shirley over a diffuse room', [
        single('f-ggx', 'glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.05, 'color': [0.85, 0.85, 0.9]}),
        single('f-glass', 'glass_bsdf', {'distribution': 'beckmann', 'roughness': 0.05, 'IOR': 1.45, 'color': [0.9, 0.95, 0.95]}),
        mixed('f-mix', ('glossy_bsdf', {'distribution': 'ashikhmin_shirley', 'roughness': 0.1, 'color': [0.8, 0.7, 0.5]}),
              ('diffuse_bsdf', {'color': [0.4, 0.5, 0.6], 'roughness': 0.2}), 0.5)], filter_glossy=1.0)
    out['glass-anisotropic.json'] = scene('bsdf/glass-anisotropic', 'Anisotropic Glass BSDF with a tangent: GGX with a rotated tangent, Beckmann with a negative anisotropy, and a tinted GGX glass with a low roughness', [
        tangent_shader('g-ggx', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.35, 'anisotropy': 0.6, 'rotation': 0.2, 'IOR': 1.5, 'color': [0.9, 0.95, 1.0]}),
        tangent_shader('g-beckmann', 'glass_bsdf', {'distribution': 'beckmann', 'roughness': 0.4, 'anisotropy': -0.5, 'IOR': 1.45, 'color': [0.95, 0.9, 0.8]}),
        tangent_shader('g-tinted', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.2, 'anisotropy': 0.8, 'IOR': 1.33, 'color': [0.6, 0.85, 0.7]})])
    out['glass-multiscatter.json'] = scene('bsdf/glass-multiscatter', 'Glass BSDF with the multiscatter GGX distribution: roughness 0.3 (IOR 1.5), roughness 0.7 (IOR 1.33), and anisotropic with a tangent (anisotropy 0.5, rotation 0.15)', [
        single('gm-smooth', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.3, 'IOR': 1.5, 'color': [0.9, 0.95, 1.0]}),
        single('gm-rough', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.7, 'IOR': 1.33, 'color': [0.95, 0.85, 0.7]}),
        tangent_shader('gm-aniso', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.4, 'anisotropy': 0.5, 'rotation': 0.15, 'IOR': 1.45, 'color': [0.85, 0.95, 0.9]})])
    # The other branches of the multiscatter Glass BSDF's energy preservation (closure/bsdf_microfacet.h
    # bsdf_microfacet_setup_fresnel_generalized_schlick, microfacet_ggx_preserve_energy): a singular lobe, a white glass at
    # roughness 1 with a high IOR (the average Fresnel equals one: no darkening), and an IOR below 1 with a negative anisotropy.
    out['glass-multiscatter-extremes.json'] = scene('bsdf/glass-multiscatter-extremes', 'Glass BSDF with the multiscatter GGX distribution at its extremes: roughness 0 (IOR 1.5), '
                                                    'a white glass at roughness 1 (IOR 2.4), and an IOR below 1 (0.8) with a negative anisotropy (-0.7) and a tangent', [
        single('gx-sharp', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.0, 'IOR': 1.5, 'color': [0.9, 0.95, 1.0]}),
        single('gx-white', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 1.0, 'IOR': 2.4, 'color': [1.0, 1.0, 1.0]}),
        tangent_shader('gx-low', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.5, 'anisotropy': -0.7, 'IOR': 0.8, 'color': [0.8, 0.9, 0.7]})])
    # With refractive caustics off, a diffuse path's glass has a zero transmission tint: the average Fresnel is the reflection
    # tint times mix(f0, 1, s) of the closure's IOR (the reflective branch); a black glass has both tints zero.
    document = scene('bsdf/glass-multiscatter-caustics-off', 'Glass BSDF with the multiscatter GGX distribution with refractive caustics off (a diffuse path sees the reflection '
                     'only): roughness 0.3 (IOR 1.5), roughness 0.7 (IOR 1.33), and a black glass', [
        single('gc-smooth', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.3, 'IOR': 1.5, 'color': [0.9, 0.95, 1.0]}),
        single('gc-rough', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.7, 'IOR': 1.33, 'color': [0.95, 0.85, 0.7]}),
        single('gc-black', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.4, 'IOR': 1.45, 'color': [0.0, 0.0, 0.0]})])
    document['nodes'][0]['set']['caustics_refractive'] = False
    out['glass-multiscatter-caustics-off.json'] = document
    out['principled-anisotropic-transmission.json'] = scene('bsdf/principled-anisotropic-transmission', 'Principled BSDF with Anisotropic and Transmission Weight (the anisotropic transmission lobe): GGX, multiscatter GGX with a rotated tangent and a partial transmission, and a part-metallic tinted one', [
        tangent_shader('p-ggx', 'principled_bsdf', {'distribution': 'ggx', 'base_color': [0.9, 0.95, 1.0], 'roughness': 0.3, 'ior': 1.5, 'anisotropic': 0.7, 'transmission_weight': 1.0}),
        tangent_shader('p-multi', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.85, 0.6, 0.4], 'roughness': 0.4, 'ior': 1.45, 'anisotropic': 0.5,
                                                      'anisotropic_rotation': 0.25, 'transmission_weight': 0.6}),
        tangent_shader('p-metal', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.5, 0.8, 0.7], 'roughness': 0.25, 'ior': 1.33, 'anisotropic': 0.9,
                                                      'metallic': 0.3, 'specular_tint': [0.9, 0.7, 0.6], 'transmission_weight': 0.8})])
    out['principled-layers.json'] = scene('bsdf/principled-layers', 'Principled BSDF layering: a tinted coat of fractional weight, an emissive base under sheen and a tinted coat (the layers attenuate the emission, also at the last bounce), and a rough thin-wall transmission', [
        single('l-coat', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.8, 0.5, 0.25], 'roughness': 0.4, 'ior': 1.45, 'coat_weight': 0.6,
                                             'coat_roughness': 0.15, 'coat_ior': 1.6, 'coat_tint': [0.9, 0.4, 0.3]}),
        single('l-emission', 'principled_bsdf', {'distribution': 'ggx', 'base_color': [0.3, 0.5, 0.8], 'roughness': 0.5, 'ior': 1.5, 'coat_weight': 1.0, 'coat_roughness': 0.3,
                                                 'coat_ior': 1.5, 'coat_tint': [0.5, 0.8, 0.6], 'sheen_weight': 0.7, 'sheen_roughness': 0.4, 'sheen_tint': [0.9, 0.8, 0.6],
                                                 'emission_color': [1.0, 0.6, 0.3], 'emission_strength': 2.0}),
        single('l-thin', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.7, 0.9, 0.85], 'roughness': 0.45, 'ior': 1.5, 'transmission_weight': 0.9,
                                             'thin_wall': 1, 'specular_tint': [0.9, 0.85, 0.8]})])
    out['principled-dispersion.json'] = scene('bsdf/principled-dispersion', 'Principled BSDF with Transmission Dispersion Scale and Transmission Weight (dispersion): a smooth glass with a low Abbe number, a rough one with a half scale, and a partial transmission', [
        single('d-smooth', 'principled_bsdf', {'distribution': 'ggx', 'base_color': [0.95, 0.97, 1.0], 'roughness': 0.0, 'ior': 1.5, 'transmission_weight': 1.0,
                                               'transmission_dispersion_scale': 1.0, 'transmission_dispersion_abbe_number': 20.0}),
        single('d-rough', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.9, 0.9, 0.9], 'roughness': 0.2, 'ior': 1.6, 'transmission_weight': 1.0,
                                              'transmission_dispersion_scale': 0.5, 'transmission_dispersion_abbe_number': 30.0}),
        single('d-partial', 'principled_bsdf', {'distribution': 'ggx', 'base_color': [0.8, 0.7, 0.5], 'roughness': 0.1, 'ior': 1.45, 'transmission_weight': 0.6,
                                                'transmission_dispersion_scale': 0.8, 'transmission_dispersion_abbe_number': 45.0})])
    # The Glass BSDF node's thin film (svm/closure.h; closure/bsdf_microfacet.h generalized_schlick_fresnel's iridescence
    # branch): the three distributions; paths inside a sphere meet the film from behind (film IOR / IOR).
    out['glass-thin-film.json'] = scene('bsdf/glass-thin-film', 'Glass BSDF with a thin film: GGX (320 nm, film IOR 1.33), multiscatter GGX (600 nm, film IOR 1.8), '
                                        'and anisotropic Beckmann with a tangent (900 nm, film IOR 2.0)', [
        single('gf-ggx', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.25, 'IOR': 1.5, 'color': [0.9, 0.95, 1.0], 'thin_film_thickness': 320.0, 'thin_film_ior': 1.33}),
        single('gf-multi', 'glass_bsdf', {'distribution': 'multi_ggx', 'roughness': 0.4, 'IOR': 1.45, 'color': [0.95, 0.85, 0.7], 'thin_film_thickness': 600.0, 'thin_film_ior': 1.8}),
        tangent_shader('gf-beckmann', 'glass_bsdf', {'distribution': 'beckmann', 'roughness': 0.35, 'anisotropy': 0.5, 'IOR': 1.33, 'color': [0.85, 0.95, 0.9],
                                                     'thin_film_thickness': 900.0, 'thin_film_ior': 2.0})])
    # A linked Thin Film Thickness: a checker gives 0 (no film: below the cutoff of 0.1) or its scale in nanometres, so one
    # shader takes both branches; a film thinner than a nanometre (0.6: the blend toward no film); a sharp glass (roughness 0).
    out['glass-thin-film-linked.json'] = scene('bsdf/glass-thin-film-linked', 'Glass BSDF with a thin film: a linked thickness (a checker of 0 and 450 nm, film IOR 2.0, multiscatter GGX), '
                                               'a film of 0.6 nm (GGX), and a sharp glass (roughness 0, 500 nm, film IOR 2.2)', [
        shader('gl-checker', [{'handle': 'n', 'type': 'glass_bsdf', 'set': {'distribution': 'multi_ggx', 'roughness': 0.3, 'IOR': 1.5, 'color': [0.9, 0.95, 1.0], 'thin_film_ior': 2.0}},
                              {'handle': 'c', 'type': 'checker_texture', 'set': {'scale': 3.0, 'color1': [1.0, 1.0, 1.0], 'color2': [0.0, 0.0, 0.0]}},
                              {'handle': 'm', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 450.0}}],
               [['c', 'Fac', 'm', 'Value1'], ['m', 'Value', 'n', 'Thin Film Thickness'], ['n', 'BSDF', 'output', 'Surface']]),
        single('gl-thin', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.3, 'IOR': 1.45, 'color': [0.95, 0.9, 0.8], 'thin_film_thickness': 0.6, 'thin_film_ior': 1.6}),
        single('gl-sharp', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.0, 'IOR': 1.5, 'color': [0.9, 0.95, 1.0], 'thin_film_thickness': 500.0, 'thin_film_ior': 2.2})])
    # Dispersion on the rough transmission lobes: both GGX distributions over the roughness range, a high IOR.
    out['principled-dispersion-rough.json'] = scene('bsdf/principled-dispersion-rough', 'Principled BSDF dispersion on rough glass: multiscatter GGX at roughness 0.5 with a low Abbe number, '
                                                    'GGX at roughness 0.7 (IOR 1.33), and a tinted multiscatter GGX glass at roughness 1 with a high IOR (2.4)', [
        single('dr-multi', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.95, 0.97, 1.0], 'roughness': 0.5, 'ior': 1.5, 'transmission_weight': 1.0,
                                               'transmission_dispersion_scale': 1.0, 'transmission_dispersion_abbe_number': 15.0}),
        single('dr-ggx', 'principled_bsdf', {'distribution': 'ggx', 'base_color': [0.9, 0.9, 0.9], 'roughness': 0.7, 'ior': 1.33, 'transmission_weight': 1.0,
                                             'transmission_dispersion_scale': 1.0, 'transmission_dispersion_abbe_number': 10.0}),
        single('dr-high', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.85, 0.75, 0.6], 'roughness': 1.0, 'ior': 2.4, 'transmission_weight': 1.0,
                                              'specular_tint': [0.9, 0.8, 0.7], 'transmission_dispersion_scale': 0.7, 'transmission_dispersion_abbe_number': 25.0})])
    # A linked Transmission Dispersion Scale and Abbe Number: the scale from a Checker Texture's Fac (0 on half of the
    # surface at run time), the scale and the Abbe number from a Noise Texture, and a dispersive glass mixed with diffuse.
    out['principled-dispersion-linked.json'] = scene('bsdf/principled-dispersion-linked', 'Principled BSDF dispersion with linked inputs: the scale from a Checker Texture (0 on half of the surface), '
                                                     'the scale and the Abbe number from a Noise Texture, and a dispersive glass mixed with a diffuse BSDF', [
        shader('dl-checker', [{'handle': 'n', 'type': 'principled_bsdf', 'set': {'distribution': 'ggx', 'base_color': [0.95, 0.97, 1.0], 'roughness': 0.05, 'ior': 1.5,
                                                                                'transmission_weight': 1.0, 'transmission_dispersion_abbe_number': 12.0}},
                              {'handle': 'g', 'type': 'geometry', 'set': {}}, {'handle': 'c', 'type': 'checker_texture', 'set': {'scale': 2.5}}],
               [['g', 'Position', 'c', 'Vector'], ['c', 'Fac', 'n', 'Transmission Dispersion Scale'], ['n', 'BSDF', 'output', 'Surface']]),
        shader('dl-noise', [{'handle': 'n', 'type': 'principled_bsdf', 'set': {'distribution': 'multi_ggx', 'base_color': [0.9, 0.9, 0.9], 'roughness': 0.15, 'ior': 1.6,
                                                                              'transmission_weight': 1.0}},
                            {'handle': 'g', 'type': 'geometry', 'set': {}}, {'handle': 't', 'type': 'noise_texture', 'set': {'scale': 1.8, 'detail': 2.0}},
                            {'handle': 'a', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 40.0}}],
               [['g', 'Position', 't', 'Vector'], ['t', 'Fac', 'n', 'Transmission Dispersion Scale'], ['t', 'Fac', 'a', 'Value1'],
                ['a', 'Value', 'n', 'Transmission Dispersion Abbe Number'], ['n', 'BSDF', 'output', 'Surface']]),
        mixed('dl-mix', ('principled_bsdf', {'distribution': 'ggx', 'base_color': [0.8, 0.9, 0.85], 'roughness': 0.1, 'ior': 1.45, 'transmission_weight': 1.0,
                                             'transmission_dispersion_scale': 1.0, 'transmission_dispersion_abbe_number': 18.0}),
              ('diffuse_bsdf', {'color': [0.7, 0.4, 0.2], 'roughness': 0.43}), 0.35)])
    # A Transmission Dispersion Scale that is linked and 0 on every path at run time: the shaders need a wavelength (the
    # link is not folded), and no closure disperses. Fac < -1 and Fac > 2 are 0 for every Fac in [0, 1].
    out['principled-dispersion-zero-link.json'] = scene('bsdf/principled-dispersion-zero-link', 'Principled BSDF with a linked Transmission Dispersion Scale that is 0 at run time on every path '
                                                        '(a comparison of a texture value that is never true): the materials of bsdf/principled-dispersion, none of which disperses', [
        zero_scale('dz-smooth', 'checker_texture', {'scale': 2.5}, 'less_than', -1.0,
                   {'distribution': 'ggx', 'base_color': [0.95, 0.97, 1.0], 'roughness': 0.0, 'ior': 1.5, 'transmission_weight': 1.0, 'transmission_dispersion_abbe_number': 20.0}),
        zero_scale('dz-rough', 'noise_texture', {'scale': 1.8, 'detail': 2.0}, 'greater_than', 2.0,
                   {'distribution': 'multi_ggx', 'base_color': [0.9, 0.9, 0.9], 'roughness': 0.2, 'ior': 1.6, 'transmission_weight': 1.0, 'transmission_dispersion_abbe_number': 30.0}),
        zero_scale('dz-partial', 'checker_texture', {'scale': 4.0}, 'greater_than', 2.0,
                   {'distribution': 'ggx', 'base_color': [0.8, 0.7, 0.5], 'roughness': 0.1, 'ior': 1.45, 'transmission_weight': 0.6, 'transmission_dispersion_abbe_number': 45.0})])
    # The Sheen BSDF with the microfiber distribution: a constant roughness, a roughness from a checker (0.15 or 0.85 at run
    # time) with a noise-perturbed normal, and a mix with a GGX glossy BSDF.
    out['sheen-microfiber.json'] = scene('bsdf/sheen-microfiber', 'Sheen BSDF with the microfiber distribution: roughness 0.3, a linked roughness '
                                         '(a checker of 0.15 and 0.85) with a bumped normal, and a mix with a GGX glossy BSDF', [
        single('sh-const', 'sheen_bsdf', {'distribution': 'microfiber', 'roughness': 0.3, 'color': [0.8, 0.3, 0.5]}),
        shader('sh-linked', [{'handle': 'n', 'type': 'sheen_bsdf', 'set': {'distribution': 'microfiber', 'color': [0.4, 0.7, 0.3]}},
                             {'handle': 'c', 'type': 'checker_texture', 'set': {'scale': 3.0, 'color1': [0.85, 0.85, 0.85], 'color2': [0.15, 0.15, 0.15]}},
                             {'handle': 't', 'type': 'noise_texture', 'set': {'scale': 4.0, 'detail': 1.0}},
                             {'handle': 'b', 'type': 'bump', 'set': {'strength': 0.4, 'distance': 0.1}}],
               [['c', 'Fac', 'n', 'Roughness'], ['t', 'Fac', 'b', 'Height'], ['b', 'Normal', 'n', 'Normal'], ['n', 'BSDF', 'output', 'Surface']]),
        mixed('sh-mix', ('sheen_bsdf', {'distribution': 'microfiber', 'roughness': 0.6, 'color': [0.6, 0.5, 0.9]}),
              ('glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.85, 0.85, 0.9]}), 0.5)])
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    for name, document in scenes().items():
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
