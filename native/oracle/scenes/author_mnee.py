#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the MNEE cbcOracleScene files of the caustic lights and casters added after mnee/glass-sphere-caustic. Rerunning
reproduces them exactly. Every scene is mnee/glass-sphere-caustic (a smooth sharp glass sphere casting shadow caustics on a
diffuse floor and wall) with one change, the same change as a case of native/cycles/mnee_fixture.h:
mnee/glass-sphere-moving-caster: the caster sphere moves (object motion blur, case 18).
mnee/spot-caustic, mnee/spot-sphere-caustic: the caustic lamp is a spot light with a disk radius or a spherical spot light (19, 20).
mnee/background-caustic: the point light beside a background light with caustics and an importance map, under a gradient world (21).
mnee/lamp-shader-caustic: the point light's emission strength from a Light Falloff node, an emission shader that is not constant (22).
mnee/beckmann-refraction-caustic: the sphere is a rough refraction BSDF with the Beckmann distribution (23, on case 1's slab there).
mnee/volume-caustic: the sphere and the floor under it inside an absorbing box (24).
mnee/huang-hair-receiver: hair/huang-default-thick with the floor and the Huang hair tuft as caustic receivers of a rough glass sphere
between them and the lamp, which casts shadow caustics (25).
shadow-catcher/caustics-receiver: shadow-catcher/catcher-point-light with the catcher floor a caustic receiver of a small glass sphere
under the point lamp, which casts shadow caustics (shadow_catcher_gate case 23).
mnee/area-caustic, mnee/area-disk-spread-caustic, mnee/area-zero-spread-caustic: the caustic lamp is a rectangular area light with
the full spread, a disk area light with a narrow spread or a rectangular area light with zero spread (26, 27, 28).
mnee/anisotropic-glass-caustic, mnee/anisotropic-principled-caustic: the sphere is a rough anisotropic Glass BSDF (GGX, a constant
tangent) or a Principled BSDF with full transmission, an Anisotropic value and the same tangent: the anisotropic branch of
mnee_sample_bsdf_dh (no mnee_fixture.h case; the nearest is case 1, a rough isotropic refraction caster).
"""
import json
from pathlib import Path
HERE=Path(__file__).resolve().parent
# The spot and area lamps face down (local -Z to world -Y), as author_light_tree.py's DOWN.
DOWN=lambda x,y,z:[1,0,0,x, 0,0,1,y, 0,-1,0,z]
def derived(identifier,description,case):
 data=json.loads((HERE/'mnee/glass-sphere-caustic.json').read_text());data['id']=identifier;data['description']=description
 data['sourceFixture']='native/oracle/scenes/author_mnee.py; native/cycles/mnee_fixture.h mnee_fixture() case '+str(case)
 return data,data['nodes']
def node(nodes,handle):return next(n for n in nodes if n['handle']==handle)
def lamp(nodes,kind,settings,tfm):
 """Replace the caustic point lamp by another lamp of the same strength and shader."""
 light=node(nodes,'light');light['create']=kind
 light['set']={k:v for k,v in light['set'].items() if k not in ('radius','is_sphere')}|settings
 node(nodes,'light-object')['set']['tfm']=tfm
def moving_caster():
 data,nodes=derived('mnee/glass-sphere-moving-caster','MNEE shadow caustics with object motion blur on the caster: the glass sphere moves and turns '
  'over the shutter above the static floor and wall receivers',18)
 node(nodes,'integrator')['set']['motion_blur']=True;node(nodes,'camera')['set']['shuttertime']=0.6
 node(nodes,'sphere-object')['set']['motion']=[[0.9973546,0.0,-0.0726914,-0.27, 0.0,1.0,0.0,-0.08, 0.0726914,0.0,0.9973546,-0.06],
  [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0],[0.9973546,0.0,0.0726914,0.27, 0.0,1.0,0.0,0.08, -0.0726914,0.0,0.9973546,0.06]]
 return data
def spot(sphere):
 data,nodes=derived('mnee/spot-sphere-caustic' if sphere else 'mnee/spot-caustic',
  'MNEE shadow caustics from a spherical spot light (radius 0.3)' if sphere else 'MNEE shadow caustics from a spot light with a disk radius',
  20 if sphere else 19)
 lamp(nodes,'spotlight',dict(radius=0.3,is_sphere=True,angle=1.2,smooth=0.2) if sphere else dict(radius=0.25,is_sphere=False,angle=1.0,smooth=0.3),
  DOWN(0.15,2.6 if sphere else 2.2,0.5))
 return data
def area(identifier,description,case,settings):
 data,nodes=derived(identifier,description,case)
 lamp(nodes,'arealight',dict(is_portal=False)|settings,DOWN(0.15,2.2,0.5))
 return data
def background():
 data,nodes=derived('mnee/background-caustic','MNEE shadow caustics from a background light (importance map, MIS) beside the caustic point light, under a '
  'gradient world (a constant world leaves the background light disabled)',21)
 node(nodes,'world')['graph']=dict(nodes=[dict(handle='sky',type='gradient_texture',set=dict(gradient_type='linear')),dict(handle='coordinates',type='texture_coordinate'),
   dict(handle='bsdf',type='background_shader',set=dict(strength=1.5))],
   connect=[['coordinates','Generated','sky','Vector'],['sky','Color','bsdf','Color'],['bsdf','Background','output','Surface']])
 at=nodes.index(node(nodes,'bg'))+1
 nodes[at:at]=[dict(handle='world-light',create='backgroundlight',set=dict(use_mis=True,use_caustics=True,map_resolution=64,strength=[1.0,1.0,1.0],used_shaders=['world'])),
  dict(handle='world-light-object',create='object',set=dict(geometry='world-light'))]
 return data
def lamp_shader():
 data,nodes=derived('mnee/lamp-shader-caustic','MNEE shadow caustics from a point light whose emission strength comes from a Light Falloff node '
  '(an emission shader that is not constant)',22)
 node(nodes,'emitter')['graph']=dict(nodes=[dict(handle='falloff',type='light_falloff',set=dict(strength=1.0,smooth=0.5)),
   dict(handle='bsdf',type='emission',set=dict(color=[1,1,1]))],connect=[['falloff','Quadratic','bsdf','Strength'],['bsdf','Emission','output','Surface']])
 return data
def beckmann():
 data,nodes=derived('mnee/beckmann-refraction-caustic','MNEE shadow caustics through a rough refraction BSDF with the Beckmann distribution',23)
 node(nodes,'glass')['graph']=dict(nodes=[dict(handle='bsdf',type='refraction_bsdf',set=dict(distribution='beckmann',roughness=0.2,IOR=1.33,color=[0.9,0.95,1.0]))],
  connect=[['bsdf','BSDF','output','Surface']])
 return data
def anisotropic(principled):
 data,nodes=derived('mnee/anisotropic-principled-caustic' if principled else 'mnee/anisotropic-glass-caustic',
  'MNEE shadow caustics through a Principled BSDF with full transmission, Anisotropic 0.7 and a constant tangent (an anisotropic transmission lobe)'
  if principled else 'MNEE shadow caustics through a rough anisotropic Glass BSDF (GGX, anisotropy 0.6, a constant tangent)',1)
 data['sourceFixture']='native/oracle/scenes/author_mnee.py; no mnee_fixture() case (nearest: case 1, a rough isotropic refraction caster)'
 bsdf=dict(handle='bsdf',type='principled_bsdf',set=dict(distribution='ggx',base_color=[0.9,0.95,1.0],roughness=0.3,ior=1.45,anisotropic=0.7,transmission_weight=1.0)) \
  if principled else dict(handle='bsdf',type='glass_bsdf',set=dict(distribution='ggx',roughness=0.2,IOR=1.45,color=[0.95,0.97,1.0],anisotropy=0.6))
 node(nodes,'glass')['graph']=dict(nodes=[bsdf,dict(handle='t',type='color',set=dict(value=[0.8,0.3,0.2]))],
  connect=[['t','Color','bsdf','Tangent'],['bsdf','BSDF','output','Surface']])
 return data
def volume():
 data,nodes=derived('mnee/volume-caustic','MNEE shadow caustics inside an absorbing medium: the sphere and the floor under it in a box with an absorption volume',24)
 at=nodes.index(node(nodes,'world'))
 nodes[at:at]=[dict(handle='medium',create='shader',graph=dict(nodes=[dict(handle='a',type='absorption_volume',set=dict(density=0.15,color=[0.8,0.85,0.9]))],
   connect=[['a','Volume','output','Volume']]),set=dict(emission_sampling_method='none')),
  dict(handle='box',create='mesh',mesh=dict(positions=[-2,-2.5,-1, 2,-2.5,-1, -2,1.5,-1, 2,1.5,-1, -2,-2.5,2.5, 2,-2.5,2.5, -2,1.5,2.5, 2,1.5,2.5],
   indices=[0,2,3,0,3,1, 4,5,7,4,7,6, 0,1,5,0,5,4, 2,6,7,2,7,3, 0,4,6,0,6,2, 1,3,7,1,7,5]),set=dict(used_shaders=['medium'])),
  dict(handle='box-object',create='object',set=dict(geometry='box'))]
 return data
def sphere_mesh():
 return node(json.loads((HERE/'mnee/glass-sphere-caustic.json').read_text())['nodes'],'sphere')
def huang():
 data=json.loads((HERE/'hair/huang-default-thick.json').read_text());data['id']='mnee/huang-hair-receiver'
 data['description']='MNEE shadow caustics on Huang hair curves and the floor: a rough glass sphere between them and the caustic lamp'
 data['sourceFixture']='native/oracle/scenes/author_mnee.py; native/cycles/mnee_fixture.h mnee_fixture() case 25';nodes=data['nodes']
 node(nodes,'floor-object')['set']['is_caustics_receiver']=True;node(nodes,'tuft-object')['set']['is_caustics_receiver']=True
 node(nodes,'lamp')['set']['use_caustics']=True
 mesh=sphere_mesh();mesh['set']=dict(used_shaders=['glass'])
 at=nodes.index(node(nodes,'world'))
 nodes[at:at]=[mesh,dict(handle='sphere-object',create='object',set=dict(geometry='sphere',is_caustics_caster=True,tfm=[1,0,0,1.3, 0,1,0,0.55, 0,0,1,-1.2]))]
 return data
def catcher():
 data=json.loads((HERE/'shadow-catcher/catcher-point-light.json').read_text());data['id']='shadow-catcher/caustics-receiver'
 data['description']='The shadow catcher floor as a caustic receiver of a small glass sphere under the point lamp, which casts shadow caustics (MNEE)'
 data['sourceFixture']='native/oracle/scenes/author_mnee.py; native/cycles/shadow_catcher_gate.cpp case 23';nodes=data['nodes']
 node(nodes,'floor-object')['set']['is_caustics_receiver']=True;node(nodes,'point')['set']['use_caustics']=True
 mesh=sphere_mesh();mesh['set']=dict(used_shaders=['caustic-glass'])
 at=nodes.index(node(nodes,'point-shader'))
 nodes[at:at]=[dict(handle='caustic-glass',create='shader',graph=dict(nodes=[dict(handle='bsdf',type='glass_bsdf',set=dict(distribution='ggx',roughness=0.0,IOR=1.45,color=[0.95,0.97,1.0]))],
   connect=[['bsdf','BSDF','output','Surface']]),set=dict(emission_sampling_method='none')),mesh,
  dict(handle='sphere-object',create='object',set=dict(geometry='sphere',is_caustics_caster=True,tfm=[0.5,0,0,0.95, 0,0.5,0,1.075, 0,0,0.5,-1.2]))]
 return data
def main():
 for data in [moving_caster(),spot(False),spot(True),
              background(),lamp_shader(),beckmann(),volume(),huang(),catcher(),anisotropic(False),anisotropic(True),
              area('mnee/area-caustic','MNEE shadow caustics from a rectangular area light with the full spread',26,dict(sizeu=1.2,sizev=0.9,ellipse=False,spread=3.1415927)),
              area('mnee/area-disk-spread-caustic','MNEE shadow caustics from a disk area light with a narrow spread (the clamped sample region)',27,
                   dict(sizeu=0.8,sizev=0.8,ellipse=True,spread=1.2)),
              area('mnee/area-zero-spread-caustic','MNEE shadow caustics from a rectangular area light with zero spread (a fixed-direction walk)',28,
                   dict(sizeu=2.4,sizev=2.4,ellipse=False,spread=0.0))]:
  path=HERE/(data['id']+'.json');path.write_text(json.dumps(data,indent=1)+'\n');print(path.relative_to(HERE))
if __name__=='__main__':main()
