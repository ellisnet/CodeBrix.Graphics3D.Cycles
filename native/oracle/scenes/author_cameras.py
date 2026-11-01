#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the camera oracle scenes (scenes/cameras/) byte for byte.

Orthographic and panoramic cameras (equirectangular, equiangular cubemap face,
mirror ball, fisheye equidistant, equisolid and lens polynomial, central
cylindrical) and depth of field (disk and polygonal apertures, anamorphic
ratio) in the sampling family's room: glossy, glass and diffuse spheres in a
diffuse room, lit by a sphere lamp with MIS and a constant world, through the
public C API.
Usage: python3 native/oracle/scenes/author_cameras.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_sampling as room

HERE = Path(__file__).resolve().parent/'cameras'


def scene(scene_id, description, camera, samples=8, extra_integrator=None):
    document = room.scene(scene_id, description, extra_integrator or {}, samples=samples)
    document['sourceFixture'] = 'native/oracle/scenes/author_cameras.py (the sampling family room)'
    for node in document['nodes']:
        if node.get('scene') == 'camera':
            node['set'] = camera
    return document


FRONT = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]
# Orthographic rays cover the camera-space view plane ([-aspect, aspect] x [-1, 1]); the camera
# transform scales it to the room.
ORTHO = [3.0, 0.0, 0.0, 0.0, 0.0, 3.0, 0.0, 0.0, 0.0, 0.0, 3.0, -7.0]
# Panoramic cameras inside the room, in front of the spheres.
INSIDE = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.5, 0.0, 0.0, 1.0, -2.5]


def panorama(kind, **values):
    return {'camera_type': 'panorama', 'panorama_type': kind, 'matrix': INSIDE, **values}


def scenes():
    out = {}
    out['orthographic.json'] = scene('cameras/orthographic', 'Orthographic camera', {'camera_type': 'orthograph', 'matrix': ORTHO})
    out['orthographic-dof.json'] = scene('cameras/orthographic-dof', 'Orthographic camera with depth of field (disk aperture)',
                                         {'camera_type': 'orthograph', 'matrix': ORTHO, 'aperturesize': 0.05, 'focaldistance': 2.3})
    out['perspective-dof.json'] = scene('cameras/perspective-dof', 'Perspective camera with depth of field (disk aperture, focus on the middle sphere)',
                                        {'fov': 0.9, 'matrix': FRONT, 'aperturesize': 0.25, 'focaldistance': 7.4})
    out['perspective-bokeh.json'] = scene('cameras/perspective-bokeh', 'Perspective camera with a hexagonal aperture, rotated blades and an anamorphic ratio',
                                          {'fov': 0.9, 'matrix': FRONT, 'aperturesize': 0.3, 'focaldistance': 6.0, 'blades': 6, 'bladesrotation': 0.4,
                                           'aperture_ratio': 1.6})
    out['equirectangular.json'] = scene('cameras/equirectangular', 'Panoramic camera: full equirectangular',
                                        panorama('equirectangular', latitude_min=-math.pi/2, latitude_max=math.pi/2, longitude_min=-math.pi, longitude_max=math.pi))
    out['equirectangular-range-dof.json'] = scene('cameras/equirectangular-range-dof', 'Panoramic camera: equirectangular sub-range with depth of field',
                                                  panorama('equirectangular', latitude_min=-1.0, latitude_max=0.8, longitude_min=-1.6, longitude_max=1.2,
                                                           aperturesize=0.08, focaldistance=3.0))
    out['equiangular-cubemap-face.json'] = scene('cameras/equiangular-cubemap-face', 'Panoramic camera: equiangular cubemap face',
                                                 panorama('equiangular_cubemap_face'))
    out['mirrorball.json'] = scene('cameras/mirrorball', 'Panoramic camera: mirror ball (directions outside the ball sample nothing)',
                                   panorama('mirrorball'))
    out['fisheye-equidistant.json'] = scene('cameras/fisheye-equidistant', 'Panoramic camera: fisheye equidistant, 180 degrees',
                                            panorama('fisheye_equidistant', fisheye_fov=math.pi))
    out['fisheye-equisolid.json'] = scene('cameras/fisheye-equisolid', 'Panoramic camera: fisheye equisolid (10.5 mm lens, 36 x 24 mm sensor)',
                                          panorama('fisheye_equisolid', fisheye_lens=10.5, fisheye_fov=math.pi, sensorwidth=36.0, sensorheight=24.0))
    out['fisheye-lens-polynomial.json'] = scene('cameras/fisheye-lens-polynomial', 'Panoramic camera: fisheye lens polynomial (about 1.5 radians at the sensor corners)',
                                                panorama('fisheye_lens_polynomial', fisheye_fov=3.4, sensorwidth=36.0, sensorheight=24.0,
                                                         fisheye_polynomial_k0=-1.1735e-5, fisheye_polynomial_k1=-0.07,
                                                         fisheye_polynomial_k2=-3.3601e-6, fisheye_polynomial_k3=3.0997e-8, fisheye_polynomial_k4=-6.4e-11))
    out['central-cylindrical.json'] = scene('cameras/central-cylindrical', 'Panoramic camera: central cylindrical',
                                            panorama('panorama_central_cylindrical', central_cylindrical_range_u_min=-2.6, central_cylindrical_range_u_max=2.6,
                                                     central_cylindrical_range_v_min=-1.2, central_cylindrical_range_v_max=1.2))
    out['fisheye-dof-sobol-burley.json'] = scene('cameras/fisheye-dof-sobol-burley', 'Fisheye equidistant camera with depth of field and the Sobol-Burley pattern',
                                                 panorama('fisheye_equidistant', fisheye_fov=2.6, aperturesize=0.06, focaldistance=3.0),
                                                 extra_integrator={'sampling_pattern': 'sobol_burley'})
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
