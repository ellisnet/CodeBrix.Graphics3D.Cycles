// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "volume_reference.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "scene/light.h"
#include "scene/pass.h"
#include "util/progress.h"
#include "util/task.h"
#include "util/types_rgbe.h"
#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace ccl;
#include "curve_gate_tuft.h"
using namespace wavefront_test;
#include "surface_fixture.h"
#include "object_motion_fixture.h"
struct Metrics {uint64_t floats=0,integers=0,strict=0,scatter=0,transmit=0,terminated=0,shadows=0;unsigned cases=0,rejected=0;float maximum_actual=0,maximum_expected=0;double maximum=0,ratio=0;std::string field;};
static void number(float a,float e,const std::string &field,Metrics &m){
  const double error=std::abs(double(a)-e);m.floats++;m.strict+=error>1e-7+1e-7*std::abs(double(e));
  if(error>m.maximum){m.maximum=error;m.maximum_actual=a;m.maximum_expected=e;m.field=field;}m.ratio=std::max(m.ratio,error/(5e-5+2e-4*std::abs(double(e))));
  if(!(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)))){std::cerr<<std::setprecision(10)<<field<<" actual="<<a<<" expected="<<e<<'\n';throw std::runtime_error("Volume practical comparison failed");}
}
// The shadow path fields (shadow_state_template.h) of IntegratorShadowStateCPU and their GPU arrays, with the
// shadow volume stack: volume direct light (VOLUME_LIGHT) writes one shadow path per input in its reserved slot.
static std::vector<Field> shadow_fields(WebGPUDevice &d){std::vector<Field> out;
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(d.state.parent.name)out.push_back({#parent "." #name,uint32_t(uintptr_t(d.state.parent.name)),sizeof(gpu_state_storage<type>::gpu_type),offsetof(IntegratorShadowStateCPU,parent.name),std::is_same_v<type,float>||std::is_same_v<type,packed_float3>||std::is_same_v<type,PackedSpectrum>||std::is_same_v<type,FloatCompressedOnGPU>||std::is_same_v<type,SpectrumCompressedOnGPU>});
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/shadow_state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
  for(unsigned slot=0;slot<d.data.volume_stack_size;slot++)
    for(auto [name,address,offset]:{std::tuple{"object",d.state.shadow_volume_stack[slot].object,offsetof(IntegratorShadowStateCPU,shadow_volume_stack[0].object)},std::tuple{"shader",d.state.shadow_volume_stack[slot].shader,offsetof(IntegratorShadowStateCPU,shadow_volume_stack[0].shader)}})
      if(address)out.push_back({"shadow_volume_stack["+std::to_string(slot)+"]."+name,uint32_t(uintptr_t(address)),sizeof(int),offset+slot*sizeof(IntegratorShadowStateCPU{}.shadow_volume_stack[0]),false});
  return out;
}
static void evaluate(WebGPUDevice &d,Scene &scene,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU>&input,
                     const std::vector<int>&ids,Metrics &m,unsigned test){
  const auto &data=scene.dscene.data;std::vector<float> wanted(512*data.film.pass_stride);
  for(unsigned pixel=0;pixel<512;pixel++){
    auto at=pixel*data.film.pass_stride;
    if(pixel%3){wanted[at+data.film.pass_volume_scatter_denoised]=rgb_to_rgbe(make_float3(.13,.27,.09)).f;
      wanted[at+data.film.pass_volume_transmit_denoised]=rgb_to_rgbe(make_float3(.9,.12,.25)).f;
      wanted[at+data.film.pass_volume_majorant]=.9;wanted[at+data.film.pass_volume_majorant_sample_count]=2;}
    // Diagnostic (CODEBRIX_CYCLES_VOLUME_GATE_MAJORANT=DEPTH, case 92): every pixel's majorant pass DEPTH over one sample and no
    // denoised guidance (the state of the real render's pixel 440 at sample 4: a large majorant scale).
    if(test==33)if(const char *depth=std::getenv("CODEBRIX_CYCLES_VOLUME_GATE_MAJORANT")){wanted[at+data.film.pass_volume_scatter_denoised]=0;wanted[at+data.film.pass_volume_transmit_denoised]=0;
      wanted[at+data.film.pass_volume_majorant]=std::stof(depth);wanted[at+data.film.pass_volume_majorant_sample_count]=1;}
  }
  auto expected=input;auto initial=wanted;original_volume(scene.dscene,ids,expected,wanted);
  // Denoising cases: the CPU kernel wrote albedo and scatter normals, so the comparison below covers both passes.
  if(data.kernel_features&KERNEL_FEATURE_DENOISING){double albedo=0,normal=0;
    for(unsigned pixel=0;pixel<512;pixel++){for(unsigned c=0;c<3;c++){albedo+=std::abs(wanted[pixel*data.film.pass_stride+data.film.pass_denoising_albedo+c]);
      normal+=std::abs(wanted[pixel*data.film.pass_stride+data.film.pass_denoising_normal+c]);}}
    require(albedo>0&&normal>0,"Volume denoising case without features");}
  for(auto i:ids){auto &s=expected[i];if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
      s.path.shader_sort_key=(scene.dscene.tri_shader[s.isect.prim]&SHADER_MASK)+data.max_shaders*(i/d.state.sort_partition_divisor);m.transmit++;}
    else if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)m.transmit++;
    else if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)m.scatter++;
    else if(!s.path.queued_kernel)m.terminated++;
  }
  upload(d,layout,input);device_vector<float> film(&d,"volume_film",MEM_READ_WRITE);film.alloc(wanted.size());std::copy(initial.begin(),initial.end(),film.data());film.copy_to_device();
  device_vector<int> selected(&d,"volume_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  // The volume kernel the paths queued (ray marching: DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME_RAY_MARCHING).
  const DeviceKernel kernel=DeviceKernel(input[ids[0]].path.queued_kernel);
  IntegratorQueueCounter count{};count.num_queued[kernel]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&count,sizeof(count));
  const bool direct=data.integrator.use_direct_light;std::vector<Field> shadow_layout;
  if(direct){
    // Every shadow slot starts terminated; the reserved slots start at 0 (next_shadow_path_index).
    shadow_layout=shadow_fields(d);int zero=0;d.upload(uintptr_t(d.state.next_shadow_path_index),&zero,4);
    for(auto &f:shadow_layout){std::vector<uint8_t> bytes(size_t(d.remaining(f.address)),0);d.upload(f.address,bytes.data(),bytes.size());}
  }
  for(auto kernel:{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE})if(d.state.sort_key_counter[kernel])d.zero(uintptr_t(d.state.sort_key_counter[kernel]),d.remaining(uintptr_t(d.state.sort_key_counter[kernel])));
  auto queue=d.gpu_queue_create();int n=ids.size();DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&film.device_pointer);args.add(&n);
  require(queue->enqueue(kernel,n+63,args),d.error_message());require(queue->synchronize(),d.error_message());
  const auto actual=download(d,layout,input.size());
  for(const auto &field:layout)for(size_t i=0;i<input.size();i++){
    auto a=reinterpret_cast<const char*>(&actual[i])+field.offset;auto e=reinterpret_cast<const char*>(&expected[i])+field.offset;
    const std::string label="case="+std::to_string(test)+" "+field.name+" path="+std::to_string(i);
    if(field.floating)for(size_t c=0;c<field.stride;c+=4){float x,y;std::memcpy(&x,a+c,4);std::memcpy(&y,e+c,4);number(x,y,label+" component="+std::to_string(c/4),m);}
    else{require(!std::memcmp(a,e,field.stride),label+" integer mismatch");m.integers++;}
  }
  if(direct){
    // integrate_volume_direct_light: input k's shadow path in reserved slot k, field by field against the pinned CPU kernel's.
    for(const auto &field:shadow_layout){
      std::vector<uint8_t> bytes(ids.size()*field.stride);d.download(field.address,bytes.data(),bytes.size());
      for(size_t k=0;k<ids.size();k++){
        const auto &cpu=expected[ids[k]].shadow;if(!cpu.shadow_path.queued_kernel&&field.name!="shadow_path.queued_kernel")continue;
        auto a=reinterpret_cast<const char*>(bytes.data()+k*field.stride);auto e=reinterpret_cast<const char*>(&cpu)+field.offset;
        const std::string label="case="+std::to_string(test)+" shadow "+field.name+" path="+std::to_string(ids[k]);
        if(field.floating)for(size_t c=0;c<field.stride;c+=4){float x,y;std::memcpy(&x,a+c,4);std::memcpy(&y,e+c,4);number(x,y,label+" component="+std::to_string(c/4),m);}
        else{require(!std::memcmp(a,e,field.stride),label+" integer mismatch");m.integers++;}
      }
    }
    for(int i:ids)m.shadows+=expected[i].shadow.shadow_path.queued_kernel!=0;
  }
  film.copy_from_device();for(size_t i=0;i<wanted.size();i++)number(film[i],wanted[i],"case="+std::to_string(test)+" film="+std::to_string(i),m);
  IntegratorQueueCounter expected_count{};for(int i:ids)if(expected[i].path.queued_kernel)expected_count.num_queued[expected[i].path.queued_kernel]++;
  for(int i:ids)if(direct&&expected[i].shadow.shadow_path.queued_kernel)expected_count.num_queued[expected[i].shadow.shadow_path.queued_kernel]++;
  d.download(uintptr_t(d.state.queue_counter),&count,sizeof(count));require(!std::memcmp(&count,&expected_count,sizeof(count)),"Volume queue counter mismatch");m.cases++;
  std::cerr<<"Passed volume case "<<test<<'\n';
}
// Case 95 (and the diagnostic CODEBRIX_CYCLES_VOLUME_GATE_REAL=1 on case 92): the native scheduler renders case 92's scene (32x24, 16 samples)
// and every SHADE_VOLUME stage is compared with the pinned CPU kernel on the GPU's input states: the path fields, the film
// and each shadow path the stage queued (matched to its path by pixel). The first difference beyond the practical criterion stops it.
static void real_paths(WebGPUDevice &d,Scene &scene,Scheduler &scheduler,const std::vector<Field> &layout,Metrics &m){
  const auto &data=scene.dscene.data;const auto shadow_layout=shadow_fields(d);const Field *shadow_kernel=nullptr,*shadow_pixel=nullptr;
  for(auto &f:shadow_layout){if(f.name=="shadow_path.queued_kernel")shadow_kernel=&f;if(f.name=="shadow_path.render_pixel_index")shadow_pixel=&f;}
  require(shadow_kernel&&shadow_pixel,"Real paths without shadow fields");
  auto column=[&](const Field &f){std::vector<uint8_t> b(size_t(d.remaining(f.address)));d.download(f.address,b.data(),b.size());return b;};
  auto nonzero=[](const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return true;return false;};
  BufferParams bp;bp.width=32;bp.height=24;bp.pass_stride=data.film.pass_stride;auto buffers=scheduler.get_render_buffers();buffers->reset(bp);buffers->zero();
  upload(d,layout,std::vector<IntegratorStateCPU>(scheduler.capacity()));unsigned stages=0,paths=0,shadows=0;
  for(unsigned sample=0;sample<16;sample++){
    scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Real reset failed");
    KernelWorkTile tile{};tile.w=32;tile.h=24;tile.stride=32;tile.num_samples=1;tile.start_sample=sample;
    scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Real camera failed: "+d.error_message());
    for(unsigned step=0;step<200;step++){
      const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<199,"Real render did not terminate ("+std::string(device_kernel_as_string(kernel))+")");
      if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME){
        // Pending shadow paths run first, so the stage starts with no live shadow slot (no compaction moves one).
        IntegratorQueueCounter c{};d.download(uintptr_t(d.state.queue_counter),&c,sizeof(c));
        const auto shadow=c.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW]?DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW:c.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW]?DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW:DEVICE_KERNEL_NUM;
        if(shadow!=DEVICE_KERNEL_NUM){scheduler.enqueue_path_iteration(shadow);require(scheduler.update_queue_counter_and_cache(),"Real shadow drain failed: "+d.error_message());continue;}}
      const bool volume=kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME;std::vector<IntegratorStateCPU> expected;std::vector<float> wanted;std::vector<int> ids;std::vector<uint8_t> before;
      if(volume){expected=download(d,layout,scheduler.capacity());buffers->buffer.copy_from_device();wanted.assign(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
        for(size_t i=0;i<expected.size();i++)if(expected[i].path.queued_kernel==kernel)ids.push_back(int(i));
        before=column(*shadow_kernel);require(!nonzero(before.data(),before.size()),"Real volume stage with a live shadow path");original_volume(scene.dscene,ids,expected,wanted);
        // Free shadow slots are cleared (their stale fields are not state), as the CPU's shadow states start cleared.
        for(auto &f:shadow_layout){auto bytes=column(f);const size_t n=std::min(bytes.size()/f.stride,before.size()/shadow_kernel->stride);
          for(size_t k=0;k<n;k++)if(!nonzero(&before[k*shadow_kernel->stride],shadow_kernel->stride))std::fill_n(bytes.begin()+k*f.stride,f.stride,uint8_t(0));
          d.upload(f.address,bytes.data(),bytes.size());}}
      // The scheduler's own choice (shadow path space, compaction), as in a render; with this film's few paths it runs the kernel above.
      require(scheduler.enqueue_path_iteration(),"Real scheduler stopped with pending paths");require(scheduler.update_queue_counter_and_cache(),"Real iteration failed: "+d.error_message());
      if(!volume)continue;
      const auto actual=download(d,layout,scheduler.capacity());buffers->buffer.copy_from_device();healthy(d);
      const std::string where="real sample="+std::to_string(sample)+" step="+std::to_string(step)+" ";
      for(const auto &field:layout){if(field.name=="path.shader_sort_key")continue;
        for(int i:ids){auto a=reinterpret_cast<const char*>(&actual[i])+field.offset;auto e=reinterpret_cast<const char*>(&expected[i])+field.offset;
          const std::string label=where+field.name+" path="+std::to_string(i)+" pixel="+std::to_string(expected[i].path.render_pixel_index);
          if(field.floating)for(size_t c=0;c<field.stride;c+=4){float x,y;std::memcpy(&x,a+c,4);std::memcpy(&y,e+c,4);number(x,y,label+" component="+std::to_string(c/4),m);}
          else{if(std::memcmp(a,e,field.stride)){const auto &x=actual[i],&y=expected[i];const unsigned px=y.path.render_pixel_index;const float *f=wanted.data()+size_t(px)*data.film.pass_stride;
              std::cerr<<std::setprecision(9)<<label<<": GPU bounce/kernel/volume_bounce "<<x.path.bounce<<'/'<<x.path.queued_kernel<<'/'<<x.path.volume_bounce<<" CPU "<<y.path.bounce<<'/'<<y.path.queued_kernel<<'/'<<y.path.volume_bounce
                <<" GPU throughput "<<x.path.throughput.x<<','<<x.path.throughput.y<<','<<x.path.throughput.z<<" CPU "<<y.path.throughput.x<<','<<y.path.throughput.y<<','<<y.path.throughput.z
                <<" GPU ray.P "<<x.ray.P.x<<','<<x.ray.P.y<<','<<x.ray.P.z<<" CPU "<<y.ray.P.x<<','<<y.ray.P.y<<','<<y.ray.P.z<<" input rng_pixel "<<y.path.rng_pixel<<" sample "<<y.path.sample<<" time "<<y.ray.time
                <<" film majorant/count "<<(data.film.pass_volume_majorant!=PASS_UNUSED?f[data.film.pass_volume_majorant]:-1.f)<<'/'<<(data.film.pass_volume_majorant_sample_count!=PASS_UNUSED?f[data.film.pass_volume_majorant_sample_count]:-1.f)
                <<" scatter/transmit denoised words "<<(data.film.pass_volume_scatter_denoised!=PASS_UNUSED?f[data.film.pass_volume_scatter_denoised]:-1.f)<<'/'<<(data.film.pass_volume_transmit_denoised!=PASS_UNUSED?f[data.film.pass_volume_transmit_denoised]:-1.f)
                <<" scatter/transmit "<<(data.film.pass_volume_scatter!=PASS_UNUSED?f[data.film.pass_volume_scatter]:-1.f)<<'/'<<(data.film.pass_volume_transmit!=PASS_UNUSED?f[data.film.pass_volume_transmit]:-1.f)<<'\n';}
            require(!std::memcmp(a,e,field.stride),label+" integer mismatch");m.integers++;}}}
      for(size_t i=0;i<wanted.size();i++)number(buffers->buffer.data()[i],wanted[i],where+"film="+std::to_string(i)+" pixel="+std::to_string(i/data.film.pass_stride)+" offset="+std::to_string(i%data.film.pass_stride),m);
      std::map<uint32_t,int> by_pixel;for(int i:ids)by_pixel[expected[i].path.render_pixel_index]=i;
      const auto after=column(*shadow_kernel);const auto pixel=column(*shadow_pixel);const size_t slots=after.size()/shadow_kernel->stride;unsigned matched=0;
      std::map<const Field*,std::vector<uint8_t>> columns;for(auto &f:shadow_layout)columns[&f]=column(f);
      for(size_t k=0;k<slots;k++){
        if(nonzero(&before[k*shadow_kernel->stride],shadow_kernel->stride)||!nonzero(&after[k*shadow_kernel->stride],shadow_kernel->stride))continue;
        uint32_t px;std::memcpy(&px,&pixel[k*4],4);auto it=by_pixel.find(px);if(it==by_pixel.end())continue;matched++;// (a pending path moved by the shadow compaction)
        const auto &cpu=expected[it->second].shadow;unsigned end=0;while(end<data.volume_stack_size&&cpu.shadow_volume_stack[end].shader!=SHADER_NONE)end++;
        for(auto &f:shadow_layout){
          // Stack entries past the terminator are not state (stale in a reused slot).
          if(f.name.starts_with("shadow_volume_stack[")&&std::stoul(f.name.substr(20))>end){continue;}auto a=reinterpret_cast<const char*>(columns[&f].data()+k*f.stride);auto e=reinterpret_cast<const char*>(&cpu)+f.offset;
          const std::string label=where+"shadow "+f.name+" path="+std::to_string(it->second)+" pixel="+std::to_string(px);
          if(f.floating)for(size_t c=0;c<f.stride;c+=4){float x,y;std::memcpy(&x,a+c,4);std::memcpy(&y,e+c,4);number(x,y,label+" component="+std::to_string(c/4),m);}
          else{require(!std::memcmp(a,e,f.stride),label+" integer mismatch");m.integers++;}}}
      unsigned wanted_shadows=0;for(int i:ids)wanted_shadows+=expected[i].shadow.shadow_path.queued_kernel!=0;
      require(matched==wanted_shadows,where+"shadow paths "+std::to_string(matched)+" (CPU "+std::to_string(wanted_shadows)+")");
      stages++;paths+=ids.size();shadows+=matched;
    }
  }
  std::cerr<<"Real paths: "<<stages<<" volume stages, "<<paths<<" paths, "<<shadows<<" shadow paths, within the practical criterion\n";m.cases++;
}
// The heterogeneous medium of cases 32-36: scatter density = 0.8 * Noise Texture (generated coordinates) in a box.
static void het_density(ShaderGraph &graph,ScatterVolumeNode *scatter){
  auto noise=graph.create_node<NoiseTextureNode>();noise->set_scale(1.6f);noise->set_detail(2);auto scale=graph.create_node<MathNode>();scale->set_math_type(NODE_MATH_MULTIPLY);scale->set_value2(.8f);
  // Diagnostic (CODEBRIX_CYCLES_VOLUME_GATE_DENSITY=constant): the density 0 * noise + 0.5, still a spatially varying graph.
  if(const char *v=std::getenv("CODEBRIX_CYCLES_VOLUME_GATE_DENSITY");v&&std::string(v)=="constant"){scale->set_math_type(NODE_MATH_MULTIPLY_ADD);scale->set_value2(0.f);scale->set_value3(.5f);}
  graph.connect(noise->output("Fac"),scale->input("Value1"));graph.connect(scale->output("Value"),scatter->input("Density"));
}
// Cases 84-86: the same density times 0.4 + 0.9 * Is Camera Ray (a Light Path node: SD_HAS_LIGHT_PATH_NODE), so camera rays
// read the baked octree extrema and the other rays estimate them (volume_estimate_extrema).
// Case 93: the noise density times 0.3 + 0.1 * Ray Depth + 0.05 * Transparent Depth + 0.04 * Diffuse Depth + 0.3 * the Geometry
// node's Backfacing + 0.2 * Ray Length (LIGHT_PATH_STATE: in a volume evaluation Backfacing and Ray Length read zero and the depths
// the path's counters).
static void het_density_light_path_state(ShaderGraph &graph,ScatterVolumeNode *scatter){
  auto noise=graph.create_node<NoiseTextureNode>();noise->set_scale(1.6f);noise->set_detail(2);auto path=graph.create_node<LightPathNode>();auto geometry=graph.create_node<GeometryNode>();
  ShaderOutput *sum=nullptr;const std::pair<const char*,float> terms[]={{"Ray Depth",.1f},{"Transparent Depth",.05f},{"Diffuse Depth",.04f},{"Backfacing",.3f},{"Ray Length",.2f}};
  for(auto [name,weight]:terms){auto term=graph.create_node<MathNode>();term->set_math_type(NODE_MATH_MULTIPLY_ADD);term->set_value2(weight);term->set_value3(sum?0.f:.3f);
    graph.connect(std::string(name)=="Backfacing"?geometry->output("Backfacing"):path->output(name),term->input("Value1"));
    if(sum){auto add=graph.create_node<MathNode>();add->set_math_type(NODE_MATH_ADD);graph.connect(sum,add->input("Value1"));graph.connect(term->output("Value"),add->input("Value2"));sum=add->output("Value");}
    else sum=term->output("Value");}
  auto density=graph.create_node<MathNode>();density->set_math_type(NODE_MATH_MULTIPLY);graph.connect(noise->output("Fac"),density->input("Value1"));graph.connect(sum,density->input("Value2"));
  graph.connect(density->output("Value"),scatter->input("Density"));
}
static void het_density_light_path(ShaderGraph &graph,ScatterVolumeNode *scatter){
  auto noise=graph.create_node<NoiseTextureNode>();noise->set_scale(1.6f);noise->set_detail(2);auto scale=graph.create_node<MathNode>();scale->set_math_type(NODE_MATH_MULTIPLY);scale->set_value2(.8f);
  auto path=graph.create_node<LightPathNode>();auto camera=graph.create_node<MathNode>();camera->set_math_type(NODE_MATH_MULTIPLY_ADD);camera->set_value2(.9f);camera->set_value3(.4f);
  auto density=graph.create_node<MathNode>();density->set_math_type(NODE_MATH_MULTIPLY);
  graph.connect(noise->output("Fac"),scale->input("Value1"));graph.connect(path->output("Is Camera Ray"),camera->input("Value1"));
  graph.connect(scale->output("Value"),density->input("Value1"));graph.connect(camera->output("Value"),density->input("Value2"));graph.connect(density->output("Value"),scatter->input("Density"));
}
static Object *het_box(Scene &scene,Shader *shader){
  auto box=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);box->set_used_shaders(slots);box->resize_mesh(8,12);
  for(unsigned v=0;v<8;v++)box->get_position_for_write()[v]=make_float3(v&1?4.f:-4.f,v&2?4.f:-4.f,v&4?3.f:-5.f);
  const int faces[12][3]={{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}};
  for(unsigned f=0;f<12;f++){for(unsigned k=0;k<3;k++)box->get_triangles()[3*f+k]=faces[f][k];box->get_shader()[f]=0;box->get_smooth()[f]=false;}
  box->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(box);return object;
}
// The majorant octree the WebGPU device builds (its density prepass) against the one the CPU device builds for the
// same scene: identical structure (parents, children, roots), node extrema within the practical criterion.
// world: the medium as the world volume (case 90). With cpu_nodes (the case 92 diagnostic below) only the CPU device's octree is
// built, for a box with transform tfm.
static void octree_check(WebGPUDevice &gpu,Stats &stats,Profiler &profiler,Metrics &m,bool world=false,const Transform *tfm=nullptr,std::vector<KernelOctreeNode> *cpu_nodes=nullptr,float3 scatter_color=make_float3(.75,.8,.9)){
  auto infos=Device::available_devices(DEVICE_MASK_CPU);require(!infos.empty(),"Missing CPU device");auto cpu=Device::create(infos[0],stats,profiler,true);
  std::vector<KernelOctreeNode> nodes[2];std::vector<KernelOctreeRoot> roots[2];
  for(unsigned k=0;k<(cpu_nodes?1u:2u);k++){Device *device=k?static_cast<Device*>(&gpu):cpu.get();
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,device);
    auto graph=make_unique<ShaderGraph>();auto absorption=graph->create_node<AbsorptionVolumeNode>();absorption->set_density(.05f);absorption->set_color(make_float3(.8,.65,.4));
    auto scatter=graph->create_node<ScatterVolumeNode>();scatter->set_color(scatter_color);scatter->set_anisotropy(.3f);het_density(*graph,scatter);
    auto add=graph->create_node<AddClosureNode>();graph->connect(absorption->output("Volume"),add->input("Closure1"));graph->connect(scatter->output("Volume"),add->input("Closure2"));
    graph->connect(add->output("Closure"),graph->output()->input("Volume"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);
    // world: the medium as the world volume, its octree rooted at the background light object (case 90).
    if(world){scene.background->set_shader(shader);auto light=scene.create_node<BackgroundLight>();light->set_use_mis(false);array<Node*> slots;slots.push_back_slow(shader);light->set_used_shaders(slots);
      auto object=scene.create_node<Object>();object->set_geometry(light);}
    else{auto box=het_box(scene,shader);if(tfm)box->set_tfm(*tfm);}
    if(tfm&&std::string(std::getenv("CODEBRIX_CYCLES_VOLUME_GATE_CPU_OCTREE"))=="motion"){prepare(scene);
      // Case 92's camera motion blur (three steps, a dolly and pan).
      scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);array<Transform> motion;
      motion.push_back_slow(transform_translate(make_float3(-.25f,.08f,-7.2f)));motion.push_back_slow(transform_translate(make_float3(0,0,-7)));motion.push_back_slow(transform_translate(make_float3(.25f,-.05f,-6.8f)));
      scene.camera->set_matrix(transform_translate(make_float3(0,0,-7)));scene.camera->set_motion(motion);}
    Progress progress;scene.device_update(device,progress);require(!progress.get_error(),"Octree scene preparation failed");
    nodes[k].assign(scene.dscene.volume_tree_nodes.data(),scene.dscene.volume_tree_nodes.data()+scene.dscene.volume_tree_nodes.size());
    roots[k].assign(scene.dscene.volume_tree_roots.data(),scene.dscene.volume_tree_roots.data()+scene.dscene.volume_tree_roots.size());
  }
  if(cpu_nodes){*cpu_nodes=nodes[0];return;}
  require(!nodes[0].empty()&&nodes[0].size()==nodes[1].size(),"Octree node count "+std::to_string(nodes[1].size())+" (CPU "+std::to_string(nodes[0].size())+")");
  require(roots[0].size()==roots[1].size()&&!std::memcmp(roots[0].data(),roots[1].data(),roots[0].size()*sizeof(KernelOctreeRoot)),"Octree roots differ from the CPU device's");
  unsigned identical=0;
  for(size_t i=0;i<nodes[0].size();i++){const auto &e=nodes[0][i],&a=nodes[1][i];require(a.parent==e.parent&&a.first_child==e.first_child,"Octree node "+std::to_string(i)+" structure");m.integers++;
    number(a.sigma.min,e.sigma.min,"octree node="+std::to_string(i)+" min",m);number(a.sigma.max,e.sigma.max,"octree node="+std::to_string(i)+" max",m);identical+=a.sigma.min==e.sigma.min&&a.sigma.max==e.sigma.max;}
  std::cerr<<"Octree: "<<nodes[0].size()<<" nodes, structure identical, "<<identical<<" with bit-identical extrema\n";
}
// The heterogeneous density prepass (kernel_volume_density_evaluate, 16 positions per voxel) against the pinned
// kernel: voxels of varied corners and sizes in the box object, with the heterogeneous shader.
static void density_check(WebGPUDevice &d,Scene &scene,Object *object,int shader,Metrics &m){
  constexpr unsigned total=1024;std::vector<KernelShaderEvalInput> input(total*2);std::vector<float> expected(total*2,.125f);
  for(unsigned i=0;i<total;i++){input[i*2].object=object->get_device_index();input[i*2].prim=std::bit_cast<int>(-4.f+.0071f*i);input[i*2].u=-3.9f+.0037f*(i%611);input[i*2].v=2.f-.0049f*i;
    const float size=.0625f*float(1u<<(i%5));input[i*2+1].object=i%9?shader&SHADER_MASK:SHADER_NONE;input[i*2+1].prim=std::bit_cast<int>(size);input[i*2+1].u=size;input[i*2+1].v=size;}
  original_volume_density(scene.dscene,input,expected);int offset=31,count=960;
  device_vector<KernelShaderEvalInput> in(&d,"density_input",MEM_READ_ONLY);in.alloc(input.size());std::copy(input.begin(),input.end(),in.data());in.copy_to_device();
  device_vector<float> out(&d,"density_output",MEM_READ_WRITE);out.alloc(expected.size());std::fill_n(out.data(),out.size(),.125f);out.copy_to_device();
  device_vector<uint> cache(&d,"density_cache_miss",MEM_READ_WRITE);cache.alloc(1);cache[0]=0;cache.copy_to_device();
  auto queue=d.gpu_queue_create();DeviceKernelArguments args(&in.device_pointer,&out.device_pointer,&cache.device_pointer,&offset,&count);
  require(queue->enqueue(DEVICE_KERNEL_SHADER_EVAL_VOLUME_DENSITY,count+63,args),d.error_message());require(queue->synchronize(),d.error_message());out.copy_from_device();
  unsigned identical=0;
  for(unsigned i=0;i<expected.size();i++){const float wanted=i>=unsigned(offset*2)&&i<unsigned((offset+count)*2)?expected[i]:.125f;identical+=out[i]==wanted;number(out[i],wanted,"heterogeneous density="+std::to_string(i),m);}
  std::cerr<<"Heterogeneous density: "<<identical<<"/"<<expected.size()<<" bit-identical\n";
}
// Host capability rejections must leave all uploaded path fields, film and queue
// counters intact. Direct calls retain the device for the next independent case.
static unsigned rejections(WebGPUDevice &d,const std::vector<Field>&layout,const std::vector<IntegratorStateCPU>&states){
  upload(d,layout,states);device_vector<float> film(&d,"volume_rejection_film",MEM_READ_WRITE);film.alloc(d.data.film.pass_stride);std::fill_n(film.data(),film.size(),.125f);film.copy_to_device();
  IntegratorQueueCounter before{};before.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME]=1;d.upload(uintptr_t(d.state.queue_counter),&before,sizeof(before));
  const auto original_data=d.data;const auto original_state=d.state;const auto original_shaders=d.shader_records;
  const auto original_lights=d.light_records;const auto original_distribution=d.light_distribution_records;const auto original_object_flags=d.object_flag_records;
  device_ptr indices=0;int n=1;DeviceKernelArguments args;args.add(&indices);args.add(&film.device_pointer);args.add(&n);
  for(unsigned test=0;test<18;test++){
    std::string reason;int work=1;
    switch(test){
      case 0:d.data.kernel_features&=~KERNEL_FEATURE_VOLUME;reason="without native volume feature";break;
      // Motion blur: velocity-advected volumes (SD_OBJECT_HAS_VOLUME_MOTION, volume_shader_motion_blur) are refused, with and without curves.
      case 1:d.data.bvh.have_curves=1;d.data.bvh.have_motion=1;require(!d.object_flag_records.empty(),"Volume rejection without objects");d.object_flag_records[0]|=SD_OBJECT_HAS_VOLUME_MOTION;reason="volume velocity motion blur";break;
      case 2:d.data.bvh.have_motion=1;require(!d.object_flag_records.empty(),"Volume rejection without objects");d.object_flag_records[0]|=SD_OBJECT_HAS_VOLUME|SD_OBJECT_HAS_VOLUME_MOTION;reason="volume velocity motion blur";break;
      case 3:d.data.kernel_features|=KERNEL_FEATURE_LIGHT_LINKING;d.state.path.mis_ray_object=nullptr;reason="Missing light linking path state";break;
      case 4:d.data.film.pass_volume_majorant=PASS_UNUSED;reason="volume film pass";break;
      case 5:d.data.integrator.sampling_pattern=SAMPLING_PATTERN_BLUE_NOISE_ROUND;reason="Invalid volume sampling pattern";break;
      case 6:d.data.kernel_features|=KERNEL_FEATURE_DENOISING;d.data.film.pass_denoising_albedo=1<<20;reason="volume film pass";break;
      case 7:d.data.kernel_features|=KERNEL_FEATURE_PATH_GUIDING;reason="optional volume integration";break;
      case 8:d.data.kernel_features|=KERNEL_FEATURE_DENOISING;d.data.film.pass_denoising_normal=1<<20;reason="volume film pass";break;
      case 9:d.data.film.pass_volume_indirect=1<<20;reason="volume film pass";break;
      case 10:d.data.volume_stack_size=1;reason="volume stack size";break;
      case 11:d.data.max_closures=0;reason="volume closure capacity";break;
      case 12:d.data.film.pass_volume_transmit=PASS_UNUSED;reason="volume film pass";break;
      case 13:d.data.film.pass_volume_direct=1<<20;reason="volume film pass";break;
      case 14:d.data.max_closures=MAX_CLOSURE+1;reason="volume closure capacity";break;
      case 15:d.state.ray.D=nullptr;reason="virtual pointer";break;
      case 16:d.state.volume_stack[0].shader=nullptr;reason="virtual pointer";break;
      case 17:work=0;reason="Incomplete volume dispatch";break;
    }
    std::string error;try{d.enqueue_volume(work,args);}catch(const std::exception &e){error=e.what();}
    d.data=original_data;d.state=original_state;d.shader_records=original_shaders;d.light_records=original_lights;d.light_distribution_records=original_distribution;d.object_flag_records=original_object_flags;
    require(error.find(reason)!=std::string::npos,"Volume rejection "+std::to_string(test)+": "+error);
    for(auto &field:layout){std::vector<uint8_t> bytes(states.size()*field.stride);d.download(field.address,bytes.data(),bytes.size());
      for(size_t i=0;i<states.size();i++)require(!std::memcmp(bytes.data()+i*field.stride,reinterpret_cast<const char*>(&states[i])+field.offset,field.stride),"Rejected volume changed state");}
    std::vector<float> after(film.size());d.download(film.device_pointer,after.data(),after.size()*4);require(std::all_of(after.begin(),after.end(),[](float x){return x==.125f;}),"Rejected volume changed film");
    IntegratorQueueCounter counter{};d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));require(!std::memcmp(&counter,&before,sizeof(counter)),"Rejected volume changed queue counters");
  }
  return 18;
}
int main(int argc,char **argv){
TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: volume_gate OUTPUT_PREFIX");const std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"volume_gate device creation");Metrics m;
  const float gs[]={0,.0009f,.001f,-.001f,.45f,-.7f,.999f,-.999f,1.5f,-1.5f};
  // The direct light cases' anisotropy: moderate lobes; cases 50-53 gate the near-delta lobes.
  const float direct_gs[]={.45f,-.7f,.3f,-.3f,.6f,-.45f,0,.15f};
  // Maintainer measurement (native/DEVICE.md 52), not a verdict: CODEBRIX_CYCLES_VOLUME_GATE_REPEAT=CASE:COUNT prepares only that
  // case and evaluates it COUNT times on one device and scene, one line per evaluation and a summary line on stdout.
  int repeat_case=-1;unsigned repeat_count=0,repeat_failed=0;
  if(const char *r=std::getenv("CODEBRIX_CYCLES_VOLUME_GATE_REPEAT");r&&*r){char *end=nullptr;const unsigned long c=std::strtoul(r,&end,10);
    require(end&&*end==':'&&c<96,"CODEBRIX_CYCLES_VOLUME_GATE_REPEAT must be CASE:COUNT");const unsigned long n=std::strtoul(end+1,&end,10);
    require(end&&!*end&&n>0&&n<=10000,"CODEBRIX_CYCLES_VOLUME_GATE_REPEAT must be CASE:COUNT");repeat_case=int(c);repeat_count=unsigned(n);}
  // Cases 24-31: volume direct light (VOLUME_LIGHT) with a sphere point light (MIS, distance, equiangular), a
  // disk point light, a sun light, scene rays ending on a triangle and missing, against the pinned kernel.
  // Cases 32-36: a heterogeneous medium (noise density) in a box object, integrated by null scattering through the
  // majorant octree (VOLUME_HETEROGENEOUS): without direct light, then with the light of cases 24 (MIS), 25
  // (distance), 26 (equiangular) and 28 (sun); case 37: camera rays from outside the box (the segment starts at the
  // box entry, a large ray tmin) without direct light.
  // Cases 75-78: earlier cases with the sampling patterns other than tabulated Sobol (every random number of the volume
  // integrators): the heterogeneous box with equiangular sampling (55) in Sobol-Burley, with MIS (33) in blue noise
  // (first sample), biased ray marching with MIS (39) in blue noise (pure), the homogeneous world medium with MIS (24) in
  // Sobol-Burley. Cases 79-81: denoising features (film_write_denoising_features_volume) with the albedo and normal passes,
  // for the homogeneous world medium with MIS (24), the heterogeneous box with MIS (33) and biased ray marching (39).
  // Cases 82-83: light linking (the receivers of the volume direct light, light_link_object_match): the heterogeneous box
  // receives light set 1, which the spot and the sun do not light, with the light tree's four lamps (71) and with the flat
  // light distribution and MIS (33 with the same lamps, no tree). Cases 84-86: a Light Path node in the heterogeneous box's
  // density (het_density_light_path) with MIS (33) and equiangular sampling (35), and (86) also in a homogeneous medium box
  // that overlaps it in every path's stack (the octree set-up estimates both entries for the rays that are not camera rays).
  // Case 87: biased ray marching with MIS (39) through the Light Path box (every step evaluates the whole stack, no estimate).
  // Cases 88-89: object motion blur of the heterogeneous box (object_motion_fixture modes 1 and 2: three and five steps), its
  // noise density in object space at each path's time (the stack entry's motion transforms): MIS (33) and biased ray marching
  // with MIS (39).
  // Cases 90-91: the heterogeneous medium of case 33 as the WORLD volume (MIS, the point light), with the background light
  // object that lists the world shader (the octree root of the world entry, scene/volume.cpp initialize_octree; the host adds
  // it when the caller has none), alone (90) and with the heterogeneous box inside it (91).
  // Case 95: case 92's scene rendered by the native scheduler (real_paths, 32x24, 16 samples), every volume stage against the
  // pinned kernel on the GPU's input states (native/DEVICE.md 180: a pixel whose guidance scales the majorant about 1858 times).
  for(unsigned index=0;index<96;index++){
    if(repeat_case>=0&&int(index)!=repeat_case)continue;
    const bool het_world=index==90||index==91;const bool moving_camera=index==92||index==95;
    const unsigned test=index>=90?33:index>=88?(index==88?33:39):index<75?index:index==75?55:index==76||index==80||index==83||index==84||index==86?33:index==85?35:index==77||index==81||index==87?39:index==82?71:24;
    const bool light_path=index>=84&&index<88;
    const bool denoise=index>=79&&index<82;const bool linking=index>=82&&index<84;
    const int pattern=index==75||index==78?SAMPLING_PATTERN_SOBOL_BURLEY:index==76?SAMPLING_PATTERN_BLUE_NOISE_FIRST:SAMPLING_PATTERN_BLUE_NOISE_PURE;
    // Cases 38-41: biased ray marching (shade_volume_ray_marching): the heterogeneous box without direct light, with
    // MIS and with equiangular sampling, and the homogeneous world medium with MIS (one step per segment).
    const bool rm=(test>=38&&test<42)||test==56;const bool het=(test>=32&&test!=41&&test<42)||test==54||test==55||test==56||test==71;
    // Cases 42-47: volume direct light from a spot light (sphere, then disk) and area lights (a rectangle and an ellipse
    // without spread, an ellipse with a 1.1 rad spread, a rectangle with the full spread), homogeneous world medium,
    // MIS (the light's valid ray segment: cone, slab, cylinder, spread cone, plane; the segment sample).
    // Cases 48-49: the fixture's emissive triangles in the light distribution beside the point light (front only, then
    // both sides), homogeneous world medium, MIS.
    const unsigned shape=test>=42&&test<48?test-41:test>=57&&test<63?test-56:0;const bool mesh_lights=(test>=48&&test<50)||test==63||test==70;
    // Cases 50-65: the near-delta lobes, which magnify any last-bit difference of the light direction, so the scatter
    // distance and the light sample must agree to the bit in the aligned paths: g = -0.999 with the sphere point light
    // (50 MIS, 51 distance, 52 equiangular), g = +0.999 (53 MIS), heterogeneous box (54 MIS, 55 equiangular, 56 ray
    // marching), the shaped lights of cases 42-47 (57-62), emissive triangles (63), sun (64), disk point light (65).
    const bool near_delta=test>=50&&test<66;
    // Cases 66-73: the light tree's volume segments (light_tree_sample<true>): one sphere point light (66), a point, spot,
    // area and sun light (67 MIS, 68 distance, 69 equiangular), emissive triangles beside them (70), the heterogeneous
    // box (71), g = -0.999 (72) and rays that miss with the world volume (73).
    const bool tree=test>=66&&test<74;const bool tree_lamps=(test>=67&&test<74)||index==83;
    const unsigned dcase=test==74?24:tree?(test==68?25:test==69?26:24):near_delta?(test==51?25:test==52||test==55?26:test==64?28:test==65?27:24):shape||mesh_lights?24:!het&&!rm?test:test==33||test==39||test==41?24:test==34?25:test==35||test==40?26:test==36?28:0;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;gate_curve_params(params);Scene scene(params,&d);prepare(scene);gate_curve_tufts(scene);
    auto graph=make_unique<ShaderGraph>();auto absorption=graph->create_node<AbsorptionVolumeNode>();absorption->set_density(test==20?0:.04f);absorption->set_color(test==21?make_float3(1,1,1):make_float3(.8,.65,.4));
    auto scatter=graph->create_node<ScatterVolumeNode>();scatter->set_density(test==20?0:test==22?.00001f:test==74?.6f:.035f);scatter->set_color(test==21?make_float3(.8,0,.6):make_float3(.8,.7,.6));scatter->set_anisotropy(test==72?-.999f:near_delta?(test==53?.999f:-.999f):dcase>=24?direct_gs[dcase-24]:gs[test%10]);
    auto add=graph->create_node<AddClosureNode>();graph->connect(absorption->output("Volume"),add->input("Closure1"));graph->connect(scatter->output("Volume"),add->input("Closure2"));ShaderOutput *volume=add->output("Closure");
    if(test>=10&&test<20){auto phase=graph->create_node<ScatterVolumeNode>();phase->set_density(.017f);phase->set_color(make_float3(.2,.9,.7));phase->set_anisotropy(gs[(test+4)%10]);auto mixture=graph->create_node<AddClosureNode>();graph->connect(volume,mixture->input("Closure1"));graph->connect(phase->output("Volume"),mixture->input("Closure2"));volume=mixture->output("Closure");}
    if(het){if(light_path)het_density_light_path(*graph,scatter);else if(index==93)het_density_light_path_state(*graph,scatter);else het_density(*graph,scatter);absorption->set_density(.05f);}
    if(index==94){
      // Case 94: a Principled Volume whose density and colour attributes name the box's mesh-element attributes "mydens" (0.6) and
      // "mytint" (0.9, 0.7, 0.5) (PRINCIPLED_VOLUME_ATTRIBUTE), with the noise as its density input.
      graph=make_unique<ShaderGraph>();auto principled=graph->create_node<PrincipledVolumeNode>();principled->set_color(make_float3(.75,.8,.9));principled->set_anisotropy(.3f);
      principled->set_density_attribute(ustring("mydens"));principled->set_color_attribute(ustring("mytint"));
      auto noise=graph->create_node<NoiseTextureNode>();noise->set_scale(1.6f);noise->set_detail(2);graph->connect(noise->output("Fac"),principled->input("Density"));
      volume=principled->output("Volume");
    }
    graph->connect(volume,graph->output()->input("Volume"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);
    Object *box_object=nullptr;Shader *box_medium=nullptr;
    if(!het||het_world)scene.background->set_shader(shader);
    else box_object=het_box(scene,shader);
    if(het_world){
      auto light=scene.create_node<BackgroundLight>();light->set_use_mis(false);array<Node*> world_slots;world_slots.push_back_slow(shader);light->set_used_shaders(world_slots);
      auto light_object=scene.create_node<Object>();light_object->set_geometry(light);
      if(index==91){
        auto box_graph=make_unique<ShaderGraph>();auto box_absorption=box_graph->create_node<AbsorptionVolumeNode>();box_absorption->set_density(.03f);box_absorption->set_color(make_float3(.6,.75,.9));
        auto box_scatter=box_graph->create_node<ScatterVolumeNode>();box_scatter->set_color(make_float3(.9,.8,.7));box_scatter->set_anisotropy(-.2f);het_density(*box_graph,box_scatter);
        auto box_add=box_graph->create_node<AddClosureNode>();box_graph->connect(box_absorption->output("Volume"),box_add->input("Closure1"));box_graph->connect(box_scatter->output("Volume"),box_add->input("Closure2"));
        box_graph->connect(box_add->output("Closure"),box_graph->output()->input("Volume"));box_medium=scene.create_node<Shader>();box_medium->set_graph(std::move(box_graph));box_medium->tag_update(&scene);
        box_object=het_box(scene,box_medium);
      }
    }
    if(index==94){auto mesh=static_cast<Mesh*>(box_object->get_geometry());
      mesh->attributes.add(ustring("mydens"),TypeFloat,ATTR_ELEMENT_MESH)->data_for_write<float>()[0]=.6f;
      mesh->attributes.add(ustring("mytint"),TypeRGBA,ATTR_ELEMENT_MESH)->data_for_write<float4>()[0]=make_float4(.9f,.7f,.5f,1.f);}
    // Case 92: the box of the coverage scene (0.9..3.5, -1.95..0.3, -0.8..1.6).
    if(moving_camera)box_object->set_tfm(transform_translate(make_float3(2.2f,-.825f,.4f))*transform_scale(make_float3(.325f,.28125f,.3f))*transform_translate(make_float3(0,0,1)));
    Shader *world_medium=nullptr;Object *second_object=nullptr;
    if(index==86){
      // A homogeneous medium in a second box over the first, its density 0.02 + 0.03 * Is Camera Ray. (A world medium with a
      // heterogeneous object crashes the pinned CPU kernel, so the overlap is an object's.)
      auto world_graph=make_unique<ShaderGraph>();auto world_scatter=world_graph->create_node<ScatterVolumeNode>();world_scatter->set_color(make_float3(.7,.8,.75));world_scatter->set_anisotropy(.2f);
      auto path=world_graph->create_node<LightPathNode>();auto density=world_graph->create_node<MathNode>();density->set_math_type(NODE_MATH_MULTIPLY_ADD);density->set_value2(.03f);density->set_value3(.02f);
      world_graph->connect(path->output("Is Camera Ray"),density->input("Value1"));world_graph->connect(density->output("Value"),world_scatter->input("Density"));
      world_graph->connect(world_scatter->output("Volume"),world_graph->output()->input("Volume"));
      world_medium=scene.create_node<Shader>();world_medium->set_graph(std::move(world_graph));world_medium->tag_update(&scene);second_object=het_box(scene,world_medium);
    }
    if(index>=88&&index<90)object_motion_fixture(scene,*box_object,index==88?1:2);
    if(linking)box_object->set_receiver_light_set(1);
    for(auto s:scene.shaders)s->set_emission_sampling_method(!mesh_lights?EMISSION_SAMPLING_NONE:test==48||test==63?EMISSION_SAMPLING_FRONT:EMISSION_SAMPLING_FRONT_BACK);
    const bool direct=dcase>=24;
    if(direct){
      shader->set_volume_sampling_method(dcase==25?VOLUME_SAMPLING_DISTANCE:dcase==26?VOLUME_SAMPLING_EQUIANGULAR:VOLUME_SAMPLING_MULTIPLE_IMPORTANCE);
      auto emission_graph=make_unique<ShaderGraph>();auto emission=emission_graph->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(1);
      emission_graph->connect(emission->output("Emission"),emission_graph->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(emission_graph));emitter->tag_update(&scene);
      Light *light;
      if(dcase==28||dcase==31){auto sun=scene.create_node<SunLight>();sun->set_angle(.12f);sun->set_normalize(false);light=sun;}
      else if(shape==1||shape==2){auto spot=scene.create_node<SpotLight>();spot->set_radius(shape==1?.45f:.3f);spot->set_is_sphere(shape==1);spot->set_angle(.9f);spot->set_smooth(.3f);light=spot;}
      else if(shape>=3){auto area=scene.create_node<AreaLight>();area->set_sizeu(shape%2?1.2f:.9f);area->set_sizev(shape%2?.7f:1.1f);area->set_ellipse(shape==4||shape==5);
        area->set_spread(shape<=4?0.f:shape==5?1.1f:float(M_PI));light=area;}
      else{auto point=scene.create_node<PointLight>();point->set_radius(test==74?.9f:dcase==27?.3f:.45f);point->set_is_sphere(dcase!=27);light=point;}
      light->set_strength(light->get_light_type()==LIGHT_SUN?make_float3(.6,.7,.8):make_float3(140,100,70));light->set_use_mis(true);
      array<Node*> slots;slots.push_back_slow(emitter);light->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(light);
      if(linking)object->set_light_set_membership((1ull<<0)|(1ull<<1));
      object->set_tfm((test==74?transform_translate(make_float3(0.f,0.f,0.f)):transform_translate(make_float3(.6f,1.4f,-1.1f)))*transform_rotate(2.6f,make_float3(0,1,0))*transform_rotate(-.5f,make_float3(1,0,0)));
      emitter->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
      if(tree){scene.integrator->set_use_light_tree(true);scene.integrator->set_light_sampling_threshold(0.f);}
      if(moving_camera){
        // Case 92: the heterogeneous box (MIS) with a second point light and camera motion blur (three steps, a dolly and pan),
        // the lamps where the coverage scene has them.
        object->set_tfm(transform_translate(make_float3(2.2f,2.f,-2.f)));light->set_strength(make_float3(110,90,70));
        auto fill=scene.create_node<PointLight>();fill->set_radius(.35f);fill->set_is_sphere(true);fill->set_strength(make_float3(50,65,90));fill->set_use_mis(true);
        array<Node*> fill_slots;fill_slots.push_back_slow(emitter);fill->set_used_shaders(fill_slots);auto o=scene.create_node<Object>();o->set_geometry(fill);o->set_tfm(transform_translate(make_float3(-2.4f,1.8f,-1.6f)));
        scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);array<Transform> motion;
        motion.push_back_slow(transform_translate(make_float3(-.25f,.08f,-7.2f)));motion.push_back_slow(transform_translate(make_float3(0,0,-7)));motion.push_back_slow(transform_translate(make_float3(.25f,-.05f,-6.8f)));
        scene.camera->set_matrix(transform_translate(make_float3(0,0,-7)));scene.camera->set_motion(motion);
        if(index==95||std::getenv("CODEBRIX_CYCLES_VOLUME_GATE_REAL")){scene.camera->set_fov(.9f);scene.camera->set_full_width(32);scene.camera->set_full_height(24);scene.camera->set_screen_size(32,24);scene.camera->compute_auto_viewplane();}
      }
      if(tree_lamps){
        auto spot=scene.create_node<SpotLight>();spot->set_radius(.3f);spot->set_is_sphere(true);spot->set_angle(.9f);spot->set_smooth(.3f);
        auto area=scene.create_node<AreaLight>();area->set_sizeu(1.2f);area->set_sizev(.7f);area->set_spread(1.1f);
        auto sun=scene.create_node<SunLight>();sun->set_angle(.12f);sun->set_normalize(false);
        Light *extra[3]={spot,area,sun};const float3 at[3]={make_float3(-2.1f,.8f,1.3f),make_float3(1.9f,-1.2f,-3.f),make_float3(0,0,0)};
        for(unsigned k=0;k<3;k++){extra[k]->set_strength(k==2?make_float3(.4,.5,.6):make_float3(90,80,120));extra[k]->set_use_mis(true);extra[k]->set_used_shaders(slots);auto o=scene.create_node<Object>();o->set_geometry(extra[k]);
          if(linking)o->set_light_set_membership(k==1?(1ull<<0)|(1ull<<1):(1ull<<0));
          o->set_tfm(transform_translate(at[k])*transform_rotate(1.1f+k,make_float3(0,1,0))*transform_rotate(-.5f,make_float3(1,0,0)));}
      }
    }
    if(rm)scene.integrator->set_volume_ray_marching(true);
    if(index>=75&&index<79)scene.integrator->set_sampling_pattern(SamplingPattern(pattern));
    if(denoise)for(auto [type,name]:{std::pair{PASS_DENOISING_ALBEDO,"Denoising Albedo"},std::pair{PASS_DENOISING_NORMAL,"Denoising Normal"}}){
      auto pass=scene.create_node<Pass>();pass->set_type(type);pass->set_name(ustring(name));}
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Volume scene preparation failed");auto &data=scene.dscene.data;
    if(index>=75&&index<79)require(data.integrator.sampling_pattern==pattern,"Volume sampling pattern case without its pattern");
    if(light_path)for(Shader *medium:{shader,world_medium})require(!medium||medium->has_light_path_node,"Volume Light Path case without its shader flag");
    if(index==86)require(!world_medium->has_volume_spatial_varying,"Volume Light Path second medium is not homogeneous");
    if(index==86)require(data.volume_stack_size>=3,"Volume Light Path overlap without a stack slot");
    if(het_world)require(scene.dscene.volume_tree_roots.size()>=1&&data.background.volume_shader!=SHADER_NONE&&(index==90||data.volume_stack_size>=3),"Heterogeneous world case without its octree root or stack slot");
    // native/DEVICE.md 181: in a full run cases 92 and 95 follow the light tree cases 66-73 on the same device, so their modules keep the
    // light tree regions (the device's features only grow) while the scene does not use the tree.
    if(moving_camera&&repeat_case<0)require((d.shader_features()&SF_VOLUME_LIGHT_TREE).any()&&!data.integrator.use_light_tree,"Volume camera motion case without the light tree regions of the earlier cases");
    if(moving_camera)require(data.cam.num_motion_steps==3&&!data.bvh.have_motion&&data.integrator.num_lights==2,"Volume camera motion case without camera motion or two lights (steps "+std::to_string(data.cam.num_motion_steps)+", lights "+std::to_string(data.integrator.num_lights)+", light objects "+std::to_string(std::count_if(scene.objects.begin(),scene.objects.end(),[](Object *o){return o->get_geometry()->is_light();}))+")");
    if(index>=88&&index<90)require(data.bvh.have_motion&&(scene.dscene.object_flag[box_object->get_device_index()]&SD_OBJECT_MOTION),"Volume motion case without a moving medium");
    if(linking)require((data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)&&bool(data.integrator.use_light_tree)==(index==82),"Volume light linking case without light linking");
    if(denoise)require((data.kernel_features&KERNEL_FEATURE_DENOISING)&&data.film.pass_denoising_albedo!=PASS_UNUSED&&data.film.pass_denoising_normal!=PASS_UNUSED,"Volume denoising case without its passes");
    if(!direct){data.background.use_mis=0;data.integrator.use_direct_light=0;data.integrator.use_light_mis=0;data.integrator.num_lights=0;}
    else require(data.integrator.use_direct_light&&data.integrator.num_lights>0,"Volume direct light fixture without a light");
    data.integrator.use_caustics=0;data.integrator.filter_glossy=FLT_MAX;data.film.pass_flag=0;
    // volume_phase_widen_dD's scale (the integrator's min(1, Filter Glossy)): 0.37, 0 and 1 over the cases.
    data.integrator.differential_widen_scale=index%2?.37f:index%4?0.f:1.f;
    d.const_copy_to("data",&data,sizeof(data));bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);
    std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> ids;
    for(unsigned j=0;j<512;j++){unsigned i=3*j;ids.push_back(i);auto &s=states[i];s.path.render_pixel_index=j;s.path.sample=j*137u;s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;s.path.rng_offset=uint16_t(65500u+i*79u);
      s.path.bounce=j%3;s.path.volume_bounce=j%3;s.path.queued_kernel=rm?DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME_RAY_MARCHING:DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME;s.path.visibility=s.path.bounce?PATH_RAY_VISIBILITY_VOLUME_SCATTER:PATH_RAY_VISIBILITY_CAMERA;
      s.path.flag=PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND;if(j%13==0)s.path.flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;if(j%17==0)s.path.flag|=PATH_RAY_TERMINATE_IN_NEXT_VOLUME;
      s.path.throughput=make_float3(.6,.8,.4);s.path.continuation_probability=j%23==0?0:.75f;s.path.min_ray_pdf=FLT_MAX;s.path.optical_depth=.125f;
      s.ray.P=make_float3(.1,.2,-2);s.ray.D=normalize(make_float3(.013f*j-.8f,.4f-.007f*j,j%2?1.f:-1.f));s.ray.tmin=.003f*(j%3);s.ray.tmax=FLT_MAX;s.ray.time=float(j%17)/16;s.ray.dP=.003f;s.ray.dD=.0003f*j;
      s.isect.t=s.ray.tmin+.01f+(het?.00004f:.001f)*j*j;s.isect.object=0;s.isect.prim=0;s.isect.type=PRIMITIVE_TRIANGLE;
      if(test==23||test==29||test==31||test==73||(index==90&&j%4==0)){s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;s.isect.type=PRIMITIVE_NONE;}
      s.volume_stack[0].object=data.background.object_index;s.volume_stack[0].shader=data.background.volume_shader;
      if(test==37){s.path.bounce=0;s.path.volume_bounce=0;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.ray.P=make_float3(1e-6f*(j%7),-1e-6f*(j%5),-6.99999f);
        s.ray.D=normalize(make_float3(.0009f*(j%23)-.01f,.0011f*(j/23)-.012f,1.f));s.ray.tmin=(-5.f-s.ray.P.z)/s.ray.D.z;s.ray.tmax=1e5f;s.isect.t=s.ray.tmin+.9f+.005f*j;}
      if(moving_camera){
        // Camera rays of the moving camera through the coverage scene's pixels (32x24, fov 0.9) that see the box: the origin
        // on the camera's motion path at the ray's time, the ray from the box entry (tmin) to its exit (the segment end).
        const float time=float((j*37)%61)/60;s.ray.time=time;s.path.bounce=0;s.path.volume_bounce=0;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;
        s.path.flag=PATH_RAY_TRANSPARENT_BACKGROUND|PATH_RAY_MIS_SKIP;s.path.throughput=one_float3();
        s.ray.P=time<.5f?make_float3(-.25f+.5f*time,.08f-.16f*time,-7.2f+.4f*time):make_float3(.5f*(time-.5f),-.1f*(time-.5f),-7.f+.4f*(time-.5f));
        const float px=19.f+float(j%11)+.37f*float((j*7)%3),py=7.f+float((j/11)%9)+.29f*float((j*5)%3),tan_half=std::tan(.45f);
        s.ray.D=normalize(make_float3((2*px/32-1)*tan_half,(2*py/24-1)*tan_half*.75f,1.f));
        const float3 lo=make_float3(.9f,-1.95f,-.8f),hi=make_float3(3.5f,.3f,1.6f);float t0=0,t1=1e30f;
        for(int k=0;k<3;k++){const float o=k==0?s.ray.P.x:k==1?s.ray.P.y:s.ray.P.z,dd=k==0?s.ray.D.x:k==1?s.ray.D.y:s.ray.D.z,l=k==0?lo.x:k==1?lo.y:lo.z,h=k==0?hi.x:k==1?hi.y:hi.z;
          float a=(l-o)/dd,b=(h-o)/dd;if(a>b)std::swap(a,b);t0=std::max(t0,a);t1=std::min(t1,b);}
        if(!(t1>t0)){t0=7.f;t1=8.f;}s.ray.tmin=t0;s.ray.tmax=1e5f;s.isect.t=t1;}
      if(linking)s.path.mis_ray_object=j%3?int(box_object->get_device_index()):int(data.background.object_index);
      if(denoise&&j%4){s.path.flag|=PATH_RAY_DENOISING_FEATURES;s.path.denoising_feature_throughput=make_float3(.9f,.7f+.01f*(j%5),.5f);}
      if(het&&!het_world){s.volume_stack[0].object=box_object->get_device_index();s.volume_stack[0].shader=scene.shader_manager->get_shader_id(shader);}s.volume_stack[1].object=OBJECT_NONE;s.volume_stack[1].shader=SHADER_NONE;
      if(index==91){s.volume_stack[1].object=box_object->get_device_index();s.volume_stack[1].shader=scene.shader_manager->get_shader_id(box_medium);s.volume_stack[2].object=OBJECT_NONE;s.volume_stack[2].shader=SHADER_NONE;}
      if(index==86){s.volume_stack[1].object=second_object->get_device_index();s.volume_stack[1].shader=scene.shader_manager->get_shader_id(world_medium);s.volume_stack[2].object=OBJECT_NONE;s.volume_stack[2].shader=SHADER_NONE;}
    }
    if(index==0)m.rejected=rejections(d,layout,states);
    if(index==32)density_check(d,scene,box_object,scene.shader_manager->get_shader_id(shader),m);
    auto batched=[&](Metrics &into){
      // 24 batches of the 512 camera paths with other pixel hashes, samples and bounce offsets (CODEBRIX_CYCLES_VOLUME_GATE_BATCHES).
      unsigned batches=24;if(const char *b=std::getenv("CODEBRIX_CYCLES_VOLUME_GATE_BATCHES"))batches=unsigned(std::strtoul(b,nullptr,10));
      for(unsigned batch=0;batch<batches;batch++){auto varied=states;
        for(unsigned j=0;j<512;j++){auto &s=varied[3*j];s.path.rng_pixel=0x2545f491u*(batch+1)+j*0x9e3779b9u;s.path.sample=(j*7u+batch*5u)%16u;s.path.rng_offset=0;}
        std::cerr<<"volume case 92 batch "<<batch<<"\n";evaluate(d,scene,layout,varied,ids,into,index);}
    };
    if(index==95||(index==92&&std::getenv("CODEBRIX_CYCLES_VOLUME_GATE_REAL"))){real_paths(d,scene,scheduler,layout,m);continue;}
    // Diagnostic (CODEBRIX_CYCLES_VOLUME_GATE_CPU_OCTREE=1, case 92): the CPU oracle reads the octree the CPU device builds
    // for this box (the GPU keeps its own), as in an oracle render, where each device builds its own.
    if(index==92)if(std::getenv("CODEBRIX_CYCLES_VOLUME_GATE_CPU_OCTREE")){const Transform tfm=box_object->get_tfm();std::vector<KernelOctreeNode> cpu_nodes;
      Metrics octree;octree_check(d,stats,profiler,octree,false,&tfm,&cpu_nodes,make_float3(.8,.7,.6));
      require(cpu_nodes.size()==scene.dscene.volume_tree_nodes.size(),"Octree diagnostic node count");
      unsigned differing=0,structure=0;double largest=0;size_t at=0;
      for(size_t i=0;i<cpu_nodes.size();i++){const auto &c=cpu_nodes[i],&g=scene.dscene.volume_tree_nodes[i];structure+=c.parent!=g.parent||c.first_child!=g.first_child;
        differing+=c.sigma.min!=g.sigma.min||c.sigma.max!=g.sigma.max;
        for(double e:{std::abs(double(c.sigma.min)-g.sigma.min),std::abs(double(c.sigma.max)-g.sigma.max)})if(e>largest){largest=e;at=i;}}
      std::cerr<<std::setprecision(9)<<"Octree diagnostic: "<<cpu_nodes.size()<<" nodes, "<<structure<<" structure differences, "<<differing<<" with extrema not bit-identical to the GPU-built ones; largest |difference| "<<largest
               <<" at node "<<at<<" (CPU "<<cpu_nodes[at].sigma.min<<".."<<cpu_nodes[at].sigma.max<<", GPU "<<scene.dscene.volume_tree_nodes[at].sigma.min<<".."<<scene.dscene.volume_tree_nodes[at].sigma.max<<")\n";
      std::copy(cpu_nodes.begin(),cpu_nodes.end(),scene.dscene.volume_tree_nodes.data());}
    if(repeat_case>=0){
      for(unsigned k=0;k<repeat_count;k++){Metrics one;
        try{if(index==92)batched(one);else evaluate(d,scene,layout,states,ids,one,index);std::cout<<"repeat "<<k<<" passed"<<std::endl;}
        catch(const std::exception &e){repeat_failed++;std::cout<<"repeat "<<k<<" FAILED "<<e.what()<<std::endl;}}
      continue;
    }
    if(index==92)batched(m);else evaluate(d,scene,layout,states,ids,m,index);
    if(index==36)octree_check(d,stats,profiler,m);
    // The world medium's octree (rooted at the background light object) built by both devices.
    if(index==90)octree_check(d,stats,profiler,m,true);
  }
  if(repeat_case>=0){std::cout<<"{\"repeatCase\":"<<repeat_case<<",\"evaluations\":"<<repeat_count<<",\"failed\":"<<repeat_failed<<"}"<<std::endl;}else{
  require(m.scatter>0&&m.transmit>0&&m.terminated>0&&m.shadows>0,"Missing native volume events");require(stats.mem_used==0,"Volume allocation leak");
  std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"originalCpuVolumeIntegrator\":true,\"cases\":"<<m.cases<<",\"invalidCasesRejected\":"<<m.rejected<<",\"paths\":"<<m.cases*512<<",\"scatterEvents\":"<<m.scatter<<",\"transmitEvents\":"<<m.transmit<<",\"terminatedEvents\":"<<m.terminated<<",\"volumeShadowPaths\":"<<m.shadows<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strictFailures\":"<<m.strict<<",\"maximumError\":"<<m.maximum<<",\"maximumErrorActual\":"<<m.maximum_actual<<",\"maximumErrorExpected\":"<<m.maximum_expected<<",\"maximumErrorField\":\""<<m.field<<"\",\"maximumPracticalToleranceRatio\":"<<m.ratio<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  }
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
