#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: derive probe captures (copies) of a capture with the triangle-light cache rewritten.
Usage: make_variants.py SRC_CAPTURE_DIR OUT_ROOT"""
import shutil, sys
from pathlib import Path
src=Path(sys.argv[1]); root=Path(sys.argv[2])
OLD_CACHE='''struct TriangleLightCacheEntry {key:vec3<u32>,v:TriangleVertices}
var<private> triangle_light_cache:array<TriangleLightCacheEntry,4>;
var<private> triangle_light_cache_count:u32;
fn triangle_light_vertices(object:u32,prim:u32,time:f32)->TriangleVertices{
    let key=vec3<u32>(object,prim,bitcast<u32>(time));
    for(var k=0u;k<triangle_light_cache_count;k++){if(all(triangle_light_cache[k].key==key)){return triangle_light_cache[k].v;}}
    fail();var v:TriangleVertices;v.error=1u;return v;
}'''
NEW_CACHE='''var<private> triangle_light_cache:array<u32,52>;
var<private> triangle_light_cache_count:u32;
fn triangle_light_cache_store(k:u32,key:vec3<u32>,v:TriangleVertices){
    let b=k*13u;triangle_light_cache[b]=key.x;triangle_light_cache[b+1u]=key.y;triangle_light_cache[b+2u]=key.z;
    triangle_light_cache[b+3u]=bitcast<u32>(v.a.x);triangle_light_cache[b+4u]=bitcast<u32>(v.a.y);triangle_light_cache[b+5u]=bitcast<u32>(v.a.z);
    triangle_light_cache[b+6u]=bitcast<u32>(v.b.x);triangle_light_cache[b+7u]=bitcast<u32>(v.b.y);triangle_light_cache[b+8u]=bitcast<u32>(v.b.z);
    triangle_light_cache[b+9u]=bitcast<u32>(v.c.x);triangle_light_cache[b+10u]=bitcast<u32>(v.c.y);triangle_light_cache[b+11u]=bitcast<u32>(v.c.z);
    triangle_light_cache[b+12u]=v.error;
}
fn triangle_light_vertices(object:u32,prim:u32,time:f32)->TriangleVertices{
    let key=vec3<u32>(object,prim,bitcast<u32>(time));
    for(var k=0u;k<triangle_light_cache_count;k++){let b=k*13u;
        if(triangle_light_cache[b]==key.x&&triangle_light_cache[b+1u]==key.y&&triangle_light_cache[b+2u]==key.z){var v:TriangleVertices;
            v.a=vec3<f32>(bitcast<f32>(triangle_light_cache[b+3u]),bitcast<f32>(triangle_light_cache[b+4u]),bitcast<f32>(triangle_light_cache[b+5u]));
            v.b=vec3<f32>(bitcast<f32>(triangle_light_cache[b+6u]),bitcast<f32>(triangle_light_cache[b+7u]),bitcast<f32>(triangle_light_cache[b+8u]));
            v.c=vec3<f32>(bitcast<f32>(triangle_light_cache[b+9u]),bitcast<f32>(triangle_light_cache[b+10u]),bitcast<f32>(triangle_light_cache[b+11u]));
            v.error=triangle_light_cache[b+12u];return v;}}
    fail();var v:TriangleVertices;v.error=1u;return v;
}'''
OLD_FILL='''        triangle_light_cache[k]=TriangleLightCacheEntry(keys[k],triangle_light_vertices_compute(keys[k].x,keys[k].y,bitcast<f32>(keys[k].z)));'''
NEW_FILL='''        triangle_light_cache_store(k,keys[k],triangle_light_vertices_compute(keys[k].x,keys[k].y,bitcast<f32>(keys[k].z)));'''
# keys: four scalar-vector locals selected by constant index instead of a dynamically indexed function array
OLD_KEYS_DECL='var keys:array<vec3<u32>,4>;var count=0u;'
NEW_KEYS_DECL='var key0:vec3<u32>;var key1:vec3<u32>;var key2:vec3<u32>;var key3:vec3<u32>;var count=0u;'
def keyset(s):
    s=s.replace(OLD_KEYS_DECL,NEW_KEYS_DECL)
    for e in ['vec3<u32>(sd.object_id,sd.prim,bitcast<u32>(sd.time))','vec3<u32>(sd.object_id,sd.prim,bitcast<u32>(-1.0))','vec3<u32>(object,u32(selection.prim),bitcast<u32>(sd.time))','vec3<u32>(object,u32(selection.prim),bitcast<u32>(-1.0))']:
        old='keys[count]='+e+';count++;'; assert s.count(old)==1,old
        s=s.replace(old,'{let e='+e+';if(count==0u){key0=e;}else if(count==1u){key1=e;}else if(count==2u){key2=e;}else{key3=e;}}count++;')
    a='keys[k],triangle_light_vertices_compute(keys[k].x,keys[k].y,bitcast<f32>(keys[k].z))'; assert s.count(a)==1
    s=s.replace(a,'select(select(select(key3,key2,k==2u),key1,k==1u),key0,k==0u),triangle_light_vertices_compute(select(select(select(key3,key2,k==2u),key1,k==1u),key0,k==0u).x,select(select(select(key3,key2,k==2u),key1,k==1u),key0,k==0u).y,bitcast<f32>(select(select(select(key3,key2,k==2u),key1,k==1u),key0,k==0u).z))')
    assert 'keys[' not in s; return s
def cache(s):
    assert s.count(OLD_CACHE)==1 and s.count(OLD_FILL)==1
    return s.replace(OLD_CACHE,NEW_CACHE).replace(OLD_FILL,NEW_FILL)
variants={'vcache':cache,'vkeys':lambda s:keyset(s),'vboth':lambda s:keyset(cache(s))}
for name,fn in variants.items():
    dst=root/name
    if dst.exists(): shutil.rmtree(dst)
    shutil.copytree(src,dst,ignore=shutil.ignore_patterns('run.arena.bin','run.*.film.bin','result.json'))
    (dst/'surface.wgsl').write_text(fn((src/'surface.wgsl').read_text()))
    print(name)
