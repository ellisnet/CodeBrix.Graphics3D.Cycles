#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the light-tree cbcOracleScene files. Rerunning reproduces them exactly.
light-tree/lamps-and-mesh-lights: the integrator's light tree with point, spot, area and sun lamps and two
emissive meshes (one a grid of separate quads), so the tree has inner nodes, a distant node and mesh subtrees.
light-tree/light-linking: the same tree with light linking (receiver light sets on the floor and the sphere,
light set membership on every emitter), so the host builds one specialized tree per receiver set.
"""
import json,math
from pathlib import Path
from author_starters import HERE,translate,integrator,world,material,mesh,sphere,light,scene
def camera(width,height,fov,tfm):
 return [dict(handle='camera',scene='camera',set=dict(camera_type='perspective',fov=fov,nearclip=0.1,farclip=100.0,matrix=tfm)),
  dict(handle='film',scene='film',set=dict(exposure=1.0,filter_type='box',filter_width=1.0))]
def emitter(handle,strength,sampling):
 return dict(handle=handle,create='shader',graph=dict(nodes=[dict(handle='e',type='emission',set=dict(color=[1,0.9,0.75],strength=strength))],
  connect=[['e','Emission','output','Surface']]),set=dict(emission_sampling_method=sampling))
def grid(columns,rows,y):
 positions=[];indices=[]
 for r in range(rows):
  for c in range(columns):
   x0=-2.4+4.8*c/columns+0.1;x1=-2.4+4.8*(c+1)/columns-0.1;z0=-1+3*r/rows+0.1;z1=-1+3*(r+1)/rows-0.1;b=len(positions)//3
   positions+=[x0,y,z0, x0,y,z1, x1,y,z1, x1,y,z0];indices+=[b,b+1,b+2, b,b+2,b+3]
 return positions,indices
# Area lights and spots emit along their object's -Z; this rotation points -Z down.
DOWN=lambda x,y,z:[1,0,0,x, 0,0,1,y, 0,-1,0,z]
def base(identifier,description,linking):
 positions,indices=sphere(0.8,24,12)
 nodes=[integrator(use_light_tree=True,light_sampling_threshold=0.0)]+camera(64,48,0.85,translate(0,0.6,-5))+world([0.02,0.025,0.03],1.0)
 nodes+=[material('floor-material','diffuse_bsdf',dict(color=[0.6,0.6,0.6],roughness=0.0)),
  material('ball','glossy_bsdf',dict(distribution='ggx',roughness=0.35,color=[0.8,0.7,0.6])),
  emitter('panel-emission',4.0,'front'),emitter('grid-emission',2.5,'front_back')]
 nodes+=mesh('floor',[-4,-1,-2, 4,-1,-2, 4,-1,6, -4,-1,6],[0,2,1, 0,3,2],'floor-material')
 nodes+=mesh('sphere',positions,indices,'ball',translate(-0.4,-0.2,1.2))
 nodes+=mesh('panel',[-0.6,-0.4,0, 0.6,-0.4,0, 0.6,0.4,0, -0.6,0.4,0],[0,1,2, 0,2,3],'panel-emission',[0.94,0,0.34,1.6, -0.2,0.8,0.5,1.2, -0.27,-0.6,0.75,-0.8])
 grid_positions,grid_indices=grid(3,2,2.6)
 nodes+=mesh('grid',grid_positions,grid_indices,'grid-emission')
 nodes+=light('point','pointlight',dict(radius=0.3,is_sphere=True),translate(-2.5,2,-1.5),[60,50,40])
 nodes+=light('spot','spotlight',dict(radius=0.2,is_sphere=True,angle=1.0,smooth=0.3),DOWN(0.8,2.8,1.0),[80,80,90])
 nodes+=light('area','arealight',dict(sizeu=1.2,sizev=0.8,ellipse=False,spread=1.2,is_portal=False),DOWN(2.4,2.2,2.5),[50,60,80])
 nodes+=light('sun','sunlight',dict(angle=0.1),[1,0,0,0, 0,0.8,0.6,0, 0,-0.6,0.8,0],[0.4,0.35,0.3])
 if linking:
  # Receivers: floor set 1, sphere set 2 (the rest set 0, every emitter). Membership bit k = light set k.
  for handle,receiver in (('floor-object',1),('sphere-object',2)):
   next(n for n in nodes if n['handle']==handle)['set']['receiver_light_set']=receiver
  for handle,mask in (('point-object',0b011),('spot-object',0b101),('area-object',0b111),('sun-object',0b011),('panel-object',0b001),('grid-object',0b101)):
   next(n for n in nodes if n['handle']==handle)['set']['light_set_membership']=mask
 return scene(identifier,description,64,48,16,nodes)
def main():
 for data in [base('light-tree/lamps-and-mesh-lights','Light tree: point, spot, area and sun lamps, an emissive panel and a 6-quad emissive grid over a diffuse floor and a GGX sphere',False),
              base('light-tree/light-linking','Light tree with light linking: per-receiver light sets on the floor and the sphere, set membership on every emitter',True)]:
  path=HERE/(data['id']+'.json');path.parent.mkdir(parents=True,exist_ok=True)
  path.write_text(json.dumps(data,indent=1)+'\n');print(path.relative_to(HERE))
if __name__=='__main__':main()
