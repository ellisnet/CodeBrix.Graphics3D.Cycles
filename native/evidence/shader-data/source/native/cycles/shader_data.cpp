// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront.h"
#include "kernel/types.h"
#include <sstream>
namespace ccl {
static constexpr char source[]=
#include "shader_data_shader.inc"
;
std::string shader_data_source(){
  std::ostringstream code;code<<wavefront_layout();
  auto constant=[&](const char *name,size_t value){code<<"const "<<name<<":u32="<<value<<"u;\n";};
  constant("O_tfm",offsetof(KernelObject,tfm));constant("O_normal_offset",offsetof(KernelObject,normal_offset));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(SD_OBJECT_TRANSFORM_APPLIED);ENUM(SD_OBJECT_NEGATIVE_SCALE);ENUM(SD_OBJECT_HAS_CORNER_NORMALS);
  ENUM(SD_OBJECT_MOTION);ENUM(SD_OBJECT_HAS_VERTEX_MOTION);ENUM(SHADER_SMOOTH_NORMAL);ENUM(SR_BACKFACING);
#undef ENUM
  code<<source;return code.str();
}
}
