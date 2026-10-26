#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Author the procedural environment (HDRI) of the bench oracle scene and its scene variant.

    python3 native/oracle/scenes/author_environment.py --bundle DIR [--scene NAME] [--check]

DIR is a bench bundle written by the glTF tool (scene.json plus raw files; see
native/oracle/README.md). The script writes into DIR:
  environment.float4   512 x 256 equirectangular float RGBA (little-endian, bottom row first):
                       a sky gradient over the world +Y axis (the bench is Y-up), a dark ground,
                       a small very bright sun disc (radiance 2000) and three soft bright blobs,
                       so environment importance sampling has a sharp peak
  bench_01-hdri.json   the bench scene with the converter's default sun removed and the world
                       replaced by an Environment Texture (equirectangular, linear, scene_linear)
                       bound to environment.float4, background strength 1, world emission sampling
                       "auto", and a background light object (use_mis, map_resolution 512) so the
                       world is importance sampled with MIS, as Blender's sync sets it up
The pixels use only IEEE double additions, multiplications, divisions and square roots (no libm
calls; sine and cosine are fixed polynomials here) and are rounded to float32, so every platform
writes the same bytes. --check compares the output with the SHA-256 recorded in this file.
Nothing is downloaded; no image library is used.
"""
import argparse,array,hashlib,json,sys
from pathlib import Path
WIDTH,HEIGHT=512,256
PI=3.141592653589793
EXPECTED_SHA256='14dd0de9b800b27ed1cdc52e78e8dd515cc75bf70d033e4b15f817a8c2cfdbaa'  # also in native/oracle/scenes/assets/manifest.json


def sin_poly(x):
    """sin(x) for x in [-pi, pi] with a fixed odd polynomial (no libm)."""
    x2=x*x;term=x;total=x
    for k in range(1,12):
        term=-term*x2/((2*k)*(2*k+1));total+=term
    return total


def cos_poly(x):
    x2=x*x;term=1.0;total=1.0
    for k in range(1,12):
        term=-term*x2/((2*k-1)*(2*k));total+=term
    return total


def normalize(v):
    length=(v[0]*v[0]+v[1]*v[1]+v[2]*v[2])**0.5
    return (v[0]/length,v[1]/length,v[2]/length)


def lobe(direction,axis,cos_edge,radiance):
    """A soft lobe: (t^2)^2 falloff from the axis to the cone edge, zero beyond."""
    c=direction[0]*axis[0]+direction[1]*axis[1]+direction[2]*axis[2]
    if c<=cos_edge:return 0.0
    t=(c-cos_edge)/(1.0-cos_edge)
    return radiance*t*t*t*t


SUN=normalize((0.45,0.62,0.64))
BLOBS=[(normalize((-0.7,0.35,0.6)),0.93,(18.0,14.0,9.0)),(normalize((0.2,0.15,-0.95)),0.9,(6.0,8.0,14.0)),
       (normalize((-0.4,0.8,-0.45)),0.95,(10.0,10.0,10.0))]


def radiance(d):
    e=d[1]
    if e>=0.0:
        t=e**0.5
        rgb=[0.62+(0.12-0.62)*t,0.72+(0.28-0.72)*t,0.88+(0.75-0.88)*t]
    else:
        rgb=[0.11,0.095,0.08]
    c=d[0]*SUN[0]+d[1]*SUN[1]+d[2]*SUN[2]
    if c>=0.99966:  # about 1.5 degrees: a sharp, very bright disc
        rgb=[2000.0,1900.0,1750.0]
    else:
        halo=lobe(d,SUN,0.985,40.0)
        rgb=[rgb[0]+halo,rgb[1]+halo*0.95,rgb[2]+halo*0.85]
    for axis,edge,colour in BLOBS:
        w=lobe(d,axis,edge,1.0)
        rgb=[rgb[i]+w*colour[i] for i in range(3)]
    return rgb


def author():
    pixels=array.array('f')
    for y in range(HEIGHT):
        # Cycles equirectangular: v = atan2(dir.z, hypot(dir.x, dir.y)) / pi + 0.5, u = -atan2(dir.y, dir.x) / (2 pi) + 0.5.
        theta=((y+0.5)/HEIGHT-0.5)*PI
        cz,sz=cos_poly(theta),sin_poly(theta)
        for x in range(WIDTH):
            phi=-((x+0.5)/WIDTH-0.5)*2.0*PI
            d=(cz*cos_poly(phi),cz*sin_poly(phi),sz)
            r=radiance(d)
            pixels.extend((r[0],r[1],r[2],1.0))
    if sys.byteorder!='little':pixels.byteswap()
    return pixels.tobytes()


def variant(scene):
    nodes=[]
    for entry in scene['nodes']:
        if entry['handle']=='light-shader' or entry['handle'].startswith('light-'):continue
        if entry.get('create')=='object' and str(entry.get('set',{}).get('geometry','')).startswith('light-'):continue
        if entry['handle']=='world':
            entry={'handle':'world','create':'shader','graph':{'nodes':[
                {'handle':'environment','type':'environment_texture','set':{'image':'environment','colorspace':'scene_linear',
                 'interpolation':'linear','projection':'equirectangular','alpha_type':'ignore'}},
                {'handle':'background','type':'background_shader','set':{'strength':1.0}}],
                'connect':[['environment','Color','background','Color'],['background','Background','output','Surface']]},
                'set':{'emission_sampling_method':'auto'}}
        nodes.append(entry)
        if entry['handle']=='world':
            # Standalone Cycles samples the world only through a background light object
            # (Blender's sync creates it); its map resolution is set explicitly (512 -> 512x256).
            nodes.append({'handle':'world-light','create':'backgroundlight','set':{'use_mis':True,'map_resolution':WIDTH,
                          'strength':[1.0,1.0,1.0],'used_shaders':['world']}})
            nodes.append({'handle':'world-light-object','create':'object','set':{'geometry':'world-light'}})
    out=dict(scene);out['nodes']=nodes
    out['id']=scene['id']+'-hdri'
    out['description']=scene['description']+'; lit by the procedural environment of author_environment.py (importance sampled), no sun'
    out['images']=list(scene.get('images',[]))+[{'handle':'environment','width':WIDTH,'height':HEIGHT,'format':'float4',
        'file':'environment.float4','colorspace':'linear'}]
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--bundle',type=Path,required=True);parser.add_argument('--check',action='store_true')
    parser.add_argument('--scene',default='scene.json',help='the bench scene document in DIR (default scene.json)')
    args=parser.parse_args()
    data=author();digest=hashlib.sha256(data).hexdigest()
    (args.bundle/'environment.float4').write_bytes(data)
    scene=json.loads((args.bundle/args.scene).read_text())
    (args.bundle/'bench_01-hdri.json').write_text(json.dumps(variant(scene),indent=2)+'\n',newline='\n')
    print(f'environment.float4 {WIDTH}x{HEIGHT} float4 sha256 {digest}')
    if args.check and EXPECTED_SHA256 and digest!=EXPECTED_SHA256:
        print('author_environment.py: SHA-256 differs from the recorded value',EXPECTED_SHA256,file=sys.stderr);return 1
    return 0


if __name__=='__main__':sys.exit(main())
