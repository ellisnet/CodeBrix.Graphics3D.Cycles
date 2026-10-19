// Cycles SVM adaptation: SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Requires a read-only storage array named code and the Closure helpers.
fn stack_float(stack: ptr<function,array<f32,255>>, bits: u32) -> f32 {
    if ((bits>>8u)==0x7fc000u) { return (*stack)[bits&255u]; }
    return bitcast<f32>(bits);
}
fn stack_color(stack: ptr<function,array<f32,255>>, offset: u32) -> vec3<f32> {
    if ((code[offset]>>8u)==0x7fc000u) {
        let k=code[offset]&255u; return vec3<f32>((*stack)[k],(*stack)[k+1u],(*stack)[k+2u]);
    }
    return bitcast<vec3<f32>>(vec3<u32>(code[offset],code[offset+1u],code[offset+2u]));
}
fn decode(shader: u32, n: vec3<f32>, wi: vec3<f32>) -> Closure {
    var c: Closure; var weight=vec3<f32>(0); var stack: array<f32,255>;
    let size=arrayLength(&code);
    if (shader>=size/4u) { c.error=1u; return c; }
    var offset=shader*4u;
    for(var instruction=0u;instruction<256u;instruction++) {
        if (offset>=size) { c.error=1u; return c; }
        let op=code[offset]; offset++;
        if (op==OP_END) { return c; }
        if (op==OP_JUMP) {
            if (offset+3u>size) { c.error=2u; return c; }
            offset=code[offset]; continue;
        }
        if (op==OP_GEOMETRY) {
            if (offset+2u>size) { c.error=3u; return c; }
            let bits=code[offset]; let out=bits>>24u;
            if ((bits&255u)!=1u || out+2u>=255u || (bits&0xffff00u)!=0u) { c.error=4u; return c; }
            stack[out]=n.x; stack[out+1u]=n.y; stack[out+2u]=n.z; offset+=2u; continue;
        }
        if (op==OP_SET_WEIGHT) {
            if (offset+3u>size) { c.error=5u; return c; }
            weight=bitcast<vec3<f32>>(vec3<u32>(code[offset],code[offset+1u],code[offset+2u])); offset+=3u; continue;
        }
        if (op==OP_BSDF) {
            if (offset+2u>size) { c.error=6u; return c; }
            let kind=code[offset]; let mix_offset=code[offset+1u]&255u; offset+=2u;
            var mix=1.0; if (mix_offset!=255u) { mix=stack[mix_offset]; }
            var normal_offset: u32; var roughness=0.0; var color=vec3<f32>(0);
            if (kind==DIFFUSE) {
                if (offset+5u>size) { c.error=7u; return c; }
                if ((code[offset]>>8u)==0x7fc000u && (code[offset]&255u)>252u) { c.error=14u; return c; }
                if ((code[offset+3u]>>8u)==0x7fc000u && (code[offset+3u]&255u)==255u) { c.error=15u; return c; }
                color=stack_color(&stack,offset); roughness=stack_float(&stack,code[offset+3u]);
                normal_offset=code[offset+4u]&255u; offset+=5u;
            } else if (kind==TRANSLUCENT) {
                if (offset+2u>size) { c.error=8u; return c; }
                normal_offset=code[offset+1u]&255u; offset+=2u;
            } else { c.error=9u; return c; }
            var normal=n;
            if (normal_offset!=255u) {
                if (normal_offset+2u>=255u) { c.error=10u; return c; }
                normal=vec3<f32>(stack[normal_offset],stack[normal_offset+1u],stack[normal_offset+2u]);
            }
            let magnitude=length(normal); if (magnitude!=0.0) { normal*=1.0/magnitude; } else { normal=n; }
            let nonnegative=max(weight*mix,vec3<f32>(0));
            let sample_weight=abs((nonnegative.x+nonnegative.y+nonnegative.z)*(1.0/3.0));
            if (sample_weight<1e-5 || mix==0.0) { continue; }
            if (c.count!=0u) { c.error=11u; return c; }
            c.count=1u; c.weight=nonnegative; c.sample_weight=sample_weight; c.normal=normal; c.kind=kind;
            if (kind==DIFFUSE && roughness>=1e-5) {
                c.kind=OREN_NAYAR; oren_setup(&c,clamp(color,vec3<f32>(0),vec3<f32>(1)),dot(normal,wi),roughness);
            }
            continue;
        }
        c.error=12u; return c;
    }
    c.error=13u; return c;
}
