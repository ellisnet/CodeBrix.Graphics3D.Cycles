#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the attribute value kind oracle scenes (scenes/attributes/) byte for byte.

Quaternion and spherical-harmonics attributes (value kinds quaternion and spherical_harmonics_rest) on meshes and on point
clouds, read through Attribute nodes, in the sampling family's room: a quaternion on the mesh, face, vertex and corner
domains of spheres, on the vertex and mesh domains of clouds and on the curve key, curve and mesh domains of curves
(Color and Vector w, x, y, Fac their average, Alpha z, interpolated unnormalized between vertices and along curve
segments); spherical-harmonics coefficients of bands 1 to 3 beside a geometry that has
none (an Attribute node finds them and reads black with alpha 1, a missing one reads its Missing inputs); and both kinds
with other attribute types on a sphere and an instanced cloud. Every value is built from a fixed formula rounded through
binary32, so every platform gets the same bytes; the quaternions come from an axis and an angle (w = cos of half the
angle, x, y, z = the axis times its sine). Through the public C API.
Usage: python3 native/oracle/scenes/author_attributes.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_curves as curves
import author_points as points
import author_sampling as room

HERE = Path(__file__).resolve().parent/'attributes'
f32 = room.f32
COEFFICIENTS = 15  # bands 1 to 3, three colour channels each


def quaternion(i, scale=1.0):
    """A quaternion with w, x, y and z in [0, 1] (an angle up to pi about an axis in the positive octant), times scale."""
    angle = 0.3+2.6*((i*0.618034) % 1.0)
    axis = (0.2+0.8*((i*0.381966) % 1.0), 0.2+0.8*((i*0.754877) % 1.0), 0.2+0.8*((i*0.569840) % 1.0))
    length = math.sqrt(sum(a*a for a in axis))
    s = math.sin(0.5*angle)/length
    return [f32(scale*v) for v in (math.cos(0.5*angle), s*axis[0], s*axis[1], s*axis[2])]


def quaternions(count, scale=None):
    """count quaternions, flat (w, x, y, z per element); scale(i) makes some of them longer or shorter than 1."""
    return [v for i in range(count) for v in quaternion(i, scale(i) if scale else 1.0)]


def harmonics(count, seed=0.0):
    """count elements of 45 coefficients in [-1, 1], multiples of 1/32 (exact in binary32), the two ends included."""
    values = []
    for i in range(count):
        for k in range(3*COEFFICIENTS):
            values.append(f32(round(32.0*math.sin(seed+1.7*i+0.9*k+0.31*i*k))/32.0))
    return values


def base(scene_id, description):
    """The sampling room without its spheres, as the point scenes use it."""
    document = points.base(scene_id, description)
    document['sourceFixture'] = 'native/oracle/scenes/author_attributes.py (attribute value kinds in the sampling family room)'
    return document


def attribute(name, value, domain, data):
    return {'name': name, 'value': value, 'domain': domain, 'data': data}


def sphere(handle, material, center, radius, attributes, stacks=6, slices=8):
    """A smooth sphere of the room with the attributes attributes(vertices, faces, corners) returns (coarser where every
    element carries 45 spherical-harmonics coefficients)."""
    entries = room.sphere(handle, material, center, radius, stacks=stacks, slices=slices)
    vertices, faces = (stacks+1)*(slices+1), 2*stacks*slices
    entries[0]['mesh']['attributes'] = attributes(vertices, faces, 3*faces)
    return entries


def quaternion_material(handle='quaternion'):
    """Color (w, x, y) as the diffuse colour, Alpha (z) mixing in a glossy closure, Fac (the average) as a faint emission."""
    return room.shader(handle, [
        {'handle': 'q', 'type': 'attribute', 'set': {'attribute': 'q'}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
        {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.8, 0.8, 0.8]}},
        {'handle': 'm', 'type': 'mix_closure'},
        {'handle': 'strength', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.4}},
        {'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.9, 0.7]}},
        {'handle': 'add', 'type': 'add_closure'},
    ], [['q', 'Color', 'd', 'Color'], ['q', 'Alpha', 'm', 'Fac'], ['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'],
        ['q', 'Fac', 'strength', 'Value1'], ['strength', 'Value', 'e', 'Strength'], ['m', 'Closure', 'add', 'Closure1'],
        ['e', 'Emission', 'add', 'Closure2'], ['add', 'Closure', 'output', 'Surface']])


def harmonics_material(handle='harmonics'):
    """The sh attribute's Color added to a grey and its Alpha mixing a red and a green diffuse: a found spherical-harmonics
    attribute reads black with alpha 1 (green); a missing one reads the Missing inputs (magenta added, mostly red)."""
    return room.shader(handle, [
        {'handle': 'sh', 'type': 'attribute', 'set': {'attribute': 'sh', 'missing': [0.6, 0.1, 0.5], 'missing_alpha': 0.2}},
        {'handle': 'tint', 'type': 'mix', 'set': {'mix_type': 'add', 'fac': 1.0, 'color1': [0.3, 0.3, 0.3]}},
        {'handle': 'red', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0, 'color': [0.8, 0.15, 0.1]}},
        {'handle': 'green', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0, 'color': [0.15, 0.7, 0.2]}},
        {'handle': 'base', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
        {'handle': 'm', 'type': 'mix_closure'},
        {'handle': 'm2', 'type': 'mix_closure', 'set': {'fac': 0.5}},
    ], [['sh', 'Color', 'tint', 'Color2'], ['tint', 'Color', 'base', 'Color'], ['sh', 'Alpha', 'm', 'Fac'],
        ['red', 'BSDF', 'm', 'Closure1'], ['green', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'm2', 'Closure1'],
        ['base', 'BSDF', 'm2', 'Closure2'], ['m2', 'Closure', 'output', 'Surface']])


def combined_material():
    """Both kinds and two plain types in one graph: several attribute tables and their offsets at once."""
    return room.shader('combined', [
        {'handle': 'q', 'type': 'attribute', 'set': {'attribute': 'q'}},
        {'handle': 'sh', 'type': 'attribute', 'set': {'attribute': 'sh', 'missing': [0.5, 0.0, 0.5], 'missing_alpha': 0.0}},
        {'handle': 'tint', 'type': 'attribute', 'set': {'attribute': 'tint'}},
        {'handle': 'weight', 'type': 'attribute', 'set': {'attribute': 'weight'}},
        {'handle': 'shade', 'type': 'mix', 'set': {'mix_type': 'multiply'}},
        {'handle': 'plus', 'type': 'mix', 'set': {'mix_type': 'add', 'fac': 1.0}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
        {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.25}},
        {'handle': 'm', 'type': 'mix_closure'},
        {'handle': 'gate', 'type': 'math', 'set': {'math_type': 'multiply'}},
    ], [['q', 'Color', 'shade', 'Color1'], ['tint', 'Color', 'shade', 'Color2'], ['weight', 'Fac', 'shade', 'Fac'],
        ['shade', 'Color', 'plus', 'Color1'], ['sh', 'Color', 'plus', 'Color2'], ['plus', 'Color', 'd', 'Color'],
        ['q', 'Vector', 'g', 'Color'], ['q', 'Alpha', 'gate', 'Value1'], ['sh', 'Alpha', 'gate', 'Value2'], ['gate', 'Value', 'm', 'Fac'],
        ['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def quaternion_mesh():
    """One sphere per domain, every one with its own q; the vertex values are not all of unit length."""
    def domain(name):
        def make(vertices, faces, corners):
            count = {'mesh': 1, 'face': faces, 'vertex': vertices, 'corner': corners}[name]
            scale = (lambda i: 0.55+0.45*((i*0.414214) % 1.0)) if name == 'vertex' else None
            return [attribute('q', 'quaternion', name, quaternions(count, scale))]
        return make
    nodes = [quaternion_material()]
    for k, name in enumerate(('mesh', 'face', 'vertex', 'corner')):
        nodes += sphere('sphere-'+name, 'quaternion', (-2.7+1.8*k, -0.9, 0.4), 0.8, domain(name))
    return points.with_nodes(base('attributes/quaternion-mesh', 'A quaternion attribute on the mesh, face, vertex and corner '
                                         'domains of four spheres, read through an Attribute node: Color (w, x, y) as the diffuse colour, '
                                         'Alpha (z) mixing in a glossy closure, Fac (the average of w, x and y) as an emission strength; '
                                         'the vertex values are interpolated unnormalized'), *nodes)


def quaternion_points():
    def per_point(count):
        return [attribute('q', 'quaternion', 'vertex', quaternions(count, lambda i: 0.6+0.4*((i*0.707107) % 1.0)))]

    def per_cloud(_count):
        return [attribute('q', 'quaternion', 'mesh', quaternions(1))]
    return points.with_nodes(base('attributes/quaternion-points', 'A quaternion attribute on the vertex domain (one per point) of a '
                                         'cloud and on the mesh domain of a second cloud, read through an Attribute node as on the '
                                         'spheres of attributes/quaternion-mesh'),
                             quaternion_material(),
                             points.cloud('per-point', 'quaternion', count=50, center=(-1.2, -0.8, 0.4), spread=1.2, radius=0.3, attributes=per_point),
                             points.place('per-point-object', 'per-point'),
                             points.cloud('per-cloud', 'quaternion', seed=1.1, count=30, center=(1.8, -0.9, 0.2), spread=0.9, radius=0.28, attributes=per_cloud),
                             points.place('per-cloud-object', 'per-cloud'))


def quaternion_curves():
    """Three tufts of curves with the quaternion material: per key (not all of unit length, mixed along each segment) and per
    curve on thick curves, on the mesh domain of ribbons."""
    def per_key(_curves, keys):
        return [attribute('q', 'quaternion', 'curve_key', quaternions(keys, lambda i: 0.6+0.4*((i*0.707107) % 1.0)))]

    def per_curve(count, _keys):
        return [attribute('q', 'quaternion', 'curve', quaternions(count))]

    def whole(_curves, _keys):
        return [attribute('q', 'quaternion', 'mesh', quaternions(1))]
    return points.with_nodes(base('attributes/quaternion-curves', 'A quaternion attribute on the curve key domain (mixed along '
                                         'each segment, not all of unit length) and the curve domain of two tufts of thick curves and on '
                                         'the mesh domain of a tuft of ribbons, read through an Attribute node as on the spheres of '
                                         'attributes/quaternion-mesh'),
                             quaternion_material(),
                             curves.hair('per-key', 'quaternion', 'thick', seed=0.3, attributes=per_key, center=(-2.2, -2, 0.4), spread=0.8),
                             curves.place('per-key-object', 'per-key'),
                             curves.hair('per-curve', 'quaternion', 'thick', seed=1.1, attributes=per_curve, center=(0.0, -2, 0.4), spread=0.8),
                             curves.place('per-curve-object', 'per-curve'),
                             curves.hair('whole', 'quaternion', 'ribbon', seed=2.3, attributes=whole, center=(2.2, -2, 0.4), spread=0.8),
                             curves.place('whole-object', 'whole'))


def harmonics_mesh():
    def domain(name, seed):
        def make(vertices, faces, _corners):
            return [attribute('sh', 'spherical_harmonics_rest', name, harmonics({'mesh': 1, 'face': faces, 'vertex': vertices}[name], seed))]
        return make
    nodes = [harmonics_material()]
    for k, (name, seed) in enumerate((('mesh', 0.2), ('face', 1.3), ('vertex', 2.9))):
        nodes += sphere('sphere-'+name, 'harmonics', (-2.7+1.8*k, -0.9, 0.4), 0.8, domain(name, seed), stacks=4, slices=6)
    nodes += room.sphere('sphere-without', 'harmonics', (2.7, -0.9, 0.4), 0.8, stacks=4, slices=6)
    return points.with_nodes(base('attributes/spherical-harmonics-mesh', 'Spherical-harmonics coefficients of bands 1 to 3 on the '
                                         'mesh, face and vertex domains of three spheres and none on a fourth, read through an Attribute '
                                         'node: found, they read black with alpha 1 (a green diffuse); the fourth sphere reads the '
                                         'Missing inputs (a red mix with magenta added)'), *nodes)


def harmonics_points():
    def per_point(count):
        return [attribute('sh', 'spherical_harmonics_rest', 'vertex', harmonics(count, 0.7))]

    def per_cloud(_count):
        return [attribute('sh', 'spherical_harmonics_rest', 'mesh', harmonics(1, 4.1))]
    return points.with_nodes(base('attributes/spherical-harmonics-points', 'Spherical-harmonics coefficients on the vertex domain of '
                                         'a cloud and on the mesh domain of a second, and none on a third, read through an Attribute node '
                                         'as on the spheres of attributes/spherical-harmonics-mesh'),
                             harmonics_material(),
                             points.cloud('per-point', 'harmonics', count=40, center=(-1.9, -0.8, 0.4), spread=1.0, radius=0.3, attributes=per_point),
                             points.place('per-point-object', 'per-point'),
                             points.cloud('per-cloud', 'harmonics', seed=1.1, count=30, center=(0.3, -0.9, 0.2), spread=0.9, radius=0.28, attributes=per_cloud),
                             points.place('per-cloud-object', 'per-cloud'),
                             points.cloud('without', 'harmonics', seed=2.3, count=20, center=(2.4, -1.0, 0.0), spread=0.7, radius=0.25),
                             points.place('without-object', 'without'))


def combined():
    def mesh_attributes(vertices, faces, corners):
        return [attribute('q', 'quaternion', 'vertex', quaternions(vertices)),
                attribute('sh', 'spherical_harmonics_rest', 'face', harmonics(faces, 5.3)),
                attribute('tint', 'color', 'corner', [f32(0.3+0.6*((i*c) % 1.0)) for i in range(corners) for c in (0.618034, 0.381966, 0.236068)]),
                attribute('weight', 'float', 'vertex', [f32((i*0.414214) % 1.0) for i in range(vertices)])]

    def cloud_attributes(count):
        return [attribute('weight', 'float', 'vertex', [f32((i*0.732051) % 1.0) for i in range(count)]),
                attribute('sh', 'spherical_harmonics_rest', 'vertex', harmonics(count, 6.1)),
                attribute('tint', 'color', 'vertex', [f32(0.25+0.7*((i*c) % 1.0)) for i in range(count) for c in (0.381966, 0.236068, 0.618034)]),
                attribute('q', 'quaternion', 'vertex', quaternions(count, lambda i: 0.7+0.3*((i*0.302776) % 1.0)))]
    c, s = f32(math.cos(0.4)), f32(math.sin(0.4))
    return points.with_nodes(base('attributes/quaternion-and-spherical-harmonics', 'Both kinds beside a colour and a float attribute '
                                         'on a sphere (quaternion per vertex, spherical harmonics per face, colour per corner, float per '
                                         'vertex) and on a cloud on two objects (every attribute per point), read by one graph: the '
                                         'quaternion Color times the colour, the spherical-harmonics Color (black) added, and the product of '
                                         'the two Alphas mixing in a glossy closure tinted by the quaternion Vector'),
                             combined_material(),
                             *sphere('sphere', 'combined', (-1.6, -0.8, 0.6), 1.0, mesh_attributes, stacks=4, slices=6),
                             points.cloud('cloud', 'combined', seed=0.9, count=36, center=(0.0, -1.0, 0.0), spread=0.8, radius=0.26, attributes=cloud_attributes),
                             points.place('cloud-a', 'cloud', [1.0, 0.0, 0.0, 1.3, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]),
                             points.place('cloud-b', 'cloud', [c, 0.0, s, 2.9, 0.0, 0.9, 0.0, 0.6, -s, 0.0, c, -0.4]))


def scenes():
    return {'quaternion-mesh.json': quaternion_mesh(), 'quaternion-points.json': quaternion_points(),
            'quaternion-curves.json': quaternion_curves(),
            'spherical-harmonics-mesh.json': harmonics_mesh(), 'spherical-harmonics-points.json': harmonics_points(),
            'quaternion-and-spherical-harmonics.json': combined()}


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
