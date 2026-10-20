#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the starter cbcOracleScene files. Rerunning reproduces them exactly.

starter/diffuse-triangle repeats native/tests/host/consumer.c fixture() call for
call, so its CPU film can be compared bitwise with that lifecycle test. The other
scenes set every integrator, film and camera control the oracle relies on.
"""
import json,math
from pathlib import Path
HERE=Path(__file__).resolve().parent
IDENTITY=[1,0,0,0, 0,1,0,0, 0,0,1,0]


def translate(x,y,z):return [1,0,0,x, 0,1,0,y, 0,0,1,z]


def integrator(**extra):
 values=dict(sampling_pattern='tabulated_sobol',seed=0,use_adaptive_sampling=False,use_light_tree=False,use_guiding=False,
  use_denoise=False,min_bounce=3,max_bounce=8,max_diffuse_bounce=4,max_glossy_bounce=4,max_transmission_bounce=8,
  max_volume_bounce=0,transparent_min_bounce=0,transparent_max_bounce=8,sample_clamp_direct=0.0,sample_clamp_indirect=0.0,
  caustics_reflective=False,caustics_refractive=False,filter_glossy=0.0,light_sampling_threshold=0.01,use_sample_subset=False)
 values.update(extra);return dict(handle='integrator',scene='integrator',set=values)


def common(width,height,fov,camera):
 return [integrator(),
  dict(handle='camera',scene='camera',set=dict(camera_type='perspective',fov=fov,nearclip=0.1,farclip=100.0,matrix=camera)),
  dict(handle='film',scene='film',set=dict(exposure=1.0,filter_type='box',filter_width=1.0))]


def world(color,strength):
 return [dict(handle='world',create='shader',graph=dict(nodes=[dict(handle='b',type='background_shader',set=dict(color=color,strength=strength))],
   connect=[['b','Background','output','Surface']]),set=dict(emission_sampling_method='none')),
  dict(handle='bg',scene='background',set=dict(transparent=False,shader='world'))]


def material(handle,node_type,settings,output='BSDF'):
 return dict(handle=handle,create='shader',graph=dict(nodes=[dict(handle='m',type=node_type,set=settings)],connect=[['m',output,'output','Surface']]),
  set=dict(emission_sampling_method='none'))


def mesh(handle,positions,indices,shader,tfm=IDENTITY,attributes=None):
 data=dict(positions=[round(v,7) for v in positions],indices=indices)
 if attributes:data['attributes']=attributes
 return [dict(handle=handle,create='mesh',mesh=data,set=dict(used_shaders=[shader])),
  dict(handle=handle+'-object',create='object',set=dict(geometry=handle,tfm=tfm))]


def sphere(radius,segments,rings):
 positions=[0,radius,0];indices=[]
 for r in range(1,rings):
  theta=math.pi*r/rings
  for s in range(segments):
   phi=2*math.pi*s/segments
   positions+= [radius*math.sin(theta)*math.cos(phi),radius*math.cos(theta),radius*math.sin(theta)*math.sin(phi)]
 positions+=[0,-radius,0];bottom=len(positions)//3-1
 ring=lambda r,s:1+(r-1)*segments+s%segments
 for s in range(segments):indices+=[0,ring(1,s+1),ring(1,s)]
 for r in range(1,rings-1):
  for s in range(segments):indices+=[ring(r,s),ring(r,s+1),ring(r+1,s+1), ring(r,s),ring(r+1,s+1),ring(r+1,s)]
 for s in range(segments):indices+=[ring(rings-1,s),ring(rings-1,s+1),bottom]
 return positions,indices


def box(sx,sy,sz):
 x,y,z=sx/2,sy/2,sz/2
 corners=[(-x,-y,-z),(x,-y,-z),(x,y,-z),(-x,y,-z),(-x,-y,z),(x,-y,z),(x,y,z),(-x,y,z)]
 faces=[(0,3,2,1),(4,5,6,7),(0,1,5,4),(2,3,7,6),(1,2,6,5),(0,4,7,3)]
 return [c for p in corners for c in p],[i for a,b,c,d in faces for i in (a,b,c,a,c,d)]


def light(handle,kind,settings,tfm,strength):
 return [dict(handle=handle+'-shader',create='shader',graph=dict(nodes=[dict(handle='e',type='emission',set=dict(color=[1,1,1],strength=1.0))],
   connect=[['e','Emission','output','Surface']])),
  dict(handle=handle,create=kind,set=dict(strength=strength,use_mis=True,cast_shadow=True,max_bounces=1024,is_enabled=True,
   normalize=True,used_shaders=[handle+'-shader'],**settings)),
  dict(handle=handle+'-object',create='object',set=dict(geometry=handle,tfm=tfm))]


def scene(identifier,description,width,height,samples,nodes,images=None,source='authored'):
 return {'cbcOracleScene':1,'id':identifier,'description':description,'sourceFixture':source,
  'render':dict(width=width,height=height,samples=samples,repeats=2),'colorConfig':None,'nodes':nodes,'images':images or []}


def diffuse_triangle():
 # Same calls, order and values as native/tests/host/consumer.c fixture().
 nodes=[dict(handle='integrator',scene='integrator',set=dict(use_guiding=False,use_light_tree=False,use_adaptive_sampling=False,
   use_denoise=False,caustics_reflective=False,caustics_refractive=False,min_bounce=2,max_bounce=4,max_diffuse_bounce=4,sampling_pattern='tabulated_sobol')),
  dict(handle='camera',scene='camera',set=dict(fov=1.1,matrix=[1,0,0,0, 0,1,0,0, 0,0,1,-4])),
  dict(handle='material',create='shader',graph=dict(nodes=[dict(handle='diffuse',type='diffuse_bsdf',set=dict(roughness=.3)),
    dict(handle='value',type='color',set=dict(value=[.7,.2,.1]))],connect=[['value','Color','diffuse','Color'],['diffuse','BSDF','output','Surface']]),
   set=dict(emission_sampling_method='none')),
  dict(handle='mesh',create='mesh',set=dict(used_shaders=['material']),mesh=dict(positions=[-1.5,-1,0, 1.5,-1,0, 0,1.5,0],indices=[0,1,2])),
  dict(handle='object',create='object',set=dict(geometry='mesh')),
  dict(handle='world',create='shader',graph=dict(nodes=[dict(handle='background',type='background_shader',set=dict(color=[.25,.4,.6],strength=.8))],
    connect=[['background','Background','output','Surface']]),set=dict(emission_sampling_method='none')),
  dict(handle='bg',scene='background',set=dict(shader='world'))]
 return scene('starter/diffuse-triangle','Static diffuse triangle, constant world: the C lifecycle fixture (consumer.c fixture(), first render)',
  32,24,8,nodes,source='native/tests/host/consumer.c fixture()')


def glossy_sphere():
 positions,indices=sphere(1.0,32,16)
 nodes=common(64,48,0.8,translate(0,0,-4))+world([0.05,0.05,0.05],1.0)
 nodes+=[material('glossy','glossy_bsdf',dict(distribution='ggx',roughness=0.3,color=[0.8,0.8,0.8]))]
 nodes+=mesh('sphere',positions,indices,'glossy')
 nodes+=light('point','pointlight',dict(radius=0.25,is_sphere=True),translate(2.5,3,-3),[400,400,400])
 return scene('starter/glossy-sphere-point-light','Faceted UV sphere, GGX glossy roughness 0.3, one point light, constant world',64,48,16,nodes)


def glass_area():
 floor=[-4,-1,-2, 4,-1,-2, 4,-1,6, -4,-1,6];slab,slab_indices=box(1.6,0.4,1.0)
 nodes=common(64,48,0.9,translate(0,0.5,-4))+world([0.02,0.02,0.02],1.0)
 nodes+=[material('floor-material','diffuse_bsdf',dict(color=[0.6,0.6,0.6],roughness=0.0)),
  material('glass','glass_bsdf',dict(distribution='ggx',roughness=0.1,IOR=1.45,color=[1,1,1]))]
 nodes+=mesh('floor',floor,[0,2,1, 0,3,2],'floor-material')
 nodes+=mesh('slab',slab,slab_indices,'glass',translate(0,-0.6,1))
 # Area lights emit along their object's -Z; this rotation points -Z down.
 nodes+=light('area','arealight',dict(sizeu=2.0,sizev=2.0,ellipse=False,spread=math.pi,is_portal=False),[1,0,0,0, 0,0,1,3, 0,-1,0,1],[300,300,300])
 return scene('starter/glass-and-area-light','GGX glass slab (IOR 1.45) over a diffuse floor, one rectangular area light',64,48,16,nodes)


def textured_plane():
 quad=[-1.5,-1,0, 1.5,-1,0, 1.5,1,0, -1.5,1,0]
 uv=dict(name='uv',value='point2',domain='corner',flags=['standard'],data=[0,0, 1,1, 1,0, 0,0, 0,1, 1,1])
 nodes=common(48,32,0.9,translate(0,0,-3))+world([0.2,0.2,0.2],1.0)
 nodes+=[dict(handle='textured',create='shader',graph=dict(nodes=[dict(handle='d',type='diffuse_bsdf',set=dict(roughness=0.0)),
    dict(handle='t',type='image_texture',set=dict(image='checker',colorspace='__builtin_srgb',alpha_type='unassociated',interpolation='linear',extension='clamp')),
    dict(handle='uv',type='uvmap')],
   connect=[['uv','UV','t','Vector'],['t','Color','d','Color'],['d','BSDF','output','Surface']]),set=dict(emission_sampling_method='none'))]
 nodes+=mesh('plane',quad,[0,2,1, 0,3,2],'textured',attributes=[uv])
 images=[dict(handle='checker',width=64,height=64,format='byte4',file='textured-plane.checker.byte4',
  provenance='imaging-bridge checker 64, then to-raw byte4 (CodeBrix.Imaging.ApacheLicenseForever 1.0.290.858); rows bottom-up')]
 return scene('starter/textured-plane','Diffuse plane with a 64x64 sRGB byte4 texture bound from memory (CodeBrix.Imaging checker PNG)',48,32,8,nodes,images)


def main():
 for build in [diffuse_triangle,glossy_sphere,glass_area,textured_plane]:
  data=build();path=HERE/(data['id']+'.json');path.parent.mkdir(parents=True,exist_ok=True)
  path.write_text(json.dumps(data,indent=1)+'\n');print(path.relative_to(HERE))


if __name__=='__main__':main()
