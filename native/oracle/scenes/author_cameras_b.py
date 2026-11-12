#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the stereo, panoramic Window and camera motion pass oracle scenes byte for byte.

scenes/cameras/stereo-*:
Spherical stereo (a left or right Stereo Eye, so a non-zero interocular offset): perspective cameras (with a convergence
distance, and parallel with depth of field) and panoramic cameras (equirectangular with pole merge, fisheye equidistant)
in the sampling family's room, as author_cameras.py writes its scenes. The perspective camera is turned about the room's
vertical axis: the pinned stereo transform takes world z as up, and the room's z is its depth, so a camera looking along
z would put its centre pixel where cross(D, up) vanishes.
Usage: python3 native/oracle/scenes/author_cameras_b.py [--check]."""
import argparse
import copy
import json
import math
import struct
from pathlib import Path

from author_cameras import scene, panorama

SCENES = Path(__file__).resolve().parent
HERE = SCENES/'cameras'
# FLT_MAX: the parallel convergence mode (spherical_stereo_transform keeps the direction).
FLT_MAX = struct.unpack('<f', struct.pack('<I', 0x7f7fffff))[0]


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def turned(angle, target, distance):
    """A camera looking along R_y(angle) (0, 0, 1) at target from distance (row-major 3x4)."""
    c, s = math.cos(angle), math.sin(angle)
    position = [target[0]-distance*s, target[1], target[2]-distance*c]
    rows = [[c, 0.0, s, position[0]], [0.0, 1.0, 0.0, position[1]], [-s, 0.0, c, position[2]]]
    return [f32(v) for row in rows for v in row]


TURNED = turned(0.5, (0.0, -0.8, 1.0), 8.0)


def scenes():
    out = {}
    out['stereo-perspective-left.json'] = scene(
        'cameras/stereo-perspective-left', 'Perspective camera, left eye: interocular distance 0.3, convergence distance 6',
        {'fov': 0.9, 'matrix': TURNED, 'stereo_eye': 'left', 'interocular_distance': 0.3, 'convergence_distance': 6.0})
    out['stereo-perspective-right-dof.json'] = scene(
        'cameras/stereo-perspective-right-dof', 'Perspective camera, right eye, parallel convergence, with depth of field (disk aperture)',
        {'fov': 0.9, 'matrix': TURNED, 'stereo_eye': 'right', 'interocular_distance': 0.4, 'convergence_distance': FLT_MAX,
         'aperturesize': 0.2, 'focaldistance': 8.0})
    out['stereo-equirectangular-pole-merge.json'] = scene(
        'cameras/stereo-equirectangular-pole-merge', 'Panoramic camera: full equirectangular, left eye with pole merge between 0.5 and 1.1 radians',
        panorama('equirectangular', latitude_min=-math.pi/2, latitude_max=math.pi/2, longitude_min=-math.pi, longitude_max=math.pi,
                 stereo_eye='left', interocular_distance=0.5, convergence_distance=3.0, use_pole_merge=True,
                 pole_merge_angle_from=0.5, pole_merge_angle_to=1.1))
    out['stereo-fisheye-right-dof.json'] = scene(
        'cameras/stereo-fisheye-right-dof', 'Panoramic camera: fisheye equidistant, right eye, with depth of field',
        panorama('fisheye_equidistant', fisheye_fov=2.6, stereo_eye='right', interocular_distance=0.4, convergence_distance=2.5,
                 aperturesize=0.06, focaldistance=3.0))
    return out


# Every panoramic type, with the parameters of the committed cameras/ scenes.
PANORAMAS = {
    'equirectangular': dict(latitude_min=-math.pi/2, latitude_max=math.pi/2, longitude_min=-math.pi, longitude_max=math.pi),
    'equiangular-cubemap-face': dict(panorama_type='equiangular_cubemap_face'),
    'mirrorball': dict(panorama_type='mirrorball'),
    'fisheye-equidistant': dict(panorama_type='fisheye_equidistant', fisheye_fov=math.pi),
    'fisheye-equisolid': dict(panorama_type='fisheye_equisolid', fisheye_lens=10.5, fisheye_fov=math.pi, sensorwidth=36.0, sensorheight=24.0),
    'fisheye-lens-polynomial': dict(panorama_type='fisheye_lens_polynomial', fisheye_fov=3.4, sensorwidth=36.0, sensorheight=24.0,
                                    fisheye_polynomial_k0=-1.1735e-5, fisheye_polynomial_k1=-0.07, fisheye_polynomial_k2=-3.3601e-6,
                                    fisheye_polynomial_k3=3.0997e-8, fisheye_polynomial_k4=-6.4e-11),
    'central-cylindrical': dict(panorama_type='panorama_central_cylindrical', central_cylindrical_range_u_min=-2.6, central_cylindrical_range_u_max=2.6,
                                central_cylindrical_range_v_min=-1.2, central_cylindrical_range_v_max=1.2),
}


def panorama_camera(name, **extra):
    values = dict(PANORAMAS[name])
    kind = values.pop('panorama_type', 'equirectangular')
    return panorama(kind, **values, **extra)


def window_shader(handle, emission=False):
    out = {'handle': 'n', 'type': 'background_shader' if emission else 'diffuse_bsdf', 'set': {'strength': 1.0} if emission else {'roughness': 0.25}}
    return {'handle': handle, 'create': 'shader', 'graph': {
        'nodes': [{'handle': 'tc', 'type': 'texture_coordinate', 'set': {}}, out],
        'connect': [['tc', 'Window', 'n', 'Color'], ['n', 'Background' if emission else 'BSDF', 'output', 'Surface']]},
        'set': {'emission_sampling_method': 'none'}}


def window_scenes():
    out = {}
    for name in PANORAMAS:
        document = scene('cameras/window-'+name, 'Texture Coordinate Window output on the walls, a sphere and the world, through a '
                         'panoramic camera: '+name.replace('-', ' '), panorama_camera(name))
        for i, node in enumerate(document['nodes']):
            if node.get('create') == 'shader' and node['handle'] in ('wall', 'matte', 'world'):
                document['nodes'][i] = window_shader(node['handle'], node['handle'] == 'world')
        out['window-'+name+'.json'] = document
    return out


def motion_scenes():
    out = {}

    def variant(scene_id, description, camera, source='passes/motion'):
        document = copy.deepcopy(json.loads((SCENES/(source+'.json')).read_text()))
        document['id'] = scene_id
        document['description'] = description
        document['sourceFixture'] = 'native/oracle/scenes/'+source+'.json (author_passes.py) with another camera'
        for node in document['nodes']:
            if node.get('scene') == 'camera':
                node['set'] = camera
        return document

    ortho = lambda dx, dy, z: [3.0, 0.0, 0.0, dx, 0.0, 3.0, 0.0, dy, 0.0, 0.0, 3.0, z]
    out['motion-orthographic.json'] = variant(
        'passes/motion-orthographic', 'motion and motion weight passes without motion blur through a moving orthographic camera',
        {'camera_type': 'orthograph', 'nearclip': 0.1, 'farclip': 100.0, 'matrix': ortho(0.0, 0.0, -7.0),
         'motion': [ortho(-0.15, 0.05, -7.2), ortho(0.0, 0.0, -7.0), ortho(0.15, -0.05, -6.8)], 'shuttertime': 0.5})
    c, s = f32(math.cos(0.06)), f32(math.sin(0.06))
    inside = lambda dx, dy, dz: [1.0, 0.0, 0.0, dx, 0.0, 1.0, 0.0, -0.5+dy, 0.0, 0.0, 1.0, -2.5+dz]
    turned_post = [c, 0.0, s, 0.12, 0.0, 1.0, 0.0, -0.53, -s, 0.0, c, -2.38]
    for name in PANORAMAS:
        camera = panorama_camera(name, nearclip=0.1, farclip=100.0, shuttertime=0.5,
                                 motion=[inside(-0.1, 0.03, -0.1), inside(0.0, 0.0, 0.0), turned_post])
        out['motion-'+name+'.json'] = variant('passes/motion-'+name, 'motion and motion weight passes without motion blur through a moving '
                                              'panoramic camera ('+name.replace('-', ' ')+'): direction_to_panorama of the motion steps', camera)
    # The denoising motion features (backward and specular motion) through the same cameras.
    out['denoising-motion-orthographic.json'] = variant(
        'passes/denoising-motion-orthographic', 'denoising motion features without motion blur through a moving orthographic camera',
        dict(out['motion-orthographic.json']['nodes'][2]['set']), 'passes/denoising-motion')
    out['denoising-motion-equirectangular.json'] = variant(
        'passes/denoising-motion-equirectangular', 'denoising motion features without motion blur through a moving equirectangular camera',
        dict(out['motion-equirectangular.json']['nodes'][2]['set']), 'passes/denoising-motion')
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    documents = [(HERE/name, d) for name, d in list(scenes().items())+list(window_scenes().items())]
    documents += [(SCENES/'passes'/name, d) for name, d in motion_scenes().items()]
    for path, document in documents:
        text = json.dumps(document, indent=1)+'\n'
        name = path.name
        if args.check:
            if path.read_text() != text:
                raise SystemExit('Scene differs: '+str(path))
        else:
            path.write_text(text)
        print(name, 'ok')


if __name__ == '__main__':
    main()
