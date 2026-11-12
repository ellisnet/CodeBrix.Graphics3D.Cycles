#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the Geometry node Tangent and the motion-triangle smooth-normal bump oracle scenes byte for byte.

The Geometry node's Tangent output on triangles (geom/primitive.h primitive_tangent: the radial tangent of the generated
coordinates through the object's normal transform), as a colour (the plain form) and as the height of a Bump node (the
form with derivatives), on a transform-applied box, a smooth sphere and two instances of one rotated and scaled box, in the
surface node family's room (scenes/surface-nodes/geometry-tangent*). scenes/light-motion/deforming-smooth-normal-bump:
light-motion/camera-deforming-motion with its deforming mesh smooth-shaded and bumped from the Texture Coordinate node's
Normal output, so the bump evaluates the motion triangles' smooth normal with derivatives (motion_triangle_smooth_normal).
scenes/displacement/stateful-nodes: displacement/true-height with each grid's height the sum of a noise and the outputs of
the Ambient Occlusion, Light Path, Wireframe, Bevel and Raycast nodes, which the displacement program evaluates under
KERNEL_FEATURE_NODE_MASK_DISPLACEMENT (no path state, no ray tracing); one grid with displacement method Both.
Usage: python3 native/oracle/scenes/author_geometry_tangent.py [--check]."""
import argparse
import copy
import json
import math
from pathlib import Path
from author_surface_nodes import scene, box
from author_bsdf import shader, sphere, f32

HERE = Path(__file__).resolve().parent/'surface-nodes'


def tangent_colour(handle):
    return shader(handle, [{'handle': 'geo', 'type': 'geometry', 'set': {}},
                           {'handle': 'abs', 'type': 'vector_math', 'set': {'math_type': 'absolute'}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}}],
                  [['geo', 'Tangent', 'abs', 'Vector1'], ['abs', 'Vector', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']])


def tangent_bump(handle):
    return shader(handle, [{'handle': 'geo', 'type': 'geometry', 'set': {}},
                           {'handle': 'sep', 'type': 'separate_xyz', 'set': {}},
                           {'handle': 'sum', 'type': 'math', 'set': {'math_type': 'add'}},
                           {'handle': 'bump', 'type': 'bump', 'set': {'strength': 0.8, 'distance': 0.3}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.7, 0.55, 0.4], 'roughness': 0.2}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.5, 0.6, 0.8], 'roughness': 0.25}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.35}}],
                  [['geo', 'Tangent', 'sep', 'Vector'], ['sep', 'X', 'sum', 'Value1'], ['sep', 'Z', 'sum', 'Value2'], ['sum', 'Value', 'bump', 'Height'],
                   ['bump', 'Normal', 'd', 'Normal'], ['bump', 'Normal', 'g', 'Normal'], ['g', 'BSDF', 'm', 'Closure1'], ['d', 'BSDF', 'm', 'Closure2'],
                   ['m', 'Closure', 'output', 'Surface']])


def instance(handle, geometry, center, angle, scale):
    c, s = math.cos(angle), math.sin(angle)
    rows = [[c*scale[0], 0.0, s*scale[2], center[0]], [0.0, scale[1], 0.0, center[1]], [-s*scale[0], 0.0, c*scale[2], center[2]]]
    return {'handle': handle, 'create': 'object', 'set': {'geometry': geometry, 'tfm': [f32(v) for row in rows for v in row]}}


def document(scene_id, description, material, instances=True):
    shapes = box('applied', 'tangent', (-2.4, -1.2, 0.6), (1.2, 1.4, 1.2)) + sphere('ball', 'tangent', (0.0, -0.9, 0.3), 1.0)
    if instances:
        shapes += [box('shared', 'tangent', (0.0, 0.0, 0.0), (1.0, 1.0, 1.0))[0],
                   instance('shared-a', 'shared', (2.2, -1.3, 0.2), 0.6, (1.1, 1.3, 0.9)),
                   instance('shared-b', 'shared', (2.6, 0.3, 1.2), -0.4, (0.8, 0.7, 1.2))]
    return scene(scene_id, description, [material('tangent')], shapes)


def deforming_bump():
    doc = copy.deepcopy(json.loads((HERE.parent/'light-motion/camera-deforming-motion.json').read_text()))
    doc['id'] = 'light-motion/deforming-smooth-normal-bump'
    doc['description'] = ('Deforming smooth-shaded mesh bumped from the Texture Coordinate Normal output (motion-triangle smooth normals with '
                          'derivatives), with camera and object motion')
    doc['sourceFixture'] = 'native/oracle/scenes/light-motion/camera-deforming-motion.json with a smooth bumped deforming mesh'
    for i, node in enumerate(doc['nodes']):
        if node['handle'] == 'moving':
            doc['nodes'][i] = shader('moving', [{'handle': 'tc', 'type': 'texture_coordinate', 'set': {}},
                                                {'handle': 'noise', 'type': 'noise_texture', 'set': {'scale': 3.0, 'detail': 1.0}},
                                                {'handle': 'bump', 'type': 'bump', 'set': {'strength': 0.9, 'distance': 0.2}},
                                                {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.6, 0.45, 0.3], 'roughness': 0.2}}],
                                     [['tc', 'Normal', 'noise', 'Vector'], ['noise', 'Fac', 'bump', 'Height'], ['bump', 'Normal', 'd', 'Normal'],
                                      ['d', 'BSDF', 'output', 'Surface']])
        if node['handle'] == 'center':
            node['set']['smooth'] = [True]*(len(node['mesh']['indices'])//3)
    return doc


def stateful_displacement():
    doc = copy.deepcopy(json.loads((HERE.parent/'displacement/true-height.json').read_text()))
    doc['id'] = 'displacement/stateful-nodes'
    doc['description'] = ('True displacement (one grid Both) by the sum of a noise and the Ambient Occlusion, Light Path, Wireframe, Bevel and '
                          'Raycast node outputs, as the displacement node mask evaluates them')
    doc['sourceFixture'] = 'native/oracle/scenes/displacement/true-height.json with stateful nodes in the displacement height'
    for node in doc['nodes']:
        if node.get('create') != 'shader' or node['handle'] not in ('object-height', 'world-height'):
            continue
        graph = node['graph']
        graph['nodes'] += [{'handle': 'ao', 'type': 'ambient_occlusion', 'set': {'samples': 4, 'distance': 0.7}},
                           {'handle': 'path', 'type': 'light_path', 'set': {}},
                           {'handle': 'wire', 'type': 'wireframe', 'set': {'size': 0.08}},
                           {'handle': 'bevel', 'type': 'bevel', 'set': {'samples': 4, 'radius': 0.1}},
                           {'handle': 'bevel-x', 'type': 'separate_xyz', 'set': {}},
                           {'handle': 'ray', 'type': 'raycast', 'set': {'length': 1.5, 'direction': [0.0, 1.0, 0.0]}}]
        graph['connect'] = [c for c in graph['connect'] if c[2] != 'disp']+[['bevel', 'Normal', 'bevel-x', 'Vector']]
        last = ['noise', 'Fac']
        terms = [['ao', 'AO'], ['path', 'Is Camera Ray'], ['path', 'Ray Length'], ['path', 'Ray Depth'], ['path', 'Transparent Depth'],
                 ['wire', 'Fac'], ['bevel-x', 'X'], ['ray', 'Is Hit'], ['ray', 'Hit Distance']]
        for i, term in enumerate(terms):
            graph['nodes'].append({'handle': 'add-%d' % i, 'type': 'math', 'set': {'math_type': 'add'}})
            graph['connect'] += [[last[0], last[1], 'add-%d' % i, 'Value1'], [term[0], term[1], 'add-%d' % i, 'Value2']]
            last = ['add-%d' % i, 'Value']
        graph['connect'].append([last[0], last[1], 'disp', 'Height'])
        for n in graph['nodes']:
            if n['handle'] == 'disp':
                n['set']['midlevel'] = 1.5
                n['set']['scale'] = 0.12
        if node['handle'] == 'world-height':
            node['set']['displacement_method'] = 'both'
    return doc


def scenes():
    return {'geometry-tangent.json': document('surface-nodes/geometry-tangent', 'Geometry node Tangent output (radial tangent of the generated '
                                              'coordinates) as a colour on a transform-applied box, a smooth sphere and two rotated, scaled instances '
                                              '(their side faces have a degenerate frame, as on the CPU)', tangent_colour),
            'geometry-tangent-bump.json': document('surface-nodes/geometry-tangent-bump', 'Geometry node Tangent output driving a Bump node '
                                                   '(the tangent with derivatives) on the same objects', tangent_bump)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    documents = [(HERE/name, doc) for name, doc in scenes().items()]+[(HERE.parent/'light-motion/deforming-smooth-normal-bump.json', deforming_bump()),
                                                                (HERE.parent/'displacement/stateful-nodes.json', stateful_displacement())]
    for path, doc in documents:
        name = path.name
        text = json.dumps(doc, indent=1)+'\n'
        if args.check:
            if path.read_text() != text:
                raise SystemExit('Scene differs: '+str(path))
        else:
            path.write_text(text)
        print(name, 'ok')


if __name__ == '__main__':
    main()
