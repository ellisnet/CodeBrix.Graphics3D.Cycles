// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cycles_backend.h"
#include <vector>
namespace cbc {
struct SceneData {
  std::vector<uint32_t> geometry,camera,lights;
  uint32_t width,height,samples,bounces;
  explicit SceneData(const CBCBackendScene &source);
};
}
