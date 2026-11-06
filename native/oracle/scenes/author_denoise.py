#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the denoising oracle scenes (scenes/denoise/) byte for byte.

Committed scenes with the integrator's Open Image Denoise settings on (prefilter accurate, quality high, start sample 0,
the CPU device, the only denoising device the WebGPU backend uses) and, as controls, off; the shadow catcher pair also
requests the noisy passes. The WebGPU film is the WGSL render denoised by CPU Open Image Denoise, judged against the CPU
film denoised the same way. Light groups with denoising are refused by the product, so that combination has no scene.
Usage: python3 native/oracle/scenes/author_denoise.py [--check]."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HERE = ROOT/'denoise'
DENOISE = {'use_denoise': True, 'denoiser_type': 'openimagedenoise', 'denoiser_prefilter': 'accurate', 'denoiser_quality': 'high',
           'denoise_start_sample': 0, 'denoise_use_gpu': False}
NOISY = [('Noisy Image', 'combined'), ('Shadow Catcher Noisy', 'shadow_catcher'), ('Shadow Catcher Matte Noisy', 'shadow_catcher_matte')]
SOURCES = [('enclosure-emissive-panel', 'combinations/enclosure-emissive-panel.json', False),
           ('beckmann', 'bsdf/beckmann.json', False),
           ('catcher-point-light', 'shadow-catcher/catcher-point-light.json', True)]


def variant(name, source, denoise, noisy):
    document = json.loads((ROOT/source).read_text())
    suffix = ('on' if denoise else 'off')+('-noisy' if noisy else '')
    document['id'] = 'denoise/'+name+'-'+suffix
    document['description'] = source.removesuffix('.json')+(' with Open Image Denoise (prefilter accurate, quality high, start sample 0, CPU device)' if denoise else ' with denoising off (the control)')+(' and the noisy pass requests' if noisy else '')
    document['sourceFixture'] = 'native/oracle/scenes/author_denoise.py ('+source+')'
    integrator = next(entry for entry in document['nodes'] if entry.get('handle') == 'integrator')
    integrator['set'].update(DENOISE if denoise else {'use_denoise': False})
    if noisy:
        for pass_name, pass_type in NOISY:
            document['nodes'].append({'handle': 'pass-noisy-'+pass_type, 'create': 'pass', 'set': {'type': pass_type, 'name': pass_name, 'mode': 'noisy'}})
    return name+'-'+suffix+'.json', document


def scenes():
    out = {}
    for name, source, noisy in SOURCES:
        for denoise in (True, False):
            key, document = variant(name, source, denoise, noisy)
            out[key] = document
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
