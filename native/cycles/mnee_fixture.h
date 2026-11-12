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
// their outcomes and iteration counts). 9, 10, 11 and 12: cases 0, 1, 2 and 4 on the integrator's light tree; 13 and 14:
// case 4 with light linking (the floor and the wall each linked to their own caustic or ordinary lamps), on the flat CDF
// and on the tree; 15, 16 and 17: cases 5, 6 and 7 on the tree; 18: case 0 with the caster sphere moving (object motion blur).
// 19 to 22: case 0 with its caustic lamp replaced or joined: 19 a spot light with a disk radius, 20 a spherical spot light,
// 21 the point light beside a background light with caustics (a fixed-direction walk; a gradient world, so that the
// background light is enabled), 22 the point light with an emission shader that is not constant (a Light Falloff strength:
// shade_light_nee evaluates it on the MNEE shadow path). 23: case 1 with the slab's Beckmann refraction.
// 24: case 0 inside an absorbing box (the receivers reached after shade_volume).
// 25: case 0 with a tuft of thick Huang hair curves (caustic receivers too) on the floor beside the sphere.
// 26 to 28: case 0 with its caustic lamp replaced by an area light: 26 a rectangle with the full spread, 27 a disk with a
// narrow spread (the clamped sample region), 28 a larger rectangle with zero spread (a fixed-direction walk:
// the light point is the receiver's projection onto the light, so the light covers the sphere's footprint).
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "curve_fixture.h"
#include "hair_huang_fixture.h"
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
constexpr int mnee_cases=29;
constexpr int mnee_area_case=26;
constexpr int mnee_huang_case=25;
constexpr int mnee_beckmann_case=23,mnee_volume_case=24;
// The geometry and lamps of cases 5-17 are those of cases 0, 1, 2 or 4.
inline int mnee_base(int mode){return mode==mnee_beckmann_case?1:mode<5?mode:mode<=7||mode==9||mode>=15?0:mode==8||mode==10?1:mode==11?2:4;}
constexpr int mnee_moving_caster=18;
constexpr int mnee_first_lamp_case=19,mnee_background_case=21,mnee_lamp_shader_case=22;
inline bool mnee_uses_tree(int mode){return (mode>=9&&mode<=12)||mode==14||(mode>=15&&mode<=17);}
inline bool mnee_uses_linking(int mode){return mode==13||mode==14;}
// Cases whose CPU walks sample MNEE paths (at least five).
inline bool mnee_samples(int mode){const int base=mnee_base(mode);return (base==0||base==1)&&mode!=8;}
inline ccl::Shader *mnee_shader(ccl::Scene &scene,int kind){
  using namespace ccl;auto graph=std::make_unique<ShaderGraph>();ShaderOutput *out;
  if(kind==0){auto n=graph->create_node<DiffuseBsdfNode>();n->set_color(make_float3(.7f,.65f,.6f));out=n->output("BSDF");}
  else if(kind==1){auto n=graph->create_node<GlassBsdfNode>();n->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);n->set_roughness(0.f);n->set_IOR(1.45f);n->set_color(make_float3(.95f,.97f,1.f));out=n->output("BSDF");}
  else if(kind==2||kind==4){auto n=graph->create_node<RefractionBsdfNode>();n->set_distribution(kind==4?CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID:CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID);n->set_roughness(.2f);n->set_IOR(1.33f);n->set_color(make_float3(.9f,.95f,1.f));out=n->output("BSDF");}
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
  if(mode==1)casters.push_back(mnee_box(scene,mnee_shader(scene,variant==mnee_beckmann_case?4:2),make_float3(-1.2f,-.9f,-.6f),make_float3(1.f,-.4f,.8f)));
  else casters.push_back(mnee_sphere(scene,mnee_shader(scene,1),make_float3(-.1f,-.95f,.6f),1.f));
  if(mode==2)casters.push_back(mnee_box(scene,mnee_shader(scene,3),make_float3(1.3f,-2.f,-.2f),make_float3(2.1f,-1.f,.6f)));
  for(auto c:casters)c->set_is_caustics_caster(true);
  if(variant==mnee_huang_case){
    CurveFixtureGeometry tuft;curve_fixture_hair(scene,tuft,CURVE_THICK,huang_shader(scene,huang_materials()[0]),2026811u);
    auto object=scene.create_node<Object>();object->set_geometry(tuft.hair);object->set_is_caustics_receiver(true);
    object->set_tfm(transform_translate(make_float3(.85f,-2.f,.4f))*transform_rotate(-float(M_PI_2),make_float3(1,0,0))*transform_scale(make_float3(.45f)));}
  if(variant==mnee_volume_case){
    auto graph=std::make_unique<ShaderGraph>();auto absorption=graph->create_node<AbsorptionVolumeNode>();absorption->set_density(.15f);absorption->set_color(make_float3(.8f,.85f,.9f));
    graph->connect(absorption->output("Volume"),graph->output()->input("Volume"));auto medium=scene.create_node<Shader>();medium->set_graph(std::move(graph));medium->tag_update(&scene);
    mnee_box(scene,medium,make_float3(-2.f,-2.5f,-1.f),make_float3(2.f,1.5f,2.5f));}
  if(variant==5||variant==15){scene.integrator->set_ao_bounces(1);scene.integrator->set_ao_factor(.5f);scene.integrator->set_ao_distance(1.2f);scene.integrator->set_ao_additive_factor(.7f);}
  if(variant==6||variant==16)object_motion_fixture(scene,*floor,2);
  if(variant==mnee_moving_caster)object_motion_fixture(scene,*casters[0],1);
  if(mnee_uses_tree(variant))scene.integrator->set_use_light_tree(true);
  if(variant==7||variant==17){deforming_motion_fixture(scene,*floor,1);deforming_motion_fixture(scene,*wall,0);}
  if(variant==8){deforming_motion_fixture(scene,*casters[0],0);deforming_motion_fixture(scene,*floor,1);}
  if(variant==mnee_lamp_shader_case){
    auto graph=std::make_unique<ShaderGraph>();auto falloff=graph->create_node<LightFalloffNode>();falloff->set_strength(1.f);falloff->set_smooth(.5f);
    auto emission=graph->create_node<EmissionNode>();emission->set_color(one_float3());graph->connect(falloff->output("Quadratic"),emission->input("Strength"));
    graph->connect(emission->output("Emission"),graph->output()->input("Surface"));emitter=scene.create_node<Shader>();emitter->set_graph(std::move(graph));emitter->tag_update(&scene);}
  if(variant==mnee_background_case){
    // A direction-dependent world: with a constant one LightManager::test_enabled_lights leaves the background light disabled.
    auto graph=std::make_unique<ShaderGraph>();auto coordinates=graph->create_node<TextureCoordinateNode>();auto gradient=graph->create_node<GradientTextureNode>();
    gradient->set_gradient_type(NODE_BLEND_LINEAR);auto background=graph->create_node<BackgroundNode>();background->set_strength(1.5f);
    graph->connect(coordinates->output("Generated"),gradient->input("Vector"));graph->connect(gradient->output("Color"),background->input("Color"));
    graph->connect(background->output("Background"),graph->output()->input("Surface"));auto world=scene.create_node<Shader>();world->set_graph(std::move(graph));world->tag_update(&scene);
    scene.background->set_shader(world);
    auto light=scene.create_node<BackgroundLight>();light->set_map_resolution(64);light->set_use_mis(true);light->set_use_caustics(true);light->set_strength(one_float3());
    array<Node*> slots;slots.push_back_slow(world);light->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(light);}
  const bool replaced=(variant>=mnee_first_lamp_case&&variant<mnee_background_case)||variant>=mnee_area_case;
  for(int lamp=0;lamp<(mode==4?2:1);lamp++){
    Light *light;
    if(replaced){
      if(variant>=mnee_area_case){const int kind=variant-mnee_area_case;
        auto area=scene.create_node<AreaLight>();area->set_sizeu(kind==1?.8f:kind==2?2.4f:1.2f);area->set_sizev(kind==1?.8f:kind==2?2.4f:.9f);area->set_ellipse(kind==1);
        area->set_spread(kind==0?float(M_PI):kind==1?1.2f:0.f);area->set_is_portal(false);light=area;}
      else{const int kind=variant-mnee_first_lamp_case;
        auto spot=scene.create_node<SpotLight>();spot->set_radius(kind==0?.25f:.3f);spot->set_is_sphere(kind==1);spot->set_angle(kind==0?1.f:1.2f);spot->set_smooth(kind==0?.3f:.2f);light=spot;}
      light->set_strength(make_float3(160,150,140));
    }
    else if(mode==2){auto sun=scene.create_node<SunLight>();sun->set_angle(.05f);sun->set_normalize(false);light=sun;light->set_strength(make_float3(2.5f,2.4f,2.2f));}
    else{auto point=scene.create_node<PointLight>();point->set_radius(mode==1?.3f:0.f);point->set_is_sphere(mode==1);light=point;light->set_strength(make_float3(160,150,140));}
    light->set_use_mis(true);light->set_use_caustics(lamp==0);
    array<Node*> slots;slots.push_back_slow(emitter);light->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(light);
    if(mnee_uses_linking(variant)){floor->set_receiver_light_set(1);wall->set_receiver_light_set(2);object->set_light_set_membership(lamp==0?(1ull<<0)|(1ull<<1):(1ull<<0)|(1ull<<2));}
    // Spot and area lamps face down (local -Z to world -Y), as author_light_tree.py's DOWN.
    if(replaced)object->set_tfm(transform_translate(make_float3(.15f,variant==20?2.6f:2.2f,.5f))*transform_rotate(-float(M_PI_2),make_float3(1,0,0)));
    else object->set_tfm(mode==2?transform_rotate(-1.35f,make_float3(1,0,0))*transform_rotate(.2f,make_float3(0,0,1)):
                    transform_translate(lamp?make_float3(-2.5f,2.2f,-1.5f):make_float3(.15f,2.2f,.5f)));
  }
}
