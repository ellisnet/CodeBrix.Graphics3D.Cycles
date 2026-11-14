#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write (or with --check verify) tools/licensing/replacements/tables_d65_codebrix.h.

Reads the number tokens of one array from the unchanged documentation copy third_party/pbrt-v4/src/pbrt/util/spectrum.cpp
and writes the 81 values for 380 to 780 nm, each with exactly the digit string of the copy and the suffix f, with the
normalisation constant computed here from those values and the colour-matching table of the vendored kernel/tables.h.
Nothing but numbers is read from the copy.
The origin record is in THIRD-PARTY-NOTICES.txt.
"""
import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COPY = ROOT/'third_party/pbrt-v4/src/pbrt/util/spectrum.cpp'
COPY_SHA256 = '4c907c7655ad5431bd2797692b963ac34bc6a533c7a053a16e6988aea03c750b'
ARRAY = 'CIE_Illum_D6500'
TABLES = ROOT/'third_party/cycles/src/kernel/tables.h'
OUTPUT = Path(__file__).parent/'replacements/tables_d65_codebrix.h'
VENDORED = ROOT/'third_party/cycles/src/kernel/tables_d65_codebrix.h'
FIRST, LAST, STEP = 380, 780, 5
NUMBER = re.compile(r'[0-9]+\.[0-9]+')


def f32(value):
    """The 32-bit float nearest to VALUE, as a Python float."""
    return struct.unpack('<f', struct.pack('<f', value))[0]


def shortest(value):
    """Shortest decimal text that parses to the same 32-bit float."""
    for digits in range(1, 18):
        text = '%.*g' % (digits, value)
        if f32(float(text)) == f32(value):
            return text
    raise RuntimeError('no round-trip text')


def source_tokens():
    data = COPY.read_bytes()
    if hashlib.sha256(data).hexdigest() != COPY_SHA256:
        raise RuntimeError('The documentation copy is not the recorded file: ' + str(COPY))
    text = data.decode('ascii')
    opening = 'const Float ' + ARRAY + '[] = {'
    if text.count(opening) != 1:
        raise RuntimeError('Array not found exactly once: ' + ARRAY)
    body = text[text.index(opening) + len(opening):]
    body = body[:body.index('};')]
    tokens = [t.strip() for t in body.split(',') if t.strip()]
    if any(not NUMBER.fullmatch(t) for t in tokens) or len(tokens) % 2:
        raise RuntimeError('Unexpected text in the array')
    pairs = list(zip(tokens[0::2], tokens[1::2]))
    wanted = list(range(FIRST, LAST + 1, STEP))
    by_wavelength = {float(w): v for w, v in pairs}
    if len(by_wavelength) != len(pairs) or any(float(w) not in by_wavelength for w in wanted):
        raise RuntimeError('The array does not hold every wavelength from %d to %d nm' % (FIRST, LAST))
    return [by_wavelength[float(w)] for w in wanted]


def colour_matching_y():
    text = TABLES.read_text()
    opening = 'ccl_inline_constant float cie_color_match[][3] = {'
    if text.count(opening) != 1:
        raise RuntimeError('Colour-matching table not found in kernel/tables.h')
    body = text[text.index(opening) + len(opening):]
    body = body[:body.index('};')]
    rows = re.findall(r'\{\s*([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f\s*\}', body)
    if len(rows) != 81:
        raise RuntimeError('Colour-matching table does not have 81 rows')
    return [f32(float(row[1])) for row in rows]


def normalisation(tokens):
    """The kernel interpolates the colour-matching table and the spectrum linearly, each on its own, inside every 5 nm
    interval and multiplies them; the dispersion weight is that product divided by a probability relative to a uniform
    choice over 380 to 780 nm. Its mean is therefore the mean of the product over the range, which for one interval
    with end values (y0, y1) and (s0, s1) is (2 y0 s0 + y0 s1 + y1 s0 + 2 y1 s1) / 6. The constant makes the mean of
    the Y (luminance) row equal to 1."""
    y = colour_matching_y()
    s = [f32(float(t)) for t in tokens]
    total = sum((2*y[i]*s[i] + y[i]*s[i+1] + y[i+1]*s[i] + 2*y[i+1]*s[i+1]) / 6 for i in range(len(s) - 1))
    return (len(s) - 1) / total


def render():
    tokens = source_tokens()
    constant = shortest(normalisation(tokens))
    lines = ['// Copyright (c) 2026 Jeremy Ellis and contributors',
             '// SPDX-License-Identifier: Apache-2.0',
             '',
             '/* Written by tools/licensing/d65_numbers.py (source: tools/licensing/replacements/tables_d65_codebrix.h);',
             ' * included by kernel/tables.h, inside its namespace, and by nothing else.',
             ' *',
             ' * The numbers of cie_d65_spd were copied from the array CIE_Illum_D6500 of spectrum.cpp. The unchanged copy of',
             ' * that file in this repository is third_party/pbrt-v4/src/pbrt/util/spectrum.cpp. Only the numbers were',
             ' * copied. The complete origin record is in THIRD-PARTY-NOTICES.txt at the repository root. */',
             '',
             '/* CIE_D65_NORMALIZATION = 80 / sum over the 80 intervals of (2 y0 s0 + y0 s1 + y1 s0 + 2 y1 s1) / 6, with y the',
             ' * second column of cie_color_match and s cie_d65_spd: the mean over 380 to 780 nm of the product of the two',
             ' * linearly interpolated tables, times this constant, is 1. Computed by the same script. */',
             '#define CIE_D65_NORMALIZATION ' + constant + 'f',
             '',
             '/* 81 values, 380 to 780 nm, 5 nm steps. */',
             'ccl_inline_constant float cie_d65_spd[] = {']
    for start in range(0, len(tokens), 6):
        lines.append('  ' + ' '.join(t + 'f,' for t in tokens[start:start + 6]))
    lines.append('};')
    return '\n'.join(lines) + '\n'


def check():
    expected = render()
    errors = []
    for path in (OUTPUT, VENDORED):
        if not path.is_file():
            errors.append('missing: ' + str(path.relative_to(ROOT)))
            continue
        text = path.read_text()
        if text != expected:
            errors.append('differs from what the documentation copy gives: ' + str(path.relative_to(ROOT)))
        body = text[text.index('cie_d65_spd[] = {'):] if 'cie_d65_spd[] = {' in text else ''
        written = re.findall(r'([0-9.]+)f,', body)
        source = source_tokens()
        # Token for token: each value is the digit string of the documentation copy with only the suffix f appended.
        if written != source:
            errors.append('a number is not written with the digit string of the documentation copy: '
                          + str(path.relative_to(ROOT)))
        if len(written) != len(source) or any(struct.pack('<f', float(a)) != struct.pack('<f', float(b))
                                              for a, b in zip(written, source)):
            errors.append('a number is not the same 32-bit float as in the documentation copy: '
                          + str(path.relative_to(ROOT)))
    return errors


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true',
                        help='fail if the numbers file (source or vendored) and the documentation copy disagree')
    arguments = parser.parse_args()
    if arguments.check:
        problems = check()
        for problem in problems:
            print(problem)
        print('numbers file check:', 'FAILED' if problems else 'passed (81 values, digit strings equal, 32-bit equal, constant recomputed)')
        sys.exit(1 if problems else 0)
    OUTPUT.write_text(render())
    print(OUTPUT.relative_to(ROOT))
