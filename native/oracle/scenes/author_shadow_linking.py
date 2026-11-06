#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the shadow-linking cbcOracleScene files. Rerunning reproduces them exactly.
Objects carry a blocker_shadow_set; emitters carry a shadow_set_membership whose bit k lets blockers of set k cast their shadow.
shadow-linking/blockers: lamps and emissive meshes behind blockers of sets 1 and 2, the flat CDF; indirect rays reach them through the
dedicated light kernels. shadow-linking/light-tree: the same on the integrator's light tree, with light linking on the floor and the ball.
shadow-linking/hdri-world: the flat CDF with a procedural world and its background light, sampled with MIS.
shadow-linking/volume: a scattering box: a phase scatter also traces the dedicated light ray; shadow rays attenuate through the medium.
shadow-linking/caustics and mnee/light-tree-caustic, mnee/light-linking-caustic: mnee/glass-sphere-caustic with shadow linking on the tree,
on the light tree alone, and with light linking on the flat CDF.
"""
import json
from pathlib import Path
from author_starters import HERE,translate,integrator,world,material,mesh,sphere,light,scene
from author_light_tree import camera,emitter,DOWN
SET_0_1,SET_0_2,SET_0=0b011,0b101,0b001
def base(identifier,description,tree,linking,hdri,volume=False):
 positions,indices=sphere(0.8,24,12)
 nodes=[integrator(use_light_tree=tree,light_sampling_threshold=0.0,**(dict(max_volume_bounce=3) if volume else {}))]+camera(64,48,0.85,translate(0,0.6,-5))
 if hdri:
  nodes+=[dict(handle='world',create='shader',graph=dict(nodes=[dict(handle='sky',type='gradient_texture',set=dict(gradient_type='linear')),dict(handle='coordinates',type='texture_coordinate'),
     dict(handle='background',type='background_shader',set=dict(strength=1.5))],
     connect=[['coordinates','Generated','sky','Vector'],['sky','Color','background','Color'],['background','Background','output','Surface']]),set=dict(emission_sampling_method='auto')),
   dict(handle='background',scene='background',set=dict(shader='world')),
   dict(handle='world-light',create='backgroundlight',set=dict(use_mis=True,map_resolution=64,strength=[1.0,1.0,1.0],used_shaders=['world'])),
   dict(handle='world-light-object',create='object',set=dict(geometry='world-light'))]
 else:nodes+=world([0.02,0.025,0.03],1.0)
 nodes+=[material('floor-material','diffuse_bsdf',dict(color=[0.6,0.6,0.6],roughness=0.0)),
  material('ball-material','glossy_bsdf',dict(distribution='ggx',roughness=0.35,color=[0.8,0.7,0.6])),
  emitter('panel-emission',4.0,'none' if volume else 'front_back')]
 nodes+=mesh('floor',[-4,-1,-2, 4,-1,-2, 4,-1,6, -4,-1,6],[0,2,1, 0,3,2],'floor-material')
 nodes+=mesh('wall',[-4,-1,4, 4,-1,4, 4,3,4, -4,3,4],[0,1,2, 0,2,3],'floor-material')
 nodes+=mesh('ball',positions,indices,'ball-material',translate(-0.4,-0.2,1.2))
 nodes+=mesh('blocker-1',[-1.2,1.2,-1, 1.4,1.2,-1, 1.4,1.2,1.8, -1.2,1.2,1.8],[0,1,2, 0,2,3],'floor-material')
 nodes+=mesh('blocker-2',[1.6,-1,1.5, 2.8,-1,1.5, 2.8,1.6,1.5, 1.6,1.6,1.5],[0,1,2, 0,2,3],'floor-material')
 nodes+=mesh('blocker-all',[-3.2,-1,2, -2.4,-1,2, -2.4,1,2, -3.2,1,2],[0,1,2, 0,2,3],'floor-material')
 nodes+=mesh('panel',[-0.7,-0.5,0, 0.7,-0.5,0, 0.7,0.5,0, -0.7,0.5,0],[0,1,2, 0,2,3],'panel-emission',[0.94,0,0.34,0.6, -0.2,0.8,0.5,2.2, -0.27,-0.6,0.75,0.2])
 nodes+=mesh('panel-plain',[-0.5,-0.4,0, 0.5,-0.4,0, 0.5,0.4,0, -0.5,0.4,0],[0,1,2, 0,2,3],'panel-emission',[1,0,0,-2.2, 0,0,1,1.8, 0,-1,0,0.5])
 nodes+=light('point','pointlight',dict(radius=0.5,is_sphere=True),translate(-0.2,2.2,0.4),[70,60,50])
 if not volume:
  nodes+=light('spot','spotlight',dict(radius=0.4,is_sphere=True,angle=1.2,smooth=0.3),DOWN(0.3,2.7,0.6),[90,90,100])
  nodes+=light('area','arealight',dict(sizeu=1.4,sizev=1.0,ellipse=False,spread=1.3,is_portal=False),DOWN(2.2,2.3,1.0),[50,60,80])
 else:
  nodes+=[dict(handle='medium',create='shader',graph=dict(nodes=[dict(handle='s',type='scatter_volume',set=dict(density=0.12,color=[0.8,0.75,0.7],anisotropy=0.2))],connect=[['s','Volume','output','Volume']]),set=dict(emission_sampling_method='none'))]
  box=[-3.5,-0.95,-1.9, 3.5,-0.95,-1.9, -3.5,1.1,-1.9, 3.5,1.1,-1.9, -3.5,-0.95,3.8, 3.5,-0.95,3.8, -3.5,1.1,3.8, 3.5,1.1,3.8]
  nodes+=mesh('box',box,[0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,4,2, 2,4,6, 1,3,5, 3,7,5, 0,1,4, 1,5,4, 2,6,3, 3,6,7],'medium')
 nodes+=light('sun','sunlight',dict(angle=0.3),[1,0,0,0, 0,0.8,0.6,0, 0,-0.6,0.8,0],[0.5,0.45,0.4])
 nodes+=light('plain','pointlight',dict(radius=0.3,is_sphere=True),translate(-2.6,2.0,0.2),[25,25,30])
 def node(handle):return next(n for n in nodes if n['handle']==handle)
 node('blocker-1-object')['set']['blocker_shadow_set']=1;node('blocker-2-object')['set']['blocker_shadow_set']=2
 for handle,mask in (('point-object',SET_0_2),('spot-object',SET_0),('area-object',SET_0_1),('sun-object',SET_0_2),('panel-object',SET_0_2)):
  if not volume or handle not in ('spot-object','area-object'):node(handle)['set']['shadow_set_membership']=mask
 if linking:
  for handle,receiver in (('floor-object',1),('ball-object',2)):node(handle)['set']['receiver_light_set']=receiver
  for handle,mask in (('point-object',0b011),('spot-object',0b101),('area-object',0b111),('sun-object',0b011),('panel-object',0b101)):node(handle)['set']['light_set_membership']=mask
 return scene(identifier,description,64,48,16,nodes)
def caustic(identifier,description,tree,light_linking,shadow_linking):
 """mnee/glass-sphere-caustic (the point light casts shadow caustics through the glass sphere onto the floor and the wall) with an integrator or link change."""
 data=json.loads((HERE/'mnee/glass-sphere-caustic.json').read_text());data['id']=identifier;data['description']=description;data['sourceFixture']='native/oracle/scenes/author_shadow_linking.py'
 nodes=data['nodes']
 def node(handle):return next(n for n in nodes if n['handle']==handle)
 node('integrator')['set']['use_light_tree']=tree
 # A second, ordinary lamp, so the selection has a choice: the tree or the flat CDF can pick a lamp without caustics.
 at=nodes.index(node('world'))
 nodes[at:at]=[dict(handle='fill',create='pointlight',set=dict(radius=0.2,is_sphere=True,strength=[70,70,80],use_mis=True,used_shaders=['emitter'])),
  dict(handle='fill-object',create='object',set=dict(geometry='fill',tfm=[1,0,0,-2.4, 0,1,0,2.0, 0,0,1,-1.5]))]
 if light_linking:
  node('floor-object')['set']['receiver_light_set']=1;node('wall-object')['set']['receiver_light_set']=2
  node('light-object')['set']['light_set_membership']=0b011;node('fill-object')['set']['light_set_membership']=0b101
 if shadow_linking:
  # A blocker of set 1 above the sphere and the floor; the caustic lamp's rays pass it.
  blocker=mesh('blocker',[-1.6,1.3,-1.2, 1.6,1.3,-1.2, 1.6,1.3,1.6, -1.6,1.3,1.6],[0,1,2, 0,2,3],'diffuse')
  nodes[at:at]=blocker;node('blocker-object')['set']['blocker_shadow_set']=1;node('light-object')['set']['shadow_set_membership']=0b001
 return data
def main():
 for data in [base('shadow-linking/blockers','Shadow linking on the flat CDF: lamps, a sun and an emissive panel seen past blockers of sets 1 and 2; a default blocker stops every ray',False,False,False),
              base('shadow-linking/light-tree','Shadow linking on the light tree with light linking on the floor and the ball',True,True,False),
              base('shadow-linking/hdri-world','Shadow linking on the flat CDF with a procedural world and its background light, sampled with MIS',False,False,True),
              base('shadow-linking/volume','Shadow linking with a scattering box: phase scatters take the dedicated light kernels, shadow rays attenuate through the medium',False,False,True,True),
              caustic('shadow-linking/caustics','Shadow caustics from a shadow-linked point lamp past a blocker of set 1, on the light tree',True,False,True),
              caustic('mnee/light-tree-caustic','Manifold next-event estimation with the light tree choosing between a caustic and an ordinary lamp',True,False,False),
              caustic('mnee/light-linking-caustic','Manifold next-event estimation with light linking on the flat CDF: the floor and the wall each linked to one lamp',False,True,False)]:
  path=HERE/(data['id']+'.json');path.parent.mkdir(parents=True,exist_ok=True)
  path.write_text(json.dumps(data,indent=1)+'\n');print(path.relative_to(HERE))
if __name__=='__main__':main()
