// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cycles_webgpu.h"
#include <cmath>
inline CBCCamera demo_camera(unsigned width,unsigned height) {
  CBCCamera c{};
  c.origin[0]=6; c.origin[1]=-9; c.origin[2]=6;
  float target[]={0,1,.7f}, length=0;
  for(int j=0;j<3;j++) { c.forward[j]=target[j]-c.origin[j]; length+=c.forward[j]*c.forward[j]; }
  for(float &v:c.forward) v/=std::sqrt(length);
  c.right[0]=c.forward[1]; c.right[1]=-c.forward[0];
  length=std::sqrt(c.right[0]*c.right[0]+c.right[1]*c.right[1]);
  for(float &v:c.right) v/=length;
  c.up[0]=c.right[1]*c.forward[2]; c.up[1]=-c.right[0]*c.forward[2];
  c.up[2]=c.right[0]*c.forward[1]-c.right[1]*c.forward[0];
  c.tan_half_fov=.4f; c.aspect=float(width)/height;
  c.light_position[0]=-3; c.light_position[1]=-4; c.light_position[2]=8; c.light_power=250;
  c.background[0]=.025f; c.background[1]=.04f; c.background[2]=.07f;
  return c;
}
