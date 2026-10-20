// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "scene/scene.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
namespace ccl {
// Authored trajectories. Cycles owns decomposition, bounds and native uploads.
inline void object_motion_fixture(Scene &scene,Object &object,unsigned mode){
  scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);
  const Transform center=object.get_tfm();const unsigned count=mode%3==0?2:mode%3==1?3:5;
  array<Transform> frames;frames.resize(count);
  for(unsigned i=0;i<count;i++){
    const float u=2.f*float(i)/float(count-1)-1;
    Transform local=transform_rotate((mode==0?.0001f:.23f)*u,normalize(make_float3(.3f,1.f,.2f)))*
      transform_scale(make_float3(1+.08f*u,1-.05f*u,1+.03f*u));
    if(mode>=3)local.x.y+=.03f*u;
    frames[i]=transform_translate(make_float3(.34f*u,.11f*u,.07f*u))*center*local;
  }
  object.set_motion(frames);
}
}
