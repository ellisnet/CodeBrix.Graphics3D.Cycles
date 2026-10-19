// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
void original_prefix(int *counter,int *prefix,int count);
void original_sorted(const uint16_t *queued,const uint32_t *keys,int count,int limit,int kernel,
                     int *indices,int *counter,int *prefix);
void original_move(ccl::IntegratorStateGPU &state,int volume_stack_size,bool shadow,int to,int from);
