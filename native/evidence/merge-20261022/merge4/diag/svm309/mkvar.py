#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4 diagnostic: WGSL variants of the merged node SVM probe (orig.wgsl).
vT: precise_disk_sincos and motion_acos bodies taken from lane S's module (unrolled Horner steps).
vG: svm_gabor_texture body emptied (Gabor graphs then differ; only non-Gabor paths are judged).
vTG: both."""
import re
def span(text, name):
    m = re.search(r'\bfn\s+'+re.escape(name)+r'\s*\(', text); i = text.index('{', m.end()); d = 0
    for j in range(i, len(text)):
        d += (text[j] == '{') - (text[j] == '}')
        if d == 0: return i, j+1
def take(dst, src, name):
    a, b = span(dst, name); c, e = span(src, name); return dst[:a]+src[c:e]+dst[b:]
o = open('orig.wgsl').read(); s = open('laneS.wgsl').read()
t = take(take(o, s, 'precise_disk_sincos'), s, 'motion_acos')
def nog(x): a, b = span(x, 'svm_gabor_texture'); return x[:a]+'{}'+x[b:]
open('vT.wgsl', 'w').write(t); open('vG.wgsl', 'w').write(nog(o)); open('vTG.wgsl', 'w').write(nog(t))
