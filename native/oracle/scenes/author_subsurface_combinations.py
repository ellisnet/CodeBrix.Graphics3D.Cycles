#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the subsurface combination oracle scenes (scenes/subsurface/, the files named below) byte for byte.

The sampling family's room of author_subsurface.py with subsurface scattering together with other features: film passes
(light, data, denoising feature, AOV and cryptomatte passes) and Open Image Denoise, light and shadow linking, holdout
objects and the shadow catcher, object and deformation motion blur, volumes crossing and enclosing the spheres, and MNEE
caustics on subsurface receivers. Every scene carries both families of random walk and Christensen-Burley (disk) paths,
through the public C API.
Usage: python3 native/oracle/scenes/author_subsurface_combinations.py [--check]."""
import argparse
import json

import author_passes as passes
import author_subsurface as subsurface

HERE = subsurface.HERE
SOURCE = 'native/oracle/scenes/author_subsurface_combinations.py (subsurface spheres in the sampling family room, with {})'


def scene(scene_id, description, middle, right, what, integrator=None, film=None, extra=()):
    """author_subsurface.scene with integrator and film settings and extra nodes appended."""
    document = subsurface.scene(scene_id, description, middle, right)
    document['sourceFixture'] = SOURCE.format(what)
    for entry in document['nodes']:
        if entry['handle'] == 'integrator':
            entry['set'].update(integrator or {})
        if entry['handle'] == 'film':
            entry['set'].update(film or {})
    document['nodes'] = document['nodes']+list(extra)
    return document


def burley_node():
    return subsurface.node('sss', {'method': 'burley', 'scale': 0.6})


def walk_node():
    return subsurface.node('sss-walk', {'subsurface_anisotropy': 0.3})


def bumped(handle, values, aov=None):
    """A Subsurface Scattering node whose Normal comes from a Bump node (SD_HAS_BSSRDF_BUMP: the exit point evaluates the shader
    for the bump-mapped BSSRDF normal), with optional AOV outputs (colour from the noise, value from the noise factor)."""
    settings = {'method': 'random_walk', 'color': [0.85, 0.7, 0.55], 'scale': 0.4, 'radius': [1.0, 0.6, 0.35], 'subsurface_ior': 1.4,
                'subsurface_roughness': 1.0, 'subsurface_anisotropy': 0.0}
    settings.update(values)
    nodes = [{'handle': 'n', 'type': 'noise_texture', 'set': {'scale': 3.0, 'detail': 1.0, 'dimensions': '3D'}},
             {'handle': 'bump', 'type': 'bump', 'set': {'strength': 0.6, 'distance': 0.4}},
             {'handle': 's', 'type': 'subsurface_scattering', 'set': settings}]
    connect = [['n', 'Fac', 'bump', 'Height'], ['bump', 'Normal', 's', 'Normal'], ['s', 'BSSRDF', 'output', 'Surface']]
    if aov:
        nodes += [{'handle': 'ac', 'type': 'aov_output', 'set': {'name': aov[0]}}, {'handle': 'av', 'type': 'aov_output', 'set': {'name': aov[1]}}]
        connect += [['n', 'Color', 'ac', 'Color'], ['n', 'Fac', 'av', 'Value']]
    return subsurface.room.shader(handle, nodes, connect)


def variant_passes():
    return passes.passes([p for p in passes.VARIANT_PASSES if p[0] != 'ao'])


def edit(document, handle, values):
    """Merge values into the 'set' of the node named handle."""
    next(n for n in document['nodes'] if n['handle'] == handle)['set'].update(values)
    return document


def fill_lamp(extra):
    """A second, cooler point lamp on the left (the light and shadow linking scenes give the two lamps different sets)."""
    return [subsurface.room.shader('fill-emitter', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}],
                                   [['e', 'Emission', 'output', 'Surface']]),
            {'handle': 'fill', 'create': 'pointlight', 'set': {'radius': 0.35, 'is_sphere': True, 'use_mis': True, 'strength': [50, 65, 90],
                                                                'used_shaders': ['fill-emitter']}},
            {'handle': 'fill-object', 'create': 'object', 'set': {'geometry': 'fill', 'tfm': [1.0, 0.0, 0.0, -2.4, 0.0, 1.0, 0.0, 1.8, 0.0, 0.0, 1.0, -1.6],
                                                                   **extra}}]


def moving(scale, offset):
    """A 3x4 object transform: scale on the diagonal, offset in the last column."""
    return [scale, 0.0, 0.0, offset[0], 0.0, scale, 0.0, offset[1], 0.0, 0.0, scale, offset[2]]


def deform(document, handle, amount):
    """Deformation motion for the mesh named handle: three steps, the outer ones the centre positions pushed out and in
    radially from the mesh's centre and shifted (motion triangles; the object keeps its applied transform)."""
    mesh = next(n for n in document['nodes'] if n['handle'] == handle)
    p = mesh['mesh']['positions']
    n = len(p)//3
    center = [sum(p[3*i+k] for i in range(n))/n for k in range(3)]
    def step(s, shift):
        return [subsurface.room.f32(center[k]+(p[3*i+k]-center[k])*s+shift[k]) for i in range(n) for k in range(3)]
    mesh['mesh']['motion'] = {'steps': 3, 'positions': step(1.0-amount, (-0.12, 0.05, 0.0))+list(p)+step(1.0+amount, (0.12, -0.05, 0.08))}
    return document


def scenes():
    crypto = passes.passes([('cryptomatte', name, {}) for name in ('CryptoObject00', 'CryptoObject01', 'CryptoMaterial00', 'CryptoMaterial01',
                                                                     'CryptoAsset00', 'CryptoAsset01')])
    aov = passes.passes([('aov_color', 'Coords', {}), ('aov_value', 'Height', {})])
    out = {
        # Seal point 1: data, denoising, AOV and cryptomatte passes; the denoising features.
        'with-passes.json': scene('subsurface/with-passes', 'Christensen-Burley (middle) and random walk (right) Subsurface Scattering nodes with every light, '
                                  'data and denoising feature pass: the exit points write no data pass and no denoising feature',
                                  burley_node(), walk_node(), 'every light, data and denoising feature pass', extra=variant_passes()),
        'aov-cryptomatte-bump.json': scene('subsurface/aov-cryptomatte-bump', 'Bump-mapped Subsurface Scattering nodes, random walk (middle) and '
                                           'Christensen-Burley (right), with AOV outputs in the subsurface shaders and cryptomatte object, material and asset passes: the exit '
                                           'point evaluates the shader for the bump-mapped normal',
                                           bumped('sss', {}, ('Coords', 'Height')), bumped('sss-burley', {'method': 'burley', 'scale': 0.6}, ('Coords', 'Height')),
                                           'AOV outputs, a bump-mapped BSSRDF normal and cryptomatte passes', film={'cryptomatte_passes': 7, 'cryptomatte_depth': 2},
                                           extra=crypto+aov),
        'aov-only-bump.json': scene('subsurface/aov-only-bump', 'Bump-mapped Subsurface Scattering nodes, random walk (middle) and Christensen-Burley (right), '
                                    'with AOV outputs and no other data pass: the AOV nodes also run at the bump-mapped exit point',
                                    bumped('sss', {}, ('Coords', 'Height')), bumped('sss-burley', {'method': 'burley', 'scale': 0.6}, ('Coords', 'Height')),
                                    'AOV outputs only and a bump-mapped BSSRDF normal', extra=aov),
        'denoise-on.json': scene('subsurface/denoise-on', 'Christensen-Burley (middle) and random walk (right) Subsurface Scattering nodes with Open Image Denoise '
                                 '(prefilter accurate, quality high, start sample 0, CPU device)', burley_node(), walk_node(), 'Open Image Denoise',
                                 integrator={'use_denoise': True, 'denoiser_type': 'openimagedenoise', 'denoiser_prefilter': 'accurate',
                                             'denoiser_quality': 'high', 'denoise_start_sample': 0, 'denoise_use_gpu': False}),
    }
    # Seal point 2: light linking and shadow linking.
    light_linking = scene('subsurface/light-linking', 'Christensen-Burley (middle, receiver set 1) and random walk (right, receiver set 2) spheres lit by '
                          'two lamps: the warm lamp links sets 0 and 1, the cool one sets 0 and 2, so each subsurface sphere and its exit points see one '
                          'lamp', burley_node(), walk_node(), 'light linking', extra=fill_lamp({'light_set_membership': 0b101}))
    edit(edit(edit(light_linking, 'lamp-object', {'light_set_membership': 0b011}), 'sphere-1-object', {'receiver_light_set': 1}), 'sphere-2-object',
         {'receiver_light_set': 2})
    shadow_linking = scene('subsurface/shadow-linking', 'Christensen-Burley (middle, blocker set 1) and random walk (right, blocker set 2) spheres: the warm '
                           'lamp is blocked by sets 0 and 2 only, so the middle sphere casts no shadow from it and its own exit points see it through '
                           'itself; the cool lamp by sets 0 and 1; indirect rays reach the lamps through the dedicated light kernels',
                           burley_node(), walk_node(), 'shadow linking', extra=fill_lamp({'shadow_set_membership': 0b011}))
    edit(edit(edit(shadow_linking, 'lamp-object', {'shadow_set_membership': 0b101}), 'sphere-1-object', {'blocker_shadow_set': 1}), 'sphere-2-object',
         {'blocker_shadow_set': 2})
    out.update({'light-linking.json': light_linking, 'shadow-linking.json': shadow_linking})
    # Seal point 3: holdout objects and the shadow catcher.
    holdout_mix = subsurface.room.shader('sss-holdout', [{'handle': 's', 'type': 'subsurface_scattering', 'set': {'method': 'random_walk', 'color': [0.85, 0.7, 0.55],
                                                                                                                'scale': 0.4, 'radius': [1.0, 0.6, 0.35]}},
                                                         {'handle': 'h', 'type': 'holdout'}, {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.3}}],
                                         [['s', 'BSSRDF', 'm', 'Closure1'], ['h', 'Holdout', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])
    holdout = scene('subsurface/holdout', 'A holdout Christensen-Burley sphere (middle; indirect rays still scatter in it) and a random walk sphere mixed '
                    'with a holdout closure (right), a transparent film', burley_node(), holdout_mix, 'holdout objects')
    edit(edit(holdout, 'sphere-1-object', {'use_holdout': True}), 'bg', {'transparent': True})
    catcher = scene('subsurface/shadow-catcher', 'A shadow catcher floor and a shadow catcher Christensen-Burley sphere (middle) beside a random walk sphere '
                    '(right), a transparent film and the three shadow catcher passes', burley_node(), walk_node(), 'the shadow catcher',
                    film={'use_approximate_shadow_catcher': False},
                    extra=passes.passes([('shadow_catcher', 'Shadow Catcher', {}), ('shadow_catcher_matte', 'Shadow Catcher Matte', {}),
                                         ('shadow_catcher_sample_count', 'Shadow Catcher Sample Count', {})]))
    edit(edit(edit(catcher, 'floor-object', {'is_shadow_catcher': True}), 'sphere-1-object', {'is_shadow_catcher': True}), 'bg', {'transparent': True})
    out.update({'holdout.json': holdout, 'shadow-catcher.json': catcher})
    # Seal point 4: motion blur (object motion and deformation motion).
    object_motion = scene('subsurface/object-motion', 'Moving Christensen-Burley (middle) and random walk (right) spheres (object motion blur: the local '
                          'traversals enter the objects with their motion transforms at the ray time)', burley_node(), walk_node(), 'object motion blur',
                          integrator={'motion_blur': True})
    edit(object_motion, 'sphere-1-object', {'motion': [moving(0.92, (-0.25, 0.1, 0.0)), moving(1.06, (0.25, -0.05, 0.1))]})
    edit(object_motion, 'sphere-2-object', {'motion': [moving(1.05, (0.0, -0.2, -0.1)), moving(0.95, (0.1, 0.2, 0.1))]})
    deformation = scene('subsurface/deformation-motion', 'Deforming Christensen-Burley (middle) and random walk (right) spheres (motion triangles: the '
                        'local traversals intersect the vertices at the ray time)', burley_node(), walk_node(), 'deformation motion blur',
                        integrator={'motion_blur': True})
    deform(deform(deformation, 'sphere-1', 0.08), 'sphere-2', 0.06)
    out.update({'object-motion.json': object_motion, 'deformation-motion.json': deformation})
    # Seal point 5: volumes (the volume stack update of subsurface_scatter).
    crossing = scene('subsurface/volume-crossing', 'An absorbing and scattering box whose faces cut through the Christensen-Burley sphere (middle: '
                     'two faces) and the random walk sphere (right: its top face): subsurface paths whose entry and exit points lie on either side '
                     'of a box face enter or leave the medium', burley_node(), walk_node(), 'a volume crossing the spheres', integrator={'max_volume_bounce': 4},
                     extra=medium_box('medium', (0.6, -1.95, -1.0), (3.5, -0.6, 2.0), 0.35, 0.25))
    enclosed = scene('subsurface/volume-enclosed', 'Christensen-Burley (middle) and random walk (right) spheres wholly inside a scattering box: every '
                     'subsurface path starts and ends in the medium', burley_node(), walk_node(), 'a volume enclosing the spheres',
                     integrator={'max_volume_bounce': 4}, extra=medium_box('medium', (-1.2, -1.95, -0.8), (3.5, 0.3, 1.6), 0.15, 0.3))
    out.update({'volume-crossing.json': crossing, 'volume-enclosed.json': enclosed})
    # Seal point 6: MNEE caustics (point light) on subsurface receivers.
    caustic = scene('subsurface/mnee-receivers', 'A glass caustic caster (left) and the point lamp casting caustics onto the Christensen-Burley (middle) '
                    'and random walk (right) spheres and the floor, all caustic receivers: subsurface exit points go through MNEE',
                    burley_node(), walk_node(), 'MNEE caustics')
    edit(edit(edit(edit(edit(caustic, 'sphere-0', {'used_shaders': ['glass']}), 'sphere-0-object', {'is_caustics_caster': True}),
                    'sphere-1-object', {'is_caustics_receiver': True}), 'sphere-2-object', {'is_caustics_receiver': True}), 'floor-object',
         {'is_caustics_receiver': True})
    edit(caustic, 'lamp', {'use_caustics': True})
    out['mnee-receivers.json'] = caustic
    return out


def medium_box(handle, lo, hi, absorption, scatter):
    """A box mesh with an absorption plus scatter volume shader (no surface), from corner lo to corner hi."""
    positions = [c for k in range(8) for c in ((hi if k & 1 else lo)[0], (hi if k & 2 else lo)[1], (hi if k & 4 else lo)[2])]
    indices = [0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7]
    shader = {'handle': handle, 'create': 'shader', 'graph': {'nodes': [
        {'handle': 'a', 'type': 'absorption_volume', 'set': {'density': absorption, 'color': [0.8, 0.65, 0.4]}},
        {'handle': 's', 'type': 'scatter_volume', 'set': {'density': scatter, 'color': [0.75, 0.8, 0.9], 'anisotropy': 0.3}},
        {'handle': 'add', 'type': 'add_closure'}],
        'connect': [['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'output', 'Volume']]},
        'set': {'emission_sampling_method': 'none', 'volume_sampling_method': 'multiple_importance'}}
    return [shader, {'handle': handle+'-box', 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices}, 'set': {'used_shaders': [handle]}},
            {'handle': handle+'-box-object', 'create': 'object', 'set': {'geometry': handle+'-box'}}]


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
