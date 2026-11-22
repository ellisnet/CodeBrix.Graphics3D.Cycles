#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the Gaussian splat oracle scenes (scenes/gsplat/) byte for byte.

Point clouds rendered as Gaussian splats (render_as gaussian_splats) in the sampling family's room, with the standard
point attributes a splat needs: radiance:base (float4, the band-0 colour coefficients and the opacity), scale (vector)
and rotation (quaternion), and in some scenes radiance:sh (spherical_harmonics_rest, bands 1 to 3). Cycles' default splat
shader (an emission of the radiance evaluated from the coefficients) and caller shaders that read the radiance; band 0
alone and with bands 1 to 3; object motion and deformation motion of the positions; single-user and instanced clouds;
splats beside meshes, inside a volume, behind a transparent sheet and as transparent shadow casters, beside a plain
point cloud, and beside the features with kernels of their own (the light tree, light and shadow linking, the shadow
catcher, bakes, passes, denoising, the Ambient Occlusion and Raycast nodes, caustics). Every value comes from a fixed formula rounded through binary32, so every platform gets the same bytes; the
rotations come from an axis and an angle (w = cos of half the angle, x, y, z = the axis times its sine), some of them
longer or shorter than 1 (Cycles normalizes them). The radius given with the points is the authored one; Cycles bounds
each splat with a radius of its own. Through the public C API.
Usage: python3 native/oracle/scenes/author_gsplat.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_attributes as kinds
import author_points as points
import author_sampling as room

HERE = Path(__file__).resolve().parent/'gsplat'
f32 = room.f32
COEFFICIENTS = 15  # bands 1 to 3, three colour channels each
SH_C0 = 0.28209479177387814  # the band-0 basis function, so a colour c is the coefficient (c - 0.5) / SH_C0


def fraction(value):
    return value % 1.0


def splat_attributes(seed, harmonics=False, faint=()):
    """The standard splat attributes of count points: a colour in [0.15, 0.9] per channel as its band-0 coefficient and an
    opacity in [0.3, 0.95] (0.005 for the points in faint, below the 0.01 that Cycles bounds), an anisotropic scale and
    a rotation of length 0.6 to 1.4; with harmonics, coefficients of bands 1 to 3 in [-0.3, 0.3]."""
    def make(count):
        base, scale, rotation = [], [], []
        for i in range(count):
            colour = (0.15+0.75*fraction(i*0.618034+seed), 0.15+0.75*fraction(i*0.381966+2.0*seed), 0.2+0.7*fraction(i*0.754877+0.5*seed))
            opacity = 0.005 if i in faint else 0.3+0.65*fraction(i*0.569840+seed)
            base += [f32((c-0.5)/SH_C0) for c in colour]+[f32(opacity)]
            scale += [f32(0.1+0.25*fraction(i*0.414214+seed)), f32(0.06+0.16*fraction(i*0.732051+seed)), f32(0.02+0.06*fraction(i*0.302776+seed))]
            rotation += kinds.quaternion(i+int(10*seed), 0.6+0.8*fraction(i*0.236068+seed))
        entries = [{'name': 'radiance:base', 'value': 'float4', 'domain': 'vertex', 'flags': ['standard'], 'data': base},
                   {'name': 'scale', 'value': 'vector', 'domain': 'vertex', 'flags': ['standard'], 'data': scale},
                   {'name': 'rotation', 'value': 'quaternion', 'domain': 'vertex', 'flags': ['standard'], 'data': rotation}]
        if harmonics:
            entries.append({'name': 'radiance:sh', 'value': 'spherical_harmonics_rest', 'domain': 'vertex', 'flags': ['standard'],
                            'data': [f32(round(64.0*0.3*math.sin(seed+1.3*i+0.7*k+0.17*i*k))/64.0) for i in range(count) for k in range(3*COEFFICIENTS)]})
        return entries
    return make


def splat_cloud(handle, material=None, seed=0.3, count=60, center=(0.0, -0.6, 0.4), spread=1.6, radius=0.25, harmonics=False, faint=(), steps=0):
    """A cloud of the points family rendered as splats; without a material its splats take Cycles' default splat shader."""
    entry = points.cloud(handle, material or [], seed=seed, count=count, center=center, spread=spread, radius=radius,
                         attributes=splat_attributes(seed, harmonics, faint), steps=steps, extra={'render_as': 'gaussian_splats'})
    if not material:
        del entry['set']['used_shaders']
    return entry


def base(scene_id, description, integrator=None, **kwargs):
    document = points.base(scene_id, description, integrator, **kwargs)
    document['sourceFixture'] = 'native/oracle/scenes/author_gsplat.py (Gaussian splats in the sampling family room)'
    return document


def beside(path, scene_id, description, *entries):
    document = points.beside(path, scene_id, description, *entries)
    document['sourceFixture'] = 'native/oracle/scenes/author_gsplat.py (Gaussian splats added to '+path+')'
    return document


def radiance_material():
    """A caller shader on splats: the radiance (Cycles evaluates it from the splat's coefficients when no radiance
    attribute exists) as a diffuse colour lit by the room, plus half of it as an emission."""
    return room.shader('radiance-lit', [
        {'handle': 'r', 'type': 'attribute', 'set': {'attribute': 'radiance'}},
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
        {'handle': 'e', 'type': 'emission', 'set': {'strength': 0.5}},
        {'handle': 'add', 'type': 'add_closure'},
    ], [['r', 'Color', 'd', 'Color'], ['r', 'Color', 'e', 'Color'], ['d', 'BSDF', 'add', 'Closure1'], ['e', 'Emission', 'add', 'Closure2'],
        ['add', 'Closure', 'output', 'Surface']])


def transparent_material():
    """A transparent shader on splats: shadow rays record them as transparent hits instead of stopping at them."""
    return room.shader('radiance-transparent', [
        {'handle': 'r', 'type': 'attribute', 'set': {'attribute': 'radiance'}},
        {'handle': 't', 'type': 'transparent_bsdf'},
        {'handle': 'e', 'type': 'emission', 'set': {'strength': 0.4}},
        {'handle': 'add', 'type': 'add_closure'},
    ], [['r', 'Color', 't', 'Color'], ['r', 'Color', 'e', 'Color'], ['t', 'BSDF', 'add', 'Closure1'], ['e', 'Emission', 'add', 'Closure2'],
        ['add', 'Closure', 'output', 'Surface']])


def scenes():
    out = {}
    out['band0.json'] = points.with_nodes(base('gsplat/band0', 'A splat cloud with band 0 only (radiance:base, scale, rotation) and Cycles\' default '
        'splat shader, an emission of the radiance: anisotropic scales, rotations of lengths 0.6 to 1.4 (normalized by Cycles), two '
        'splats below the opacity 0.01 that Cycles bounds (a bounding radius of 0: never hit)'), splat_cloud('splats', faint=(7, 31)), points.place('splats-object', 'splats'))
    out['spherical-harmonics.json'] = points.with_nodes(base('gsplat/spherical-harmonics', 'The cloud of gsplat/band0 with spherical-harmonics '
        'coefficients of bands 1 to 3 (radiance:sh): the radiance of each splat depends on the view direction in its object space'),
        splat_cloud('splats', harmonics=True, faint=(7, 31)), points.place('splats-object', 'splats'))
    out['caller-shader.json'] = points.with_nodes(base('gsplat/caller-shader', 'Splats with a caller shader that reads the radiance through an '
        'Attribute node (Cycles evaluates it from bands 0 to 3) as a diffuse colour lit by the lamp, plus half of it as an emission; the '
        'diffuse closure shades with the splat normal (the rotated axis of the smallest scale)'), radiance_material(),
        splat_cloud('splats', 'radiance-lit', harmonics=True), points.place('splats-object', 'splats'))
    c, s = f32(math.cos(0.6)), f32(math.sin(0.6))
    out['single-user-transform.json'] = points.with_nodes(base('gsplat/single-user-transform', 'A single-user splat cloud under a uniform scale, '
        'a rotation and a translation, where a plain point cloud would be baked into world space: splats are never baked, so the kernel '
        'transforms rays into the cloud\'s object space (scale, rotation and the spherical-harmonics view direction included)'),
        splat_cloud('splats', seed=0.8, count=40, center=(0.0, 0.0, 0.0), spread=1.1, harmonics=True),
        points.place('splats-object', 'splats', [f32(1.3*c), 0.0, f32(1.3*s), 0.3, 0.0, 1.3, 0.0, -0.6, f32(-1.3*s), 0.0, f32(1.3*c), 0.5]))
    out['instanced.json'] = points.with_nodes(base('gsplat/instanced', 'A splat cloud on three objects: rotated, non-uniformly scaled and mirrored '
        'instances; the splats\' scale and rotation go through each instance transform'),
        splat_cloud('splats', seed=1.4, count=36, center=(0.0, -1.0, 0.4), spread=0.9, harmonics=True),
        points.place('a', 'splats', [1.0, 0.0, 0.0, -2.2, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]),
        points.place('b', 'splats', [c, 0.0, s, 0.0, 0.0, 0.7, 0.0, 0.2, -s, 0.0, c, 0.0]),
        points.place('c', 'splats', [-1.0, 0.0, 0.0, 2.2, 0.0, 1.3, 0.0, 0.2, 0.0, 0.0, 1.0, -0.5]))
    moving = [[1.0, 0.0, 0.0, -0.25, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.0],
              [1.0, 0.0, 0.0, 0.25, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.1]]
    out['object-motion.json'] = points.camera_blur(points.with_nodes(base('gsplat/object-motion', 'A splat cloud on two objects, one moving '
        '(object motion blur on splats)', {'motion_blur': True}), splat_cloud('splats', seed=0.6, count=40, center=(0.0, -1.0, 0.4), spread=1.0, harmonics=True),
        points.place('moving', 'splats', [1.0, 0.0, 0.0, -1.0, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.0], motion=moving),
        points.place('still', 'splats', [1.0, 0.0, 0.0, 1.6, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0])))
    out['deform-3.json'] = points.camera_blur(points.with_nodes(base('gsplat/deform-3', 'Deforming splats, three motion steps of the positions '
        '(cbc_host_point_motion; Cycles bounds every step with its own splat radius), with a caller shader', {'motion_blur': True}),
        radiance_material(), splat_cloud('splats', 'radiance-lit', seed=0.2, steps=3), points.place('splats-object', 'splats')))
    out['beside-meshes.json'] = points.with_nodes(room.scene('gsplat/beside-meshes', 'Splats in the room with its glossy, glass and diffuse '
        'spheres: splats seen in reflection and through refraction, and blocking the lamp\'s light on the floor and the spheres '
        '(opaque to shadow rays where their stochastic test accepts)', {}, samples=8, width=64, height=48),
        splat_cloud('splats', seed=1.1, count=50, center=(0.0, 0.9, -0.8), spread=1.3, harmonics=True), points.place('splats-object', 'splats'))
    out['beside-meshes.json']['sourceFixture'] = 'native/oracle/scenes/author_gsplat.py (Gaussian splats in the sampling family room with its spheres)'
    out['beside-volume.json'] = beside('volumes/absorption-point', 'gsplat/beside-volume', 'Splats inside an absorbing volume box, lit through the '
        'medium: camera, shadow and volume rays meet splats in the medium', radiance_material(),
        splat_cloud('splats', 'radiance-lit', seed=0.4, count=40, center=(0.2, -0.8, 0.0), spread=1.0, harmonics=True), points.place('splats-object', 'splats'))
    sheet = room.single('sheet', 'transparent_bsdf', {'color': [0.75, 0.8, 0.9]})
    out['transparent-shadows.json'] = points.with_nodes(base('gsplat/transparent-shadows', 'Splats between the lamp and the floor beside a '
        'transparent sheet: the transparent-shadow traversal records the sheet and the splats of a transparent shader, and stops at '
        'the splats of an opaque one where their stochastic test accepts', {'transparent_max_bounce': 16}),
        sheet, *room.quad('sheet-quad', 'sheet', [(-0.6, -1.9, -1.6), (2.4, -1.9, -1.6), (2.4, 1.4, -0.2), (-0.6, 1.4, -0.2)]),
        radiance_material(), transparent_material(),
        splat_cloud('opaque', 'radiance-lit', seed=0.9, count=30, center=(1.4, 0.2, -1.2), spread=0.8),
        points.place('opaque-object', 'opaque'),
        splat_cloud('transparent', 'radiance-transparent', seed=1.7, count=30, center=(-1.2, -0.4, -0.6), spread=0.9),
        points.place('transparent-object', 'transparent'))
    out['with-points.json'] = points.with_nodes(base('gsplat/with-points', 'A splat cloud and a plain point cloud in one scene: splat and point '
        'primitives side by side in the BVH'), splat_cloud('splats', seed=0.5, count=40, center=(-1.3, -0.8, 0.4), spread=1.0, harmonics=True),
        points.place('splats-object', 'splats'), points.cloud('points', 'glossy', seed=2.0, count=30, center=(1.6, -1.0, 0.2), spread=0.8, radius=0.22),
        points.place('points-object', 'points'))
    vector = [{'handle': 'pass-Vector', 'create': 'pass', 'set': {'type': 'motion', 'name': 'Vector'}}, {'handle': 'pass-Vector Weight', 'create': 'pass', 'set': {'type': 'motion_weight', 'name': 'Vector Weight'}}]
    out['motion-pass.json'] = points.with_nodes(base('gsplat/motion-pass', 'The motion pass on deforming splats and on a moving instance, without motion blur: the splat '
        'centers and their steps'), radiance_material(), splat_cloud('splats', 'radiance-lit', seed=0.2, steps=3), points.place('splats-object', 'splats'),
        splat_cloud('more', seed=2.0, count=30, center=(-2.0, -1.2, -0.5), spread=0.8, harmonics=True),
        points.place('more-a', 'more', [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.1, 0.0, 0.0, 1.0, 0.0], motion=moving), points.place('more-b', 'more', [1.0, 0.0, 0.0, 4.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]), *vector)
    # Beside the features with their own kernels (the splats take part as hit geometry, as blockers and, with Cycles' default
    # splat shader, as emissive hits).
    out['beside-light-tree.json'] = beside('light-tree/lamps-and-mesh-lights', 'gsplat/beside-light-tree', 'The light tree with splats in the scene: a cloud with '
        'Cycles\' default splat shader (emissive hits, which are not in the tree) and a cloud lit through the tree', radiance_material(),
        splat_cloud('splats', seed=0.7, count=30, center=(0.9, 0.3, 0.2), spread=0.7, harmonics=True), points.place('splats-object', 'splats'),
        splat_cloud('lit', 'radiance-lit', seed=1.9, count=30, center=(-1.5, 0.0, 0.0), spread=0.7), points.place('lit-object', 'lit'))
    out['beside-light-linking.json'] = beside('light-tree/light-linking', 'gsplat/beside-light-linking', 'Light linking through the light tree with splats as '
        'receivers of one light set and as blockers', radiance_material(),
        splat_cloud('splats', 'radiance-lit', seed=0.7, count=30, center=(0.9, 0.3, 0.2), spread=0.7, harmonics=True), points.place('splats-object', 'splats', receiver_light_set=1),
        splat_cloud('more', 'radiance-lit', seed=1.9, count=30, center=(-1.5, 0.0, 0.0), spread=0.7), points.place('more-object', 'more', receiver_light_set=2))
    out['beside-shadow-linking.json'] = beside('shadow-linking/blockers', 'gsplat/beside-shadow-linking', 'Shadow linking with splats as blockers in a shadow set '
        'and as receivers', radiance_material(),
        splat_cloud('splats', 'radiance-lit', seed=0.3, count=30, center=(0.3, 0.6, 0.6), spread=0.6), points.place('splats-object', 'splats', blocker_shadow_set=1),
        splat_cloud('more', 'radiance-lit', seed=1.2, count=20, center=(-1.0, 0.0, 1.2), spread=0.5, harmonics=True), points.place('more-object', 'more'))
    out['shadow-linking-emissive.json'] = beside('shadow-linking/blockers', 'gsplat/shadow-linking-emissive', 'Shadow-linked splats with Cycles\' default splat '
        'shader: the dedicated light kernels pick the emissive splat hits and evaluate the radiance at the picked hit',
        splat_cloud('splats', seed=0.5, count=20, center=(0.3, 1.6, 0.4), spread=0.4, harmonics=True), points.place('splats-object', 'splats', shadow_set_membership=5))
    out['beside-shadow-catcher.json'] = beside('shadow-catcher/catcher-point-light', 'gsplat/beside-shadow-catcher', 'A shadow catcher floor with splats above it: '
        'the catcher records the shadows the splats cast where their stochastic test accepts', radiance_material(),
        splat_cloud('splats', 'radiance-lit', seed=0.6, count=30, center=(0.1, 0.2, 0.0), spread=0.8, harmonics=True), points.place('splats-object', 'splats'))
    catcher = beside('shadow-catcher/catcher-point-light', 'gsplat/shadow-catcher-splats', 'A splat cloud that is itself a shadow catcher, beside the catcher floor',
        radiance_material(), splat_cloud('splats', 'radiance-lit', seed=0.6, count=30, center=(0.1, 0.2, 0.0), spread=0.8), points.place('splats-object', 'splats', is_shadow_catcher=True))
    out['shadow-catcher-splats.json'] = catcher
    for kind in ('combined', 'diffuse'):
        out['bake-'+kind+'.json'] = beside('bake/'+kind, 'gsplat/bake-'+kind, 'A bake of the ball (' + kind + ') with splats around it: the bake\'s rays meet splats as '
            'blockers and as emissive hits', splat_cloud('splats', seed=0.8, count=40, center=(0.9, 0.9, -0.9), spread=0.9, harmonics=True), points.place('splats-object', 'splats'))
    out['denoising-features.json'] = beside('passes/denoising-features', 'gsplat/denoising-features', 'The denoising feature passes (albedo, normal, depth) with splats: '
        'an emissive cloud (Cycles\' default splat shader) and a lit one', radiance_material(),
        splat_cloud('splats', seed=0.4, count=30, center=(-0.2, 0.2, -1.2), spread=0.9, harmonics=True), points.place('splats-object', 'splats'),
        splat_cloud('lit', 'radiance-lit', seed=1.6, count=30, center=(1.5, -0.6, -1.4), spread=0.7), points.place('lit-object', 'lit'))
    out['data-passes.json'] = beside('passes/data-passes', 'gsplat/data-passes', 'The data passes (depth, position, normal, object and material index) with splats',
        radiance_material(), splat_cloud('splats', 'radiance-lit', seed=0.4, count=40, center=(-0.2, 0.2, -1.2), spread=0.9, harmonics=True),
        points.place('splats-object', 'splats', pass_id=5))
    out['denoise-on.json'] = beside('denoise/beckmann-on', 'gsplat/denoise-on', 'A denoised render with splats in the scene: the guiding passes of splat hits feed the denoiser',
        splat_cloud('splats', seed=1.3, count=30, center=(0.0, 1.0, -1.0), spread=0.9, harmonics=True), points.place('splats-object', 'splats'))
    out['beside-ao-node.json'] = beside('ao/ao-node', 'gsplat/beside-ao-node', 'The Ambient Occlusion node with splats in the scene: the global traversal tests them, '
        'the local one ignores them', splat_cloud('splats', seed=0.9, count=40, center=(0.0, 0.0, -0.8), spread=1.0), points.place('splats-object', 'splats'))
    out['beside-raycast.json'] = beside('surface-nodes/raycast', 'gsplat/beside-raycast', 'The Raycast node hitting splats: the hit distance and the splat normal of the hit',
        radiance_material(), splat_cloud('splats', 'radiance-lit', seed=0.9, count=40, center=(0.0, 0.0, -0.8), spread=1.0, harmonics=True), points.place('splats-object', 'splats'))
    out['beside-caustics.json'] = beside('mnee/glass-sphere-caustic', 'gsplat/beside-caustics', 'Caustics with splats in the scene: a splat between the receiver and the '
        'light ends the walk where its stochastic test accepts', splat_cloud('splats', seed=1.7, count=40, center=(0.0, 0.3, -0.5), spread=1.0), points.place('splats-object', 'splats'))
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
