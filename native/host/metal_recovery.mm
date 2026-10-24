// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Apple Metal builds only: lets the C host recover Cycles' Metal device after a
// scene-caused error (ABI.md, "Device errors"). MetalDevice keeps a sticky
// has_error flag beside the recorded message; while it is set, pipeline lookup
// refuses every kernel, so both are cleared before the host re-uploads the scene.
#include "device/metal/device_impl.h"

namespace ccl {

bool codebrix_metal_clear_error(Device *device)
{
  auto *metal = dynamic_cast<MetalDevice *>(device);
  if (!metal) return false;
  metal->clear_error();
  metal->has_error = false;
  return true;
}

}  // namespace ccl
