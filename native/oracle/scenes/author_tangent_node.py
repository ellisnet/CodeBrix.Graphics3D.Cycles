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


def scenes():
    def doc(scene_id, description, material):
        d = geometry.document(scene_id, description, material)
        d['sourceFixture'] = 'native/oracle/scenes/author_tangent_node.py (the surface node family room)'
        return d
    return {'tangent-node-radial.json': doc('surface-nodes/tangent-node-radial', 'Tangent node, radial about Z, as a colour on a transform-applied box, a smooth sphere '
                                            'and two rotated, scaled instances (degenerate frames on their side faces, as on the CPU)', tangent_colour('z')),
            'tangent-node-radial-y.json': doc('surface-nodes/tangent-node-radial-y', 'Tangent node, radial about Y, as a colour on the same objects', tangent_colour('y')),
            'tangent-node-bump.json': doc('surface-nodes/tangent-node-bump', 'Tangent node, radial about X, driving a Bump node (the tangent with derivatives) on the same objects',
                                          tangent_bump)}


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
