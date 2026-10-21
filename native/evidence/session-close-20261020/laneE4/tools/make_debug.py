#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: debug probe variants of a capture's surface.wgsl: shade_surface_light writes 13 chosen
intermediate words of triangle_light_sample_native into the light-sample slot of its stage record.
Usage: make_debug.py SRC_CAPTURE OUT_CAPTURE STAGE   (STAGE in verts, sphere1, sphere2)"""
import shutil, sys
from pathlib import Path
src=Path(sys.argv[1]); dst=Path(sys.argv[2]); stage=sys.argv[3]
s=(src/'surface.wgsl').read_text()
decl='var<private> dbg:array<u32,13>;\nfn dbgset(i:u32,v:f32){if(i==0u){dbg[0]=bitcast<u32>(v);}else if(i==1u){dbg[1]=bitcast<u32>(v);}else if(i==2u){dbg[2]=bitcast<u32>(v);}else if(i==3u){dbg[3]=bitcast<u32>(v);}else if(i==4u){dbg[4]=bitcast<u32>(v);}else if(i==5u){dbg[5]=bitcast<u32>(v);}else if(i==6u){dbg[6]=bitcast<u32>(v);}else if(i==7u){dbg[7]=bitcast<u32>(v);}else if(i==8u){dbg[8]=bitcast<u32>(v);}else if(i==9u){dbg[9]=bitcast<u32>(v);}else if(i==10u){dbg[10]=bitcast<u32>(v);}else if(i==11u){dbg[11]=bitcast<u32>(v);}else{dbg[12]=bitcast<u32>(v);}}\n'
a='fn triangle_light_sample_native('; assert s.count(a)==1; s=s.replace(a,decl+a)
w='light_sample_write(at+308u,ls);'; assert s.count(w)==1
s=s.replace(w,'for(var j=0u;j<13u;j++){store_word(at+308u+j*4u,dbg[j]);}')
def after(anchor,code):
    global s; assert s.count(anchor)==1,anchor; s=s.replace(anchor,anchor+code)
if stage=='verts':
    after('var ls:NativeLightSample;let v=triangle_light_vertices(object,prim,time);if(v.error!=0u){return ls;}',
      'dbgset(0u,v.a.x);dbgset(1u,v.a.y);dbgset(2u,v.a.z);dbgset(3u,v.b.x);dbgset(4u,v.b.y);dbgset(5u,v.b.z);dbgset(6u,v.c.x);dbgset(7u,v.c.y);dbgset(8u,v.c.z);dbgset(9u,P.x);dbgset(10u,P.y);dbgset(11u,P.z);dbgset(12u,f32(object*1000u+prim));')
elif stage=='sphere1':
    after('let ca=rounded_dot(rounded_safe_normalize(rounded_cross(A,B)),rounded_safe_normalize(rounded_cross(A,C)));',
      'dbgset(0u,A.x);dbgset(1u,A.y);dbgset(2u,A.z);dbgset(3u,B.x);dbgset(4u,B.y);dbgset(5u,B.z);dbgset(6u,C.x);dbgset(7u,C.y);dbgset(8u,C.z);dbgset(9u,cb);dbgset(10u,cc);dbgset(11u,solid);dbgset(12u,ca);')
elif stage=='sphere2':
    after('var q=1.0;if(denominator!=0.0){q=rounded_divide(numerator,denominator);}',
      'dbgset(0u,sa);dbgset(1u,alpha);dbgset(2u,sc.x);dbgset(3u,sc.y);dbgset(4u,u);dbgset(5u,w);dbgset(6u,numerator);dbgset(7u,denominator);dbgset(8u,q);dbgset(9u,random.x);dbgset(10u,random.y);')
    after('let dp=rounded_dot(Cp,B);','dbgset(11u,dp);')
    after('if(!c_ray_triangle_intersect(P,ls.D,0.0,bitcast<f32>(0x7f7fffffu),v.a,v.b,v.c,&hit_u,&hit_v,&hit_t)||solid==0.0){','dbgset(12u,-7.0);')
elif stage=='sphere3':
    after('let dp=rounded_dot(Cp,B);','dbgset(0u,U.x);dbgset(1u,U.y);dbgset(2u,U.z);dbgset(3u,Cp.x);dbgset(4u,Cp.y);dbgset(5u,Cp.z);dbgset(6u,dp);')
    after('let z=rounded_add(1.0,-random.y*rounded_add(1.0,-dp));','dbgset(7u,z);')
    after('ls.D=rounded_add3(z*B,triangle_light_sin_from_cos(z)*rounded_safe_normalize(rounded_add3(Cp,-dp*B)));','dbgset(8u,ls.D.x);dbgset(9u,ls.D.y);dbgset(10u,ls.D.z);')
    after('var hit_u=0.0;var hit_v=0.0;var hit_t=0.0;','dbgset(11u,triangle_light_sin_from_cos(z));')
    after('if(!c_ray_triangle_intersect(P,ls.D,0.0,bitcast<f32>(0x7f7fffffu),v.a,v.b,v.c,&hit_u,&hit_v,&hit_t)||solid==0.0){','dbgset(12u,-7.0);')
if dst.exists(): shutil.rmtree(dst)
shutil.copytree(src,dst,ignore=shutil.ignore_patterns('run.arena.bin','run.*.film.bin','result.json'))
(dst/'surface.wgsl').write_text(s); print(dst)
