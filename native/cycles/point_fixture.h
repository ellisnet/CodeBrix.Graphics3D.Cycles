// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Point cloud fixture: clouds baked into a single-user object (the static-BVH path sets SD_OBJECT_TRANSFORM_APPLIED) and
// instanced by two objects with rotated, non-uniformly scaled and mirrored transforms, plus a triangle floor; points of
// zero radius and repeated points, two materials per cloud, optional object or deformation motion (with moving radii).
// Rays aim at points (centre, inside, on and just outside the radius), start inside a point, use short tmin/tmax
// windows, scaled directions and random directions, then are perturbed by 0-2 ULP per component.
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/pointcloud.h"
#include "scene/scene.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "object_motion_fixture.h"
#include "util/transform.h"
#include <cmath>
#include <cstring>
#include <random>
#include <vector>
namespace ccl {
struct PointFixtureGeometry {PointCloud *cloud=nullptr;std::vector<float3> points;std::vector<float> radius;};
struct PointFixtureObject {unsigned geometry;Transform tfm;};
struct PointFixture {std::vector<PointFixtureGeometry> geometry;std::vector<PointFixtureObject> objects;};
struct PointRay {float3 P,D;float tmin,tmax,time;unsigned kind;};
enum PointMotion {POINT_STATIC,POINT_OBJECT_MOTION,POINT_DEFORM_3,POINT_DEFORM_5};
inline PointCloud *point_fixture_cloud(Scene &scene,PointFixtureGeometry &g,Shader *a,Shader *b,unsigned seed,unsigned count,PointMotion motion){
  std::mt19937 rng(seed);std::uniform_real_distribution<float> unit(-1.f,1.f);
  for(unsigned i=0;i<count;i++){
    float3 at=make_float3(unit(rng)*1.8f,unit(rng)*1.8f,unit(rng)*1.2f);float radius=.015f+.12f*std::abs(unit(rng));
    if(i%13==7)radius=0;                                  // Zero radius: only an exact centre ray can hit it.
    if(i%17==9&&!g.points.empty())at=g.points.back();     // A repeated point.
    g.points.push_back(at);g.radius.push_back(radius);
  }
  auto cloud=scene.create_node<PointCloud>();array<Node*> shaders;shaders.push_back_slow(a);shaders.push_back_slow(b);cloud->set_used_shaders(shaders);
  cloud->resize(int(count));
  auto P=cloud->attributes.find(ATTR_STD_POSITION)->data_for_write<packed_float3>();auto R=cloud->attributes.find(ATTR_STD_RADIUS)->data_for_write<float>();
  for(size_t i=0;i<count;i++){P[i]=g.points[i];R[i]=g.radius[i];cloud->get_shader()[i]=i%3==2?1:0;}
  if(motion==POINT_DEFORM_3||motion==POINT_DEFORM_5){
    const unsigned steps=motion==POINT_DEFORM_5?5:3;scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);
    cloud->set_motion_steps(steps);cloud->set_use_motion_blur(true);auto attr_P=cloud->attributes.find(ATTR_STD_POSITION);auto attr_R=cloud->attributes.find(ATTR_STD_RADIUS);attr_P->add_motion(cloud);attr_R->add_motion(cloud);
    for(unsigned step=0;step<steps;step++){
      if(step==(steps-1)/2)continue;
      const float u=2.f*step/(steps-1)-1;const int attr_step=attr_P->time_step_to_attr_step(step,steps);
      auto out=attr_P->data_for_write<packed_float3>(attr_step);auto out_radius=attr_R->data_for_write<float>(attr_step);
      for(size_t i=0;i<count;i++){const float3 p=g.points[i];out[i]=p+make_float3(.08f*u*p.y,.05f*u*p.x,.13f*u*p.x*p.y+.035f*u*u*p.x);out_radius[i]=g.radius[i]*(1.f+.3f*u*(i%2?1.f:-1.f));}
    }
  }
  cloud->tag_position_modified();cloud->tag_radius_modified();cloud->tag_shader_modified();
  g.cloud=cloud;return cloud;
}
inline PointFixture point_fixture(Scene &scene,Shader *a,Shader *b,Shader *floor_shader,unsigned count,PointMotion motion){
  PointFixture f;f.geometry.resize(2);
  point_fixture_cloud(scene,f.geometry[0],a,b,2026110,count,motion);point_fixture_cloud(scene,f.geometry[1],a,b,2026311,count,motion);
  // Single user with a uniform scale: baked by the static BVH update. Two users: instanced, one non-uniformly scaled, one mirrored.
  f.objects.push_back({0,transform_translate(make_float3(-5.f,0.f,1.f))*transform_rotate(.4f,normalize(make_float3(.3f,.5f,.8f)))*transform_scale(make_float3(1.2f))});
  f.objects.push_back({1,transform_translate(make_float3(0.f,0.f,1.f))*transform_rotate(.7f,normalize(make_float3(.9f,.2f,.4f)))*transform_scale(make_float3(1.3f,.8f,1.1f))});
  f.objects.push_back({1,transform_translate(make_float3(5.f,0.f,1.f))*transform_rotate(-.3f,normalize(make_float3(.1f,.8f,.5f)))*transform_scale(make_float3(-1.1f,1.2f,.9f))});
  unsigned mode=1;
  for(const auto &o:f.objects){auto object=scene.create_node<Object>();object->set_geometry(f.geometry[o.geometry].cloud);object->set_tfm(o.tfm);
    if(motion==POINT_OBJECT_MOTION&&o.geometry==1)object_motion_fixture(scene,*object,mode++);}
  auto floor=scene.create_node<Mesh>();array<Node*> shaders;shaders.push_back_slow(floor_shader);floor->set_used_shaders(shaders);floor->resize_mesh(4,2);
  const float v[][3]={{-12,-12,-.5f},{12,-12,-.5f},{12,12,-.5f},{-12,12,-.5f}};for(int i=0;i<4;i++)floor->get_position_for_write()[i]=make_float3(v[i][0],v[i][1],v[i][2]);
  const int tris[]={0,1,2,0,2,3};std::copy_n(tris,6,floor->get_triangles().data());std::ranges::fill(floor->get_smooth(),false);std::ranges::fill(floor->get_shader(),0);floor->tag_triangles_modified();
  auto ground=scene.create_node<Object>();ground->set_geometry(floor);
  return f;
}
inline float point_fixture_bits(float x,int ulps){uint32_t b;std::memcpy(&b,&x,4);b=uint32_t(int64_t(b)+ulps);std::memcpy(&x,&b,4);return x;}
inline std::vector<PointRay> point_rays(const PointFixture &f,unsigned count,unsigned seed,bool moving){
  std::mt19937 rng(seed);std::uniform_real_distribution<float> unit(-1.f,1.f);std::vector<PointRay> rays;rays.reserve(count);
  for(unsigned i=0;i<count;i++){
    const auto &o=f.objects[rng()%f.objects.size()];const auto &g=f.geometry[o.geometry];
    const size_t p=rng()%g.points.size();const float radius=g.radius[p]*1.3f;const float3 target=transform_point(&o.tfm,g.points[p]);
    const unsigned kind=i%8;PointRay ray;ray.tmin=0;ray.tmax=1e6f;ray.kind=kind;ray.time=moving?.5f+.5f*unit(rng):.5f;
    float3 dir=normalize(make_float3(unit(rng),unit(rng),unit(rng)));
    // Offset across the point: centre, inside, the silhouette and just outside.
    const float3 side=normalize(cross(dir,make_float3(unit(rng),unit(rng),unit(rng))));
    const float offsets[]={0.f,.5f,.95f,1.0f,1.05f,2.f};
    const float3 aim=target+side*radius*offsets[rng()%6]*(kind==6?.2f:1.f);
    const float distance=kind==6?.0f:(2.f+6.f*std::abs(unit(rng)));
    if(kind==6){ray.P=aim;ray.tmin=0;}else ray.P=aim-dir*distance;
    ray.D=dir;
    if(kind==7){
      // Fully random, from outside the fixture.
      ray.P=make_float3(unit(rng)*9.f,unit(rng)*9.f,4.f+3.f*std::abs(unit(rng)));ray.D=normalize(make_float3(unit(rng),unit(rng),-.3f-std::abs(unit(rng))));
    }
    if(kind==5){const float scale=.25f+4.f*std::abs(unit(rng));ray.D=ray.D*scale;}
    if(kind==2){ray.tmax=distance+radius*unit(rng);ray.tmin=std::max(0.f,distance-radius);}
    const int ulps=int(rng()%5)-2;
    ray.P.x=point_fixture_bits(ray.P.x,ulps);ray.P.y=point_fixture_bits(ray.P.y,-ulps);ray.D.z=point_fixture_bits(ray.D.z,ulps);
    rays.push_back(ray);
  }
  return rays;
}
}
