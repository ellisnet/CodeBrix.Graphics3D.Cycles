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
  std::array<uint64_t,69> pipeline_dispatches{};
  static void file(const std::string &name,const void *data,size_t bytes){std::ofstream out(name,std::ios::binary);out.write(static_cast<const char*>(data),bytes);if(!out)throw std::runtime_error("Capture write failed: "+name);}
  void header(uint32_t kind,uint32_t address,uint32_t bytes){uint32_t h[]={kind,address,bytes};commands.write(reinterpret_cast<const char*>(h),sizeof(h));count++;}
  RenderCapture(ccl::WebGPUDevice &d,const std::string &prefix):device(d),commands(prefix+".commands.bin",std::ios::binary),arena_size(d.arena_bytes){
    auto staging=d.gpu.buffer(arena_size,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);wgpuCommandEncoderCopyBufferToBuffer(d.commands(),d.arena,0,staging,0,arena_size);d.flush();std::vector<uint8_t> bytes(arena_size);d.gpu.read(staging,bytes.data(),bytes.size());file(prefix+".arena.bin",bytes.data(),bytes.size());
    std::filesystem::path out=std::filesystem::path(prefix).parent_path();
    // Record the modules this device compiles for the uploaded scene (specialized
    // to the reachable SVM handlers unless specialization is disabled). With sun
    // guiding, portals, or a background light beside area lights the surface light entries come from the module variant with
    // the background light's sample entry (surface.cpp), and so does surface.wgsl.
    std::ofstream(out/"controls.wgsl")<<ccl::controls_wgsl();std::ofstream(out/"camera.wgsl")<<d.module_wgsl(ccl::camera_wgsl());std::ofstream(out/"closest.wgsl")<<d.module_wgsl(ccl::closest_wgsl());std::ofstream(out/"background.wgsl")<<d.module_wgsl(ccl::background_wgsl());std::ofstream(out/"surface.wgsl")<<((d.data.kernel_features&KERNEL_FEATURE_SUBSURFACE)?ccl::specialize_wgsl(ccl::surface_source(),d.shader_features()|ccl::SF_SUBSURFACE_ENTRY|((d.shader_features()&ccl::SF_SUBSURFACE_DISK).any()?ccl::SF_SUBSURFACE_DISK_ENTRY:ccl::SF_NONE)):d.surface_background_stage()?ccl::specialize_wgsl(ccl::surface_source(),d.shader_features()|ccl::SF_BACKGROUND_SAMPLE_ENTRY):d.module_wgsl(ccl::surface_source()));std::ofstream(out/"shadow.wgsl")<<d.module_wgsl(ccl::shadow_source());
    if(d.data.integrator.has_shadow_catcher)std::ofstream(out/"shadow_catcher.wgsl")<<d.module_wgsl(ccl::shadow_catcher_split_wgsl());
    if(d.film_cryptomatte())std::ofstream(out/"film_post.wgsl")<<ccl::film_post_wgsl();
    if(d.data.film.pass_adaptive_aux_buffer!=PASS_UNUSED)std::ofstream(out/"film_adaptive.wgsl")<<ccl::film_adaptive_wgsl();
    if(d.data.film.pass_volume_scatter_denoised!=PASS_UNUSED)std::ofstream(out/"film_volume_guiding.wgsl")<<ccl::film_volume_guiding_wgsl();
    std::ofstream(out/"surface_raytrace.wgsl")<<ccl::specialize_wgsl(ccl::surface_source(),d.shader_features()|ccl::SF_NODE_RAYTRACE);
    // The INTERSECT_MNEE module (entries 27-33): the caustic casters' SVM regions and the MNEE entry points (mnee.cpp).
    if((d.shader_features()&ccl::SF_MNEE).any())std::ofstream(out/"surface_mnee.wgsl")<<ccl::specialize_wgsl(ccl::surface_source(),(d.shader_features()&~d.mnee_unused_svm_features())|ccl::SF_MNEE_ENTRIES);
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
    // 27-33 (mnee_receiver, mnee_seed, mnee_eval, mnee_setup, mnee_solve, mnee_transfer, mnee_contribute, in the MNEE
    // module surface_mnee.wgsl; older captures carry them in surface.wgsl) and 34 (mnee_cleanup, surface.wgsl), 35 (shade_surface_sample, surface.wgsl), 36 (shade_surface_passes,
    // surface.wgsl), 37 (cryptomatte_postprocess, film_post.wgsl), 38 (connect_surface_ao, surface.wgsl) and 39-41
    // (adaptive_convergence_check, adaptive_filter_x, adaptive_filter_y, film_adaptive.wgsl) and 42
    // (shade_surface_light_background, surface.wgsl), 43-44 (volume_guiding_filter_x, volume_guiding_filter_y,
    // film_volume_guiding.wgsl), 45-46 (shade_volume_segment, shade_volume_light, surface.wgsl), 47 (intersect_subsurface, surface.wgsl), 48 (shade_volume_phase, volume.wgsl), 49 (shade_volume_heterogeneous, volume.wgsl), 50 (shade_volume_ray_marching, volume.wgsl), 51 (shade_volume_tree_select, surface.wgsl), 52 (shade_volume_light_triangle, surface.wgsl), 53 (intersect_dedicated_light, closest.wgsl), 54 (shade_dedicated_light_lamp, background.wgsl), 55-56 (dedicated_light_tree_pdf, shade_dedicated_light, surface.wgsl), 57 (mnee_select, surface_mnee.wgsl), 62 (shade_volume_light_background, surface.wgsl; 61 is not assigned here), 63 (intersect_subsurface_volume_stack, volume_stack.wgsl), 64 (intersect_subsurface_motion, surface.wgsl), 65-66 (mnee_light_sample, mnee_light_update, surface_mnee.wgsl), 67 (shade_volume_heterogeneous_eval, volume.wgsl), 68 (mnee_project, surface_mnee.wgsl). All follow the
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
    // Entries from 61 on are listed only by captures that dispatch them.
    size_t listed=v10?61:21;for(size_t i=61;i<pipeline_dispatches.size();i++)if(pipeline_dispatches[i])listed=i+1;
    for(size_t i=0;i<listed;i++){if(i)meta<<',';meta<<pipeline_dispatches[i];}meta<<']';
    if(device.state.shadow_path.queued_kernel){auto shadow=uint32_t(uintptr_t(device.state.shadow_path.queued_kernel));
      meta<<",\"shadowQueuedKernelOffset\":"<<shadow<<",\"shadowPathCapacity\":"<<device.remaining(shadow)/sizeof(uint16_t);}
    meta<<",\"immutableSceneSpans\":[";
    bool first=true;for(auto &[name,address]:device.globals)if(address){if(!first)meta<<',';first=false;meta<<"{\"name\":\""<<name<<"\",\"offset\":"<<address<<",\"bytes\":"<<device.remaining(address)<<'}';}
    if(!first)meta<<',';
    auto data=device.constants.at("data");meta<<"{\"name\":\"KernelData\",\"offset\":"<<data<<",\"bytes\":"<<device.remaining(data)<<"}]}\n";
    if(!meta)throw std::runtime_error("Capture metadata write failed");
  }
};
