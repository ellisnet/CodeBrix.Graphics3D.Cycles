// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Traversal precision fixture: sliver triangles (including the Principled bench
// sliver, triangle 328 of the bench asset in render_gate presentation space),
// shared by mirrored/rotated/scaled instances, and an instanced emissive quad
// whose diagonal is grazed by near-tangent rays. Rays aim at edges, vertices and
// interiors, then are perturbed by 0-2 ULP per component.
#include "scene/scene.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/transform.h"
#include <cmath>
#include <cstring>
#include <random>
#include <vector>
namespace ccl {
struct PrecisionRay {float3 P,D;float tmax;int object,prim;};
inline float precision_bits(uint32_t bits){float value;std::memcpy(&value,&bits,4);return value;}
// The ray the Principled bench (M1) rendered at stage 30, path 84, bit for bit.
inline PrecisionRay precision_bench_ray(){
  return {make_float3(0.52153337f,-4.f,-1.887770772f),make_float3(-0.6102825999f,0.45429039f,0.6489803195f),3.4e38f,0,0};
}
inline void precision_fixture(Scene &scene){
  auto sliver=scene.create_node<Mesh>();array<Node*> surfaces;surfaces.push_back_slow(scene.default_surface);sliver->set_used_shaders(surfaces);
  std::vector<float3> v={
    make_float3(precision_bits(0xbfc4a0b1),precision_bits(0xc03e7fc6),precision_bits(0x3ebf4ed8)),
    make_float3(precision_bits(0xbfc4a0b1),precision_bits(0xbfb41c04),precision_bits(0x3e155360)),
    make_float3(precision_bits(0xbfc3509c),precision_bits(0xbfb44c4a),precision_bits(0x3e0aeea0))};
  std::mt19937 rng(20261020);std::uniform_real_distribution<float> unit(-1.f,1.f);
  for(int k=0;k<7;k++){
    const float3 a=make_float3(unit(rng)*1.5f,unit(rng)*1.5f-2.f,unit(rng)*1.5f);
    const float3 dir=normalize(make_float3(unit(rng),unit(rng),unit(rng)));const float length=.6f+.4f*k;
    const float width=std::pow(10.f,-4.f+.35f*k);const float3 n=normalize(make_float3(unit(rng),unit(rng),unit(rng)));
    v.push_back(a);v.push_back(a+length*dir);v.push_back(a+.5f*length*dir+width*n);
  }
  sliver->resize_mesh(v.size(),v.size()/3);
  for(size_t i=0;i<v.size();i++){sliver->get_position_for_write()[i]=v[i];sliver->get_triangles()[i]=int(i);}
  std::ranges::fill(sliver->get_smooth(),false);std::ranges::fill(sliver->get_shader(),0);sliver->tag_triangles_modified();
  const Transform sliver_tfms[]={transform_identity(),
    transform_translate(make_float3(3.1f,.2f,.7f))*transform_rotate(.71f,normalize(make_float3(.2f,1.f,.4f)))*transform_scale(make_float3(-1.3f,.9f,1.1f)),
    transform_translate(make_float3(-2.9f,.4f,-.3f))*transform_rotate(-1.13f,normalize(make_float3(1.f,.1f,.3f)))*transform_scale(make_float3(.7f,1.6f,-.8f))};
  for(auto &tfm:sliver_tfms){auto object=scene.create_node<Object>();object->set_geometry(sliver);object->set_tfm(tfm);}
  auto emission=scene.create_node<Shader>();auto graph=make_unique<ShaderGraph>();auto light=graph->create_node<EmissionNode>();
  light->set_color(make_float3(.8f,.7f,.5f));light->set_strength(2.f);graph->connect(light->output("Emission"),graph->output()->input("Surface"));
  emission->set_graph(std::move(graph));emission->tag_update(&scene);
  auto quad=scene.create_node<Mesh>();array<Node*> lights;lights.push_back_slow(emission);quad->set_used_shaders(lights);quad->resize_mesh(4,2);
  const float q[][3]={{-.9f,-.4f,0},{.8f,-.5f,.05f},{.7f,.6f,-.04f},{-.8f,.5f,0}};
  for(int i=0;i<4;i++)quad->get_position_for_write()[i]=make_float3(q[i][0],q[i][1],q[i][2]);
  const int tris[]={0,1,2,0,2,3};std::copy_n(tris,6,quad->get_triangles().data());
  std::ranges::fill(quad->get_smooth(),false);std::ranges::fill(quad->get_shader(),0);quad->tag_triangles_modified();
  const Transform quad_tfms[]={transform_translate(make_float3(.4f,1.9f,1.3f))*transform_rotate(.37f,make_float3(1,0,0)),
    transform_translate(make_float3(-.6f,-1.7f,1.6f))*transform_rotate(-.52f,normalize(make_float3(1.f,.3f,0.f)))*transform_scale(make_float3(-1.f,1.2f,1.f))};
  for(auto &tfm:quad_tfms){auto object=scene.create_node<Object>();object->set_geometry(quad);object->set_tfm(tfm);}
}
// Targets on edges (and the shared quad diagonal), at vertices and inside, seen
// from random origins; components then move by 0-2 ULP. tmax is the target
// distance scaled by 1 +- 2^-20 for shadow use (closest uses 3.4e38).
inline std::vector<PrecisionRay> precision_rays(const Scene &scene,unsigned count,bool shadow){
  std::vector<PrecisionRay> rays;if(!shadow)rays.push_back(precision_bench_ray());
  std::mt19937 rng(shadow?77u:41u);std::uniform_real_distribution<float> unit(0.f,1.f);
  auto ulp=[&](float x){int steps=int(rng()%5)-2;for(;steps>0;steps--)x=std::nextafter(x,INFINITY);for(;steps<0;steps++)x=std::nextafter(x,-INFINITY);return x;};
  while(rays.size()<count){
    const int object=int(rng()%scene.objects.size());const Object *o=scene.objects[object];const Mesh *mesh=static_cast<const Mesh*>(o->get_geometry());
    const int prim=int(rng()%mesh->num_triangles());const Transform tfm=o->get_tfm();
    float3 c[3];for(int k=0;k<3;k++)c[k]=transform_point(&tfm,float3(mesh->get_position()[mesh->get_triangles()[prim*3+k]]));
    float u=unit(rng),w=unit(rng);const unsigned kind=rng()%4;const float eps=std::pow(10.f,-7.f+4.f*unit(rng))*(unit(rng)<.5f?-1.f:1.f);
    if(kind==0){w=eps;}else if(kind==1){u=eps;}else if(kind==2){w=1.f-u+eps;}else if(u+w>1.f){u=1.f-u;w=1.f-w;}
    const float3 target=c[0]+u*(c[1]-c[0])+w*(c[2]-c[0]);
    const float3 offset=normalize(make_float3(unit(rng)-.5f,unit(rng)-.5f,unit(rng)-.5f))*(.5f+7.5f*unit(rng));
    float3 P=target+offset;float3 D=normalize(target-P);const float distance=len(target-P);
    if(rng()%3){P=make_float3(ulp(P.x),ulp(P.y),ulp(P.z));D=make_float3(ulp(D.x),ulp(D.y),ulp(D.z));}
    rays.push_back({P,D,shadow?distance*(unit(rng)<.5f?1.f-9.5e-7f:1.f+9.5e-7f):3.4e38f,object,prim});
  }
  return rays;
}
}
