// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original upstream scheduler helper bodies, executed with host addresses.
#include "control_reference.h"
#include "util/atomic.h"
namespace ccl {
// The GPU intrinsic accepts signed counters; the CPU atomic header exposes
// the uint32_t pointer overload. Preserve identical integer operations.
inline uint32_t atomic_fetch_and_add_uint32(int *pointer,uint32_t value){
  return ::atomic_fetch_and_add_uint32(reinterpret_cast<uint32_t*>(pointer),value);
}
}
#define __device__ inline
#include "kernel/device/gpu/parallel_prefix_sum.h"
#include "kernel/device/gpu/parallel_sorted_index.h"
#undef __device__
namespace oracle {
using namespace ccl;
using KernelGlobals=void*;
using ConstIntegratorState=int;
using ConstIntegratorShadowState=int;
static ccl::IntegratorStateGPU kernel_integrator_state;
static ccl::KernelData kernel_data;
#undef INTEGRATOR_STATE
#undef INTEGRATOR_STATE_WRITE
#define INTEGRATOR_STATE(state,parent,member) kernel_integrator_state.parent.member[state]
#define INTEGRATOR_STATE_WRITE INTEGRATOR_STATE
// MNEE cross-path ownership repair is not supported by this queue yet and is
// explicitly rejected by the production compaction entry point.
#undef __MNEE__
#include "state_copy_reference.inc"
}
void original_prefix(int *counter,int *prefix,int count){ccl::gpu_parallel_prefix_sum(0,counter,prefix,count);}
void original_sorted(const uint16_t *queued,const uint32_t *keys,int count,int limit,int kernel,
                     int *indices,int *counter,int *prefix){
  int unused=0;for(int i=0;i<count;i++)ccl::gpu_parallel_sorted_index_array(i,count,limit,indices,&unused,counter,prefix,
    [&](int index){return queued[index]==kernel?int(keys[index]):int(GPU_PARALLEL_SORTED_INDEX_INACTIVE_KEY);});
}
void original_move(ccl::IntegratorStateGPU &state,int volume_stack_size,bool shadow,int to,int from){
  oracle::kernel_integrator_state=state;oracle::kernel_data.volume_stack_size=volume_stack_size;
  if(shadow)oracle::integrator_shadow_state_move(nullptr,to,from);else oracle::integrator_state_move(nullptr,to,from);
}
