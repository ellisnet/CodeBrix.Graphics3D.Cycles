// Modified by CodeBrix: use atomic return/load for concurrent memory accounting.
// Copyright (c) 2026 Jeremy Ellis and contributors
/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "util/atomic.h"
#include <cassert>

CCL_NAMESPACE_BEGIN

class Stats {
 public:
  enum static_init_t { static_init = 0 };

  Stats() : mem_used(0), mem_peak(0) {}
  explicit Stats(static_init_t /*unused*/) {}

  void mem_alloc(const size_t size)
  {
    const size_t used = atomic_add_and_fetch_z(&mem_used, size);
    atomic_fetch_and_update_max_z(&mem_peak, used);
  }

  void mem_free(const size_t size)
  {
    assert(atomic_load_z(&mem_used) >= size);
    atomic_sub_and_fetch_z(&mem_used, size);
  }

  size_t mem_used;
  size_t mem_peak;
};

CCL_NAMESPACE_END
