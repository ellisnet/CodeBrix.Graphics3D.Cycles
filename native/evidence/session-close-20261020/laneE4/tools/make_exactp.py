#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: probe variant of a capture: contraction-proof ShaderData P (triangle point from u/v) and/or
oracle-grouped, contraction-proof static object transforms in wavefront_shader_data.
Usage: make_exactp.py SRC_CAPTURE OUT_CAPTURE WHAT  (WHAT: point | tfm | both); applies to every module that has it."""
import shutil, sys
from pathlib import Path
src=Path(sys.argv[1]); dst=Path(sys.argv[2]); what=sys.argv[3]
OLD_T='''fn surface_transform_direction(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec4(address);let y=heap_vec4(address+16u);let z=heap_vec4(address+32u);
    return v.x*vec3<f32>(x.x,y.x,z.x)+(v.y*vec3<f32>(x.y,y.y,z.y)+v.z*vec3<f32>(x.z,y.z,z.z));
}
fn surface_transform_point(address:u32,v:vec3<f32>)->vec3<f32>{
    return surface_transform_direction(address,v)+vec3<f32>(heap_float(address+12u),heap_float(address+28u),heap_float(address+44u));
}'''
NEW_T='''fn surface_transform_row(t:vec4<f32>,v:vec3<f32>,point:bool)->f32{
    if(NATIVE_TRIANGLE_ARITHMETIC==0u){
        let w=select(bitcast<f32>(bitcast<u32>(t.w)&0x80000000u),t.w,point);
        return rounded_add(rounded_add(v.x*t.x,v.y*t.y),rounded_add(v.z*t.z,w));
    }
    var sum=v.z*t.z;if(point){sum=rounded_add(t.w,sum);}
    return rounded_add(rounded_add(sum,v.y*t.y),v.x*t.x);
}
fn surface_transform_vector(address:u32,v:vec3<f32>,point:bool)->vec3<f32>{
    return vec3<f32>(surface_transform_row(heap_vec4(address),v,point),surface_transform_row(heap_vec4(address+16u),v,point),surface_transform_row(heap_vec4(address+32u),v,point));
}
fn surface_transform_direction(address:u32,v:vec3<f32>)->vec3<f32>{return surface_transform_vector(address,v,false);}
fn surface_transform_point(address:u32,v:vec3<f32>)->vec3<f32>{return surface_transform_vector(address,v,true);}'''
OLD_P='sd.P=(a+sd.u*sd.dPdu)+sd.v*sd.dPdv;'
NEW_P='sd.P=rounded_add3(rounded_add3(a,sd.u*sd.dPdu),sd.v*sd.dPdv);'
if dst.exists(): shutil.rmtree(dst)
shutil.copytree(src,dst,ignore=shutil.ignore_patterns('run.arena.bin','run.*.film.bin','result.json'))
for f in dst.glob('*.wgsl'):
    s=f.read_text(); n=0
    if what in('tfm','both') and OLD_T in s: s=s.replace(OLD_T,NEW_T); n+=1
    if what in('point','both') and OLD_P in s: s=s.replace(OLD_P,NEW_P); n+=1
    if n: f.write_text(s); print(f.name,n)
