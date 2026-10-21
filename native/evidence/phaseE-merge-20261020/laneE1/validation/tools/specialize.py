#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Measurement only: Python mirror of specialize_wgsl (pipelines.cpp) for offline size estimates."""
import sys
def specialize(source, enabled):
    out = []; stack = []
    active = lambda: all(k for k, _ in stack)
    for line in source.split('\n'):
        if line.startswith('//#if '):
            rest = line[6:]; name, _, fallback = rest.partition(' else ')
            outer = active(); stack.append((name in enabled, fallback))
            if outer and not stack[-1][0] and fallback: out.append(fallback)
            continue
        if line.startswith('//#endif'): stack.pop(); continue
        if active(): out.append(line)
    return '\n'.join(out)
if __name__ == '__main__':
    src = open(sys.argv[1]).read()
    if src.startswith('R"CBWGSL('): src = src[len('R"CBWGSL('):src.rindex(')CBWGSL"')]
    sys.stdout.write(specialize(src, set(sys.argv[3].split(',')) if len(sys.argv) > 3 else set()))
