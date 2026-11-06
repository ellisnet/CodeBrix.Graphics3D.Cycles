// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Calls the unchanged pinned subsurface kernel (integrator_intersect_subsurface).
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/shade_surface.h"
#include "kernel/integrator/intersect_subsurface.h"
#include "kernel/bake/bake.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "subsurface_reference.h"
void original_subsurface(ccl::DeviceScene &scene,const std::vector<int> &indices,std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices)integrator_intersect_subsurface(kg,&states[i]);
}
