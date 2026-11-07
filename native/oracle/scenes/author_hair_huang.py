#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the Principled Hair, Huang model oracle scenes (scenes/hair/huang-*.json) byte for byte.

Tufts of curves in the sampling family's room with the Huang model through the public C API: its three colour
parametrizations, circular and elliptical cross-sections on both sides of 1, lobe weights, roughness and tilt, the Random
and curve_random inputs, near and far field (camera and bounced rays, thin and wide strands), a mix with other closures,
data and denoising passes, and the model on triangles.
Usage: python3 native/oracle/scenes/author_hair_huang.py [--check]."""
import argparse
import json
from pathlib import Path
import author_curves as curves

HERE = Path(__file__).resolve().parent/'hair'
f32 = curves.f32


def huang(handle='hair', **values):
    settings = {'model': 'Huang', 'parametrization': 'Melanin concentration', 'melanin': 0.6, 'melanin_redness': 0.7, 'roughness': 0.3}
    settings.update(values)
    return {'handle': handle, 'type': 'principled_hair_bsdf', 'set': settings}


def tuft_scene(name, description, shape, *nodes, connect=None, attributes=None, curve_args=None, document_args=None, transparent_shadow=False):
    shader = curves.material('huang', [*nodes], connect or [['hair', 'BSDF', 'output', 'Surface']])
    # An elliptical cross-section counts as transparent (the hit may lie outside the projected width): with the shader's
    # transparent shadow on, Cycles evaluates the curve shadow transparency and traces transparent shadows through curves.
    if not transparent_shadow:
        shader['set'] = {**shader.get('set', {}), 'use_transparent_shadow': False}
    document = curves.base('hair/'+name, description, **(document_args or {}))
    return curves.with_nodes(document, shader, curves.hair('tuft', 'huang', shape, attributes=attributes, **(curve_args or {})), curves.place('tuft-object', 'tuft'))


def strand_attributes(curve_count, keys):
    return [{'name': 'curve_random', 'value': 'float', 'domain': 'curve', 'flags': ['standard'], 'data': [f32((c*0.618034) % 1.0) for c in range(curve_count)]}]


def normal_attributes(curve_count, keys):
    """A per key normal for the ellipse's major axis (ATTR_STD_VERTEX_NORMAL)."""
    return [{'name': 'N', 'value': 'normal', 'domain': 'curve_key_normal', 'flags': ['standard'], 'data': [f32(v) for k in range(keys) for v in (0.0, 0.0, 1.0)]}]


def scenes():
    out = {}
    add = lambda name, *args, **kwargs: out.__setitem__(name+'.json', tuft_scene(name, *args, **kwargs))
    add('huang-default-thick', 'The Huang model with its default elliptical cross-section on thick curves, in the lit room', 'thick', huang(aspect_ratio=0.85))
    add('huang-default-shadows-thick', 'The default elliptical cross-section with the shader\'s transparent shadow on: shadow rays through curves', 'thick', huang(aspect_ratio=0.85), transparent_shadow=True)
    add('huang-default-ribbon', 'The Huang model with its default elliptical cross-section on ribbons', 'ribbon', huang(aspect_ratio=0.85))
    add('huang-circular-linear', 'A circular cross-section on thick linear curves', 'thick_linear', huang(aspect_ratio=1.0))
    add('huang-melanin-tint', 'Melanin concentration with a tint on thick curves', 'thick', huang(aspect_ratio=1.0, melanin=0.85, melanin_redness=0.3, tint=[0.9, 0.7, 0.55]))
    add('huang-direct-coloring', 'Direct colouring on thick curves', 'thick', huang(aspect_ratio=1.0, parametrization='Direct coloring', color=[0.55, 0.3, 0.15], roughness=0.5))
    add('huang-absorption', 'The absorption coefficient on thick curves', 'thick', huang(aspect_ratio=1.0, parametrization='Absorption coefficient', absorption_coefficient=[0.6, 1.1, 2.2], roughness=0.2))
    add('huang-ellipse-wide', 'An aspect ratio above 1 (setup switches the axes), with a per key normal', 'thick', huang(aspect_ratio=1.6, roughness=0.25), attributes=normal_attributes)
    add('huang-ellipse-narrow', 'An aspect ratio of 0.4 on ribbons, with a per key normal', 'ribbon', huang(aspect_ratio=0.4, roughness=0.4), attributes=normal_attributes)
    add('huang-lobes', 'Scaled R, TT and TRT lobes', 'thick', huang(aspect_ratio=0.9, R=0.5, TT=1.5, TRT=0.25))
    add('huang-r-only', 'The R lobe alone', 'thick', huang(aspect_ratio=1.0, TT=0.0, TRT=0.0))
    add('huang-tt-only', 'The TT lobe alone', 'thick', huang(aspect_ratio=0.8, R=0.0, TRT=0.0))
    add('huang-smooth-tilted', 'Low roughness and a large tilt', 'thick', huang(aspect_ratio=1.0, roughness=0.06, offset=0.09, ior=1.6))
    add('huang-rough', 'High roughness and a negative tilt', 'thick', huang(aspect_ratio=1.0, roughness=0.85, offset=-0.07, ior=1.4))
    add('huang-random', 'Random roughness and colour from the curve_random attribute', 'thick', huang(aspect_ratio=1.0, random_roughness=0.5, random_color=0.4), attributes=strand_attributes)
    add('huang-near-field', 'Wide strands close to the camera: the near field model on camera rays', 'thick', huang(aspect_ratio=0.85), curve_args={'radius': 0.35, 'curves': 7})
    add('huang-far-field', 'Thin strands: the far field model on most hits', 'thick', huang(aspect_ratio=1.0), curve_args={'radius': 0.015, 'curves': 40})
    add('huang-mix-glossy', 'Mixed with a GGX glossy closure', 'thick', huang(aspect_ratio=1.0), {'handle': 'glossy', 'type': 'glossy_bsdf', 'set': {'roughness': 0.2}}, {'handle': 'mix', 'type': 'mix_closure', 'set': {'fac': 0.4}},
        connect=[['hair', 'BSDF', 'mix', 'Closure1'], ['glossy', 'BSDF', 'mix', 'Closure2'], ['mix', 'Closure', 'output', 'Surface']])
    add('huang-mix-two', 'Two Huang closures: the second closure evaluates after the first in the random stream', 'thick', huang(aspect_ratio=1.0), huang('hair2', aspect_ratio=0.7, roughness=0.5), {'handle': 'add', 'type': 'add_closure'},
        connect=[['hair', 'BSDF', 'add', 'Closure1'], ['hair2', 'BSDF', 'add', 'Closure2'], ['add', 'Closure', 'output', 'Surface']])
    add('huang-mix-principled', 'Mixed with the Principled BSDF', 'thick', huang(aspect_ratio=0.85), {'handle': 'principled', 'type': 'principled_bsdf', 'set': {'base_color': [0.5, 0.4, 0.3], 'roughness': 0.4}}, {'handle': 'mix', 'type': 'mix_closure', 'set': {'fac': 0.5}},
        connect=[['hair', 'BSDF', 'mix', 'Closure1'], ['principled', 'BSDF', 'mix', 'Closure2'], ['mix', 'Closure', 'output', 'Surface']])
    # On triangles: the pinned kernel accepts the closure there, with dPdu as the strand direction.
    triangles = curves.room.scene('hair/huang-triangles', 'The Huang model on the room\'s spheres and floor: the pinned kernel accepts the closure on triangles, with dPdu as the strand direction', {}, samples=8, width=64, height=48)
    triangles['sourceFixture'] = 'native/oracle/scenes/author_hair_huang.py (the sampling family room)'
    # Without the glass sphere: the room with it differs from the CPU in one pixel with or without hair (1.88 times the
    # practical tolerance at x=34, y=20 on the base tree too).
    triangles['nodes'] = [n for n in triangles['nodes'] if not n['handle'].startswith('sphere-1')]
    for node in triangles['nodes']:
        if node['handle'] == 'sphere-2' or node['handle'] == 'sphere-0' or node['handle'] == 'floor':
            node['set']['used_shaders'] = ['huang']
    triangles['nodes'] = [curves.material('huang', [huang(aspect_ratio=1.0)], [['hair', 'BSDF', 'output', 'Surface']])]+triangles['nodes']
    out['huang-triangles.json'] = triangles
    ellipse = curves.room.scene('hair/huang-triangles-ellipse', 'An elliptical cross-section on triangles: the closure aligns with the ray, as for a circle', {}, samples=8, width=64, height=48)
    ellipse['sourceFixture'] = triangles['sourceFixture']
    for node in ellipse['nodes']:
        if node['handle'] == 'sphere-2' or node['handle'] == 'sphere-1':
            node['set']['used_shaders'] = ['huang']
    ellipse['nodes'] = [curves.material('huang', [huang(aspect_ratio=0.6, roughness=0.4)], [['hair', 'BSDF', 'output', 'Surface']])]+ellipse['nodes']
    out['huang-triangles-ellipse.json'] = ellipse
    data = [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name}} for kind, name in
            (('depth', 'Depth'), ('normal', 'Normal'), ('position', 'Position'), ('roughness', 'Roughness'), ('diffuse_color', 'Diffuse Color'), ('glossy_color', 'Glossy Color'))]
    out['huang-passes.json'] = curves.with_nodes(tuft_scene('huang-passes', 'Data and colour passes: the closure\'s albedo in the glossy colour pass', 'thick', huang(aspect_ratio=1.0)), *data)
    denoising = [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name}} for kind, name in
                 (('denoising_normal', 'Denoising Normal'), ('denoising_albedo', 'Denoising Albedo'), ('denoising_depth', 'Denoising Depth'))]
    out['huang-denoising.json'] = curves.with_nodes(tuft_scene('huang-denoising', 'Denoising features: the strand direction in the normal feature', 'thick', huang(aspect_ratio=0.85)), *denoising)
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
