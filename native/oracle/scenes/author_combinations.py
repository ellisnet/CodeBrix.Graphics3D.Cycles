#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the feature-combination cbcOracleScene files (scenes/combinations/) byte for byte.

Each scene is a committed oracle scene with one or more integrator features added, so that a
combination no other scene renders is judged CPU against WGSL: the light tree (the consumer
default) with transparent shadows, mesh lights, AO, the shadow catcher, glass, Principled BSDF,
image textures and normal maps, object and deformation motion, camera motion, a textured
(non-constant) emitter and shadow terminator offsets; AO with the light tree, light linking,
glass/Principled materials, camera motion, shadow terminator offsets and transparent shadows at
ao_bounces > 0; transparent shadows with light linking and with glass/Principled receivers.
The source scenes are read from the committed files, never changed; a source edit changes the
variants on the next run. Usage: python3 native/oracle/scenes/author_combinations.py [--check]."""
import argparse
import copy
import json
import math
import shutil
from pathlib import Path

import author_starters as s

HERE = Path(__file__).resolve().parent
OUT = HERE/'combinations'
# Area lights and spots emit along their object's -Z; DOWN points -Z down, BACK points it along +Z.
DOWN = lambda x, y, z: [1, 0, 0, x, 0, 0, 1, y, 0, -1, 0, z]
BACK = lambda x, y, z: [-1, 0, 0, x, 0, 1, 0, y, 0, 0, -1, z]


def load(name):
    return json.loads((HERE/name).read_text())


def node(doc, handle):
    return next(n for n in doc['nodes'] if n['handle'] == handle)


def integrator(doc):
    return next(n for n in doc['nodes'] if n.get('scene') == 'integrator')['set']


def derive(doc, identifier, description):
    doc = copy.deepcopy(doc)
    doc['description'] = description+' (derived from '+doc['id']+')'
    doc['sourceFixture'] = 'native/oracle/scenes/'+doc['id']+'.json, '+doc['sourceFixture']
    doc['id'] = 'combinations/'+identifier
    return doc


def tree(doc):
    integrator(doc)['use_light_tree'] = True
    return doc


def add(doc, nodes):
    doc['nodes'] += nodes
    return doc


def shader(handle, nodes, connect):
    return dict(handle=handle, create='shader', graph=dict(nodes=nodes, connect=connect), set=dict(emission_sampling_method='none'))


def replace_shader(doc, handle, replacement):
    index = next(i for i, n in enumerate(doc['nodes']) if n['handle'] == handle)
    replacement = dict(replacement, handle=handle)
    doc['nodes'][index] = replacement
    return doc


def glass_mix(handle):
    # GGX glass mixed with a diffuse lobe: a transmissive BSDF at every hit (the light tree's
    # full-sphere importance, PATH_RAY_MIS_HAD_TRANSMISSION on the following bounce).
    return shader(handle, [dict(handle='g', type='glass_bsdf', set=dict(distribution='ggx', roughness=0.25, IOR=1.45, color=[0.9, 0.95, 1.0])),
                           dict(handle='d', type='diffuse_bsdf', set=dict(color=[0.55, 0.32, 0.18], roughness=0.31)),
                           dict(handle='m', type='mix_closure', set=dict(fac=0.35))],
                  [['g', 'BSDF', 'm', 'Closure1'], ['d', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def principled(handle):
    return shader(handle, [dict(handle='p', type='principled_bsdf', set=dict(
        distribution='multi_ggx', base_color=[0.8, 0.5, 0.25], metallic=0.1, roughness=0.3, ior=1.45, transmission_weight=0.6,
        coat_weight=0.3, coat_roughness=0.1, coat_ior=1.5, sheen_weight=0.4, sheen_roughness=0.3, subsurface_weight=0.0))],
                  [['p', 'BSDF', 'output', 'Surface']])


def lamp(handle, kind, settings, tfm, strength):
    return s.light(handle, kind, settings, tfm, strength)


def link(doc, receivers, memberships):
    for handle, receiver in receivers.items():
        node(doc, handle)['set']['receiver_light_set'] = receiver
    for handle, mask in memberships.items():
        node(doc, handle)['set']['light_set_membership'] = mask
    return doc


def camera_motion(doc, steps=3):
    # Three camera transforms around the scene camera (translation and a small yaw), shutter 0.6.
    camera = next(n for n in doc['nodes'] if n.get('scene') == 'camera')['set']
    m = camera['matrix']
    motion = []
    for i in range(steps):
        u = 2*i/(steps-1)-1
        a = 0.06*u
        c, sn = math.cos(a), math.sin(a)
        # matrix * translate(0.25u, 0.08u^2, 0) * rotate_y(a)
        local = [[c, 0, sn, 0.25*u], [0, 1, 0, 0.08*u*u], [-sn, 0, c, 0]]
        rows = [m[0:4], m[4:8], m[8:12]]
        out = []
        for r in rows:
            out += [round(sum(r[k]*local[k][j] for k in range(3)), 7) for j in range(3)]
            out += [round(sum(r[k]*local[k][3] for k in range(3))+r[3], 7)]
        motion.append(out)
    camera['motion'] = motion
    camera['shuttertime'] = 0.6
    integrator(doc)['motion_blur'] = True
    return doc


def terminator_sphere(handle, material, tfm, geometry, shading):
    # A coarse smooth sphere (few large triangles), the object offsets set explicitly.
    positions, indices = s.sphere(1.0, 9, 5)
    nodes = s.mesh(handle, positions, indices, material, tfm)
    nodes[0]['set']['smooth'] = [True]*(len(indices)//3)
    nodes[1]['set'].update(shadow_terminator_geometry_offset=geometry, shadow_terminator_shading_offset=shading)
    return nodes


def ao_extra_lamp():
    return lamp('area', 'arealight', dict(sizeu=1.0, sizev=1.0, ellipse=False, spread=1.2, is_portal=False), DOWN(-2.5, 2.5, -1.0), [40, 50, 60])


def enclosure_panel():
    # The emissive back-wall panel (two sampled triangles, front and back) of the enclosure below.
    panel = s.mesh('panel', [1.0, 0.0, 7.9, 1.0, 3.0, 7.9, 4.0, 3.0, 7.9, 4.0, 0.0, 7.9], [0, 1, 2, 0, 2, 3], 'panel-emission')
    emission = dict(handle='panel-emission', create='shader', graph=dict(nodes=[dict(handle='e', type='emission', set=dict(color=[1, 0.85, 0.7], strength=3.0))],
                    connect=[['e', 'Emission', 'output', 'Surface']]), set=dict(emission_sampling_method='front_back'))
    return [emission]+panel


def enclosure_lamps(panel_emitter=True):
    # glass-render / textured-plane enclosure (x -6..6, y -4..5, z -4..8, camera at z -10): lamps,
    # a spot from the camera side and (glass) an emissive back-wall panel seen through the glass objects.
    return ((enclosure_panel() if panel_emitter else [])+lamp('point', 'pointlight', dict(radius=0.4, is_sphere=True), s.translate(-3, 3.5, -2), [300, 260, 220])
            + lamp('area', 'arealight', dict(sizeu=2.0, sizev=1.5, ellipse=False, spread=1.2, is_portal=False), DOWN(2.5, 4.8, 3.0), [250, 250, 300])
            + lamp('spot', 'spotlight', dict(radius=0.2, is_sphere=True, angle=0.9, smooth=0.3), BACK(0.5, 1.0, -6.0), [400, 380, 360]))


def copy_image(doc, folder):
    for image in doc['images']:
        target = image['file']
        shutil.copyfile(HERE/folder/target, OUT/target)
    return doc


def scenes():
    out = {}

    def put(doc):
        out[doc['id'].split('/', 1)[1]+'.json'] = doc

    # Finding 1: the light tree with everything else.
    for mode in range(8):
        put(tree(derive(load('linux-gates/transparent-lighting-%d.json' % mode), 'tree-transparent-lighting-%d' % mode,
                        'Light tree with transparent shadows, transparent-lighting mode %d' % mode)))
    for mode in (1, 2, 3, 5):
        put(tree(derive(load('linux-gates/triangle-lighting-%d.json' % mode), 'tree-triangle-lighting-%d' % mode,
                        'Light tree with sampled mesh lights, triangle-lighting mode %d' % mode)))
    for name in ('object-motion-4', 'deforming-motion-5'):
        put(tree(derive(load('linux-gates/%s.json' % name), 'tree-'+name, 'Light tree with motion blur and a normal map, '+name)))
    glass = tree(derive(load('linux-gates/glass-render.json'), 'tree-glass-render',
                        'Light tree with the twenty glass/refraction graphs: point, area and spot lamps and an emissive back panel seen through the glass'))
    put(add(glass, enclosure_lamps()))
    textured = tree(derive(load('linux-gates/textured-plane.json'), 'tree-textured-plane-normal-map',
                           'Light tree with the cubic periodic image texture on the enclosure, an object-space normal map on it, and the enclosure point, area and spot lamps'))
    wall = node(textured, 'wall')['graph']
    wall['nodes'].append(dict(handle='n', type='normal_map', set=dict(space='object', color=[0.55, 0.42, 0.97], strength=0.35)))
    wall['connect'].append(['n', 'Normal', 'd', 'Normal'])
    # No emissive panel here: inside this enclosure the panel's plane meets the walls, and wall points
    # a few hundredths off that plane sample it edge-on, where a one-ulp difference in the shading point
    # decides the spherical-triangle sample (native/DEVICE.md open items).
    put(copy_image(add(textured, enclosure_lamps(False)), 'linux-gates'))
    # Lambert enclosure lit only by the sampled emissive panel (flat CDF): the bounce-1 shading points
    # on the walls sample the panel nearly edge-on, so the GPU must write the bounce direction with the
    # host's normalize(bsdf_wo) and bsdf_eval / bsdf_pdf rounding (native/DEVICE.md item 27).
    panel_only = derive(load('linux-gates/textured-plane.json'), 'enclosure-emissive-panel',
                        'Lambert enclosure lit only by a sampled emissive back-wall panel, without the prepare() graphs and the image texture')
    panel_only['nodes'] = [n for n in panel_only['nodes'] if not n['handle'].startswith(('graph-', 'prepared-')) and n['handle'] not in ('shared', 'copy')]
    panel_only['images'] = []
    replace_shader(panel_only, 'wall', shader('wall', [dict(handle='d', type='diffuse_bsdf', set=dict(color=[0.6, 0.65, 0.7], roughness=0.0))],
                                              [['d', 'BSDF', 'output', 'Surface']]))
    put(add(panel_only, enclosure_panel()))
    # The same panel-lit enclosure with Oren-Nayar, translucent and multi-closure (mix) walls: the
    # Oren-Nayar intensity, the translucent bounce and the closure pick / MIS pdf sums must round as
    # the host kernel does (scalar divisions, unfused products) for the edge-on panel samples.
    walls = (('oren-nayar', 'Oren-Nayar (roughness 0.5)', [dict(handle='d', type='diffuse_bsdf', set=dict(color=[0.6, 0.65, 0.7], roughness=0.5))],
              [['d', 'BSDF', 'output', 'Surface']]),
             ('translucent', 'translucent', [dict(handle='t', type='translucent_bsdf', set=dict(color=[0.6, 0.65, 0.7]))],
              [['t', 'BSDF', 'output', 'Surface']]),
             ('mix', 'Oren-Nayar (roughness 0.35) / translucent / Lambert mix', [
                 dict(handle='d', type='diffuse_bsdf', set=dict(color=[0.6, 0.65, 0.7], roughness=0.35)),
                 dict(handle='t', type='translucent_bsdf', set=dict(color=[0.5, 0.45, 0.4])),
                 dict(handle='l', type='diffuse_bsdf', set=dict(color=[0.7, 0.6, 0.5], roughness=0.0)),
                 dict(handle='m', type='mix_closure', set=dict(fac=0.3)), dict(handle='n', type='mix_closure', set=dict(fac=0.45))],
              [['d', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'n', 'Closure1'], ['l', 'BSDF', 'n', 'Closure2'],
               ['n', 'Closure', 'output', 'Surface']]))
    for suffix, label, nodes, connect in walls:
        variant = derive(load('linux-gates/textured-plane.json'), 'enclosure-emissive-panel-'+suffix,
                         label[0].upper()+label[1:]+' enclosure lit only by a sampled emissive back-wall panel, without the prepare() graphs and the image texture')
        variant['nodes'] = [n for n in variant['nodes'] if not n['handle'].startswith(('graph-', 'prepared-')) and n['handle'] not in ('shared', 'copy')]
        variant['images'] = []
        replace_shader(variant, 'wall', shader('wall', nodes, connect))
        put(add(variant, enclosure_panel()))
    for name in ('fast-gi-additive', 'ao-node'):
        put(add(tree(derive(load('ao/%s.json' % name), 'tree-'+name, 'Light tree with AO, '+name+', a second (area) lamp')), ao_extra_lamp()))
    for name in ('catcher-point-light', 'catcher-approximate'):
        put(add(tree(derive(load('shadow-catcher/%s.json' % name), 'tree-'+name, 'Light tree with the shadow catcher, '+name+', a second (area) lamp')),
                lamp('area', 'arealight', dict(sizeu=1.0, sizev=0.8, ellipse=False, spread=1.2, is_portal=False), DOWN(-1.8, 2.4, 0.5), [40, 45, 50])))
    base = load('light-tree/lamps-and-mesh-lights.json')
    put(replace_shader(derive(base, 'tree-glass-sphere', 'Light tree, the GGX sphere replaced by a glass/diffuse mix'), 'ball', glass_mix('ball')))
    put(replace_shader(derive(base, 'tree-principled', 'Light tree, the sphere Principled BSDF (transmission, coat, sheen) and the floor glass/diffuse mix'),
                       'ball', principled('ball')))
    replace_shader(out['tree-principled.json'], 'floor-material', glass_mix('floor-material'))
    textured_emitter = derive(base, 'tree-textured-emitter', 'Light tree with a non-constant (checker-textured) emissive panel')
    replace_shader(textured_emitter, 'panel-emission', dict(create='shader', graph=dict(nodes=[
        dict(handle='tc', type='texture_coordinate'), dict(handle='c', type='checker_texture', set=dict(color1=[1, 0.9, 0.6], color2=[0.2, 0.4, 1], scale=3.0)),
        dict(handle='e', type='emission', set=dict(strength=4.0))], connect=[['tc', 'Object', 'c', 'Vector'], ['c', 'Color', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']]),
        set=dict(emission_sampling_method='front')))
    put(textured_emitter)
    put(camera_motion(derive(base, 'tree-camera-motion', 'Light tree with camera motion blur (three camera transforms, shutter 0.6)')))
    terminator = derive(base, 'tree-shadow-terminator', 'Light tree with a coarse smooth sphere and shadow terminator geometry 0.1 / shading 0.5 offsets')
    put(add(terminator, terminator_sphere('coarse', 'floor-material', [0.7, 0, 0, 1.4, 0, 0.7, 0, -0.3, 0, 0, 0.7, 0.4], 0.1, 0.5)))
    # Finding 4: AO with light linking, glass/Principled, camera motion, terminator offsets, transparent shadows at ao_bounces > 0.
    for name in ('fast-gi-additive', 'ao-node'):
        doc = add(derive(load('ao/%s.json' % name), 'link-'+name, 'Light linking with AO, '+name+': the floor receives only the area lamp'), ao_extra_lamp())
        put(link(doc, {'floor-object': 1}, {'lamp-object': 0b001, 'area-object': 0b011}))
    put(replace_shader(derive(load('ao/fast-gi-additive.json'), 'glass-fast-gi-additive', 'fast GI additive AO with a glass/diffuse centre quad'),
                       'center-material', glass_mix('center-material')))
    put(replace_shader(derive(load('ao/ao-node.json'), 'principled-ao-node', 'AO node with a Principled BSDF (transmission, coat, sheen) floor'),
                       'floor-material', principled('floor-material')))
    for name in ('fast-gi-additive', 'ao-node'):
        put(camera_motion(derive(load('ao/%s.json' % name), 'camera-motion-'+name, 'Camera motion blur with AO, '+name)))
    put(add(derive(load('ao/fast-gi-additive.json'), 'terminator-fast-gi-additive', 'fast GI additive AO with a coarse smooth sphere and shadow terminator offsets'),
            terminator_sphere('coarse', 'center-material', [0.8, 0, 0, 1.9, 0, 0.8, 0, -1.2, 0, 0, 0.8, -0.8], 0.1, 0.5)))
    replace = derive(load('ao/fast-gi-additive.json'), 'replace-transparent-fast-gi', 'fast GI in replace mode (ao_bounces 1) with transparent shadows through a transparent/diffuse pane')
    integrator(replace).update(ao_additive_factor=0.0, ao_factor=0.5, ao_bounces=1)
    pane = shader('pane-material', [dict(handle='d', type='diffuse_bsdf', set=dict(color=[0.55, 0.32, 0.18], roughness=0.31)),
                                    dict(handle='t', type='transparent_bsdf', set=dict(color=[0.8, 0.9, 0.7])), dict(handle='m', type='mix_closure', set=dict(fac=0.55))],
                  [['d', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])
    put(add(replace, [pane]+s.mesh('pane', [-2.9, 0.6, -1.8, 2.9, 0.6, -1.8, 2.9, 0.6, 2.8, -2.9, 0.6, 2.8], [0, 1, 2, 0, 2, 3], 'pane-material')))
    put(tree(derive(out['replace-transparent-fast-gi.json'], 'tree-replace-transparent-fast-gi', 'Light tree with fast GI replace mode and transparent shadows')))
    # Finding 5: transparent shadows with light linking (flat CDF and tree) and glass/Principled receivers.
    links = ({'floor-object': 1, 'center-object': 2}, {'light-0-object': 0b011, 'light-1-object': 0b101, 'light-2-object': 0b111})
    for mode in (4, 5):
        put(link(derive(load('linux-gates/transparent-lighting-%d.json' % mode), 'link-transparent-lighting-%d' % mode,
                        'Light linking (flat CDF) with transparent shadows, transparent-lighting mode %d' % mode), *links))
    for mode in (6, 7):
        put(link(tree(derive(load('linux-gates/transparent-lighting-%d.json' % mode), 'tree-link-transparent-lighting-%d' % mode,
                             'Light linking on the light tree with transparent shadows, transparent-lighting mode %d' % mode)), *links))
    for mode, kind, use_tree in ((4, 'glass', False), (5, 'principled', False), (6, 'glass', True), (7, 'principled', True)):
        doc = derive(load('linux-gates/transparent-lighting-%d.json' % mode), ('tree-' if use_tree else '')+'%s-receiver-transparent-lighting-%d' % (kind, mode),
                     ('Light tree, ' if use_tree else '')+'transparent shadows onto %s receivers (floor and centre), transparent-lighting mode %d' % (kind, mode))
        replace_shader(doc, 'mixed', glass_mix('mixed') if kind == 'glass' else principled('mixed'))
        put(tree(doc) if use_tree else doc)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    for name, document in sorted(scenes().items()):
        text = json.dumps(document, indent=1)+'\n'
        path = OUT/name
        if args.check:
            if path.read_text() != text:
                raise SystemExit('Scene differs: '+str(path))
        else:
            path.write_text(text)
        print(name, 'ok')


if __name__ == '__main__':
    main()
