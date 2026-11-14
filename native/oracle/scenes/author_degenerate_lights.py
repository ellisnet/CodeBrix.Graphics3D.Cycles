#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the degenerate area light oracle scenes (scenes/lamps/area-degenerate-*) byte for byte.

An area light whose axis length underflows to zero (a size or an object scale of 1e-23): the host keeps it, and the
kernels see a zero length and an axis of infinities and NaNs. Its next-event samples are rejected; a camera ray sees a
rectangle or an ellipse with one such axis as a strip without emission (the spread clamp rejects it), and an ellipse with
both axes zero as an emitting plane. Next to the sampling family room's sphere lamp, with distribution sampling and with
the light tree, through the public C API. Also the cases the WebGPU device refuses by name (native/DEVICE.md item 148): a
spread of zero (with the Glossy Direct pass) and of 1e-6 (in a volume too), axes of infinities without a NaN (the squared
lengths of normal 1e-23-scale axes underflow), and the light tree at a transmissive point (the glass sphere kept).
Usage: python3 native/oracle/scenes/author_degenerate_lights.py [--check]."""
import argparse
import json
from pathlib import Path

import author_sampling as room

HERE = Path(__file__).resolve().parent/'lamps'
SOURCE = 'native/oracle/scenes/author_degenerate_lights.py (the sampling family room)'
# Emits downwards (-y) from y = 1.6; the u axis along x, the v axis along -z.
DOWN = [1, 0, 0, 0.0, 0, 0, 1, 1.6, 0, -1, 0, 0.5]
# The same light with both axes scaled by 1e-23 (sizes 1).
DOWN_TINY = [1e-23, 0, 0, 0.0, 0, 0, 1, 1.6, 0, -1e-23, 0, 0.5]
# Axes 1e-23 * (0.6, 0.48, 0.64) and 1e-23 * (0.8, -0.36, -0.48): orthonormal directions, every component a normal float, the
# squared lengths underflow to 0, so normalize_len gives axes of infinities without a NaN; emits along -column 2 = (0, -0.8, 0.6).
SKEW_TINY = [6e-24, 8e-24, 0, 0.0, 4.8e-24, -3.6e-24, 0.8, 1.6, 6.4e-24, -4.8e-24, -0.6, 0.5]
GLOSSY_DIRECT = [('glossy_direct', 'GlossDir')]


def degenerate(scene_id, description, tfm, sizeu, sizev, ellipse=False, spread=None, strength=6.0, tree=False, emission=None, keep_glass=False,
               passes=()):
    document = room.scene(scene_id, description, {'use_light_tree': tree}, samples=16)
    document['sourceFixture'] = SOURCE
    if emission is None:
        emission = room.shader('degenerate-emission', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.9, 0.7], 'strength': 1}}],
                               [['e', 'Emission', 'output', 'Surface']])
    values = {'strength': [strength]*3, 'use_mis': True, 'cast_shadow': True, 'max_bounces': 1024, 'is_enabled': True, 'normalize': True,
              'used_shaders': ['degenerate-emission'], 'sizeu': sizeu, 'sizev': sizev, 'ellipse': ellipse}
    if spread is not None:
        values['spread'] = spread
    if tree and not keep_glass:
        # No transmissive closure: at a transmissive point the CPU's light tree weighs the light with infinite importance
        # (refused on the device by name, native/DEVICE.md item 148); the glass sphere becomes a second glossy one.
        for n in document['nodes']:
            if n['handle'] == 'sphere-1':
                n['set']['used_shaders'] = ['glossy']
    document['nodes'] += [emission, {'handle': 'degenerate', 'create': 'arealight', 'set': values},
                          {'handle': 'degenerate-object', 'create': 'object', 'set': {'geometry': 'degenerate', 'tfm': tfm}}]
    document['nodes'] += [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name}} for kind, name in passes]
    return document


def volume(document):
    """A thin scattering volume in the world (volume direct light from both lamps)."""
    for n in document['nodes']:
        if n['handle'] == 'world':
            n['graph']['nodes'].append({'handle': 'v', 'type': 'scatter_volume', 'set': {'density': 0.05, 'color': [0.8, 0.8, 0.8]}})
            n['graph']['connect'].append(['v', 'Volume', 'output', 'Volume'])
    return document


def scenes():
    parametric = room.shader('degenerate-emission', [{'handle': 'g', 'type': 'geometry'}, {'handle': 'e', 'type': 'emission', 'set': {'strength': 1.0}}],
                             [['g', 'Parametric', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']])
    return {
        'area-degenerate-rect.json': degenerate('lamps/area-degenerate-rect', 'A rectangle light whose u size (1e-23) underflows: no sample, an unlit strip for camera rays, beside the sphere lamp',
                                                DOWN, 1e-23, 1.0),
        'area-degenerate-ellipse-spread.json': degenerate('lamps/area-degenerate-ellipse-spread', 'An ellipse light with spread 1 whose u size (1e-23) underflows, beside the sphere lamp',
                                                          DOWN, 1e-23, 1.0, ellipse=True, spread=1.0),
        'area-degenerate-disk-plane.json': degenerate('lamps/area-degenerate-disk-plane', 'An ellipse light whose two axes (object scale 1e-23) underflow: an emitting plane for camera rays, beside the sphere lamp',
                                                      DOWN_TINY, 1.0, 1.0, ellipse=True, strength=2.0),
        'area-degenerate-lamp-shader.json': degenerate('lamps/area-degenerate-lamp-shader', 'The emitting plane of a degenerate ellipse light with its Parametric output as emission colour',
                                                       DOWN_TINY, 1.0, 1.0, ellipse=True, strength=2.0, emission=parametric),
        'area-degenerate-volume.json': volume(degenerate('lamps/area-degenerate-volume', 'A rectangle light whose u size underflows, beside the sphere lamp, in a scattering world volume',
                                                        DOWN, 1e-23, 1.0)),
        'area-degenerate-tree.json': degenerate('lamps/area-degenerate-tree', 'A rectangle light whose u size underflows, with the light tree (zero importance), the glass sphere glossy',
                                                DOWN, 1e-23, 1.0, tree=True),
        'area-degenerate-spread-zero.json': degenerate('lamps/area-degenerate-spread-zero', 'A rectangle light with spread 0 whose u size (1e-23) underflows, beside the sphere lamp, with the Glossy Direct pass',
                                                       DOWN, 1e-23, 1.0, spread=0.0, passes=GLOSSY_DIRECT),
        'area-degenerate-ellipse-spread-zero.json': degenerate('lamps/area-degenerate-ellipse-spread-zero', 'An ellipse light with spread 0 whose u size (1e-23) underflows, beside the sphere lamp, with the Glossy Direct pass',
                                                               DOWN, 1e-23, 1.0, ellipse=True, spread=0.0, passes=GLOSSY_DIRECT),
        'area-degenerate-spread-tiny.json': degenerate('lamps/area-degenerate-spread-tiny', 'A rectangle light with spread 1e-6 whose u size (1e-23) underflows, beside the sphere lamp',
                                                       DOWN, 1e-23, 1.0, spread=1e-6),
        'area-degenerate-spread-tiny-volume.json': volume(degenerate('lamps/area-degenerate-spread-tiny-volume', 'A rectangle light with spread 1e-6 whose u size underflows, beside the sphere lamp, in a scattering world volume',
                                                                     DOWN, 1e-23, 1.0, spread=1e-6)),
        'area-degenerate-infinite-axis.json': degenerate('lamps/area-degenerate-infinite-axis', 'A rectangle light whose two object axes (1e-23 scale, every component normal) have squared lengths that underflow: axes of infinities without a NaN',
                                                         SKEW_TINY, 1.0, 1.0),
        'area-degenerate-tree-transmissive.json': degenerate('lamps/area-degenerate-tree-transmissive', 'A rectangle light whose u size underflows, with the light tree, the glass sphere kept (the light tree at a transmissive point)',
                                                             DOWN, 1e-23, 1.0, tree=True, keep_glass=True),
    }


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
