// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "device.h"
#include "integrator/path_trace_work_gpu.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
namespace wavefront_test {
using namespace ccl;
inline void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
inline void healthy(WebGPUDevice &d){require(!d.have_error(),d.error_message());}
struct Scheduler:PathTraceWorkGPU {
  using PathTraceWorkGPU::PathTraceWorkGPU;
  using PathTraceWorkGPU::enqueue_reset;
  using PathTraceWorkGPU::enqueue_work_tiles;
  using PathTraceWorkGPU::enqueue_path_iteration;
  using PathTraceWorkGPU::update_queue_counter_and_cache;
  using PathTraceWorkGPU::get_most_queued_kernel;
  using PathTraceWorkGPU::compute_sorted_queued_paths;
  int capacity()const{return max_num_paths_;}
  void set_active(int n){max_active_main_path_index_=n;}
  device_vector<int>& sorted_paths(){return queued_paths_;}
};
struct Field {std::string name;uint32_t address;size_t stride,offset;bool floating;};
inline std::vector<Field> fields(WebGPUDevice &d){std::vector<Field> out;
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(d.state.parent.name)out.push_back({#parent "." #name,uint32_t(uintptr_t(d.state.parent.name)),sizeof(type),offsetof(IntegratorStateCPU,parent.name),std::is_same_v<type,float>||std::is_same_v<type,packed_float3>||std::is_same_v<type,PackedSpectrum>});
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
  return out;
}
inline void upload(WebGPUDevice &d,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU> &states){
  for(auto &f:layout){std::vector<uint8_t> bytes(states.size()*f.stride);for(size_t i=0;i<states.size();i++)std::memcpy(bytes.data()+i*f.stride,reinterpret_cast<const uint8_t*>(&states[i])+f.offset,f.stride);d.upload(f.address,bytes.data(),bytes.size());}
}
inline std::vector<IntegratorStateCPU> download(WebGPUDevice &d,const std::vector<Field> &layout,unsigned count){
  std::vector<IntegratorStateCPU> states(count);
  for(auto &f:layout){std::vector<uint8_t> bytes(size_t(count)*f.stride);d.download(f.address,bytes.data(),bytes.size());for(unsigned i=0;i<count;i++)std::memcpy(reinterpret_cast<uint8_t*>(&states[i])+f.offset,bytes.data()+i*f.stride,f.stride);}
  healthy(d);return states;
}
}
