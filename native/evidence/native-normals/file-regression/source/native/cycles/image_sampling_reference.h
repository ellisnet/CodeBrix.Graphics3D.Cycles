// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "scene/scene.h"
struct ImageProbe {int id;unsigned flags;float u,v;};
std::vector<ccl::float4> original_image_samples(ccl::DeviceScene&,const std::vector<ImageProbe>&);
