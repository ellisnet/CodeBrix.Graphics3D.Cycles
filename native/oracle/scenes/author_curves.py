#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the curve oracle scenes (scenes/curves/) byte for byte.

Tufts of curves in the sampling family's room: ribbons, thick curves and thick linear curves, single-user and instanced,
with the closures the WebGPU device has, per curve and per key attributes, UVs, the Hair Info node's outputs, the
Geometry and Texture Coordinate nodes, bump, lights, the light tree, emission and data passes, through the public C API.
Usage: python3 native/oracle/scenes/author_curves.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_sampling as room

HERE = Path(__file__).resolve().parent/'curves'
f32 = room.f32
SHAPES = ('ribbon', 'thick', 'thick_linear')


def tuft(seed, curves=14, keys=5, center=(0, -2, 0.4), spread=1.5, height=3.2, radius=0.1):
    """Curving strands from a floor patch: a fixed formula, so the same bytes on every platform."""
    xyz, radii, first = [], [], []
    for c in range(curves):
        first.append(len(radii))
        phase = seed+c*2.399963
        x0 = center[0]+spread*math.cos(phase)*math.sqrt((c+0.5)/curves)
        z0 = center[2]+spread*math.sin(phase)*math.sqrt((c+0.5)/curves)
        for k in range(keys):
            t = k/(keys-1)
            bend = 0.55*t*t
            xyz += [f32(x0+bend*math.cos(phase*1.7)), f32(center[1]+height*t*(0.8+0.2*math.sin(phase))), f32(z0+bend*math.sin(phase*1.7))]
            radii.append(f32(radius*(1.0-0.75*t)))
    return xyz, radii, first


def hair(handle, material, shape, seed=0.3, attributes=None, **kwargs):
    xyz, radii, first = tuft(seed, **kwargs)
    block = {'keys': xyz, 'radius': radii, 'firstKey': first, 'shape': shape}
    if attributes:
        block['attributes'] = attributes(len(first), len(radii))
    return {'handle': handle, 'create': 'hair', 'hair': block, 'set': {'used_shaders': [material]}}


def place(handle, geometry, tfm=None):
    values = {'geometry': geometry}
    if tfm:
        values['tfm'] = tfm
    return {'handle': handle, 'create': 'object', 'set': values}


def base(scene_id, description, integrator=None, samples=8, width=64, height=48, curve_subdivisions=None):
    """The sampling room without its spheres."""
    document = room.scene(scene_id, description, integrator or {}, samples=samples, width=width, height=height)
    document['sourceFixture'] = 'native/oracle/scenes/author_curves.py (curves in the sampling family room)'
    document['nodes'] = [n for n in document['nodes'] if not n['handle'].startswith('sphere-')]
    if curve_subdivisions is not None:
        document['render']['curveSubdivisions'] = curve_subdivisions
    return document


def with_nodes(document, *entries):
    document['nodes'] = document['nodes']+list(entries)
    return document


def material(handle, nodes, connect):
    return room.shader(handle, nodes, connect)


def shape_scene(shape):
    name = shape.replace('_', '-')
    return base('curves/'+name+'-diffuse', 'A tuft of '+shape.replace('_', ' ')+' curves with a diffuse closure in the lit room'), [
        hair('tuft', 'matte', shape), place('tuft-object', 'tuft')]


def closure_scene(name, shape, closure, description, **kwargs):
    document = base('curves/'+name, description, **kwargs)
    return with_nodes(document, closure, hair('tuft', closure['handle'], shape), place('tuft-object', 'tuft'))


def attributes_scene(name, shape):
    def attributes(curves, keys):
        return [
            {'name': 'curve_color', 'value': 'color', 'domain': 'curve', 'data': [f32(v) for c in range(curves) for v in (0.2+0.7*c/curves, 0.8-0.6*c/curves, 0.3)]},
            {'name': 'key_color', 'value': 'color', 'domain': 'curve_key', 'data': [f32(v) for k in range(keys) for v in (0.2+0.15*(k % 5), 0.5, 0.9-0.15*(k % 5))]},
            {'name': 'key_value', 'value': 'float', 'domain': 'curve_key', 'data': [f32(0.2*(k % 5)) for k in range(keys)]},
            {'name': 'uv', 'value': 'point2', 'domain': 'curve', 'flags': ['standard'], 'data': [f32(v) for c in range(curves) for v in (c/curves, 1.0-c/curves)]},
        ]
    shader = material('attributes', [
        {'handle': 'per-curve', 'type': 'attribute', 'set': {'attribute': 'curve_color'}},
        {'handle': 'per-key', 'type': 'attribute', 'set': {'attribute': 'key_color'}},
        {'handle': 'value', 'type': 'attribute', 'set': {'attribute': 'key_value'}},
        {'handle': 'uvmap', 'type': 'uvmap', 'set': {'attribute': 'uv'}},
        {'handle': 'checker', 'type': 'checker_texture', 'set': {'scale': 7.0, 'color1': [0.9, 0.8, 0.2], 'color2': [0.1, 0.2, 0.6]}},
        {'handle': 'mix-a', 'type': 'mix', 'set': {'mix_type': 'mix', 'fac': 0.5}},
        {'handle': 'mix-b', 'type': 'mix', 'set': {'mix_type': 'multiply'}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
    ], [['uvmap', 'UV', 'checker', 'Vector'], ['per-curve', 'Color', 'mix-a', 'Color1'], ['per-key', 'Color', 'mix-a', 'Color2'], ['mix-a', 'Color', 'mix-b', 'Color1'],
        ['checker', 'Color', 'mix-b', 'Color2'], ['value', 'Fac', 'mix-b', 'Fac'], ['mix-b', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']])
    document = base('curves/'+name, 'Per curve and per key colours, a per key value and a UV checker on '+shape.replace('_', ' ')+' curves')
    return with_nodes(document, shader, hair('tuft', 'attributes', shape, attributes=attributes), place('tuft-object', 'tuft'))


def hair_info_scene(name, shape, tfm=None, second=False):
    def attributes(curves, keys):
        strand = keys//curves
        return [
            {'name': 'curve_intercept', 'value': 'float', 'domain': 'curve_key', 'flags': ['standard'], 'data': [f32((k % strand)/(strand-1)) for k in range(keys)]},
            {'name': 'curve_length', 'value': 'float', 'domain': 'curve', 'flags': ['standard'], 'data': [f32(2.0+0.2*c) for c in range(curves)]},
            {'name': 'curve_random', 'value': 'float', 'domain': 'curve', 'flags': ['standard'], 'data': [f32((c*0.618034) % 1.0) for c in range(curves)]},
        ]
    shader = material('info', [
        {'handle': 'hi', 'type': 'hair_info'},
        {'handle': 'rgb', 'type': 'combine_color', 'set': {'color_type': 'rgb'}},
        {'handle': 'grey', 'type': 'combine_color', 'set': {'color_type': 'rgb'}},
        {'handle': 'thick', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 8.0}},
        {'handle': 'length', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.25}},
        {'handle': 'tn', 'type': 'vector_math', 'set': {'math_type': 'absolute'}},
        {'handle': 'add', 'type': 'mix', 'set': {'mix_type': 'add', 'fac': 0.4}},
        {'handle': 'strand', 'type': 'mix', 'set': {'mix_type': 'multiply'}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
    ], [['hi', 'Intercept', 'rgb', 'Red'], ['hi', 'Random', 'rgb', 'Blue'], ['hi', 'Thickness', 'thick', 'Value1'], ['thick', 'Value', 'rgb', 'Green'],
        ['rgb', 'Color', 'add', 'Color1'], ['hi', 'Tangent Normal', 'tn', 'Vector1'], ['tn', 'Vector', 'add', 'Color2'],
        ['hi', 'Is Strand', 'strand', 'Fac'], ['add', 'Color', 'strand', 'Color1'], ['hi', 'Length', 'length', 'Value1'],
        ['length', 'Value', 'grey', 'Red'], ['length', 'Value', 'grey', 'Green'], ['length', 'Value', 'grey', 'Blue'], ['grey', 'Color', 'strand', 'Color2'],
        ['strand', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']])
    document = base('curves/'+name, 'The Hair Info node on '+shape.replace('_', ' ')+' curves'+(' on two instancing objects' if second else '')+': intercept, length, random, thickness, tangent normal and is strand')
    entries = [shader, hair('tuft', 'info', shape, attributes=attributes), place('tuft-object', 'tuft', tfm)]
    if second:
        entries.append(place('tuft-object-2', 'tuft', [1.0, 0.0, 0.0, 2.0, 0.0, 0.7, 0.0, 0.0, 0.0, 0.0, 1.3, 0.0]))
    return with_nodes(document, *entries)


def geometry_scene(name, shape):
    shader = material('geo', [
        {'handle': 'g', 'type': 'geometry'},
        {'handle': 'tc', 'type': 'texture_coordinate'},
        {'handle': 'lp', 'type': 'light_path'},
        {'handle': 'abs', 'type': 'vector_math', 'set': {'math_type': 'absolute'}},
        {'handle': 'abs2', 'type': 'vector_math', 'set': {'math_type': 'absolute'}},
        {'handle': 'fract', 'type': 'vector_math', 'set': {'math_type': 'fraction'}},
        {'handle': 'par', 'type': 'vector_math', 'set': {'math_type': 'add'}},
        {'handle': 'sum', 'type': 'vector_math', 'set': {'math_type': 'add'}},
        {'handle': 'sum2', 'type': 'vector_math', 'set': {'math_type': 'add'}},
        {'handle': 'scale', 'type': 'vector_math', 'set': {'math_type': 'scale', 'scale': 0.4}},
        {'handle': 'n', 'type': 'noise_texture', 'set': {'scale': 3.0, 'detail': 1.0, 'dimensions': '3D'}},
        {'handle': 'bump', 'type': 'bump', 'set': {'strength': 0.6, 'distance': 0.4}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
    ], [['g', 'Tangent', 'abs', 'Vector1'], ['g', 'Incoming', 'abs2', 'Vector1'], ['abs', 'Vector', 'sum', 'Vector1'], ['abs2', 'Vector', 'sum', 'Vector2'],
        ['tc', 'Normal', 'par', 'Vector1'], ['tc', 'Object', 'fract', 'Vector1'], ['fract', 'Vector', 'par', 'Vector2'], ['par', 'Vector', 'sum2', 'Vector1'], ['sum', 'Vector', 'sum2', 'Vector2'],
        ['sum2', 'Vector', 'scale', 'Vector1'], ['scale', 'Vector', 'd', 'Color'], ['g', 'Position', 'n', 'Vector'], ['n', 'Fac', 'bump', 'Height'], ['bump', 'Normal', 'd', 'Normal'], ['d', 'BSDF', 'output', 'Surface']])
    document = base('curves/'+name, 'The Geometry and Texture Coordinate nodes and a bumped diffuse on '+shape.replace('_', ' ')+' curves')
    return with_nodes(document, shader, hair('tuft', 'geo', shape), place('tuft-object', 'tuft', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.2, 0.0]))


def instanced_scene():
    document = base('curves/instanced-mixed', 'All three shapes: one single-user tuft (baked into world space) and instancing objects with rotated, non-uniform and mirrored transforms')
    rotate = 0.5
    c, s = f32(math.cos(rotate)), f32(math.sin(rotate))
    return with_nodes(document,
        hair('ribbons', 'matte', 'ribbon', seed=0.4, curves=6, center=(-2.4, -2, 0.4), spread=0.8),
        place('ribbons-a', 'ribbons'),
        hair('thick', 'glossy', 'thick', seed=1.1, curves=6, center=(0, -2, 0.4), spread=0.8),
        place('thick-a', 'thick'),
        place('thick-b', 'thick', [c, 0.0, s, 1.2, 0.0, 0.8, 0.0, 0.0, -s, 0.0, c, -1.0]),
        hair('linear', 'matte', 'thick_linear', seed=2.3, curves=6, center=(2.4, -2, 0.4), spread=0.8),
        place('linear-a', 'linear'),
        place('linear-b', 'linear', [-1.0, 0.0, 0.0, 4.0, 0.0, 1.1, 0.0, 0.2, 0.0, 0.0, 1.0, -1.0]))


def lights_scene(name, description, integrator, extra, samples=8):
    document = base('curves/'+name, description, integrator, samples=samples)
    return with_nodes(document, hair('tuft', 'matte', 'thick'), place('tuft-object', 'tuft'), hair('ribbons', 'glossy', 'ribbon', seed=2.0, center=(-2.2, -2, -0.4), spread=0.7), place('ribbons-object', 'ribbons'), *extra)


def scenes():
    out = {}
    for shape in SHAPES:
        document, entries = shape_scene(shape)
        out[shape.replace('_', '-')+'-diffuse.json'] = with_nodes(document, *entries)
    out['ribbon-subdivisions-1.json'] = with_nodes(base('curves/ribbon-subdivisions-1', 'Ribbons with two sections per segment (curve subdivisions 1), against the default eight', curve_subdivisions=1), hair('tuft', 'matte', 'ribbon'), place('tuft-object', 'tuft'))
    out['instanced-mixed.json'] = instanced_scene()
    glossy = room.single('hair-glossy', 'glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.8, 0.6, 0.4]})
    glass = room.single('hair-glass', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.15, 'IOR': 1.45, 'color': [0.9, 0.95, 0.95]})
    translucent = room.single('hair-translucent', 'translucent_bsdf', {'color': [0.5, 0.8, 0.5]})
    principled = room.single('hair-principled', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.7, 0.45, 0.3], 'roughness': 0.4, 'ior': 1.45, 'metallic': 0.2})
    oren = room.single('hair-oren', 'diffuse_bsdf', {'color': [0.7, 0.5, 0.6], 'roughness': 0.6})
    out['closure-glossy-ribbon.json'] = closure_scene('closure-glossy-ribbon', 'ribbon', glossy, 'A GGX glossy closure on ribbons (the smooth normal stands in for the geometric one when sampling)')
    out['closure-glossy-thick.json'] = closure_scene('closure-glossy-thick', 'thick', glossy, 'A GGX glossy closure on thick curves')
    out['closure-glass-thick.json'] = closure_scene('closure-glass-thick', 'thick', glass, 'A rough glass closure on thick curves: rays through the curve')
    out['closure-translucent-ribbon.json'] = closure_scene('closure-translucent-ribbon', 'ribbon', translucent, 'A translucent closure on ribbons')
    out['closure-principled-thick.json'] = closure_scene('closure-principled-thick', 'thick', principled, 'A Principled BSDF on thick curves')
    out['closure-oren-linear.json'] = closure_scene('closure-oren-linear', 'thick_linear', oren, 'An Oren-Nayar diffuse closure on thick linear curves')
    out['attributes-ribbon.json'] = attributes_scene('attributes-ribbon', 'ribbon')
    out['attributes-thick.json'] = attributes_scene('attributes-thick', 'thick')
    out['attributes-linear.json'] = attributes_scene('attributes-linear', 'thick_linear')
    out['hair-info-ribbon.json'] = hair_info_scene('hair-info-ribbon', 'ribbon')
    out['hair-info-thick.json'] = hair_info_scene('hair-info-thick', 'thick', second=True)
    out['hair-info-linear.json'] = hair_info_scene('hair-info-linear', 'thick_linear')
    out['geometry-ribbon.json'] = geometry_scene('geometry-ribbon', 'ribbon')
    out['geometry-thick.json'] = geometry_scene('geometry-thick', 'thick')
    emitter = room.shader('hair-emission', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.6, 0.3], 'strength': 4.0}}], [['e', 'Emission', 'output', 'Surface']])
    emitter['set'] = {'emission_sampling_method': 'auto'}
    out['emission-thick.json'] = closure_scene('emission-thick', 'thick', emitter, 'Emissive thick curves with MIS flags on the shader: a curve hit takes no forward MIS (the kernel tests for a triangle)')
    area = room.single('area-emission', 'emission', {'color': [1, 1, 1], 'strength': 30}, output='Emission')
    out['lights-area-sun.json'] = lights_scene('lights-area-sun', 'Thick curves and ribbons under an area light and a sun, next-event estimation from curve shading points', {},
        [area, {'handle': 'area', 'create': 'arealight', 'set': {'sizeu': 1.5, 'sizev': 1.5, 'use_mis': True, 'strength': [20, 20, 20], 'used_shaders': ['area-emission']}},
         {'handle': 'area-object', 'create': 'object', 'set': {'geometry': 'area', 'tfm': [1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 1.0, 2.5, 0.0, -1.0, 0.0, 0.0]}},
         {'handle': 'sun', 'create': 'sunlight', 'set': {'angle': 0.1, 'strength': [1.5, 1.4, 1.2], 'used_shaders': ['area-emission']}},
         {'handle': 'sun-object', 'create': 'object', 'set': {'geometry': 'sun', 'tfm': [1.0, 0.0, 0.0, 0.0, 0.0, 0.7, -0.7, 0.0, 0.0, 0.7, 0.7, 0.0]}}])
    out['light-tree.json'] = lights_scene('light-tree', 'Thick curves and ribbons with the light tree', {'use_light_tree': True}, [])
    passes = [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name}} for kind, name in
              (('depth', 'Depth'), ('normal', 'Normal'), ('position', 'Position'), ('uv', 'UV'), ('object_id', 'Object ID'), ('diffuse_color', 'Diffuse Color'), ('glossy_color', 'Glossy Color'))]
    def uv_attribute(curves, keys):
        return [{'name': 'uv', 'value': 'point2', 'domain': 'curve', 'flags': ['standard'], 'data': [f32(v) for c in range(curves) for v in (c/curves, 1.0-c/curves)]}]
    out['data-passes.json'] = with_nodes(base('curves/data-passes', 'Depth, normal, position, UV, object index and colour passes on thick curves and ribbons'),
        hair('tuft', 'matte', 'thick', attributes=uv_attribute), place('tuft-object', 'tuft'), hair('ribbons', 'glossy', 'ribbon', seed=2.0, center=(-2.2, -2, -0.4), spread=0.7, attributes=uv_attribute), place('ribbons-object', 'ribbons'), *passes)
    catcher = with_nodes(base('curves/shadow-catcher', 'Thick curves over a shadow catcher plane: the path split and the curve shadows in the catcher passes'),
        hair('tuft', 'matte', 'thick'), place('tuft-object', 'tuft'),
        {'handle': 'pass-Shadow Catcher', 'create': 'pass', 'set': {'type': 'shadow_catcher', 'name': 'Shadow Catcher'}})
    for entry in catcher['nodes']:
        if entry['handle'] == 'floor-object':
            entry['set']['is_shadow_catcher'] = True
    out['shadow-catcher.json'] = catcher
    out['fast-gi.json'] = lights_scene('fast-gi', 'Fast GI (additive ambient occlusion) with thick curves and ribbons: AO shadow rays through the curve traversal', {'ao_bounces': 1, 'ao_factor': 0.5, 'ao_distance': 2.0}, [])
    camera_motion = lights_scene('camera-motion', 'Static curves with camera motion blur', {'motion_blur': True}, [])
    for entry in camera_motion['nodes']:
        if entry['handle'] == 'camera':
            entry['set'].update({'shuttertime': 0.5, 'motion': [1.0, 0.0, 0.0, -0.1, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0, 1.0, 0.0, 0.0, 0.1, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]})
    out['camera-motion.json'] = camera_motion
    tangent = material('anisotropic', [
        {'handle': 't', 'type': 'tangent', 'set': {'direction_type': 'radial', 'axis': 'y'}},
        {'handle': 'nm', 'type': 'normal_map', 'set': {'space': 'object', 'strength': 0.7, 'color': [0.6, 0.5, 0.8]}},
        {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.35, 'anisotropy': 0.5, 'color': [0.8, 0.7, 0.5]}},
    ], [['t', 'Tangent', 'g', 'Tangent'], ['nm', 'Normal', 'g', 'Normal'], ['g', 'BSDF', 'output', 'Surface']])
    out['anisotropic-tangent-thick.json'] = closure_scene('anisotropic-tangent-thick', 'thick', tangent, 'Anisotropic glossy with a radial Tangent node and an object space Normal Map on thick curves')
    denoising = [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name}} for kind, name in
                 (('denoising_normal', 'Denoising Normal'), ('denoising_albedo', 'Denoising Albedo'), ('denoising_depth', 'Denoising Depth'))]
    out['denoising-features.json'] = with_nodes(base('curves/denoising-features', 'Denoising feature passes on thick curves and ribbons'),
        hair('tuft', 'matte', 'thick'), place('tuft-object', 'tuft'), hair('ribbons', 'glossy', 'ribbon', seed=2.0, center=(-2.2, -2, -0.4), spread=0.7), place('ribbons-object', 'ribbons'), *denoising)
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
