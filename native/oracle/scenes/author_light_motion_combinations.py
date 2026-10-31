#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the light and motion feature-combination cbcOracleScene files (scenes/light-motion/). Rerunning reproduces them exactly.

Each scene combines integrator features that are gated one at a time elsewhere, so the
CPU kernels judge the WGSL device on the combination end to end (compare.py):
  light linking with object motion (a moving emitter and a moving receiver; light tree
  and flat CDF), with transparent receivers and transparent shadows, with an importance
  sampled environment (background MIS forward hits), with glass receivers;
  a light's cast_shadow off and a light's max_bounces below the path depth (flat CDF and
  light tree);
  camera motion together with object motion and with deformation motion;
  motion blur with glass and principled transmission, spot and area lamps, an importance
  sampled environment and a non-constant (procedural) mesh emitter;
  sample clamps, the caustics filters and the light exclude flags (lamp object
  visibility) at non-default values.
light-motion/sky.float4 is the 64x32 float RGBA environment (equirectangular, bottom row
first): IEEE additions and multiplications only, rounded to float32, so every platform
writes the same bytes.
"""
import array,copy,json
from pathlib import Path
import author_starters as s
import author_light_tree as lt
HERE=Path(__file__).resolve().parent
FAMILY='light-motion'
SKY_WIDTH,SKY_HEIGHT=64,32
# Lamp object visibility bits (PATH_RAY_VISIBILITY_*): camera, transmit, diffuse, glossy.
CAMERA,TRANSMIT,DIFFUSE,GLOSSY,ALL=1,2,4,8,255


def sky_pixels():
 """A sky gradient with a compact bright source and a soft strip (the environment
 fixture's formula), so importance sampling has a peak."""
 data=array.array('f')
 for y in range(SKY_HEIGHT):
  for x in range(SKY_WIDTH):
   u=(x+0.5)/SKY_WIDTH;v=(y+0.5)/SKY_HEIGHT;sky=0.25+0.75*v
   rgb=[0.2*sky+0.05,0.35*sky+0.05,0.6*sky+0.1]
   du=u-0.3;dv=v-0.7;d2=du*du*4+dv*dv
   if d2<0.004:rgb=[rgb[0]+40.0,rgb[1]+36.0,rgb[2]+28.0]
   if abs(u-0.75)<0.05 and 0.55<v<0.7:rgb=[rgb[0]+3.0,rgb[1]+0.5,rgb[2]]
   data.extend(rgb+[1.0])
 return data


def node(nodes,handle):return next(n for n in nodes if n['handle']==handle)


def hdri_world(nodes):
 """Replace the world with the importance-sampled environment (background light with MIS)."""
 world=node(nodes,'world')
 world['graph']=dict(nodes=[dict(handle='environment',type='environment_texture',set=dict(image='sky',colorspace='scene_linear',interpolation='linear',
   projection='equirectangular',alpha_type='ignore')),dict(handle='background',type='background_shader',set=dict(strength=1.0))],
  connect=[['environment','Color','background','Color'],['background','Background','output','Surface']])
 world['set']=dict(emission_sampling_method='auto')
 nodes+=[dict(handle='world-light',create='backgroundlight',set=dict(use_mis=True,map_resolution=64,strength=[1.0,1.0,1.0],used_shaders=['world'])),
  dict(handle='world-light-object',create='object',set=dict(geometry='world-light'))]
 return [dict(handle='sky',width=SKY_WIDTH,height=SKY_HEIGHT,format='float4',file='sky.float4',
  provenance='author_light_motion_combinations.py sky_pixels(): IEEE arithmetic only, float32, rows bottom-up')]


def mix_material(handle,first,first_settings,second,second_settings,fac):
 return dict(handle=handle,create='shader',graph=dict(nodes=[dict(handle='a',type=first,set=first_settings),dict(handle='b',type=second,set=second_settings),
   dict(handle='mix',type='mix_closure',set=dict(fac=fac))],
  connect=[['a','BSDF','mix','Closure1'],['b','BSDF','mix','Closure2'],['mix','Closure','output','Surface']]),set=dict(emission_sampling_method='none'))


def checker_emitter(handle,strength):
 """A non-constant emitter: procedural checker emission through generated coordinates."""
 return dict(handle=handle,create='shader',graph=dict(nodes=[dict(handle='coords',type='texture_coordinate'),
   dict(handle='checker',type='checker_texture',set=dict(scale=3.0,color1=[1.0,0.8,0.3],color2=[0.1,0.3,1.0])),
   dict(handle='e',type='emission',set=dict(strength=strength))],
  connect=[['coords','Generated','checker','Vector'],['checker','Color','e','Color'],['e','Emission','output','Surface']]),
  set=dict(emission_sampling_method='front_back'))


def moving(nodes,handle,pre,post):
 node(nodes,handle)['set']['motion']=[pre,post]


def motion_on(nodes,shuttertime=0.6):
 node(nodes,'integrator')['set']['motion_blur']=True;node(nodes,'camera')['set']['shuttertime']=shuttertime


def camera_motion(nodes):
 """Three camera steps around the authored camera matrix (translation and a small turn)."""
 m=node(nodes,'camera')['set']['matrix']
 pre=[0.9998,0,0.02,m[3]-0.25, 0,1,0,m[7]-0.04, -0.02,0,0.9998,m[11]];post=[0.9998,0,-0.02,m[3]+0.25, 0,1,0,m[7]+0.05, 0.02,0,0.9998,m[11]]
 node(nodes,'camera')['set']['motion']=[[round(v,7) for v in pre],list(m),[round(v,7) for v in post]]


# ---- light linking (the light-tree linking scene) ----
def linking(identifier,description,tree=True,motion=False,transparent=False,hdri=False,glass=False):
 data=lt.base(identifier,description,True);nodes=data['nodes'];images=[]
 node(nodes,'integrator')['set']['use_light_tree']=tree
 if motion:
  motion_on(nodes)
  # The receiver of light set 2 and the panel emitter (light set membership 1) move.
  moving(nodes,'sphere-object',s.translate(-0.7,-0.25,1.1),s.translate(-0.1,-0.15,1.3))
  moving(nodes,'panel-object',[0.94,0,0.34,1.3, -0.2,0.8,0.5,1.1, -0.27,-0.6,0.75,-0.8],[0.92,0.08,0.38,1.9, -0.24,0.82,0.46,1.3, -0.29,-0.57,0.77,-0.7])
 if transparent:
  # Receiver set 2 is a half-transparent sphere; a transparent pane (receiver set 1, like
  # the floor) stands between the lamps and the floor, so shadow rays pass transparent surfaces.
  i=nodes.index(node(nodes,'ball'))
  nodes[i]=mix_material('ball','transparent_bsdf',dict(color=[0.9,0.85,0.8]),'glossy_bsdf',dict(distribution='ggx',roughness=0.35,color=[0.8,0.7,0.6]),0.55)
  nodes.insert(i+1,mix_material('pane-material','transparent_bsdf',dict(color=[0.7,0.8,0.9]),'diffuse_bsdf',dict(color=[0.5,0.6,0.3],roughness=0.0),0.4))
  nodes+=s.mesh('pane',[-2.0,-1,0.2, 0.6,-1,0.2, 0.6,1.4,0.2, -2.0,1.4,0.2],[0,1,2, 0,2,3],'pane-material')
  node(nodes,'pane-object')['set']['receiver_light_set']=1
  node(nodes,'integrator')['set']['transparent_max_bounce']=8
 if glass:
  i=nodes.index(node(nodes,'ball'))
  nodes[i]=s.material('ball','glass_bsdf',dict(distribution='ggx',roughness=0.1,IOR=1.45,color=[1,1,1]))
  node(nodes,'integrator')['set'].update(max_transmission_bounce=8,max_bounce=8)
 if hdri:images=hdri_world(nodes)
 data['images']=images
 return data


# ---- a stage for the lamp-property, clamp and motion scenes ----
DOWN=lambda x,y,z:[1,0,0,x, 0,0,1,y, 0,-1,0,z]
SUN=[0.8660254,0,0.5,0, -0.3535534,0.7071068,0.6123724,0, -0.3535534,-0.7071068,0.6123724,0]


def stage(identifier,description,tree=False,ball='glossy',slab=None,world=([0.03,0.035,0.04],1.0)):
 floor=[-4,-1,-2, 4,-1,-2, 4,-1,6, -4,-1,6];wall=[-4,-1,4, 4,-1,4, 4,3,4, -4,3,4]
 cube,cube_indices=s.box(0.9,0.9,0.9);sphere,sphere_indices=s.sphere(0.5,24,12)
 nodes=s.common(64,48,0.9,[1,0,0,0, 0,0.9950042,-0.0998334,0.6, 0,0.0998334,0.9950042,-4])+s.world(*world)
 node(nodes,'integrator')['set'].update(use_light_tree=tree,light_sampling_threshold=0.0 if tree else 0.01)
 nodes+=[s.material('floor-material','diffuse_bsdf',dict(color=[0.6,0.55,0.5],roughness=0.0)),
  s.material('wall-material','diffuse_bsdf',dict(color=[0.45,0.5,0.55],roughness=0.0)),
  s.material('cube-material','diffuse_bsdf',dict(color=[0.8,0.3,0.1],roughness=0.0))]
 if ball=='glossy':nodes+=[s.material('ball-material','glossy_bsdf',dict(distribution='ggx',roughness=0.3,color=[0.7,0.75,0.8]))]
 elif ball=='glass':nodes+=[s.material('ball-material','glass_bsdf',dict(distribution='ggx',roughness=0.15,IOR=1.45,color=[1,1,1]))]
 if slab=='principled':nodes+=[dict(handle='slab-material',create='shader',graph=dict(nodes=[dict(handle='p',type='principled_bsdf',set=dict(
   distribution='multi_ggx',base_color=[0.9,0.85,0.7],roughness=0.2,ior=1.5,transmission_weight=0.7,coat_weight=0.3,coat_roughness=0.1))],
   connect=[['p','BSDF','output','Surface']]),set=dict(emission_sampling_method='none'))]
 elif slab=='glass':nodes+=[s.material('slab-material','glass_bsdf',dict(distribution='ggx',roughness=0.05,IOR=1.5,color=[1,1,1]))]
 nodes+=s.mesh('floor',floor,[0,2,1, 0,3,2],'floor-material')
 nodes+=s.mesh('wall',wall,[0,1,2, 0,2,3],'wall-material')
 nodes+=s.mesh('cube',cube,cube_indices,'cube-material',s.translate(-0.7,-0.55,0.8))
 nodes+=s.mesh('ball',sphere,sphere_indices,'ball-material',s.translate(0.8,-0.5,0.3))
 if slab:
  slab_positions,slab_indices=s.box(1.2,0.3,0.7);nodes+=s.mesh('slab',slab_positions,slab_indices,'slab-material',s.translate(0.0,-0.3,-0.4))
 return s.scene('%s/%s'%(FAMILY,identifier),description,64,48,16,nodes)


def lamps(nodes,point=None,spot=None,area=None,sun=None):
 """Four lamps; each argument updates that lamp's settings (light sockets) or, under
 'visibility', its object's visibility."""
 for handle,kind,settings,tfm,strength,extra in (
   ('point','pointlight',dict(radius=0.25,is_sphere=True),s.translate(1.5,2.5,-1.5),[200,185,170],point),
   ('spot','spotlight',dict(radius=0.2,is_sphere=True,angle=1.0,smooth=0.3),DOWN(-1.2,2.8,0.6),[160,160,180],spot),
   ('area','arealight',dict(sizeu=1.2,sizev=0.8,ellipse=False,spread=1.2,is_portal=False),DOWN(0.4,2.4,1.6),[60,70,90],area),
   ('sun','sunlight',dict(angle=0.05),SUN,[0.8,0.75,0.7],sun)):
  part=s.light(handle,kind,settings,tfm,strength);extra=dict(extra or {})
  if 'visibility' in extra:part[2]['set']['visibility']=extra.pop('visibility')
  part[1]['set'].update(extra);nodes+=part


def cast_shadow(tree):
 data=stage('light-cast-shadow'+('-tree' if tree else ''),'Four lamps over a floor, a wall, a box and a GGX sphere; the point and area lamps and the sun cast no shadow (cast_shadow false), the spot does'+(' (light tree)' if tree else ''),tree)
 lamps(data['nodes'],point=dict(cast_shadow=False),area=dict(cast_shadow=False),sun=dict(cast_shadow=False))
 return data


def max_bounces(tree):
 data=stage('light-max-bounces'+('-tree' if tree else ''),'Four lamps with max_bounces 0 (point), 1 (spot), 2 (area) and 0 (sun) under a path depth of 8; the sun also casts no shadow'+(' (light tree)' if tree else ''),tree)
 lamps(data['nodes'],point=dict(max_bounces=0),spot=dict(max_bounces=1),area=dict(max_bounces=2),sun=dict(max_bounces=0,cast_shadow=False))
 return data


def clamps_and_filters():
 data=stage('clamps-filters-exclude','Sample clamps (direct 0.6, indirect 0.25), both caustics filters on, a glass sphere and slab, and lamp exclude flags: the point lamp invisible to diffuse rays, the spot to glossy rays, the area lamp to the camera, the sun to transmission',ball='glass',slab='glass')
 node(data['nodes'],'integrator')['set'].update(sample_clamp_direct=0.6,sample_clamp_indirect=0.25,caustics_reflective=True,caustics_refractive=True)
 lamps(data['nodes'],point=dict(visibility=ALL-DIFFUSE),spot=dict(visibility=ALL-GLOSSY),area=dict(visibility=ALL-CAMERA),sun=dict(visibility=ALL-TRANSMIT))
 return data


def motion_materials():
 data=stage('motion-glass-principled-lamps','Object motion blur: a moving glass sphere, a moving principled slab (transmission 0.7, coat), a moving diffuse box, under spot and area lamps',ball='glass',slab='principled')
 nodes=data['nodes'];motion_on(nodes)
 moving(nodes,'ball-object',s.translate(0.55,-0.5,0.2),s.translate(1.05,-0.45,0.4))
 moving(nodes,'slab-object',s.translate(-0.25,-0.3,-0.4),[0.995,0,0.0998,0.25, 0,1,0,-0.25, -0.0998,0,0.995,-0.4])
 moving(nodes,'cube-object',s.translate(-0.9,-0.55,0.8),s.translate(-0.5,-0.55,0.8))
 lamps(nodes,point=dict(is_enabled=False),sun=dict(is_enabled=False))
 return data


def motion_environment():
 data=stage('motion-hdri-emitter','Object and deformation motion blur under an importance sampled environment (background light with MIS) and a static non-constant emitter (procedural checker emission); a moving GGX sphere, a deforming box')
 nodes=data['nodes'];motion_on(nodes);images=hdri_world(nodes)
 moving(nodes,'ball-object',s.translate(0.55,-0.5,0.2),s.translate(1.05,-0.45,0.4))
 cube=node(nodes,'cube');p=cube['mesh']['positions']
 cube['mesh']['motion']=dict(steps=3,positions=[round(v*0.92 if i%3!=1 else v,7) for i,v in enumerate(p)]+list(p)+[round(v*1.08 if i%3!=1 else v,7) for i,v in enumerate(p)])
 i=nodes.index(node(nodes,'floor'));nodes.insert(i,checker_emitter('panel-emission',5.0))
 nodes+=s.mesh('panel',[-0.6,-0.4,0, 0.6,-0.4,0, 0.6,0.4,0, -0.6,0.4,0],[0,1,2, 0,2,3],'panel-emission',[0.94,0,0.34,-1.6, -0.2,0.8,0.5,1.6, -0.27,-0.6,0.75,-0.8])
 data['images']=images
 return data


# ---- camera motion with object or deformation motion (the motion gate scenes) ----
def with_camera_motion(source,identifier,description):
 data=json.loads((HERE/'linux-gates'/source).read_text());data=copy.deepcopy(data)
 data['id']='%s/%s'%(FAMILY,identifier);data['description']=description;data['sourceFixture']='authored (from linux-gates/%s)'%source
 camera_motion(data['nodes']);return data


def scenes():
 return [
  linking('light-motion/linking-motion-tree','Light tree with light linking under object motion blur: the light set 2 receiver (the sphere) and an emitter of light set 0 (the panel) move',motion=True),
  linking('light-motion/linking-motion-flat','Light linking (flat light distribution) under object motion blur: a moving receiver and a moving emitter',tree=False,motion=True),
  linking('light-motion/linking-transparent','Light tree with light linking and transparent shadows: a half-transparent receiver sphere and a transparent pane between the lamps and the floor',transparent=True),
  linking('light-motion/linking-hdri','Light tree with light linking under an importance sampled environment (background light with MIS): forward hits of the world on the GGX receiver',hdri=True),
  linking('light-motion/linking-glass','Light linking (flat light distribution) with a GGX glass receiver sphere',tree=False,glass=True),
  cast_shadow(False),cast_shadow(True),max_bounces(False),max_bounces(True),
  with_camera_motion('object-motion-1.json','camera-object-motion','Camera motion (three steps) together with object motion: the object-motion gate scene, mode 1, with a moving camera'),
  with_camera_motion('deforming-motion-3.json','camera-deforming-motion','Camera motion (three steps) together with deformation and object motion: the deforming-motion gate scene, mode 3, with a moving camera'),
  motion_materials(),motion_environment(),clamps_and_filters()]


def main():
 (HERE/FAMILY).mkdir(exist_ok=True)
 (HERE/FAMILY/'sky.float4').write_bytes(sky_pixels().tobytes())
 for data in scenes():
  path=HERE/(data['id']+'.json');path.write_text(json.dumps(data,indent=1)+'\n');print(path.relative_to(HERE))


if __name__=='__main__':main()
