#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the cbcOracleScene files of feature combinations that build the largest entries. Rerunning
reproduces them exactly.
mnee/huang-hair-moving-receiver: mnee/huang-hair-receiver (thick Huang hair curves and the floor as caustic receivers of a rough glass
sphere) with object motion blur on the floor receiver: caustics, thick curves and a moving receiver in one scene, the combination that
builds the largest MNEE walk (native/DEVICE.md 126).
subsurface/aov-principled-bump: subsurface/aov-cryptomatte-bump with Principled BSDFs (subsurface weight 1, random walk and
Christensen-Burley) in place of the Subsurface Scattering nodes, the Bump node on their Normal: the prepare_surface of
native/DEVICE.md 130.
surface-nodes/raycast-ao-bevel-subsurface-volume: an Ambient Occlusion node, a Bevel node, a Raycast node, a random walk subsurface
sphere and a box with a volume shader that only Raycast node rays see, in one scene: the prepare_surface_raytrace of
native/DEVICE.md 188 (e), whose shader evaluation the device builds in stages (native/DEVICE.md 202).
"""
import json
from pathlib import Path
from author_starters import translate,integrator,world,material,mesh,sphere,light,scene,box
from author_light_tree import camera
HERE=Path(__file__).resolve().parent
def node(nodes,handle):return next(n for n in nodes if n['handle']==handle)
def moving_receiver():
 data=json.loads((HERE/'mnee/huang-hair-receiver.json').read_text());data['id']='mnee/huang-hair-moving-receiver'
 data['description']=('MNEE shadow caustics on Huang hair curves and a moving floor: a rough glass sphere between them and the caustic lamp, '
  'the floor receiver moves over the shutter (object motion blur)')
 data['sourceFixture']='native/oracle/scenes/author_mnee_size.py (derived from mnee/huang-hair-receiver)';nodes=data['nodes']
 node(nodes,'integrator')['set']['motion_blur']=True;node(nodes,'camera')['set']['shuttertime']=0.6
 node(nodes,'floor-object')['set']['motion']=[[0.9987503,0.0,-0.0499792,-0.2, 0.0,1.0,0.0,-0.0, 0.0499792,0.0,0.9987503,-0.1],
  [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0],[0.9987503,0.0,0.0499792,0.2, 0.0,1.0,0.0,0.0, -0.0499792,0.0,0.9987503,0.1]]
 return data
def principled_bump():
 data=json.loads((HERE/'subsurface/aov-cryptomatte-bump.json').read_text());data['id']='subsurface/aov-principled-bump'
 data['description']=('Bump-mapped Principled BSDFs with full subsurface weight, random walk (middle) and Christensen-Burley (right), with AOV '
  'outputs and cryptomatte passes (derived from subsurface/aov-cryptomatte-bump)')
 data['sourceFixture']='native/oracle/scenes/author_mnee_size.py (derived from subsurface/aov-cryptomatte-bump)'
 for handle,method in (('sss','random_walk'),('sss-burley','burley')):
  s=node(node(data['nodes'],handle)['graph']['nodes'],'s');old=s['set']
  s['type']='principled_bsdf';s['set']=dict(distribution='multi_ggx',base_color=old['color'],roughness=0.35,ior=1.45,subsurface_weight=1.0,
   subsurface_method=method,subsurface_radius=old['radius'],subsurface_scale=old['scale'])
  g=node(data['nodes'],handle)['graph'];g['connect']=[['s','BSDF','output','Surface'] if c==['s','BSSRDF','output','Surface'] else c for c in g['connect']]
 return data
def raycast_ao_bevel_subsurface_volume():
 def shader(handle,nodes,connect):return dict(handle=handle,create='shader',graph=dict(nodes=nodes,connect=connect),set=dict(emission_sampling_method='none'))
 n=[integrator(light_sampling_threshold=0.0,max_volume_bounce=0)]+camera(48,36,0.85,translate(0,0.6,-5))+world([0.05,0.06,0.08],1.0)
 # The floor and the wall show the Ambient Occlusion node's colour as emission.
 n+=[shader('floor-material',[dict(handle='ao',type='ambient_occlusion',set=dict(samples=4,distance=1.5)),dict(handle='e',type='emission',set=dict(strength=1.0))],
  [['ao','Color','e','Color'],['e','Emission','output','Surface']])]
 n+=mesh('floor',[-4,-1,-2, 4,-1,-2, 4,-1,6, -4,-1,6],[0,2,1, 0,3,2],'floor-material')
 n+=mesh('wall',[-4,-1,4, 4,-1,4, 4,3,4, -4,3,4],[0,1,2, 0,2,3],'floor-material')
 n+=light('point','pointlight',dict(radius=0.5,is_sphere=True),translate(-0.2,2.2,0.4),[70,60,50])
 n+=[shader('ray',[dict(handle='ray',type='raycast',set=dict(length=30.0,only_local=False)),dict(handle='t',type='math',set=dict(math_type='multiply',value2=0.1)),
   dict(handle='c',type='combine_xyz',set={}),dict(handle='e',type='emission',set=dict(strength=1.0))],
   [['ray','Hit Distance','t','Value1'],['t','Value','c','X'],['ray','Is Hit','c','Y'],['c','Vector','e','Color'],['e','Emission','output','Surface']]),
  shader('bevelled',[dict(handle='bevel',type='bevel',set=dict(radius=0.3,samples=4)),dict(handle='d',type='diffuse_bsdf',set=dict(roughness=0.0,color=[0.7,0.5,0.35]))],
   [['bevel','Normal','d','Normal'],['d','BSDF','output','Surface']]),
  shader('walk',[dict(handle='s',type='subsurface_scattering',set=dict(method='random_walk',color=[0.85,0.7,0.55],scale=0.4,radius=[1.0,0.6,0.35],
   subsurface_ior=1.4,subsurface_roughness=1.0,subsurface_anisotropy=0.0))],[['s','BSSRDF','output','Surface']]),
  shader('medium',[dict(handle='a',type='absorption_volume',set=dict(density=0.4,color=[0.8,0.65,0.4]))],[['a','Volume','output','Volume']])]
 p,i=sphere(0.7,16,8);n+=mesh('ray-ball',p,i,'ray',translate(-1.6,-0.2,1.0));n+=mesh('sss-ball',p,i,'walk',translate(1.6,-0.2,1.0))
 bp,bi=box(1.0,1.0,1.0);n+=mesh('bevel-box',bp,bi,'bevelled',[0.8,0,0.6,0.0, 0,1,0,-0.5, -0.6,0,0.8,1.6])
 # The hull is around the camera and the scene; its visibility 128 leaves it to the Raycast node's rays alone.
 hp,hi=box(16.0,12.0,24.0);n+=mesh('hull',hp,hi,'medium',translate(0,2.0,0));node(n,'hull-object')['set']['visibility']=128
 return scene('surface-nodes/raycast-ao-bevel-subsurface-volume',
  'Ambient Occlusion (floor and wall), Bevel (box), Raycast (left sphere) and random walk subsurface (right sphere) in one scene, inside a box with a volume '
  'shader that only Raycast node rays see',48,36,8,n,source='native/oracle/scenes/author_mnee_size.py')
def main():
 for data in [moving_receiver(),principled_bump(),raycast_ao_bevel_subsurface_volume()]:
  path=HERE/(data['id']+'.json');path.write_text(json.dumps(data,indent=1)+'\n');print(path.relative_to(HERE))
if __name__=='__main__':main()
