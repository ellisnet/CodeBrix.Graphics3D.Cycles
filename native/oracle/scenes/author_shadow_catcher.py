#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the shadow catcher cbcOracleScene files. Rerunning reproduces them exactly.

Both scenes put a diffuse floor marked as a shadow catcher (object socket
is_shadow_catcher) under a box and a glossy sphere and add the three shadow catcher
passes as named pass nodes. The oracle output contract records the Combined film;
the passes are read through cbc_host_pass_floats by the gates and the managed tests.
"""
import json
from pathlib import Path
import author_starters as s
HERE=Path(__file__).resolve().parent
PASSES=[('shadow_catcher','Shadow Catcher'),('shadow_catcher_matte','Shadow Catcher Matte'),('shadow_catcher_sample_count','Shadow Catcher Sample Count')]


def catcher_scene(identifier,description,approximate):
 floor=[-4,-1,-2, 4,-1,-2, 4,-1,6, -4,-1,6];cube,cube_indices=s.box(0.9,0.9,0.9);ball,ball_indices=s.sphere(0.5,24,12)
 nodes=s.common(64,48,0.9,[1,0,0,0, 0,0.9950042,-0.0998334,0.6, 0,0.0998334,0.9950042,-4])
 nodes+=[dict(handle='world',create='shader',graph=dict(nodes=[dict(handle='b',type='background_shader',set=dict(color=[0.3,0.35,0.45],strength=1.0))],
   connect=[['b','Background','output','Surface']]),set=dict(emission_sampling_method='none')),
  dict(handle='bg',scene='background',set=dict(transparent=not approximate,shader='world'))]
 nodes[2]['set']['use_approximate_shadow_catcher']=approximate
 nodes+=[s.material('floor-material','diffuse_bsdf',dict(color=[0.6,0.55,0.5],roughness=0.0)),
  s.material('cube-material','diffuse_bsdf',dict(color=[0.8,0.3,0.1],roughness=0.0)),
  s.material('ball-material','glossy_bsdf',dict(distribution='ggx',roughness=0.3,color=[0.7,0.75,0.8]))]
 nodes+=s.mesh('floor',floor,[0,2,1, 0,3,2],'floor-material')
 nodes[-1]['set']['is_shadow_catcher']=True
 nodes+=s.mesh('cube',cube,cube_indices,'cube-material',s.translate(-0.7,-0.55,0.8))
 nodes+=s.mesh('ball',ball,ball_indices,'ball-material',s.translate(0.8,-0.5,0.3))
 if approximate:
  # Sun light travels along the object's -Z; this rotation tilts -Z down and to the side.
  nodes+=s.light('sun','sunlight',dict(angle=0.05),[0.8660254,0,0.5,0, -0.3535534,0.7071068,0.6123724,0, -0.3535534,-0.7071068,0.6123724,0],[3,2.9,2.7])
 else:
  nodes+=s.light('point','pointlight',dict(radius=0.25,is_sphere=True),s.translate(1.5,2.5,-1.5),[300,280,260])
  nodes[-1]['set']['is_shadow_catcher']=True
 nodes+=[dict(handle='pass-'+kind,create='pass',set=dict(type=kind,name=name)) for kind,name in PASSES]
 return s.scene(identifier,description,64,48,16,nodes)


def main():
 for data in [catcher_scene('shadow-catcher/catcher-point-light','Shadow catcher floor under a box and a GGX sphere, transparent film, a point light that is itself marked as a shadow catcher; the three shadow catcher passes',False),
              catcher_scene('shadow-catcher/catcher-approximate','Shadow catcher floor under a box and a GGX sphere, opaque film with the approximate shadow catcher (background pass), one sun light',True)]:
  path=HERE/(data['id']+'.json');path.parent.mkdir(parents=True,exist_ok=True)
  path.write_text(json.dumps(data,indent=1)+'\n');print(path.relative_to(HERE))


if __name__=='__main__':main()
