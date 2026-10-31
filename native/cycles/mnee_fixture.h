// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Caustic fixtures for the MNEE gates (lighting_gate.cpp CODEBRIX_MNEE_GATE and
// mnee_gate.cpp). Cases: 0 sharp glass sphere on a diffuse receiver under a
// point light; 1 rough refractive slab under a spherical point light; 2 glass
// sphere and a glossy (reflective, no refractive closure) caster under a sun;
// 3 casters without a receiver flag (caustics off); 4 glass sphere with one
// caustic and one ordinary point light; 5 case 0 with fast GI in its additive mode
// (KERNEL_FEATURE_AO_ADDITIVE: an AO shadow path beside the MNEE shadow path); 6 case 0
// with object motion blur on the floor receiver; 7 case 0 with
// deformation motion blur on the floor and wall receivers; 8 case 1 with the slab caster and the
// floor deforming (the pinned walk takes a deforming caster's centre-frame corners against its
// deformed intersection, so these walks mostly fail on the CPU too: the state gate compares
// their outcomes and iteration counts). Variant 9 (not a gated case): case 0 with the caster
// sphere moving, which the device refuses (mnee.cpp).
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include <cmath>
constexpr int mnee_cases=9;
// The geometry and lamps of cases 5-8 are those of case 0 or 1.
inline int mnee_base(int mode){return mode==8?1:mode>=5?0:mode;}
constexpr int mnee_refused_moving_caster=9;
// Cases whose CPU walks sample MNEE paths (at least five).
inline bool mnee_samples(int mode){const int base=mnee_base(mode);return (base==0||base==1)&&mode!=8;}
inline ccl::Shader *mnee_shader(ccl::Scene &scene,int kind){
  using namespace ccl;auto graph=std::make_unique<ShaderGraph>();ShaderOutput *out;
  if(kind==0){auto n=graph->create_node<DiffuseBsdfNode>();n->set_color(make_float3(.7f,.65f,.6f));out=n->output("BSDF");}
  else if(kind==1){auto n=graph->create_node<GlassBsdfNode>();n->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);n->set_roughness(0.f);n->set_IOR(1.45f);n->set_color(make_float3(.95f,.97f,1.f));out=n->output("BSDF");}
  else if(kind==2){auto n=graph->create_node<RefractionBsdfNode>();n->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID);n->set_roughness(.2f);n->set_IOR(1.33f);n->set_color(make_float3(.9f,.95f,1.f));out=n->output("BSDF");}
  else {auto n=graph->create_node<GlossyBsdfNode>();n->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);n->set_roughness(.1f);n->set_color(make_float3(.8f,.8f,.7f));out=n->output("BSDF");}
  graph->connect(out,graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);return shader;
}
inline ccl::Object *mnee_mesh(ccl::Scene &scene,ccl::Shader *shader,const std::vector<ccl::float3> &P,const std::vector<int> &tris,bool smooth){
  using namespace ccl;auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);
  mesh->resize_mesh(int(P.size()),int(tris.size()/3));std::copy(P.begin(),P.end(),mesh->get_position_for_write());std::copy(tris.begin(),tris.end(),mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),smooth);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(mesh);return object;
}
inline ccl::Object *mnee_sphere(ccl::Scene &scene,ccl::Shader *shader,ccl::float3 c,float r){
  std::vector<ccl::float3> P;std::vector<int> t;const int rings=8,segments=16;
  P.push_back(c+ccl::make_float3(0,r,0));
  for(int i=1;i<rings;i++){const float th=float(M_PI)*i/rings;for(int j=0;j<segments;j++){const float ph=2*float(M_PI)*j/segments;
    P.push_back(c+ccl::make_float3(r*sinf(th)*cosf(ph),r*cosf(th),r*sinf(th)*sinf(ph)));}}
  P.push_back(c+ccl::make_float3(0,-r,0));const int last=int(P.size())-1;
  for(int j=0;j<segments;j++){const int a=1+j,b=1+(j+1)%segments;t.insert(t.end(),{0,b,a});}
  for(int i=0;i<rings-2;i++)for(int j=0;j<segments;j++){const int a=1+i*segments+j,b=1+i*segments+(j+1)%segments,c2=a+segments,d=b+segments;t.insert(t.end(),{a,b,d,a,d,c2});}
  for(int j=0;j<segments;j++){const int a=1+(rings-2)*segments+j,b=1+(rings-2)*segments+(j+1)%segments;t.insert(t.end(),{a,b,last});}
  return mnee_mesh(scene,shader,P,t,true);
}
inline ccl::Object *mnee_box(ccl::Scene &scene,ccl::Shader *shader,ccl::float3 lo,ccl::float3 hi){
  using ccl::make_float3;std::vector<ccl::float3> P;for(int i=0;i<8;i++)P.push_back(make_float3(i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z));
  const std::vector<int> t={0,2,3,0,3,1,4,5,7,4,7,6,0,1,5,0,5,4,2,6,7,2,7,3,0,4,6,0,6,2,1,3,7,1,7,5};
  return mnee_mesh(scene,shader,P,t,true);
}
inline void mnee_fixture(ccl::Scene &scene,int variant,ccl::Shader *emitter){
  using namespace ccl;const int mode=mnee_base(variant);
  auto floor=mnee_mesh(scene,mnee_shader(scene,0),{make_float3(-5,-2,-3),make_float3(5,-2,-3),make_float3(5,-2,4),make_float3(-5,-2,4)},{0,2,1,0,3,2},false);
  auto wall=mnee_mesh(scene,mnee_shader(scene,0),{make_float3(-5,-2,3),make_float3(5,-2,3),make_float3(5,4,3),make_float3(-5,4,3)},{0,1,2,0,2,3},false);
  const bool receivers=mode!=3;floor->set_is_caustics_receiver(receivers);wall->set_is_caustics_receiver(receivers);
  std::vector<Object*> casters;
  if(mode==1)casters.push_back(mnee_box(scene,mnee_shader(scene,2),make_float3(-1.2f,-.9f,-.6f),make_float3(1.f,-.4f,.8f)));
  else casters.push_back(mnee_sphere(scene,mnee_shader(scene,1),make_float3(-.1f,-.95f,.6f),1.f));
  if(mode==2)casters.push_back(mnee_box(scene,mnee_shader(scene,3),make_float3(1.3f,-2.f,-.2f),make_float3(2.1f,-1.f,.6f)));
  for(auto c:casters)c->set_is_caustics_caster(true);
  if(variant==5){scene.integrator->set_ao_bounces(1);scene.integrator->set_ao_factor(.5f);scene.integrator->set_ao_distance(1.2f);scene.integrator->set_ao_additive_factor(.7f);}
  if(variant==6)object_motion_fixture(scene,*floor,2);
  if(variant==9)object_motion_fixture(scene,*casters[0],1);
  if(variant==7){deforming_motion_fixture(scene,*floor,1);deforming_motion_fixture(scene,*wall,0);}
  if(variant==8){deforming_motion_fixture(scene,*casters[0],0);deforming_motion_fixture(scene,*floor,1);}
  for(int lamp=0;lamp<(mode==4?2:1);lamp++){
    Light *light;
    if(mode==2){auto sun=scene.create_node<SunLight>();sun->set_angle(.05f);sun->set_normalize(false);light=sun;light->set_strength(make_float3(2.5f,2.4f,2.2f));}
    else{auto point=scene.create_node<PointLight>();point->set_radius(mode==1?.3f:0.f);point->set_is_sphere(mode==1);light=point;light->set_strength(make_float3(160,150,140));}
    light->set_use_mis(true);light->set_use_caustics(lamp==0);
    array<Node*> slots;slots.push_back_slow(emitter);light->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(light);
    object->set_tfm(mode==2?transform_rotate(-1.35f,make_float3(1,0,0))*transform_rotate(.2f,make_float3(0,0,1)):
                    transform_translate(lamp?make_float3(-2.5f,2.2f,-1.5f):make_float3(.15f,2.2f,.5f)));
  }
}
