#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E2: list a native command stream (kind, pipeline, address/length, uniform params)."""
import struct, sys
NAMES=["control","init_camera","intersect_closest","shade_background","shade_surface","prepare_surface","intersect_shadow","shade_shadow","shade_light_forward","prepare_shader_data","intersect_volume_stack","shade_volume","shade_shadow_transparent","shade_surface_light","shader_eval_volume_density","connect_surface_light","prepare_shadow_shader_data"]
data=open(sys.argv[1]+'/run.commands.bin','rb').read(); lo=int(sys.argv[2]) if len(sys.argv)>2 else 1; hi=int(sys.argv[3]) if len(sys.argv)>3 else 10**9
c=0;n=0
while c<len(data):
    k,a,l=struct.unpack_from('<3I',data,c);c+=12;n+=1;extra=''
    if k==1: c+=l; extra='upload'
    elif k==2: extra='clear'
    elif k in(3,5):
        wg=64
        if k==5: wg,=struct.unpack_from('<I',data,c);c+=4
        par=struct.unpack_from('<32I',data,c);c+=128;extra='dispatch %s n=%d wg=%d p=%s'%(NAMES[a],l,wg,list(par[:20]))
    elif k==4:
        v=struct.unpack_from('<%dI'%(l//4),data,c);c+=l;extra='readback %s'%(list(v[:8]))
    if lo<=n<=hi: print(n,k,a,l,extra)
