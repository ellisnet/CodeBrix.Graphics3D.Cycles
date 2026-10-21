#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Measurement-only Python port of pipelines.cpp specialize_wgsl (marker regions)."""
import sys
NAMES={'SVM_BSDF_MICROFACET':{'SVM_BSDF_GLOSSY','SVM_BSDF_GLASS','SVM_BSDF_PRINCIPLED'}}
def specialize(text,enabled):
    out=[];stack=[]
    for line in text.split('\n')[:-1] if text.endswith('\n') else text.split('\n'):
        if line.startswith('//#if '):
            rest=line[6:];name,_,fallback=rest.partition(' else ')
            keep=bool(NAMES.get(name,{name})&enabled)
            outer=all(k for k,_ in stack);stack.append((keep,fallback))
            if outer and not keep and fallback:out.append(fallback)
            continue
        if line.startswith('//#endif'):stack.pop();continue
        if all(k for k,_ in stack):out.append(line)
    return '\n'.join(out)+'\n'
if __name__=='__main__':
    src=open(sys.argv[1]).read();enabled=set(sys.argv[3].split(','))
    open(sys.argv[2],'w').write(specialize(src,enabled))
