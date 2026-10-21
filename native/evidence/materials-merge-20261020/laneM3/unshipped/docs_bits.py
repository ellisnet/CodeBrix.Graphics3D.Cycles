#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""M3: correct the feature-bit numbers M1/M2 wrote into native/DEVICE.md."""
import sys
from pathlib import Path
p=Path(sys.argv[1])/'native/DEVICE.md';s=p.read_text()
for a,b in [('(`SVM_NODE_*` feature bits\n30-47 in `pipelines.h`;','(`SVM_NODE_*` feature bits\n35-52 in `pipelines.h`; bits 30-31 are reserved for the shadow-terminator lane;'),
            ('`SVM_BSDF_PRINCIPLED` (bit 30), `SVM_BSDF_THIN_FILM` (31) and\n`SVM_BSDF_THIN_WALL` (32);','`SVM_BSDF_PRINCIPLED` (bit 32), `SVM_BSDF_THIN_FILM` (33) and\n`SVM_BSDF_THIN_WALL` (34);')]:
    assert s.count(a)==1,a;s=s.replace(a,b)
p.write_text(s);print('ok')
