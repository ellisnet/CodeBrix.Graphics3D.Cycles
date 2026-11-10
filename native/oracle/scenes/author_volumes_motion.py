#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the volume motion blur oracle scenes (scenes/volumes/motion-*) byte for byte.

Medium boxes in the sampling family room with motion blur. The camera starts inside the box (the volume stack
initialization at each camera ray's time) except in the heterogeneous scene: a static medium in a scene with camera motion,
a moving and a deforming surface object; a moving heterogeneous medium seen from outside (object motion of the volume
object: the medium's object-space noise at the ray's time); a moving scattering medium around the camera (the stack
initialization's moving instance); a deforming scattering medium around the camera (deformation motion of the volume mesh:
motion triangles as the volume boundaries).
Usage: python3 native/oracle/scenes/author_volumes_motion.py [--check]."""
import argparse
import json
from pathlib import Path

import author_volumes as volumes

HERE = Path(__file__).resolve().parent/'volumes'
SOURCE = 'native/oracle/scenes/author_volumes_motion.py (a medium box in the sampling family room, with motion blur)'


def node(document, handle):
    return next(n for n in document['nodes'] if n['handle'] == handle)


def motion_on(document, shuttertime=0.6):
    node(document, 'integrator')['set']['motion_blur'] = True
    node(document, 'camera')['set']['shuttertime'] = shuttertime


def camera_motion(document):
    """Three camera steps around the authored camera matrix (a translation and a small turn)."""
    m = node(document, 'camera')['set']['matrix']
    pre = [0.9998, 0, 0.02, m[3]-0.25, 0, 1, 0, m[7]-0.04, -0.02, 0, 0.9998, m[11]]
    post = [0.9998, 0, -0.02, m[3]+0.25, 0, 1, 0, m[7]+0.05, 0.02, 0, 0.9998, m[11]]
    node(document, 'camera')['set']['motion'] = [[round(v, 7) for v in pre], list(m), [round(v, 7) for v in post]]


def translate(x, y, z):
    return [1.0, 0.0, 0.0, x, 0.0, 1.0, 0.0, y, 0.0, 0.0, 1.0, z]


def turn(angle_cos, angle_sin, x, y, z):
    """A turn about the vertical axis with a translation."""
    return [round(angle_cos, 7), 0.0, round(angle_sin, 7), x, 0.0, 1.0, 0.0, y, round(-angle_sin, 7), 0.0, round(angle_cos, 7), z]


def deform(positions, scale_pre, scale_post):
    """Three motion steps of a mesh: x and z scaled about the origin before and after the centre positions."""
    pre = [round(v*scale_pre if i % 3 != 1 else v, 7) for i, v in enumerate(positions)]
    post = [round(v*scale_post if i % 3 != 1 else v, 7) for i, v in enumerate(positions)]
    return dict(steps=3, positions=pre+list(positions)+post)


def camera_inside(document):
    """The camera inside the medium box (z from -1.4 to 2.2), looking down +z at the spheres."""
    node(document, 'camera')['set']['matrix'][11] = -1.1


def static_media():
    document = volumes.scene('volumes/motion-static-medium', 'Motion blur in a static scattering box around the camera: camera motion, a moving sphere '
                             'inside the medium and a deforming sphere beside it (the volume stack initialization traverses the moving instance '
                             'and the motion triangles at the ray\'s time), point light, MIS')
    document['sourceFixture'] = SOURCE
    motion_on(document)
    camera_inside(document)
    camera_motion(document)
    node(document, 'sphere-1-object')['set']['motion'] = [translate(-0.35, 0.1, -0.2), translate(0.35, -0.05, 0.25)]
    sphere = node(document, 'sphere-0')
    sphere['mesh']['motion'] = deform(sphere['mesh']['positions'], 0.94, 1.06)
    return document


def moving_heterogeneous():
    document = volumes.scene('volumes/motion-moving-heterogeneous', 'Object motion blur of a heterogeneous medium box (noise density in the box\'s object '
                             'space): the box turns and moves over the shutter, so the stack initialization meets a moving instance and the '
                             'medium is evaluated with the box\'s motion transform at the ray\'s time, point light, MIS', graph=volumes.noise_medium())
    document['sourceFixture'] = SOURCE
    motion_on(document)
    node(document, 'box-object')['set']['motion'] = [turn(0.9950042, -0.0998334, -0.3, 0.05, -0.2), turn(0.9950042, 0.0998334, 0.3, -0.05, 0.2)]
    return document


def moving_medium():
    document = volumes.scene('volumes/motion-moving-medium', 'Object motion blur of the scattering box around the camera: the box turns and moves over '
                             'the shutter (the stack initialization enters the moving instance at each camera ray\'s time), point light, MIS')
    document['sourceFixture'] = SOURCE
    motion_on(document)
    camera_inside(document)
    node(document, 'box-object')['set']['motion'] = [turn(0.9950042, -0.0998334, -0.3, 0.05, -0.2), turn(0.9950042, 0.0998334, 0.3, -0.05, 0.2)]
    return document


def deforming_medium():
    document = volumes.scene('volumes/motion-deforming-medium', 'Deformation motion blur of the scattering box around the camera: the box mesh scales in x '
                             'and z over the shutter (motion triangles as the volume boundaries), with a moving sphere inside it, point light, MIS')
    document['sourceFixture'] = SOURCE
    motion_on(document)
    camera_inside(document)
    box = node(document, 'box')
    box['mesh']['motion'] = deform(box['mesh']['positions'], 0.9, 1.1)
    node(document, 'sphere-1-object')['set']['motion'] = [translate(-0.3, 0.0, 0.0), translate(0.3, 0.0, 0.0)]
    return document


def scenes():
    return {'motion-static-medium.json': static_media(), 'motion-moving-heterogeneous.json': moving_heterogeneous(),
            'motion-moving-medium.json': moving_medium(), 'motion-deforming-medium.json': deforming_medium()}


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
