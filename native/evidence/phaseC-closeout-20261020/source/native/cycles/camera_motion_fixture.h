// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/scene.h"
namespace ccl {
// Original authored camera trajectories. Native Cycles performs decomposition,
// inverse shutter-CDF construction and scene-table upload for both oracles.
inline void camera_motion_fixture(Scene &scene,const Transform &center,unsigned mode,float angle_scale=1){
  auto &camera=*scene.camera;scene.integrator->set_motion_blur(true);
  camera.set_shuttertime(.6f);camera.set_use_perspective_motion(mode>=4);
  camera.set_fov_pre(camera.get_fov()*.82f);camera.set_fov_post(camera.get_fov()*1.17f);
  const unsigned count=mode%3==0?2:mode%3==1?3:5;array<Transform> transforms;transforms.resize(count);
  for(unsigned i=0;i<count;i++){
    const float t=float(i)/float(count-1),u=2*t-1;
    Transform local=transform_translate(make_float3(.35f*u,.12f*u*u-.06f,0));
    if(mode>=1)local=local*transform_rotate(angle_scale*(mode==1?.0002f:.18f)*u,make_float3(.2f,1,.1f));
    if(mode>=3){local=local*transform_scale(make_float3(1+.08f*u,1-.04f*u,1+.02f*u));local.x.y+=.025f*u;}
    transforms[i]=center*local;
  }
  camera.set_motion(transforms);
  array<float> curve;curve.resize(256);
  for(size_t i=0;i<curve.size();i++){float t=float(i)/float(curve.size()-1);curve[i]=mode>=2?.1f+4*t*t:1;}
  camera.set_shutter_curve(curve);
  camera.set_rolling_shutter_type(mode>=5?Camera::ROLLING_SHUTTER_TOP:Camera::ROLLING_SHUTTER_NONE);
  camera.set_rolling_shutter_duration(mode==5?0:mode==6?.35f:1.f);
}
}
