// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Curve fixture: ribbons, thick curves and thick linear curves, each baked into a single-user object (the static-BVH
// path sets SD_OBJECT_TRANSFORM_APPLIED) and instanced by two objects with rotated, non-uniformly scaled and mirrored
// transforms, plus a triangle floor; curves of 1 to 7 keys, tapered radii, a curve with repeated keys. Rays aim at key
// segments (centre, inside and just outside the radius), down the tangent (near-parallel cylinders), from inside a
// thick curve, and at random, then are perturbed by 0-2 ULP per component.
#include "scene/hair.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/scene.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/transform.h"
#include <cmath>
#include <cstring>
#include <cmath>
#include <random>
#include <vector>
namespace ccl {
struct CurveFixtureGeometry {Hair *hair;std::vector<float3> keys;std::vector<float> radius;std::vector<int> first;};
struct CurveFixtureObject {unsigned geometry;Transform tfm;};
struct CurveFixture {std::vector<CurveFixtureGeometry> geometry;std::vector<CurveFixtureObject> objects;Shader *shader;};
struct CurveRay {float3 P,D;float tmin,tmax,time;unsigned kind;};
// Motion for the fixture: deformation steps (0 for none) on every geometry whose index passes the mask, with or without
// radii per step (the keys swing along per-geometry directions), and object motion on every third object.
struct CurveMotion {unsigned steps=0;bool radius=false;bool object=false;unsigned geometry_mask=~0u;};
inline float3 curve_fixture_swing(size_t geometry,float time){
  const float3 axis=normalize(make_float3(.4f+.3f*float(geometry%3),.7f-.2f*float(geometry%2),.25f));
  return axis*(.45f*(2.f*time-1.f));
}
inline void curve_fixture_motion(CurveFixture &f,Scene &scene,const CurveMotion &m){
  if(m.steps)for(size_t g=0;g<f.geometry.size();g++){
    if(!((m.geometry_mask>>g)&1u))continue;
    auto &geometry=f.geometry[g];Hair *hair=geometry.hair;
    hair->set_motion_steps(m.steps);hair->set_use_motion_blur(true);
    auto P=hair->attributes.find(ATTR_STD_POSITION);auto R=hair->attributes.find(ATTR_STD_RADIUS);P->add_motion(hair);if(m.radius)R->add_motion(hair);
    for(unsigned step=0;step<m.steps;step++){
      if(step==(m.steps-1)/2)continue;
      const float time=float(step)/float(m.steps-1);
      auto out=P->data_for_write<packed_float3>(P->time_step_to_attr_step(int(step),int(m.steps)));
      for(size_t k=0;k<geometry.keys.size();k++){
        const float bend=float(k%3)*.05f*std::sin(6.f*time);out[k]=geometry.keys[k]+curve_fixture_swing(g,time)+make_float3(bend,-bend,bend*.5f);}
      if(m.radius){auto r=R->data_for_write<float>(R->time_step_to_attr_step(int(step),int(m.steps)));
        for(size_t k=0;k<geometry.keys.size();k++)r[k]=geometry.radius[k]*(1.f+.5f*(time-.5f));}
    }
  }
  if(m.object)for(size_t o=0;o<scene.objects.size();o++){
    Object *object=scene.objects[o];
    if(!object->get_geometry()->is_hair()||o%3!=0)continue;
    array<Transform> motion(3);const Transform tfm=object->get_tfm();const float3 shift=make_float3(.3f,-.25f,.15f);
    motion[0]=transform_translate(-shift)*tfm*transform_rotate(-.05f,normalize(make_float3(.2f,.9f,.4f)));motion[1]=tfm;motion[2]=transform_translate(shift)*tfm*transform_rotate(.05f,normalize(make_float3(.2f,.9f,.4f)));
    object->set_motion(motion);
  }
}
inline Hair *curve_fixture_hair(Scene &scene,CurveFixtureGeometry &g,CurveShapeType shape,Shader *shader,unsigned seed){
  std::mt19937 rng(seed);std::uniform_real_distribution<float> unit(-1.f,1.f);
  const int counts[]={2,3,4,7,5,2,1,4,6,3,2,5};
  for(int c=0;c<12;c++){
    g.first.push_back(int(g.keys.size()));
    float3 at=make_float3(unit(rng)*1.6f,unit(rng)*1.6f,unit(rng)*.3f);const float base=.015f+.06f*std::abs(unit(rng));
    float3 dir=normalize(make_float3(unit(rng)*.6f,unit(rng)*.6f,1.f));
    for(int k=0;k<counts[c];k++){
      g.keys.push_back(at);g.radius.push_back(base*(1.f-.11f*k));
      // Curve 7 repeats its second key (a zero-length segment).
      if(!(c==7&&k==1))at=at+dir*(.25f+.2f*std::abs(unit(rng)));
      dir=normalize(dir+make_float3(unit(rng)*.45f,unit(rng)*.45f,unit(rng)*.2f));
    }
  }
  auto hair=scene.create_node<Hair>();array<Node*> shaders;shaders.push_back_slow(shader);hair->set_used_shaders(shaders);
  hair->curve_shape=shape;hair->resize_curves(int(g.first.size()),int(g.keys.size()));
  auto P=hair->attributes.find(ATTR_STD_POSITION)->data_for_write<packed_float3>();auto R=hair->attributes.find(ATTR_STD_RADIUS)->data_for_write<float>();
  for(size_t i=0;i<g.keys.size();i++){P[i]=g.keys[i];R[i]=g.radius[i];}
  for(size_t i=0;i<g.first.size();i++){hair->get_curve_first_key()[i]=g.first[i];hair->get_curve_shader()[i]=0;}
  hair->tag_curve_first_key_modified();hair->tag_curve_shader_modified();
  g.hair=hair;return hair;
}
inline CurveFixture curve_fixture(Scene &scene,Shader *shader,const std::vector<CurveShapeType> &shapes,Shader *floor_shader=nullptr){
  CurveFixture f;f.shader=shader;f.geometry.resize(shapes.size()*2);
  for(size_t s=0;s<shapes.size();s++){
    curve_fixture_hair(scene,f.geometry[2*s],shapes[s],shader,2026110+unsigned(s)*17);
    curve_fixture_hair(scene,f.geometry[2*s+1],shapes[s],shader,2026311+unsigned(s)*29);
    const float y=(float(s)-.5f*float(shapes.size()-1))*5.f;
    // Single user with a uniform scale: baked by the static BVH update.
    f.objects.push_back({unsigned(2*s),transform_translate(make_float3(-5.f,y,1.f))*transform_rotate(.4f+s,normalize(make_float3(.3f,.5f,.8f)))*transform_scale(make_float3(1.2f))});
    // Two users: instanced, one non-uniformly scaled, one mirrored.
    f.objects.push_back({unsigned(2*s+1),transform_translate(make_float3(0.f,y,1.f))*transform_rotate(.7f,normalize(make_float3(.9f,.2f,.4f)))*transform_scale(make_float3(1.3f,.8f,1.1f))});
    f.objects.push_back({unsigned(2*s+1),transform_translate(make_float3(5.f,y,1.f))*transform_rotate(-.3f,normalize(make_float3(.1f,.8f,.5f)))*transform_scale(make_float3(-1.1f,1.2f,.9f))});
  }
  for(const auto &o:f.objects){auto object=scene.create_node<Object>();object->set_geometry(f.geometry[o.geometry].hair);object->set_tfm(o.tfm);}
  auto floor=scene.create_node<Mesh>();array<Node*> shaders;shaders.push_back_slow(floor_shader?floor_shader:shader);floor->set_used_shaders(shaders);floor->resize_mesh(4,2);
  const float v[][3]={{-12,-12,-.5f},{12,-12,-.5f},{12,12,-.5f},{-12,12,-.5f}};for(int i=0;i<4;i++)floor->get_position_for_write()[i]=make_float3(v[i][0],v[i][1],v[i][2]);
  const int tris[]={0,1,2,0,2,3};std::copy_n(tris,6,floor->get_triangles().data());std::ranges::fill(floor->get_smooth(),false);std::ranges::fill(floor->get_shader(),0);floor->tag_triangles_modified();
  auto ground=scene.create_node<Object>();ground->set_geometry(floor);
  return f;
}
inline float3 curve_fixture_eval(const std::vector<float3> &c,float u){
  const float t=u,s=1-u;const float n0=-t*s*s,n1=2+t*t*(3*t-5),n2=2+s*s*(3*s-5),n3=-s*t*t;
  return .5f*(c[0]*n0+c[1]*n1+c[2]*n2+c[3]*n3);
}
inline float curve_fixture_bits(float x,int ulps){uint32_t b;std::memcpy(&b,&x,4);b=uint32_t(int64_t(b)+ulps);std::memcpy(&x,&b,4);return x;}
inline std::vector<CurveRay> curve_rays(const CurveFixture &f,unsigned count,unsigned seed,const CurveMotion &motion={}){
  std::mt19937 rng(seed);std::uniform_real_distribution<float> unit(-1.f,1.f);std::vector<CurveRay> rays;rays.reserve(count);
  for(unsigned i=0;i<count;i++){
    const auto &o=f.objects[rng()%f.objects.size()];const auto &g=f.geometry[o.geometry];
    const size_t c=rng()%g.first.size();const int first=g.first[c];const int keys=(c+1<g.first.size()?g.first[c+1]:int(g.keys.size()))-first;
    const int seg=keys>1?int(rng()%unsigned(keys-1)):0;const int k0=first+seg,k1=keys>1?k0+1:k0,ka=std::max(k0-1,first),kb=std::min(k1+1,first+keys-1);
    const float u=.5f+.5f*unit(rng);float3 local=curve_fixture_eval({g.keys[ka],g.keys[k0],g.keys[k1],g.keys[kb]},u);
    // The ray's time: the shutter's ends and centre, the steps' midpoints and at random. Aim at the swung position.
    const float times[]={0.f,1.f,.5f,.25f,.75f,.5f+.5f*unit(rng),.5f+.5f*unit(rng),.5f+.5f*unit(rng)};const float time=motion.steps||motion.object?times[rng()%8]:.5f;
    if(motion.steps&&((motion.geometry_mask>>o.geometry)&1u))local=local+curve_fixture_swing(o.geometry,time);
    const float radius=g.radius[k0]+(g.radius[k1]-g.radius[k0])*u;
    const float3 tangent=keys>1?g.keys[k1]-g.keys[k0]:make_float3(0,0,1);
    const float3 target=transform_point(&o.tfm,local);const float3 axis=transform_direction(&o.tfm,tangent);
    const unsigned kind=i%8;CurveRay ray;ray.tmin=0;ray.tmax=1e6f;ray.time=time;ray.kind=kind;
    float3 dir=normalize(make_float3(unit(rng),unit(rng),unit(rng)));
    if(len_squared(axis)>0&&(kind==3||kind==4)){
      // Down the tangent: near-parallel to the bounding cylinders.
      const float tilt=kind==3?1e-5f:1e-3f;dir=normalize(axis+make_float3(unit(rng),unit(rng),unit(rng))*tilt*len(axis));
    }
    // Offset across the curve: centre, inside, the silhouette and just outside.
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
    ray.P.x=curve_fixture_bits(ray.P.x,ulps);ray.P.y=curve_fixture_bits(ray.P.y,-ulps);ray.D.z=curve_fixture_bits(ray.D.z,ulps);
    rays.push_back(ray);
  }
  return rays;
}
}
