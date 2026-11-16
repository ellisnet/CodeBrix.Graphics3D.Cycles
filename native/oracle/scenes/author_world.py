#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the world oracle scenes (scenes/world/) byte for byte.

The sampling family's room with a ceiling that has a window, under a Sky Texture
world sampled through a background light, through the public C API: background
light portals (an area light with is_portal over the window, and an elliptical
portal at the open side), with and without the importance map (use_mis), with the
light tree and with sun guiding; scenes with two background light objects; the window portal
of zero extent (one size 0, both sizes 0, a size of 1e-23 whose square underflows); and the
Ray Portal BSDF on a panel in front of the spheres.
Usage: python3 native/oracle/scenes/author_world.py [--check]."""
import argparse
import json
from pathlib import Path

import author_sampling as room
import author_sky as sky

HERE = Path(__file__).resolve().parent/'world'
PREETHAM = {'sky_type': 'preetham', 'sun_disc': False, 'sun_direction': [0.35, -0.55, 0.76], 'turbidity': 2.5}
SCATTERING = {'sky_type': 'multiple_scattering', 'sun_disc': True, 'sun_size': 0.2, 'sun_intensity': 0.2, 'sun_elevation': 0.35,
              'sun_rotation': 1.1, 'altitude': 400.0, 'air_density': 1.2, 'aerosol_density': 0.8, 'ozone_density': 1.0}
# The ceiling at y = 3 around a window over x in [-1.5, 1.5], z in [0, 2].
CEILING = [[(-4, 3, -2), (4, 3, -2), (4, 3, 0), (-4, 3, 0)], [(-4, 3, 2), (4, 3, 2), (4, 3, 3), (-4, 3, 3)],
           [(-4, 3, 0), (-1.5, 3, 0), (-1.5, 3, 2), (-4, 3, 2)], [(1.5, 3, 0), (4, 3, 0), (4, 3, 2), (1.5, 3, 2)]]
# Portal transforms (rows): the window portal faces down into the room (direction = -column 2 = (0, -1, 0)); the
# elliptical portal at x = -4 faces +x.
WINDOW = [1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 3.0, 0.0, -1.0, 0.0, 1.0]
SIDE = [0.0, 0.0, -1.0, -4.0, 0.0, 1.0, 0.0, 0.5, 1.0, 0.0, 0.0, 1.0]


def portal(handle, tfm, sizeu, sizev, ellipse=False):
    return [{'handle': handle, 'create': 'arealight', 'set': {'is_portal': True, 'sizeu': sizeu, 'sizev': sizev, 'ellipse': ellipse}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle, 'tfm': tfm}}]


def scene(scene_id, description, values, strength, portals=(), use_mis=True, lights=1, integrator=None, window=(3.0, 2.0)):
    document = room.scene(scene_id, description, integrator or {}, samples=16)
    document['sourceFixture'] = 'native/oracle/scenes/author_world.py (the sampling family room with a ceiling window under a sky)'
    nodes = []
    for entry in document['nodes']:
        if entry['handle'] == 'world':
            nodes.append(sky.sky_world(values, strength))
            for k in range(lights):
                handle = 'world-light' if k == 0 else 'world-light-'+str(k)
                nodes.append({'handle': handle, 'create': 'backgroundlight',
                              'set': {'use_mis': use_mis, 'map_resolution': 256, 'strength': [1.0, 1.0, 1.0], 'used_shaders': ['world']}})
                nodes.append({'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}})
            continue
        nodes.append(entry)
    for k, corners in enumerate(CEILING):
        nodes += room.quad('ceiling-'+str(k), 'wall', corners)
    if 'window' in portals:
        nodes += portal('window-portal', WINDOW, *window)
    if 'side' in portals:
        nodes += portal('side-portal', SIDE, 2.5, 2.0, ellipse=True)
    document['nodes'] = nodes
    return document


def scenes():
    return {
        'portal-window.json': scene('world/portal-window', 'A background light portal over the ceiling window (portal and map strategies with MIS)',
                                    PREETHAM, 0.05, portals=('window',)),
        'portal-two.json': scene('world/portal-two', 'Two portals: the window rectangle and an ellipse at the open side (the portal pdf over '
                                 'every possible portal)', PREETHAM, 0.05, portals=('window', 'side')),
        'portal-no-mis.json': scene('world/portal-no-mis', 'Two portals with a background light without MIS (the portal strategy only)',
                                    PREETHAM, 0.05, portals=('window', 'side'), use_mis=False),
        'portal-light-tree.json': scene('world/portal-light-tree', 'Two portals, the light tree selecting the background light',
                                        PREETHAM, 0.05, portals=('window', 'side'), integrator={'use_light_tree': True}),
        'portal-sun-guiding.json': scene('world/portal-sun-guiding', 'The window portal under a multiple-scattering sky with sun guiding '
                                         '(portal, sun and map strategies with MIS)', SCATTERING, 1.0, portals=('window',)),
        'two-background-lights.json': scene('world/two-background-lights', 'Two background light objects (each a lamp of the light '
                                            'distribution, sampled as the world)', PREETHAM, 0.05, lights=2),
        'two-background-lights-tree.json': scene('world/two-background-lights-tree', 'Two background light objects and the light tree',
                                                 PREETHAM, 0.05, lights=2, integrator={'use_light_tree': True}),
        'two-background-lights-portal.json': scene('world/two-background-lights-portal', 'Two background light objects and the window portal',
                                                   PREETHAM, 0.05, portals=('window',), lights=2),
    }

def portal_zero_scenes():
    """The window portal of portal-window with sizes of zero extent (Light::has_contribution is false for portals, so they
    are uploaded whatever their size)."""
    return {
        'portal-zero-u.json': scene('world/portal-zero-u', 'The window portal with sizeu 0 (sizev 2)', PREETHAM, 0.05, portals=('window',), window=(0.0, 2.0)),
        'portal-zero.json': scene('world/portal-zero', 'The window portal with sizeu and sizev 0', PREETHAM, 0.05, portals=('window',), window=(0.0, 0.0)),
        'portal-underflow.json': scene('world/portal-underflow', 'The window portal with sizeu 1e-23 (its square underflows; sizev 2)', PREETHAM, 0.05,
                                       portals=('window',), window=(1e-23, 2.0)),
    }


# --- The Ray Portal BSDF (appended to author_world.py at item 3) -----------------------------------------------------
# A vertical quad in front of the middle sphere carries the Ray Portal BSDF: alone with a constant direction (the path
# continues from the hit point), mixed with a diffuse BSDF (the closure pick and the MIS sums), with its position moved
# up and the incoming direction (the path continues from another point, where it may hit the quad's object again), and
# with the Light Path node's portal depth on the back wall (seen through the portal) and lamp shadows the portal blocks.
PANEL = [(-1.0, -2.0, -1.0), (1.0, -2.0, -1.0), (1.0, 0.6, -1.0), (-1.0, 0.6, -1.0)]


def portal_shader(variant):
    nodes = [{'handle': 'portal', 'type': 'ray_portal_bsdf', 'set': {'color': [0.9, 0.85, 0.8], 'direction': [0.15, 0.1, 1.0]}}]
    connect, out = [], ['portal', 'BSDF']
    if variant == 'moved':
        nodes += [{'handle': 'geo', 'type': 'geometry'},
                  {'handle': 'up', 'type': 'vector_math', 'set': {'math_type': 'add', 'vector2': [0.0, 1.5, 0.0]}},
                  {'handle': 'forward', 'type': 'vector_math', 'set': {'math_type': 'scale', 'scale': -1.0}}]
        connect += [['geo', 'Position', 'up', 'Vector1'], ['up', 'Vector', 'portal', 'Position'],
                    ['geo', 'Incoming', 'forward', 'Vector1'], ['forward', 'Vector', 'portal', 'Direction']]
    if variant == 'mix':
        nodes += [{'handle': 'matte', 'type': 'diffuse_bsdf', 'set': {'color': [0.2, 0.5, 0.3], 'roughness': 0.0}},
                  {'handle': 'mix', 'type': 'mix_closure', 'set': {'fac': 0.4}}]
        connect += [['portal', 'BSDF', 'mix', 'Closure1'], ['matte', 'BSDF', 'mix', 'Closure2']]
        out = ['mix', 'Closure']
    connect.append(out+['output', 'Surface'])
    return {'handle': 'portal-material', 'create': 'shader', 'graph': {'nodes': nodes, 'connect': connect}, 'set': {'emission_sampling_method': 'none'}}


def depth_wall():
    """The back wall: grey, red where the path went through a portal (the Light Path node's portal depth as the mix factor)."""
    nodes = [{'handle': 'lp', 'type': 'light_path'}, {'handle': 'grey', 'type': 'diffuse_bsdf', 'set': {'color': [0.6, 0.62, 0.65], 'roughness': 0.25}},
             {'handle': 'red', 'type': 'diffuse_bsdf', 'set': {'color': [0.8, 0.15, 0.1], 'roughness': 0.25}}, {'handle': 'mix', 'type': 'mix_closure'}]
    connect = [['lp', 'Portal Depth', 'mix', 'Fac'], ['grey', 'BSDF', 'mix', 'Closure1'], ['red', 'BSDF', 'mix', 'Closure2'], ['mix', 'Closure', 'output', 'Surface']]
    return {'handle': 'depth-wall', 'create': 'shader', 'graph': {'nodes': nodes, 'connect': connect}, 'set': {'emission_sampling_method': 'none'}}


def ray_portal_scene(scene_id, description, variant, integrator=None):
    document = room.scene(scene_id, description, integrator or {}, samples=16)
    document['sourceFixture'] = 'native/oracle/scenes/author_world.py (the sampling family room with a Ray Portal BSDF panel)'
    nodes = []
    for entry in document['nodes']:
        if variant == 'depth' and entry['handle'] == 'back':
            entry = dict(entry, set=dict(entry['set'], used_shaders=['depth-wall']))
        nodes.append(entry)
        if entry['handle'] == 'wall':
            nodes.append(portal_shader(variant))
            if variant == 'depth':
                nodes.append(depth_wall())
    nodes += room.quad('portal-panel', 'portal-material', PANEL)
    document['nodes'] = nodes
    return document


def ray_portal_scenes():
    return {
        'ray-portal.json': ray_portal_scene('world/ray-portal', 'A Ray Portal BSDF panel with a constant direction (paths continue from the hit point; '
                                            'lamp shadows through it are blocked)', 'constant'),
        'ray-portal-mix.json': ray_portal_scene('world/ray-portal-mix', 'The Ray Portal BSDF mixed with a diffuse BSDF (the closure pick and the MIS sums)', 'mix'),
        'ray-portal-moved.json': ray_portal_scene('world/ray-portal-moved', 'The Ray Portal BSDF with its position moved up and the incoming direction '
                                                  '(paths continue from another point)', 'moved'),
        'ray-portal-depth.json': ray_portal_scene('world/ray-portal-depth', 'The back wall coloured by the Light Path node\'s portal depth, seen through the '
                                                  'Ray Portal BSDF panel', 'depth'),
        'ray-portal-light-tree.json': ray_portal_scene('world/ray-portal-light-tree', 'The Ray Portal BSDF mixed with a diffuse BSDF and the light tree', 'mix',
                                                       integrator={'use_light_tree': True}),
        'ray-portal-volume.json': ray_portal_volume(),
    }


# --- Background light portals with volume direct light ---------------------------------------------------------------
# Committed volume scenes lit by the world, with the window portal above the scattering box: the homogeneous box of
# volumes/scatter-world, the heterogeneous (noise density) box of volumes/heterogeneous-point with the world and its
# background light of volumes/scatter-world added beside its point lamp, and the light tree with a point lamp and a thin
# world volume (volumes/tree-world).
def portal_volume(scene_id, description, source, world=None):
    document = json.loads((HERE.parent/(source+'.json')).read_text())
    if world:
        lit = {n['handle']: n for n in json.loads((HERE.parent/(world+'.json')).read_text())['nodes']}
        nodes = []
        for n in document['nodes']:
            nodes += [lit['world'], lit['world-light'], lit['world-light-object']] if n['handle'] == 'world' else [n]
        document['nodes'] = nodes
    document['id'] = scene_id
    document['description'] = description
    document['sourceFixture'] = 'native/oracle/scenes/author_world.py (from scenes/'+source+'.json'+(', the world of scenes/'+world+'.json' if world else '')+')'
    document['nodes'] += portal('window-portal', WINDOW, 3.0, 2.0)
    return document


def portal_volume_scenes():
    return {
        'portal-volume.json': portal_volume('world/portal-volume', 'The window portal over the homogeneous scattering box lit by the world '
                                            '(volume direct light with the portal strategy)', 'volumes/scatter-world'),
        'portal-volume-heterogeneous.json': portal_volume('world/portal-volume-heterogeneous', 'The window portal over the heterogeneous (noise '
                                                          'density) scattering box lit by the point lamp and the world', 'volumes/heterogeneous-point',
                                                          'volumes/scatter-world'),
        'portal-volume-tree.json': portal_volume('world/portal-volume-tree', 'The window portal with the light tree, a point lamp, the background '
                                                 'light and a thin world volume', 'volumes/tree-world'),
    }


def ray_portal_volume():
    """volumes/scatter-world (the scattering box lit by the world) with the Ray Portal BSDF panel in front of it."""
    document = json.loads((HERE.parent/'volumes/scatter-world.json').read_text())
    panel = ray_portal_scene('world/ray-portal', '', 'constant')
    document['id'] = 'world/ray-portal-volume'
    document['description'] = 'The Ray Portal BSDF panel in front of the scattering box of volumes/scatter-world (paths through the portal into the medium)'
    document['sourceFixture'] = 'native/oracle/scenes/author_world.py (from scenes/volumes/scatter-world.json)'
    document['nodes'] += [n for n in panel['nodes'] if n['handle'] in ('portal-material', 'portal-panel', 'portal-panel-object')]
    return document


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    for name, document in {**scenes(), **ray_portal_scenes(), **portal_volume_scenes(), **portal_zero_scenes()}.items():
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
