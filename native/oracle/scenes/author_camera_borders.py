#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the camera border (render region) oracle scenes (scenes/cameras/border-*.json) byte for byte.

The camera family's perspective room (author_cameras.py, the sampling family room) rendered through a camera border:
an inner region, and a region touching the film's right and bottom edges. The film comes back at full size with zero
outside the region (native/host/ABI.md, Cameras).
Usage: python3 native/oracle/scenes/author_camera_borders.py [--check]."""
import argparse
import json
from pathlib import Path

import author_cameras as cameras

HERE = Path(__file__).resolve().parent/'cameras'
PERSPECTIVE = {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}


def scenes():
    out = {}
    for name, description, border in (
            ('border-inner', 'Perspective camera with a camera border: an inner region (left 0.25, right 0.75, bottom 0.25, top 0.625)',
             (0.25, 0.75, 0.25, 0.625)),
            ('border-corner', 'Perspective camera with a camera border touching the right and bottom edges (left 0.5, right 1, bottom 0, top 0.5)',
             (0.5, 1.0, 0.0, 0.5))):
        camera = dict(PERSPECTIVE, **{'border.left': border[0], 'border.right': border[1], 'border.bottom': border[2], 'border.top': border[3]})
        document = cameras.scene('cameras/'+name, description, camera)
        document['sourceFixture'] = 'native/oracle/scenes/author_camera_borders.py (the sampling family room)'
        out[name+'.json'] = document
    return out


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
