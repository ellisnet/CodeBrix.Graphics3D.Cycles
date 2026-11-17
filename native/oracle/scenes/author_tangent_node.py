#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the Tangent node oracle scenes on rotated instances (scenes/surface-nodes/tangent-node-*) byte for byte.

The Tangent node's radial tangent (svm_node_tangent: the generated coordinates' radial direction through the object's
normal transform, then cross(N, normalize(cross(t, N)))) as a colour and as the height of a Bump node (the form with
derivatives), on the objects of scenes/surface-nodes/geometry-tangent: a transform-applied box, a smooth sphere and two
rotated, scaled instances of one box, whose side faces have a degenerate frame (t parallel to N) as on the CPU.
Usage: python3 native/oracle/scenes/author_tangent_node.py [--check]."""
import argparse
import json
from pathlib import Path
from author_bsdf import shader
import author_geometry_tangent as geometry

HERE = Path(__file__).resolve().parent/'surface-nodes'


def tangent_colour(axis):
    def make(handle):
        return shader(handle, [{'handle': 'tan', 'type': 'tangent', 'set': {'direction_type': 'radial', 'axis': axis}},
                               {'handle': 'abs', 'type': 'vector_math', 'set': {'math_type': 'absolute'}},
                               {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}}],
                      [['tan', 'Tangent', 'abs', 'Vector1'], ['abs', 'Vector', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']])
    return make


def tangent_bump(handle):
    return shader(handle, [{'handle': 'tan', 'type': 'tangent', 'set': {'direction_type': 'radial', 'axis': 'x'}},
                           {'handle': 'sep', 'type': 'separate_xyz', 'set': {}},
                           {'handle': 'sum', 'type': 'math', 'set': {'math_type': 'add'}},
                           {'handle': 'bump', 'type': 'bump', 'set': {'strength': 0.8, 'distance': 0.3}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.7, 0.55, 0.4], 'roughness': 0.2}}],
                  [['tan', 'Tangent', 'sep', 'Vector'], ['sep', 'Y', 'sum', 'Value1'], ['sep', 'Z', 'sum', 'Value2'], ['sum', 'Value', 'bump', 'Height'],
                   ['bump', 'Normal', 'd', 'Normal'], ['d', 'BSDF', 'output', 'Surface']])


def nan_value(nodes, connect):
    """Y + Z of the Tangent node's radial tangent about X: NaN where the frame is degenerate, as the Bump scene's height."""
    nodes += [{'handle': 'tan', 'type': 'tangent', 'set': {'direction_type': 'radial', 'axis': 'x'}},
              {'handle': 'sep', 'type': 'separate_xyz', 'set': {}}, {'handle': 'sum', 'type': 'math', 'set': {'math_type': 'add'}}]
    connect += [['tan', 'Tangent', 'sep', 'Vector'], ['sep', 'Y', 'sum', 'Value1'], ['sep', 'Z', 'sum', 'Value2']]


def tangent_glossy_glass(handle):
    """The radial tangent as the Normal of a Glossy BSDF and of a Glass BSDF node (svm/closure.h: safe_normalize_fallback
    keeps a NaN normal, and such a closure gives no light)."""
    return shader(handle, [{'handle': 'tan', 'type': 'tangent', 'set': {'direction_type': 'radial', 'axis': 'x'}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.35, 'color': [0.8, 0.6, 0.3]}},
                           {'handle': 't', 'type': 'glass_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3, 'IOR': 1.45, 'color': [0.7, 0.85, 0.9]}},
                           {'handle': 'mix', 'type': 'mix_closure', 'set': {'fac': 0.5}}],
                  [['tan', 'Tangent', 'g', 'Normal'], ['tan', 'Tangent', 't', 'Normal'], ['g', 'BSDF', 'mix', 'Closure1'], ['t', 'BSDF', 'mix', 'Closure2'],
                   ['mix', 'Closure', 'output', 'Surface']])


def tangent_emission(handle):
    """A NaN Emission Strength of a Principled BSDF (svm/closure.h principled_bsdf_emission: !is_zero(emission) is true
    for it, so the NaN emission is recorded)."""
    nodes = [{'handle': 'p', 'type': 'principled_bsdf', 'set': {'base_color': [0.6, 0.5, 0.4], 'roughness': 0.5, 'emission_color': [0.2, 0.5, 0.9]}}]
    connect = [['p', 'BSDF', 'output', 'Surface']]
    nan_value(nodes, connect)
    connect.append(['sum', 'Value', 'p', 'Emission Strength'])
    return shader(handle, nodes, connect)


def scenes():
    def doc(scene_id, description, material):
        d = geometry.document(scene_id, description, material)
        d['sourceFixture'] = 'native/oracle/scenes/author_tangent_node.py (the surface node family room)'
        return d
    return {'tangent-node-radial.json': doc('surface-nodes/tangent-node-radial', 'Tangent node, radial about Z, as a colour on a transform-applied box, a smooth sphere '
                                            'and two rotated, scaled instances (degenerate frames on their side faces, as on the CPU)', tangent_colour('z')),
            'tangent-node-radial-y.json': doc('surface-nodes/tangent-node-radial-y', 'Tangent node, radial about Y, as a colour on the same objects', tangent_colour('y')),
            'tangent-node-bump.json': doc('surface-nodes/tangent-node-bump', 'Tangent node, radial about X, driving a Bump node (the tangent with derivatives) on the same objects',
                                          tangent_bump),
            'tangent-node-glossy-glass-normal.json': doc('surface-nodes/tangent-node-glossy-glass-normal', 'Tangent node, radial about X, as the Normal of a Glossy BSDF and of a '
                                                         'Glass BSDF node on the same objects: a NaN normal at the degenerate frames, kept as on the CPU', tangent_glossy_glass),
            'tangent-node-emission.json': doc('surface-nodes/tangent-node-emission', 'A value that is NaN at the degenerate frames of a Tangent node as the Emission Strength '
                                              'of a Principled BSDF on the same objects', tangent_emission)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
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
