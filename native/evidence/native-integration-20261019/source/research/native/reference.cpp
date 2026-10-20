// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Compile the upstream generic scalar branch, also used as the translation input.
#define CCL_NAMESPACE_BEGIN namespace ccl {
#define CCL_NAMESPACE_END }
#include "util/optimization.h"
#include "util/defines.h"
// Host branch hints have no WGSL equivalent. Preserve their boolean value while
// avoiding the platform-sized long used by __builtin_expect in the Clang AST.
#undef LIKELY
#undef UNLIKELY
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#undef __KERNEL_SSE__
#undef __KERNEL_SSE2__
#undef __KERNEL_SSE3__
#undef __KERNEL_SSE42__
#include "util/math.h"
#include "util/math_intersect.h"
#include "kernel/svm/math_util.h"

extern "C" void cycles_triangle(const float *v, float *out)
{
  using namespace ccl;
  auto vec = [&](int i) { return make_float3(v[i], v[i+1], v[i+2]); };
  out[0] = ray_triangle_intersect(vec(0), vec(4), v[3], v[7], vec(8), vec(12), vec(16),
                                  &out[1], &out[2], &out[3]) ? 1.0f : 0.0f;
}
extern "C" float cycles_math(int op, float a, float b, float c)
{
  return ccl::svm_math(static_cast<ccl::NodeMathType>(op), a, b, c);
}
