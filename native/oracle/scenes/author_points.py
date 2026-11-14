#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the point cloud oracle scenes (scenes/points/) byte for byte.

Clouds of points in the sampling family's room: single-user and instanced, with the closures the WebGPU device has, point
attributes, the Point Info and Particle Info nodes, the Geometry and Texture Coordinate nodes, bump, lights, the light
tree, emission, data passes, transparency, object and deformation motion, the motion pass, the Geometry node's Tangent output
(as a colour and as a Bump height), and points beside the features
that have their own traversals (Ambient Occlusion, Bevel and Raycast nodes, subsurface scattering, caustics, volumes,
displacement, shadow linking, curves), through the public C API.
Usage: python3 native/oracle/scenes/author_points.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_curves as curves
import author_sampling as room
from author_geometry_tangent import tangent_colour, tangent_bump

HERE = Path(__file__).resolve().parent/'points'
SCENES = Path(__file__).resolve().parent
f32 = room.f32


def spread_points(seed, count, center, spread, radius):
    """A ball of points from a fixed formula, so the same bytes on every platform."""
    xyz, radii = [], []
    for i in range(count):
        t = (i+0.5)/count
        phase = seed+i*2.399963
        height = 1.0-2.0*t
        ring = math.sqrt(max(0.0, 1.0-height*height))
        scale = spread*(0.45+0.55*((i*0.381966) % 1.0))
        xyz += [f32(center[0]+scale*ring*math.cos(phase)), f32(center[1]+scale*height), f32(center[2]+scale*ring*math.sin(phase))]
        radii.append(f32(radius*(0.45+0.55*((i*0.618034) % 1.0))))
    return xyz, radii


def cloud(handle, material, seed=0.3, count=70, center=(0.0, -0.7, 0.4), spread=1.7, radius=0.3, attributes=None, steps=0, extra=None):
    xyz, radii = spread_points(seed, count, center, spread, radius)
    block = {'positions': xyz, 'radius': radii}
    if attributes:
        block['attributes'] = attributes(count)
    if steps:
        # Time-ordered steps; the center step is the points, the radii grow and shrink with the point index.
        positions, sizes = [], []
        for step in range(steps):
            u = 2.0*step/(steps-1)-1.0
            for i in range(count):
                p = xyz[3*i:3*i+3]
                if u == 0.0:
                    positions += p
                    sizes.append(radii[i])
                else:
                    positions += [f32(p[0]+0.18*u*p[1]), f32(p[1]+0.12*u), f32(p[2]+0.1*u*p[0]+0.04*u*u)]
                    sizes.append(f32(radii[i]*(1.0+0.25*u*(1.0 if i % 2 else -1.0))))
        block['motion'] = {'steps': steps, 'positions': positions, 'radius': sizes}
    values = {'used_shaders': material if isinstance(material, list) else [material]}
    if extra:
        values.update(extra)
    return {'handle': handle, 'create': 'pointcloud', 'points': block, 'set': values}


def place(handle, geometry, tfm=None, **values):
    settings = {'geometry': geometry}
    if tfm:
        settings['tfm'] = tfm
    settings.update(values)
    return {'handle': handle, 'create': 'object', 'set': settings}


def base(scene_id, description, integrator=None, samples=8, width=64, height=48):
    """The sampling room without its spheres."""
    document = room.scene(scene_id, description, integrator or {}, samples=samples, width=width, height=height)
    document['sourceFixture'] = 'native/oracle/scenes/author_points.py (point clouds in the sampling family room)'
    document['nodes'] = [n for n in document['nodes'] if not n['handle'].startswith('sphere-')]
    return document


def with_nodes(document, *entries):
    document['nodes'] = document['nodes']+list(entries)
    return document


def beside(path, scene_id, description, *entries):
    """Points added to a committed scene of another family, for the features with their own traversals."""
    document = json.loads((SCENES/(path+'.json')).read_text())
    document['id'] = scene_id
    document['description'] = description
    document['sourceFixture'] = 'native/oracle/scenes/author_points.py (points added to '+path+')'
    document['nodes'] = document['nodes']+list(entries)
    return document


def info_material():
    return room.shader('info', [
        {'handle': 'pi', 'type': 'point_info'},
        {'handle': 'sep', 'type': 'separate_xyz'},
        {'handle': 'wrap', 'type': 'math', 'set': {'math_type': 'absolute'}},
        {'handle': 'size', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 3.0}},
        {'handle': 'rgb', 'type': 'combine_color', 'set': {'color_type': 'rgb'}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
    ], [['pi', 'Position', 'sep', 'Vector'], ['sep', 'X', 'wrap', 'Value1'], ['wrap', 'Value', 'rgb', 'Red'], ['pi', 'Radius', 'size', 'Value1'], ['size', 'Value', 'rgb', 'Green'],
        ['pi', 'Random', 'rgb', 'Blue'], ['rgb', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']])


def point_random(count):
    return [{'name': 'point_random', 'value': 'float', 'domain': 'vertex', 'flags': ['standard'], 'data': [f32((i*0.618034) % 1.0) for i in range(count)]}]


def particle_material():
    def add(handle, a, b):
        return [{'handle': handle, 'type': 'math', 'set': {'math_type': 'add'}}], [[a[0], a[1], handle, 'Value1'], [b[0], b[1], handle, 'Value2']]
    nodes = [{'handle': 'pa', 'type': 'particle_info'}, {'handle': 'rgb', 'type': 'combine_color', 'set': {'color_type': 'rgb'}}, {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
             {'handle': 'loc', 'type': 'separate_xyz'}, {'handle': 'vel', 'type': 'separate_xyz'}, {'handle': 'ang', 'type': 'separate_xyz'},
             {'handle': 'index', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.02}}]
    connect = [['pa', 'Location', 'loc', 'Vector'], ['pa', 'Velocity', 'vel', 'Vector'], ['pa', 'Angular Velocity', 'ang', 'Vector'], ['pa', 'Index', 'index', 'Value1']]
    for handle, a, b in (('r1', ('pa', 'Random'), ('index', 'Value')), ('r2', ('r1', 'Value'), ('pa', 'Age')), ('g1', ('pa', 'Lifetime'), ('pa', 'Size')),
                         ('b1', ('loc', 'X'), ('vel', 'Y')), ('b2', ('b1', 'Value'), ('ang', 'Z'))):
        n, c = add(handle, a, b)
        nodes += n
        connect += c
    connect += [['r2', 'Value', 'rgb', 'Red'], ['g1', 'Value', 'rgb', 'Green'], ['b2', 'Value', 'rgb', 'Blue'], ['rgb', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']]
    return room.shader('particle', nodes, connect)


def particles(handle, first):
    return {'handle': handle, 'create': 'particle_system', 'particles': [
        {'index': first+i*7, 'age': f32(0.1+0.1*i), 'lifetime': f32(0.3+0.05*i), 'location': [f32(0.2*i), 0.4, f32(0.5-0.1*i)], 'rotation': [0.0, 0.0, 0.0, 1.0],
         'size': f32(0.2+0.03*i), 'velocity': [0.1, f32(0.1*i), 0.0], 'angularVelocity': [0.0, 0.1, f32(0.2+0.1*i)]} for i in range(4)]}


def geometry_material():
    return room.shader('geo', [
        {'handle': 'g', 'type': 'geometry'},
        {'handle': 'tc', 'type': 'texture_coordinate'},
        {'handle': 'abs', 'type': 'vector_math', 'set': {'math_type': 'absolute'}},
        {'handle': 'fract', 'type': 'vector_math', 'set': {'math_type': 'fraction'}},
        {'handle': 'sum', 'type': 'vector_math', 'set': {'math_type': 'add'}},
        {'handle': 'sum2', 'type': 'vector_math', 'set': {'math_type': 'add'}},
        {'handle': 'scale', 'type': 'vector_math', 'set': {'math_type': 'scale', 'scale': 0.4}},
        {'handle': 'n', 'type': 'noise_texture', 'set': {'scale': 3.0, 'detail': 1.0, 'dimensions': '3D'}},
        {'handle': 'bump', 'type': 'bump', 'set': {'strength': 0.6, 'distance': 0.4}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
    ], [['g', 'Incoming', 'abs', 'Vector1'], ['tc', 'Normal', 'sum', 'Vector1'], ['tc', 'Object', 'fract', 'Vector1'], ['fract', 'Vector', 'sum', 'Vector2'], ['sum', 'Vector', 'sum2', 'Vector1'], ['abs', 'Vector', 'sum2', 'Vector2'],
        ['sum2', 'Vector', 'scale', 'Vector1'], ['scale', 'Vector', 'd', 'Color'], ['g', 'Position', 'n', 'Vector'], ['n', 'Fac', 'bump', 'Height'], ['bump', 'Normal', 'd', 'Normal'], ['d', 'BSDF', 'output', 'Surface']])


def attributes_material():
    return room.shader('attributes', [
        {'handle': 'per-point', 'type': 'attribute', 'set': {'attribute': 'point_color'}},
        {'handle': 'value', 'type': 'attribute', 'set': {'attribute': 'point_value'}},
        {'handle': 'uvmap', 'type': 'uvmap', 'set': {'attribute': 'uv'}},
        {'handle': 'checker', 'type': 'checker_texture', 'set': {'scale': 7.0, 'color1': [0.9, 0.8, 0.2], 'color2': [0.1, 0.2, 0.6]}},
        {'handle': 'mix', 'type': 'mix', 'set': {'mix_type': 'multiply'}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
    ], [['uvmap', 'UV', 'checker', 'Vector'], ['per-point', 'Color', 'mix', 'Color1'], ['checker', 'Color', 'mix', 'Color2'], ['value', 'Fac', 'mix', 'Fac'], ['mix', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']])


def point_attributes(count):
    return [{'name': 'point_color', 'value': 'color', 'domain': 'vertex', 'data': [f32(v) for i in range(count) for v in (0.2+0.7*i/count, 0.8-0.6*i/count, 0.3)]},
            {'name': 'point_value', 'value': 'float', 'domain': 'vertex', 'data': [f32(0.2*(i % 5)) for i in range(count)]},
            {'name': 'uv', 'value': 'point2', 'domain': 'vertex', 'flags': ['standard'], 'data': [f32(v) for i in range(count) for v in (i/count, 1.0-i/count)]}]


def closure_scene(name, closure, description, **kwargs):
    return with_nodes(base('points/'+name, description, **kwargs), closure, cloud('points', closure['handle']), place('points-object', 'points'))


def lights_scene(name, description, integrator, extra, samples=8):
    return with_nodes(base('points/'+name, description, integrator, samples=samples), cloud('points', 'matte'), place('points-object', 'points'),
                      cloud('more', 'glossy', seed=2.0, count=30, center=(-2.0, -1.2, -0.5), spread=0.8, radius=0.25), place('more-object', 'more'), *extra)


def camera_blur(document):
    for entry in document['nodes']:
        if entry['handle'] == 'camera':
            entry['set']['shuttertime'] = 0.5
    return document


TILT = [0.9, 0.0, 0.4, 0.0, 0.0, 1.0, 0.0, 0.0, -0.4, 0.0, 0.9, 0.0]


def scenes():
    out = {}
    out['diffuse.json'] = with_nodes(base('points/diffuse', 'A single-user cloud with a diffuse closure in the lit room: the points baked into world space'), cloud('points', 'matte'), place('points-object', 'points'))
    c, s = f32(math.cos(0.5)), f32(math.sin(0.5))
    out['instanced.json'] = with_nodes(base('points/instanced', 'A cloud on three objects: rotated, non-uniformly scaled and mirrored instances, the radius through the instance transform'),
        cloud('points', 'matte', count=40, center=(0.0, -1.0, 0.4), spread=1.0, radius=0.28), place('a', 'points', [1.0, 0.0, 0.0, -2.2, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]),
        place('b', 'points', [c, 0.0, s, 0.0, 0.0, 0.8, 0.0, 0.0, -s, 0.0, c, 0.0]), place('c', 'points', [-1.0, 0.0, 0.0, 2.2, 0.0, 1.2, 0.0, 0.2, 0.0, 0.0, 1.0, -0.5]))
    two = cloud('points', ['matte', 'glossy'], extra={'shader': [i % 2 for i in range(70)]})
    out['two-materials.json'] = with_nodes(base('points/two-materials', 'Two materials on one cloud through the shader socket'), two, place('points-object', 'points'))
    glossy = room.single('point-glossy', 'glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.8, 0.6, 0.4]})
    glass = room.single('point-glass', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.15, 'IOR': 1.45, 'color': [0.9, 0.95, 0.95]})
    translucent = room.single('point-translucent', 'translucent_bsdf', {'color': [0.5, 0.8, 0.5]})
    principled = room.single('point-principled', 'principled_bsdf', {'distribution': 'multi_ggx', 'base_color': [0.7, 0.45, 0.3], 'roughness': 0.4, 'ior': 1.45, 'metallic': 0.2})
    oren = room.single('point-oren', 'diffuse_bsdf', {'color': [0.7, 0.5, 0.6], 'roughness': 0.6})
    out['closure-glossy.json'] = closure_scene('closure-glossy', glossy, 'A GGX glossy closure on points')
    out['closure-glass.json'] = closure_scene('closure-glass', glass, 'A rough glass closure on points: rays leave a point, back-face culled, without hitting it again')
    out['closure-translucent.json'] = closure_scene('closure-translucent', translucent, 'A translucent closure on points')
    out['closure-principled.json'] = closure_scene('closure-principled', principled, 'A Principled BSDF on points')
    out['closure-oren.json'] = closure_scene('closure-oren', oren, 'An Oren-Nayar diffuse closure on points')
    out['attributes.json'] = with_nodes(base('points/attributes', 'Per point colours, a per point value and a UV checker'), attributes_material(), cloud('points', 'attributes', attributes=point_attributes), place('points-object', 'points'))
    out['point-info.json'] = with_nodes(base('points/point-info', 'The Point Info node on a baked cloud and on the floor, which reads zero: position, radius and the point_random attribute'), info_material(),
        cloud('points', 'info', attributes=point_random), place('points-object', 'points'))
    out['point-info-instanced.json'] = with_nodes(base('points/point-info-instanced', 'The Point Info node through instance transforms: the world position and the world radius'), info_material(),
        cloud('points', 'info', count=40, center=(0.0, -1.0, 0.4), spread=1.0, attributes=point_random), place('a', 'points', [1.0, 0.0, 0.0, -2.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]),
        place('b', 'points', [c, 0.0, s, 1.5, 0.0, 0.7, 0.0, 0.0, -s, 0.0, 1.2*c, 0.0]))
    floor_info = base('points/point-info-floor', 'The Point Info node reached only on triangles, without points in the scene: zero output')
    floor_info['nodes'].insert(0, info_material())
    for entry in floor_info['nodes']:
        if entry['handle'] == 'floor':
            entry['set']['used_shaders'] = ['info']
    out['point-info-floor.json'] = floor_info
    out['particle-info.json'] = with_nodes(base('points/particle-info', 'The Particle Info node on clouds of two particle systems: index, random, age, lifetime, location, size, velocity and angular velocity'), particle_material(),
        particles('system-a', 3), particles('system-b', 1000),
        cloud('left', 'particle', count=30, center=(-1.8, -1.0, 0.4), spread=1.0), place('left-object', 'left', particle_system='system-a', particle_index=1),
        cloud('middle', 'particle', count=30, center=(0.0, -1.0, 0.4), spread=1.0), place('middle-object', 'middle', particle_system='system-a', particle_index=3),
        cloud('right', 'particle', count=30, center=(1.8, -1.0, 0.4), spread=1.0), place('right-object', 'right', particle_system='system-b', particle_index=2),
        cloud('plain', 'particle', count=12, center=(0.0, 1.0, 0.8), spread=0.6), place('plain-object', 'plain'))
    mesh_particles = base('points/particle-info-mesh', 'The Particle Info node on triangles, without points in the scene: the floor holds a particle, the back wall the dummy particle')
    mesh_particles['nodes'][:0] = [particle_material(), particles('system', 40)]
    for entry in mesh_particles['nodes']:
        if entry['handle'] in ('floor', 'back'):
            entry['set']['used_shaders'] = ['particle']
        if entry['handle'] == 'floor-object':
            entry['set'].update({'particle_system': 'system', 'particle_index': 2})
    out['particle-info-mesh.json'] = mesh_particles
    out['geometry.json'] = with_nodes(base('points/geometry', 'The Geometry and Texture Coordinate nodes and a bumped diffuse on points'), geometry_material(), cloud('points', 'geo'), place('points-object', 'points', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.2, 0.0]))
    out['geometry-tangent.json'] = with_nodes(base('points/geometry-tangent', 'The Geometry node Tangent output on points (normalize(dPdu) of the point frame) as a colour'),
                                              tangent_colour('tangent'), cloud('points', 'tangent'), place('points-object', 'points', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.2, 0.0]))
    out['geometry-tangent-bump.json'] = with_nodes(base('points/geometry-tangent-bump', 'The Geometry node Tangent output on points as the height of a Bump node (the form with derivatives)'),
                                                   tangent_bump('tangent'), cloud('points', 'tangent'), place('points-object', 'points', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.2, 0.0]))
    tangent = room.shader('anisotropic', [
        {'handle': 't', 'type': 'tangent', 'set': {'direction_type': 'radial', 'axis': 'y'}},
        {'handle': 'nm', 'type': 'normal_map', 'set': {'space': 'object', 'strength': 0.7, 'color': [0.6, 0.5, 0.8]}},
        {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.35, 'anisotropy': 0.5, 'color': [0.8, 0.7, 0.5]}},
    ], [['t', 'Tangent', 'g', 'Tangent'], ['nm', 'Normal', 'g', 'Normal'], ['g', 'BSDF', 'output', 'Surface']])
    out['anisotropic-tangent.json'] = closure_scene('anisotropic-tangent', tangent, 'Anisotropic glossy with a radial Tangent node and an object space Normal Map on points')
    wire = room.shader('wire', [{'handle': 'w', 'type': 'wireframe', 'set': {'size': 0.1}}, {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}}], [['w', 'Fac', 'd', 'Roughness'], ['d', 'BSDF', 'output', 'Surface']])
    out['wireframe.json'] = closure_scene('wireframe', wire, 'The Wireframe node on points: the kernel gives zero off triangles')
    emitter = room.shader('point-emission', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.6, 0.3], 'strength': 4.0}}], [['e', 'Emission', 'output', 'Surface']])
    emitter['set'] = {'emission_sampling_method': 'auto'}
    out['emission.json'] = closure_scene('emission', emitter, 'Emissive points with MIS flags on the shader: a point hit takes no forward MIS (the kernel tests for a triangle) and points are not sampled as lights')
    area = room.single('area-emission', 'emission', {'color': [1, 1, 1], 'strength': 30}, output='Emission')
    out['lights-area-sun.json'] = lights_scene('lights-area-sun', 'Points under an area light and a sun, next-event estimation from point shading points', {},
        [area, {'handle': 'area', 'create': 'arealight', 'set': {'sizeu': 1.5, 'sizev': 1.5, 'use_mis': True, 'strength': [20, 20, 20], 'used_shaders': ['area-emission']}},
         {'handle': 'area-object', 'create': 'object', 'set': {'geometry': 'area', 'tfm': [1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 1.0, 2.5, 0.0, -1.0, 0.0, 0.0]}},
         {'handle': 'sun', 'create': 'sunlight', 'set': {'angle': 0.1, 'strength': [1.5, 1.4, 1.2], 'used_shaders': ['area-emission']}},
         {'handle': 'sun-object', 'create': 'object', 'set': {'geometry': 'sun', 'tfm': [1.0, 0.0, 0.0, 0.0, 0.0, 0.7, -0.7, 0.0, 0.0, 0.7, 0.7, 0.0]}}])
    out['light-tree.json'] = lights_scene('light-tree', 'Points with the light tree', {'use_light_tree': True}, [])
    passes = [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name}} for kind, name in
              (('depth', 'Depth'), ('normal', 'Normal'), ('position', 'Position'), ('uv', 'UV'), ('object_id', 'Object ID'), ('diffuse_color', 'Diffuse Color'), ('glossy_color', 'Glossy Color'))]
    def uv_attribute(count):
        return [{'name': 'uv', 'value': 'point2', 'domain': 'vertex', 'flags': ['standard'], 'data': [f32(v) for i in range(count) for v in (i/count, 1.0-i/count)]}]
    out['data-passes.json'] = with_nodes(base('points/data-passes', 'Depth, normal, position, UV, object index and colour passes on points'),
        cloud('points', 'matte', attributes=uv_attribute), place('points-object', 'points'), cloud('more', 'glossy', seed=2.0, count=30, center=(-2.0, -1.2, -0.5), spread=0.8, radius=0.25, attributes=uv_attribute), place('more-object', 'more'), *passes)
    denoising = [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name}} for kind, name in
                 (('denoising_normal', 'Denoising Normal'), ('denoising_albedo', 'Denoising Albedo'), ('denoising_depth', 'Denoising Depth'))]
    out['denoising-features.json'] = with_nodes(base('points/denoising-features', 'Denoising feature passes on points'), cloud('points', 'matte'), place('points-object', 'points'), *denoising)
    catcher = with_nodes(base('points/shadow-catcher', 'Points over a shadow catcher plane: the path split and the point shadows in the catcher passes'), cloud('points', 'matte'), place('points-object', 'points'),
        {'handle': 'pass-Shadow Catcher', 'create': 'pass', 'set': {'type': 'shadow_catcher', 'name': 'Shadow Catcher'}})
    for entry in catcher['nodes']:
        if entry['handle'] == 'floor-object':
            entry['set']['is_shadow_catcher'] = True
    out['shadow-catcher.json'] = catcher
    out['fast-gi.json'] = lights_scene('fast-gi', 'Fast GI (additive ambient occlusion) with points: AO shadow rays through the point traversal', {'ao_bounces': 1, 'ao_factor': 0.5, 'ao_distance': 2.0}, [])
    transparent = room.single('point-transparent', 'transparent_bsdf', {'color': [0.7, 0.8, 0.9]})
    out['transparent-shadows.json'] = closure_scene('transparent-shadows', transparent, 'Transparent points: the recording shadow traversal, more hits than the record holds, the point shader data of shadow shading', integrator={'transparent_max_bounce': 16})
    mixed = room.shader('half-transparent', [{'handle': 't', 'type': 'transparent_bsdf', 'set': {'color': [0.7, 0.6, 0.9]}}, {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.8, 0.5, 0.2], 'roughness': 0.0}},
        {'handle': 'pi', 'type': 'point_info'}, {'handle': 'm', 'type': 'mix_closure'}], [['pi', 'Random', 'm', 'Fac'], ['t', 'BSDF', 'm', 'Closure1'], ['d', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])
    out['transparent-node-weighted.json'] = with_nodes(base('points/transparent-node-weighted', 'Partly transparent points whose transparency depends on the Point Info node, evaluated in the shadow kernel', {'transparent_max_bounce': 16}), mixed,
        cloud('points', 'half-transparent', attributes=point_random), place('points-object', 'points'))
    out['transparent-limit.json'] = closure_scene('transparent-limit', transparent, 'Transparent points with a low transparent bounce limit: the count of hits decides', integrator={'transparent_max_bounce': 2})
    out['camera-motion.json'] = camera_blur(lights_scene('camera-motion', 'Static points with camera motion blur', {'motion_blur': True}, []))
    for entry in out['camera-motion.json']['nodes']:
        if entry['handle'] == 'camera':
            entry['set']['motion'] = [[1.0, 0.0, 0.0, -0.1, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0], [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0], [1.0, 0.0, 0.0, 0.1, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]]
    moving = [[1.0, 0.0, 0.0, -0.25, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.25, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.1]]
    out['object-motion.json'] = camera_blur(with_nodes(base('points/object-motion', 'A cloud on two objects, one moving (object motion blur on points)', {'motion_blur': True}), cloud('points', 'matte', count=40, center=(0.0, -1.0, 0.4), spread=1.0),
        place('moving', 'points', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.0], motion=moving), place('still', 'points', [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0])))
    out['object-motion-info.json'] = camera_blur(with_nodes(base('points/object-motion-info', 'The Point Info node on a moving instance: the position through the transform at the ray time', {'motion_blur': True}), info_material(),
        cloud('points', 'info', count=40, center=(0.0, -1.0, 0.4), spread=1.0, attributes=point_random), place('moving', 'points', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.0], motion=moving), place('still', 'points', [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0])))
    out['deform-3.json'] = camera_blur(with_nodes(base('points/deform-3', 'Deforming points, three motion steps with moving radii', {'motion_blur': True}), cloud('points', 'matte', steps=3), place('points-object', 'points')))
    out['deform-5.json'] = camera_blur(with_nodes(base('points/deform-5', 'Deforming points, five motion steps with moving radii', {'motion_blur': True}), cloud('points', 'glossy', steps=5), place('points-object', 'points')))
    out['deform-instanced.json'] = camera_blur(with_nodes(base('points/deform-instanced', 'Deforming points on two instancing objects, one of them moving', {'motion_blur': True}), cloud('points', 'matte', count=40, center=(0.0, -1.0, 0.4), spread=1.0, steps=3),
        place('moving', 'points', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.0], motion=moving), place('still', 'points', [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0])))
    out['deform-info.json'] = camera_blur(with_nodes(base('points/deform-info', 'The Point Info node on deforming points: the position at the ray time, the radius of the center step', {'motion_blur': True}), info_material(), cloud('points', 'info', steps=3, attributes=point_random), place('points-object', 'points')))
    out['deform-transparent.json'] = camera_blur(with_nodes(base('points/deform-transparent', 'Transparent deforming points: the recording shadow traversal and shadow shading at the ray time', {'motion_blur': True, 'transparent_max_bounce': 16}), transparent, cloud('points', 'point-transparent', steps=3), place('points-object', 'points')))
    vector = [{'handle': 'pass-Vector', 'create': 'pass', 'set': {'type': 'motion', 'name': 'Vector'}}, {'handle': 'pass-Vector Weight', 'create': 'pass', 'set': {'type': 'motion_weight', 'name': 'Vector Weight'}}]
    out['motion-pass.json'] = with_nodes(base('points/motion-pass', 'The motion pass on deforming points and on a moving instance, without motion blur: the point centers and their steps'), cloud('points', 'matte', steps=3), place('points-object', 'points'),
        cloud('more', 'glossy', seed=2.0, count=30, center=(-2.0, -1.2, -0.5), spread=0.8, radius=0.25), place('more-a', 'more', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.0], motion=moving), place('more-b', 'more', [1.0, 0.0, 0.0, 4.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]), *vector)
    # Beside the features with their own traversals.
    blocker = dict(count=40, center=(0.2, -1.0, -0.6), spread=0.9, radius=0.22)
    out['beside-ao-node.json'] = beside('ao/ao-node', 'points/beside-ao-node', 'The Ambient Occlusion node with points in the scene: the global traversal hits them, the local one ignores them', room.single('point-matte', 'diffuse_bsdf', {'color': [0.4, 0.55, 0.7], 'roughness': 0.4}), cloud('points', 'point-matte', **blocker), place('points-object', 'points'))
    out['ao-node-on-points.json'] = beside('ao/ao-node', 'points/ao-node-on-points', 'The Ambient Occlusion node shading points', cloud('points', ['ao-global', 'ao-local'], extra={'shader': [i % 2 for i in range(40)]}, **blocker), place('points-a', 'points', [1.0, 0.0, 0.0, -0.4, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]), place('points-b', 'points', [1.0, 0.0, 0.0, 0.5, 0.0, 1.0, 0.0, 0.4, 0.0, 0.0, 1.0, 0.0]))
    out['beside-bevel.json'] = beside('surface-nodes/bevel', 'points/beside-bevel', 'The Bevel node with points in the scene and on a cloud of its own: its local traversal ignores points', cloud('points', 'bevel-sphere', count=40, center=(0.0, 1.2, 0.0), spread=0.9, radius=0.22), place('points-object', 'points'))
    out['beside-raycast.json'] = beside('surface-nodes/raycast', 'points/beside-raycast', 'The Raycast node hitting points: the hit distance and the hit normal of a point', room.single('point-matte', 'diffuse_bsdf', {'color': [0.4, 0.55, 0.7], 'roughness': 0.4}), cloud('points', 'point-matte', **blocker), place('points-object', 'points'))
    out['raycast-on-points.json'] = beside('surface-nodes/raycast', 'points/raycast-on-points', 'The Raycast node shading points', cloud('points', 'ray-sphere', **blocker), place('points-object', 'points'))
    out['beside-subsurface.json'] = beside('subsurface/node-methods', 'points/beside-subsurface', 'Subsurface scattering with points in the scene: the random walk ignores them, its exit rays see them', cloud('points', 'matte', **blocker), place('points-object', 'points'))
    out['subsurface-on-points.json'] = beside('subsurface/node-methods', 'points/subsurface-on-points', 'A subsurface closure on points: the walk finds no hit in their own object', cloud('points', 'sss', **blocker), place('points-object', 'points'))
    out['beside-caustics.json'] = beside('mnee/glass-sphere-caustic', 'points/beside-caustics', 'Caustics with points in the scene: a point between the receiver and the light ends the walk, as a non-caster', cloud('points', 'diffuse', count=20, center=(0.8, 0.6, 0.0), spread=0.7, radius=0.1), place('points-object', 'points'))
    out['caustics-point-caster.json'] = beside('mnee/glass-sphere-caustic', 'points/caustics-point-caster', 'Points flagged as caustic casters: the kernel rejects a caster that is not a triangle', cloud('points', 'glass', count=20, center=(0.8, 0.6, 0.0), spread=0.7, radius=0.1), place('points-object', 'points', is_caustics_caster=True),
        cloud('receivers', 'diffuse', count=12, center=(-1.0, -0.8, 0.0), spread=0.4, radius=0.1), place('receivers-object', 'receivers', is_caustics_receiver=True))
    out['beside-volume.json'] = beside('volumes/absorption-point', 'points/beside-volume', 'Points inside a volume box: the volume traversals ignore them, surface paths stop at them', cloud('points', 'matte', count=40, center=(0.2, -0.8, 0.0), spread=1.0, radius=0.2), place('points-object', 'points'))
    out['beside-volume-transparent.json'] = beside('volumes/absorption-point', 'points/beside-volume-transparent', 'Transparent points inside a volume box: shadow rays attenuate through the medium between the recorded point hits', room.single('point-transparent', 'transparent_bsdf', {'color': [0.7, 0.8, 0.9]}), cloud('points', 'point-transparent', count=40, center=(0.2, -0.8, 0.0), spread=1.0, radius=0.2), place('points-object', 'points'))
    out['volume-on-points.json'] = beside('volumes/absorption-point', 'points/volume-on-points', 'A volume shader on a point cloud: the shadow traversal records them as volume bounds, the volume traversals ignore them', cloud('points', 'medium', count=30, center=(-1.8, -0.8, 0.0), spread=0.6, radius=0.2), place('points-object', 'points'))
    out['beside-displacement.json'] = beside('displacement/true-height', 'points/beside-displacement', 'True displacement on meshes with points in the scene: the evaluation sees triangles only', room.single('point-matte', 'diffuse_bsdf', {'color': [0.4, 0.55, 0.7], 'roughness': 0.4}), cloud('points', 'point-matte', count=30, center=(0.0, 0.5, -1.0), spread=0.8, radius=0.2), place('points-object', 'points'))
    out['displacement-on-points.json'] = beside('displacement/true-height', 'points/displacement-on-points', 'A true displacement shader on points: their positions stay, only meshes are displaced', cloud('points', 'object-height', count=30, center=(0.0, 0.5, -1.0), spread=0.8, radius=0.2), place('points-object', 'points'))
    out['bump-on-points.json'] = beside('displacement/bump', 'points/bump-on-points', 'A bump displacement shader on points', cloud('points', 'bump-object', count=30, center=(0.0, 0.5, -1.0), spread=0.8, radius=0.2), place('points-object', 'points'))
    out['beside-shadow-linking.json'] = beside('shadow-linking/blockers', 'points/beside-shadow-linking', 'Shadow linking with points as blockers in shadow sets and as receivers', cloud('points', 'ball-material', count=30, center=(0.3, 0.6, 0.6), spread=0.6, radius=0.12), place('points-object', 'points', blocker_shadow_set=1),
        cloud('more', 'ball-material', count=20, center=(-1.0, 0.0, 1.2), spread=0.5, radius=0.15), place('more-object', 'more'))
    emissive = room.shader('point-glow', [{'handle': 'e', 'type': 'emission', 'set': {'color': [0.4, 0.9, 0.5], 'strength': 6.0}}], [['e', 'Emission', 'output', 'Surface']])
    emissive['set'] = {'emission_sampling_method': 'auto'}
    out['shadow-linking-emissive.json'] = beside('shadow-linking/blockers', 'points/shadow-linking-emissive', 'Shadow-linked emissive points: the dedicated light kernels pick them, without forward MIS', emissive, cloud('points', 'point-glow', count=20, center=(0.3, 1.6, 0.4), spread=0.4, radius=0.12), place('points-object', 'points', shadow_set_membership=5))
    # Emission that is not constant on the points: the dedicated shadow path evaluates the shader at the picked point hit.
    textured = room.shader('point-glow-normal', [{'handle': 'g', 'type': 'geometry'}, {'handle': 'a', 'type': 'vector_math', 'set': {'math_type': 'absolute'}}, {'handle': 'e', 'type': 'emission', 'set': {'strength': 6.0}}],
        [['g', 'Normal', 'a', 'Vector1'], ['a', 'Vector', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']])
    out['shadow-linking-emissive-textured.json'] = beside('shadow-linking/blockers', 'points/shadow-linking-emissive-textured', 'Shadow-linked points whose emission follows their normal: the dedicated light kernels pick them and the light shader is evaluated at the picked hit', textured, cloud('points', 'point-glow-normal', count=20, center=(0.3, 1.6, 0.4), spread=0.4, radius=0.12), place('points-object', 'points', shadow_set_membership=5))
    out['beside-light-linking.json'] = beside('light-motion/linking-transparent', 'points/beside-light-linking', 'Light linking with transparent shadows and points', cloud('points', 'ball', count=30, center=(0.0, 0.2, 0.0), spread=0.8, radius=0.15), place('points-object', 'points'))
    out['with-curves.json'] = with_nodes(base('points/with-curves', 'Points and thick curves in one scene: unaligned BVH2 nodes beside the points\' leaves'), curves.hair('tuft', 'matte', 'thick', center=(1.4, -2, 0.4), spread=0.9), curves.place('tuft-object', 'tuft'), cloud('points', 'glossy', count=40, center=(-1.2, -0.9, 0.4), spread=1.0), place('points-object', 'points'))
    out['with-curves-info.json'] = with_nodes(base('points/with-curves-info', 'The Point Info node on points and on curves, where it reads zero'), info_material(), curves.hair('tuft', 'info', 'thick', center=(1.4, -2, 0.4), spread=0.9), curves.place('tuft-object', 'tuft'), cloud('points', 'info', count=40, center=(-1.2, -0.9, 0.4), spread=1.0, attributes=point_random), place('points-object', 'points'))
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
