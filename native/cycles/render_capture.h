// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "device.h"
#include "wavefront.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
// This recorder saves the *actual* native scheduler's writes, dispatches and
// scheduling readbacks. Replay must validate every readback that determined
// subsequent native scheduling; it may not inject those CPU decisions as data.
struct RenderCapture final:ccl::WebGPUCommandObserver {
  ccl::WebGPUDevice &device;std::ofstream commands;size_t arena_size;uint64_t count=0;bool sized_dispatch=false;
  std::array<uint64_t,36> pipeline_dispatches{};
  static void file(const std::string &name,const void *data,size_t bytes){std::ofstream out(name,std::ios::binary);out.write(static_cast<const char*>(data),bytes);if(!out)throw std::runtime_error("Capture write failed: "+name);}
  void header(uint32_t kind,uint32_t address,uint32_t bytes){uint32_t h[]={kind,address,bytes};commands.write(reinterpret_cast<const char*>(h),sizeof(h));count++;}
  RenderCapture(ccl::WebGPUDevice &d,const std::string &prefix):device(d),commands(prefix+".commands.bin",std::ios::binary),arena_size(d.arena_bytes){
    auto staging=d.gpu.buffer(arena_size,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);wgpuCommandEncoderCopyBufferToBuffer(d.commands(),d.arena,0,staging,0,arena_size);d.flush();std::vector<uint8_t> bytes(arena_size);d.gpu.read(staging,bytes.data(),bytes.size());file(prefix+".arena.bin",bytes.data(),bytes.size());
    std::filesystem::path out=std::filesystem::path(prefix).parent_path();
    // Record the modules this device compiles for the uploaded scene (specialized
    // to the reachable SVM handlers unless specialization is disabled).
    std::ofstream(out/"controls.wgsl")<<ccl::controls_wgsl();std::ofstream(out/"camera.wgsl")<<d.module_wgsl(ccl::camera_wgsl());std::ofstream(out/"closest.wgsl")<<d.module_wgsl(ccl::closest_wgsl());std::ofstream(out/"background.wgsl")<<d.module_wgsl(ccl::background_wgsl());std::ofstream(out/"surface.wgsl")<<d.module_wgsl(ccl::surface_source());std::ofstream(out/"shadow.wgsl")<<d.module_wgsl(ccl::shadow_source());
    if(d.data.integrator.has_shadow_catcher)std::ofstream(out/"shadow_catcher.wgsl")<<d.module_wgsl(ccl::shadow_catcher_split_wgsl());
    std::ofstream(out/"surface_raytrace.wgsl")<<ccl::specialize_wgsl(ccl::surface_source(),d.shader_features()|ccl::SF_NODE_RAYTRACE);
    std::ofstream(out/"volume.wgsl")<<d.module_wgsl(ccl::volume_source());std::ofstream(out/"volume_stack.wgsl")<<d.module_wgsl(ccl::volume_stack_source());
    device.observer=this;
  }
  ~RenderCapture(){device.observer=nullptr;}
  void upload(uint32_t address,const void *data,size_t bytes)override{header(1,address,bytes);commands.write(static_cast<const char*>(data),bytes);}
  void clear(uint32_t address,size_t bytes)override{header(2,address,bytes);}
  void dispatch(const std::array<uint32_t,32>&params,uint32_t work,unsigned entry,uint32_t workgroup_size)override{
    if(entry>=pipeline_dispatches.size())throw std::runtime_error("Unknown capture pipeline");
    // Entry 15 (connect_surface_light) is recorder v6; entry 16
    // (prepare_shadow_shader_data) is recorder v7; entry 17
    // (shade_surface_light_triangle) is recorder v8; entries 18 (shade_light_nee),
    // 19 (shader_eval_background) and 20 (prepare_light_nee) are recorder v9; entries 21
    // (shade_surface_light_select) and 22 (light_tree_forward) are recorder v10, as are 23-25 (the shadow catcher
    // split module), 26 (prepare_surface_raytrace, the raytrace-specialized surface module surface_raytrace.wgsl),
    // 27-33 (mnee_receiver, mnee_seed, mnee_eval, mnee_setup, mnee_solve, mnee_transfer, mnee_contribute) and 34
    // (mnee_cleanup, all MNEE entries in surface.wgsl) and 35 (shade_surface_sample, surface.wgsl). All follow the
    // v5 slots 9-14.
    pipeline_dispatches[entry]++;
    // Kind 3 retains the original implicit size 64. Kind 5 stores the explicit
    // size before the same 128-byte parameter block, preserving old captures.
    header(workgroup_size==64?3:5,entry,work);
    if(workgroup_size!=64){sized_dispatch=true;commands.write(reinterpret_cast<const char*>(&workgroup_size),sizeof(workgroup_size));}
    commands.write(reinterpret_cast<const char*>(params.data()),sizeof(params));
  }
  void readback(uint32_t address,const void *data,size_t bytes)override{header(4,address,bytes);commands.write(static_cast<const char*>(data),bytes);}
  void finish(const std::string &prefix,uint32_t film,size_t film_bytes,uint32_t queued,unsigned capacity,
              unsigned width=64,unsigned height=48,unsigned samples=8,unsigned film_stride=4,
              const char *workload="native-render"){
    device.observer=nullptr;commands.close();if(!commands)throw std::runtime_error("Capture command write failed");
    if(device.arena_bytes!=arena_size)throw std::runtime_error("Capture arena grew; prewarm the complete workload before recording");
    if(!width||!height||!film_stride||uint64_t(width)*height*film_stride*4!=film_bytes)throw std::runtime_error("Capture film layout mismatch");
    std::ofstream meta(prefix+".capture.json");bool v10=pipeline_dispatches[21]||pipeline_dispatches[22]||pipeline_dispatches[23]||pipeline_dispatches[24]||pipeline_dispatches[25];for(size_t i=26;i<pipeline_dispatches.size();i++)v10|=pipeline_dispatches[i]!=0;
    meta<<"{\"format\":\"native-command-stream-v"<<(v10?10:(pipeline_dispatches[18]||pipeline_dispatches[19]||pipeline_dispatches[20])?9:pipeline_dispatches[17]?8:pipeline_dispatches[16]?7:pipeline_dispatches[15]?6:((pipeline_dispatches[9]||pipeline_dispatches[10]||pipeline_dispatches[11]||pipeline_dispatches[12]||pipeline_dispatches[13]||pipeline_dispatches[14])?5:(pipeline_dispatches[8]?4:((pipeline_dispatches[6]||pipeline_dispatches[7])?3:(sized_dispatch?2:1)))))<<"\",\"commands\":"<<count<<",\"arenaBytes\":"<<arena_size<<",\"filmOffset\":"<<film<<",\"filmBytes\":"<<film_bytes<<",\"errorOffset\":"<<device.error_word<<",\"queuedKernelOffset\":"<<queued<<",\"queuedKernelElementBytes\":2,\"pathCapacity\":"<<capacity<<",\"width\":"<<width<<",\"height\":"<<height<<",\"samples\":"<<samples<<",\"filmStride\":"<<film_stride<<",\"workload\":\""<<workload<<"\",\"pipelineDispatches\":[";
    // Entries 21-22 (light tree: shade_surface_light_select, light_tree_forward), 23-25 (the shadow catcher split
    // module), 26 (prepare_surface_raytrace) and 27-34 (MNEE) are recorder v10; older formats keep 21 counts.
    for(size_t i=0;i<(v10?pipeline_dispatches.size():21);i++){if(i)meta<<',';meta<<pipeline_dispatches[i];}meta<<']';
    if(device.state.shadow_path.queued_kernel){auto shadow=uint32_t(uintptr_t(device.state.shadow_path.queued_kernel));
      meta<<",\"shadowQueuedKernelOffset\":"<<shadow<<",\"shadowPathCapacity\":"<<device.remaining(shadow)/sizeof(uint16_t);}
    meta<<",\"immutableSceneSpans\":[";
    bool first=true;for(auto &[name,address]:device.globals)if(address){if(!first)meta<<',';first=false;meta<<"{\"name\":\""<<name<<"\",\"offset\":"<<address<<",\"bytes\":"<<device.remaining(address)<<'}';}
    if(!first)meta<<',';
    auto data=device.constants.at("data");meta<<"{\"name\":\"KernelData\",\"offset\":"<<data<<",\"bytes\":"<<device.remaining(data)<<"}]}\n";
    if(!meta)throw std::runtime_error("Capture metadata write failed");
  }
};
