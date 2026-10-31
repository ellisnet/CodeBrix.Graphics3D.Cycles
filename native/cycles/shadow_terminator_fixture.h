// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include <cmath>
namespace ccl {
// Phase E3 shadow-terminator fixture: a coarse smooth-shaded UV sphere (the
// terminator artifact needs few, large triangles) on a flat floor, lit by a
// grazing sun (modes 0-3), a point light (4-7) or both (8-9). Object offsets:
// geometry 0.1 or 0, shading 0.5 or 0 (modes 0-7 cover all four pairs per
// light); mode 8 renders two instances of one sphere (non-uniform scale,
// object-space offset) with geometry 0.3 and shading 1 (multiplier 2); mode 9
// a moving, deforming sphere with geometry 0.1 and shading 0.5. The sphere is
// diffuse under the sun (modes 0-3) and a diffuse/GGX mix otherwise. Modes 10-13 take
// shading offsets above 1 under the sun and the point light with geometry 0.1: 1.5
// (multiplier 4), 1.9 (20), 2 (infinite) and 3 (negative, ignored by the kernel). Native
// Cycles generates the vertex normals, bounds and packed uploads.
inline constexpr int shadow_terminator_cases=14;
inline float shadow_terminator_geometry(int mode){return mode==8?.3f:mode>=9?.1f:(mode%4==0||mode%4==2)?.1f:0.f;}
inline float shadow_terminator_shading(int mode){
  if(mode>=10){const float wide[]={1.5f,1.9f,2.f,3.f};return wide[mode-10];}
  return mode==8?1.f:mode==9?.5f:(mode%4==1||mode%4==2)?.5f:0.f;}
inline Shader *shadow_terminator_material(Scene &scene,bool glossy_mix){
  auto graph=make_unique<ShaderGraph>();auto diffuse=graph->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.7,.45,.25));diffuse->set_roughness(0);
  ShaderOutput *surface=diffuse->output("BSDF");
  if(glossy_mix){auto glossy=graph->create_node<GlossyBsdfNode>();glossy->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy->set_color(make_float3(.3,.5,.8));glossy->set_roughness(.4f);
    auto mix=graph->create_node<MixClosureNode>();mix->set_fac(.35f);graph->connect(surface,mix->input("Closure1"));graph->connect(glossy->output("BSDF"),mix->input("Closure2"));surface=mix->output("Closure");}
  graph->connect(surface,graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);return shader;
}
inline Mesh *shadow_terminator_sphere(Scene &scene,Shader *shader){
  constexpr int segments=9,rings=5;auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);
  const int vertices=2+segments*(rings-1),triangles=2*segments*(rings-1);mesh->resize_mesh(vertices,triangles);
  auto P=mesh->get_position_for_write();P[0]=make_float3(0,1,0);P[1]=make_float3(0,-1,0);
  for(int r=1;r<rings;r++)for(int s=0;s<segments;s++){const float theta=M_PI_F*r/rings,phi=2*M_PI_F*s/segments+.17f*r;
    P[2+(r-1)*segments+s]=make_float3(std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi));}
  auto &tri=mesh->get_triangles();int t=0;auto ring=[&](int r,int s){return 2+(r-1)*segments+(s%segments);};
  for(int s=0;s<segments;s++){tri[3*t]=0;tri[3*t+1]=ring(1,s+1);tri[3*t+2]=ring(1,s);t++;}
  for(int r=1;r+1<rings;r++)for(int s=0;s<segments;s++){
    tri[3*t]=ring(r,s);tri[3*t+1]=ring(r,s+1);tri[3*t+2]=ring(r+1,s);t++;
    tri[3*t]=ring(r,s+1);tri[3*t+1]=ring(r+1,s+1);tri[3*t+2]=ring(r+1,s);t++;}
  for(int s=0;s<segments;s++){tri[3*t]=1;tri[3*t+1]=ring(rings-1,s);tri[3*t+2]=ring(rings-1,s+1);t++;}
  std::ranges::fill(mesh->get_smooth(),true);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();return mesh;
}
inline void shadow_terminator_fixture(Scene &scene,int mode,Shader *emitter){
  auto sphere=shadow_terminator_sphere(scene,shadow_terminator_material(scene,mode>=4));
  auto floor_material=shadow_terminator_material(scene,false);
  auto floor=scene.create_node<Mesh>();{array<Node*> slots;slots.push_back_slow(floor_material);floor->set_used_shaders(slots);floor->resize_mesh(4,2);
    const float3 corners[]={make_float3(-5,-1.25f,-3),make_float3(5,-1.25f,-3),make_float3(5,-1.25f,4),make_float3(-5,-1.25f,4)};
    std::copy(std::begin(corners),std::end(corners),floor->get_position_for_write());const int indices[]={0,1,2,0,2,3};std::copy_n(indices,6,floor->get_triangles().data());
    std::ranges::fill(floor->get_smooth(),false);std::ranges::fill(floor->get_shader(),0);floor->tag_triangles_modified();}
  auto floor_object=scene.create_node<Object>();floor_object->set_geometry(floor);
  std::vector<Object*> spheres;
  auto add=[&](const Transform &tfm){auto object=scene.create_node<Object>();object->set_geometry(sphere);object->set_tfm(tfm);
    object->set_shadow_terminator_geometry_offset(shadow_terminator_geometry(mode));object->set_shadow_terminator_shading_offset(shadow_terminator_shading(mode));spheres.push_back(object);};
  if(mode==8){add(transform_translate(make_float3(-1.3f,-.2f,0))*transform_scale(make_float3(.9f,1.05f,.8f)));
    add(transform_translate(make_float3(1.4f,-.35f,.6f))*transform_rotate(.6f,make_float3(0,1,0))*transform_scale(make_float3(.7f,.9f,1.2f)));}
  else add(transform_rotate(.3f,make_float3(0,1,0))*transform_scale(make_float3(1.2f,1.15f,1.2f)));
  if(mode==9){deforming_motion_fixture(scene,*spheres[0],0);object_motion_fixture(scene,*spheres[0],1);}
  const bool sun=mode<4||mode>=8,point=mode>=4;
  if(sun){auto light=scene.create_node<SunLight>();light->set_angle(.05f);light->set_normalize(false);light->set_strength(make_float3(2.5f,2.4f,2.2f));light->set_use_mis(true);
    array<Node*> slots;slots.push_back_slow(emitter);light->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(light);
    // Grazing light from the right: the terminator runs across the sphere's front.
    object->set_tfm(transform_rotate(1.45f,make_float3(0,1,0))*transform_rotate(-.25f,make_float3(1,0,0)));}
  if(point){auto light=scene.create_node<PointLight>();light->set_radius(.15f);light->set_is_sphere(true);light->set_strength(make_float3(180,170,150));light->set_use_mis(true);
    array<Node*> slots;slots.push_back_slow(emitter);light->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(light);
    object->set_tfm(transform_translate(make_float3(-3.2f,1.4f,1.2f)));}
}
}
