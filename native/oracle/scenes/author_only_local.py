#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the local-traversal-beside-instances oracle scenes (scenes/surface-nodes/) byte for byte.

The Ambient Occlusion node with only_local (and inside), the Bevel node and the Raycast node with only_local, each on a
transform-applied object (a single-user mesh), in a scene with instances: one box mesh used by three objects, so the
top-level BVH holds instance leaves next to the applied objects' triangles. The local traversal of an applied object
starts at the scene root and reaches those leaves. The pinned BVH2 CPU kernel then runs its triangle loop over the
leaves' negative primitive addresses, so its film of this scene is undefined; the scene names its oracle reference
instead: the same document without the instance objects (`oracleReference`, native/oracle/render_oracles.py). The
instances are visible to volume scatter rays only, and the scenes have no volume, so no other ray of either scene can
meet them: the two scenes differ only in what the local traversals pass over.
Usage: python3 native/oracle/scenes/author_only_local.py [--check]."""
import argparse
import copy
import json
from pathlib import Path
from author_surface_nodes import scene, box, bevel_material, raycast_material
from author_ao import ao_node
from author_bsdf import sphere, f32

HERE = Path(__file__).resolve().parent/'surface-nodes'
# PATH_RAY_VISIBILITY_VOLUME_SCATTER (kernel/types.h).
VOLUME_SCATTER_ONLY = 16
INSTANCE_HANDLES = ['shell', 'shell-a', 'shell-b', 'shell-c']


def shell_instance(handle, center, size):
    rows = [[size[0], 0.0, 0.0, center[0]], [0.0, size[1], 0.0, center[1]], [0.0, 0.0, size[2], center[2]]]
    return {'handle': handle, 'create': 'object', 'set': {'geometry': 'shell', 'visibility': VOLUME_SCATTER_ONLY,
                                                          'tfm': [f32(v) for row in rows for v in row]}}


# One scene per node (the three raytrace handlers together put prepare_surface_raytrace above the size guard): the node's
# material on a transform-applied object (a single-user box or sphere) and plain walls beside it.
NODES = {
    'ao': ('Ambient Occlusion (only_local, inside) on a transform-applied box', lambda: ao_node('local-material', True),
           lambda: box('applied', 'local-material', (-1.3, -1.1, 0.5), (1.6, 1.7, 1.4))),
    'bevel': ('Bevel on a transform-applied sphere', lambda: bevel_material('local-material', 0.25, 8),
              lambda: sphere('applied', 'local-material', (-1.0, -1.0, 0.4), 1.0)),
    'raycast': ('Raycast (only_local) into a transform-applied box', lambda: raycast_material('local-material', True),
                lambda: box('applied', 'local-material', (-1.3, -1.0, 0.5), (1.6, 1.8, 1.4))),
}


def with_instances(kind):
    description, material, applied = NODES[kind]
    scene_id = 'surface-nodes/local-beside-instances-'+kind
    # One unit box mesh used by three objects: not transform-applied, so each is an instance leaf of the top-level BVH,
    # around and beside the applied object, where its local rays meet their bounds.
    shell = box('shell', 'wall', (0.0, 0.0, 0.0), (1.0, 1.0, 1.0))[0]
    instances = [shell, shell_instance('shell-a', (-1.3, -1.0, 0.5), (2.3, 2.4, 2.2)),
                 shell_instance('shell-b', (0.3, -1.2, 0.6), (1.2, 1.4, 1.2)),
                 shell_instance('shell-c', (1.9, -0.9, 0.3), (1.4, 1.8, 1.6))]
    document = scene(scene_id, description+' in a scene with instances (volume-scatter visibility only): the local traversal '
                     'passes the instance leaves over', [material()], applied())
    # After every other node, so the two scenes number their objects, shaders and lights alike.
    document['nodes'] += instances
    document['oracleReference'] = {
        'scene': scene_id+'-reference', 'withoutHandles': INSTANCE_HANDLES,
        'reason': 'The pinned BVH2 local traversal (bvh/local.h) of a transform-applied object starts at the scene root and runs its '
                  'triangle loop over instance leaves, reading negative primitive addresses: its result is undefined here. The gold '
                  'standard is the same scene without the instance objects (native/DEVICE.md, open items 18 and 34).'}
    return document


def reference(document):
    out = copy.deepcopy(document)
    del out['oracleReference']
    out['id'] = document['id']+'-reference'
    out['description'] = 'The oracle reference of '+document['id']+': the same scene without the instance objects'
    out['nodes'] = [n for n in out['nodes'] if n['handle'] not in INSTANCE_HANDLES]
    return out


def scenes():
    out = {}
    for kind in NODES:
        document = with_instances(kind)
        out['local-beside-instances-'+kind+'.json'] = document
        out['local-beside-instances-'+kind+'-reference.json'] = reference(document)
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
