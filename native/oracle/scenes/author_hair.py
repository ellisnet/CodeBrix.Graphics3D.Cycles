#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the hair closure oracle scenes (scenes/hair/) byte for byte.

The Hair BSDF node (reflection, transmission) and the Principled Hair BSDF node's Chiang model in its three colour
parametrizations, on the three curve shapes and on triangles, in the curve family's room, through the public C API.
Usage: python3 native/oracle/scenes/author_hair.py [--check]."""
import argparse
import json
from pathlib import Path

import author_curves as curves
import author_sampling as room

HERE = Path(__file__).resolve().parent/'hair'
f32 = room.f32
base = curves.base
hair = curves.hair
place = curves.place
with_nodes = curves.with_nodes
BROWN = [0.45, 0.27, 0.12]
SHAPE_NAME = {'ribbon': 'ribbon', 'thick': 'thick', 'thick_linear': 'linear'}


def scene_id(name):
    return 'hair/'+name


def hair_bsdf(handle, component, u=0.2, v=0.3, offset=0.1, color=None):
    return room.single(handle, 'hair_bsdf', {'component': component, 'roughness_u': u, 'roughness_v': v, 'offset': offset, 'color': color or BROWN})


def chiang_values(parametrization, **values):
    out = {'model': 'Chiang', 'parametrization': parametrization, 'roughness': 0.3, 'radial_roughness': 0.3, 'ior': 1.55, 'offset': 0.035}
    out.update(values)
    return out


def chiang(handle, parametrization, **values):
    return room.single(handle, 'principled_hair_bsdf', chiang_values(parametrization, **values))


DIRECT = 'Direct coloring'
MELANIN = 'Melanin concentration'
ABSORPTION = 'Absorption coefficient'


def random_attribute(curves_count, keys):
    return [{'name': 'curve_random', 'value': 'float', 'domain': 'curve', 'flags': ['standard'], 'data': [f32((c*0.618034) % 1.0) for c in range(curves_count)]}]


def on_curves(name, description, material, shape='thick', attributes=None, integrator=None, extra=(), samples=8):
    document = base(scene_id(name), description, integrator=integrator, samples=samples)
    return with_nodes(document, material, hair('tuft', material['handle'], shape, attributes=attributes), place('tuft-object', 'tuft'), *extra)


def on_triangles(name, description, material, integrator=None, extra=(), samples=8):
    """A sphere (triangles): the Hair BSDF takes its tangent from dPdv and no offset, Chiang works on dPdu and the geometric normal."""
    document = base(scene_id(name), description, integrator=integrator, samples=samples)
    return with_nodes(document, material, *room.sphere('sphere', material['handle'], (0.0, -0.8, 0.4), 1.4), *extra)


def passes(*pairs):
    return [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name}} for kind, name in pairs]


def scenes():
    out = {}
    for shape, name in SHAPE_NAME.items():
        out['reflection-'+name+'.json'] = on_curves('reflection-'+name, 'Hair BSDF reflection on '+name+' curves', hair_bsdf('hair-reflection', 'reflection'), shape)
        out['transmission-'+name+'.json'] = on_curves('transmission-'+name, 'Hair BSDF transmission on '+name+' curves', hair_bsdf('hair-transmission', 'transmission'), shape)
    out['reflection-rough.json'] = on_curves('reflection-rough', 'Hair BSDF reflection, the roughness clamps', hair_bsdf('hair-reflection', 'reflection', u=0.0001, v=1.0, offset=-0.2))
    # RoughnessV stays at 0.1 here: a sharper azimuthal lobe makes the independent image ill-conditioned (native/DEVICE.md item 78).
    out['transmission-rough.json'] = on_curves('transmission-rough', 'Hair BSDF transmission, the upper roughness clamp along the strand', hair_bsdf('hair-transmission', 'transmission', u=1.0, v=0.1, offset=0.2))
    mix = curves.material('hair-mix', [
        {'handle': 'r', 'type': 'hair_bsdf', 'set': {'component': 'reflection', 'roughness_u': 0.15, 'roughness_v': 0.35, 'offset': 0.05, 'color': BROWN}},
        {'handle': 't', 'type': 'hair_bsdf', 'set': {'component': 'transmission', 'roughness_u': 0.4, 'roughness_v': 0.2, 'offset': -0.05, 'color': [0.8, 0.5, 0.3]}},
        {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.5}},
    ], [['r', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])
    out['reflection-transmission-mix-thick.json'] = on_curves('reflection-transmission-mix-thick', 'Hair BSDF reflection and transmission mixed: MIS between the two', mix)
    tangent = curves.material('hair-tangent', [
        {'handle': 'g', 'type': 'geometry', 'set': {}},
        {'handle': 'r', 'type': 'hair_bsdf', 'set': {'component': 'reflection', 'roughness_u': 0.3, 'roughness_v': 0.3, 'offset': 0.1, 'color': BROWN}},
    ], [['g', 'Tangent', 'r', 'Tangent'], ['r', 'BSDF', 'output', 'Surface']])
    out['reflection-tangent-thick.json'] = on_curves('reflection-tangent-thick', 'Hair BSDF reflection with a linked tangent', tangent)
    out['reflection-triangle.json'] = on_triangles('reflection-triangle', 'Hair BSDF reflection on triangles: the pinned kernel takes T from dPdv and drops the offset', hair_bsdf('hair-reflection', 'reflection'))
    out['transmission-triangle.json'] = on_triangles('transmission-triangle', 'Hair BSDF transmission on triangles', hair_bsdf('hair-transmission', 'transmission'))
    transparent = curves.material('hair-transparent', [
        {'handle': 'r', 'type': 'hair_bsdf', 'set': {'component': 'reflection', 'roughness_u': 0.2, 'roughness_v': 0.3, 'offset': 0.1, 'color': BROWN}},
        {'handle': 't', 'type': 'transparent_bsdf', 'set': {'color': [0.8, 0.8, 0.9]}},
        {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.4}},
    ], [['r', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])
    out['reflection-transparent-triangle.json'] = on_triangles('reflection-transparent-triangle', 'Hair BSDF reflection mixed with a Transparent BSDF on triangles', transparent)
    out['reflection-transmission-mix-ribbon.json'] = on_curves('reflection-transmission-mix-ribbon', 'Hair BSDF reflection and transmission mixed on ribbons', mix, 'ribbon')
    # Chiang: the three parametrizations on the three shapes and on triangles.
    params = {'direct': (DIRECT, {'color': [0.8, 0.5, 0.3]}), 'melanin': (MELANIN, {'melanin': 0.7, 'melanin_redness': 0.3}), 'absorption': (ABSORPTION, {'absorption_coefficient': [0.8, 1.2, 2.1]})}
    for label, (parametrization, values) in params.items():
        for shape, name in SHAPE_NAME.items():
            key = 'chiang-'+label+'-'+name
            out[key+'.json'] = on_curves(key, 'Chiang hair, '+parametrization+', on '+name+' curves', chiang('chiang', parametrization, **values), shape)
        key = 'chiang-'+label+'-triangle'
        out[key+'.json'] = on_triangles(key, 'Chiang hair, '+parametrization+', on triangles', chiang('chiang', parametrization, **values))
    out['chiang-melanin-tint-random.json'] = on_curves('chiang-melanin-tint-random', 'Chiang hair: melanin with a tint, Random Color and Random Roughness from the Hair Info random attribute',
        chiang('chiang', MELANIN, **{'melanin': 0.6, 'melanin_redness': 0.7, 'tint': [0.9, 0.6, 0.4], 'random_color': 0.5, 'random_roughness': 0.4}), attributes=random_attribute)
    linked = curves.material('chiang-linked', [
        {'handle': 'i', 'type': 'hair_info', 'set': {}},
        {'handle': 'c', 'type': 'principled_hair_bsdf', 'set': chiang_values(DIRECT, **{'color': [0.7, 0.3, 0.2], 'random_roughness': 0.6})},
    ], [['i', 'Random', 'c', 'Random'], ['c', 'BSDF', 'output', 'Surface']])
    out['chiang-random-linked.json'] = on_curves('chiang-random-linked', 'Chiang hair with a linked Random input', linked, attributes=random_attribute)
    out['chiang-coat-tilt.json'] = on_curves('chiang-coat-tilt', 'Chiang hair: coat, a large cuticle tilt, IOR 1.3', chiang('chiang', MELANIN, **{'melanin': 0.4, 'coat': 0.6, 'offset': -0.12, 'ior': 1.3}), 'ribbon')
    out['chiang-sharp.json'] = on_curves('chiang-sharp', 'Chiang hair: roughness at the clamp (the logistic and the Bessel tails)', chiang('chiang', DIRECT, **{'color': [0.4, 0.5, 0.6], 'roughness': 0.002, 'radial_roughness': 0.002}))
    out['chiang-smooth.json'] = on_curves('chiang-smooth', 'Chiang hair below v = 0.1 (the log Bessel branch)', chiang('chiang', ABSORPTION, **{'absorption_coefficient': [0.05, 0.3, 0.9], 'roughness': 0.05, 'radial_roughness': 0.1}))
    out['chiang-rough.json'] = on_curves('chiang-rough', 'Chiang hair at full roughness', chiang('chiang', MELANIN, **{'melanin': 0.2, 'roughness': 1.0, 'radial_roughness': 1.0}), 'thick_linear')
    out['chiang-black-lane.json'] = on_curves('chiang-black-lane', 'Chiang hair, direct colouring with a zero channel: an infinite absorption in that channel', chiang('chiang', DIRECT, **{'color': [0.0, 0.5, 0.8]}))
    out['chiang-filter-glossy.json'] = on_curves('chiang-filter-glossy', 'Chiang hair under Filter Glossy (bsdf_hair_chiang_blur)', chiang('chiang', DIRECT, **{'color': [0.6, 0.5, 0.4], 'roughness': 0.1, 'radial_roughness': 0.1}), integrator={'filter_glossy': 1.0})
    glossy_mix = curves.material('chiang-mix', [
        {'handle': 'c', 'type': 'principled_hair_bsdf', 'set': chiang_values(MELANIN, **{'melanin': 0.5})},
        {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.8, 0.8, 0.7]}},
        {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.4}},
    ], [['c', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])
    out['chiang-ggx-mix.json'] = on_curves('chiang-ggx-mix', 'Chiang hair mixed with a GGX glossy: MIS across the two', glossy_mix)
    data = passes(('depth', 'Depth'), ('normal', 'Normal'), ('roughness', 'roughness'), ('diffuse_color', 'Diffuse Color'), ('glossy_color', 'Glossy Color'), ('transmission_color', 'Transmission Color'))
    out['chiang-data-passes.json'] = on_curves('chiang-data-passes', 'Data and colour passes on Chiang hair (bsdf_hair_chiang_albedo)', chiang('chiang', MELANIN, **{'melanin': 0.5}), extra=data)
    out['reflection-data-passes.json'] = on_curves('reflection-data-passes', 'Data and colour passes on the Hair BSDF', mix, extra=data)
    features = passes(('denoising_normal', 'Denoising Normal'), ('denoising_albedo', 'Denoising Albedo'), ('denoising_depth', 'Denoising Depth'))
    out['chiang-denoising-features.json'] = on_curves('chiang-denoising-features', 'Denoising feature passes on Chiang hair', chiang('chiang', DIRECT, **{'color': [0.6, 0.4, 0.3]}), extra=features)
    out['chiang-light-tree.json'] = on_curves('chiang-light-tree', 'Chiang hair under the light tree: SR_BSDF_HAS_TRANSMISSION in its importance', chiang('chiang', ABSORPTION, **{'absorption_coefficient': [0.3, 0.5, 1.0]}), integrator={'use_light_tree': True})
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
