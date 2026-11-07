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
#include "util/progress.h"
#include "util/task.h"
#include "util/types_rgbe.h"
#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace ccl;
#include "curve_gate_tuft.h"
using namespace wavefront_test;
#include "surface_fixture.h"
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
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(d.state.parent.name)out.push_back({#parent "." #name,uint32_t(uintptr_t(d.state.parent.name)),sizeof(type),offsetof(IntegratorShadowStateCPU,parent.name),std::is_same_v<type,float>||std::is_same_v<type,packed_float3>||std::is_same_v<type,PackedSpectrum>});
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
  }
  auto expected=input;auto initial=wanted;original_volume(scene.dscene,ids,expected,wanted);
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
// The heterogeneous medium of cases 32-36: scatter density = 0.8 * Noise Texture (generated coordinates) in a box.
static void het_density(ShaderGraph &graph,ScatterVolumeNode *scatter){
  auto noise=graph.create_node<NoiseTextureNode>();noise->set_scale(1.6f);noise->set_detail(2);auto scale=graph.create_node<MathNode>();scale->set_math_type(NODE_MATH_MULTIPLY);scale->set_value2(.8f);
  graph.connect(noise->output("Fac"),scale->input("Value1"));graph.connect(scale->output("Value"),scatter->input("Density"));
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
static void octree_check(WebGPUDevice &gpu,Stats &stats,Profiler &profiler,Metrics &m){
  auto infos=Device::available_devices(DEVICE_MASK_CPU);require(!infos.empty(),"Missing CPU device");auto cpu=Device::create(infos[0],stats,profiler,true);
  std::vector<KernelOctreeNode> nodes[2];std::vector<KernelOctreeRoot> roots[2];
  for(unsigned k=0;k<2;k++){Device *device=k?static_cast<Device*>(&gpu):cpu.get();
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,device);
    auto graph=make_unique<ShaderGraph>();auto absorption=graph->create_node<AbsorptionVolumeNode>();absorption->set_density(.05f);absorption->set_color(make_float3(.8,.65,.4));
    auto scatter=graph->create_node<ScatterVolumeNode>();scatter->set_color(make_float3(.75,.8,.9));scatter->set_anisotropy(.3f);het_density(*graph,scatter);
    auto add=graph->create_node<AddClosureNode>();graph->connect(absorption->output("Volume"),add->input("Closure1"));graph->connect(scatter->output("Volume"),add->input("Closure2"));
    graph->connect(add->output("Closure"),graph->output()->input("Volume"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);het_box(scene,shader);
    Progress progress;scene.device_update(device,progress);require(!progress.get_error(),"Octree scene preparation failed");
    nodes[k].assign(scene.dscene.volume_tree_nodes.data(),scene.dscene.volume_tree_nodes.data()+scene.dscene.volume_tree_nodes.size());
    roots[k].assign(scene.dscene.volume_tree_roots.data(),scene.dscene.volume_tree_roots.data()+scene.dscene.volume_tree_roots.size());
  }
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
  const auto original_lights=d.light_records;const auto original_distribution=d.light_distribution_records;
  device_ptr indices=0;int n=1;DeviceKernelArguments args;args.add(&indices);args.add(&film.device_pointer);args.add(&n);
  for(unsigned test=0;test<18;test++){
    std::string reason;int work=1;
    switch(test){
      case 0:d.data.kernel_features&=~KERNEL_FEATURE_VOLUME;reason="without native volume feature";break;
      case 1:d.data.bvh.have_curves=1;d.data.bvh.have_motion=1;reason="volumes with motion blur";break;
      case 2:d.data.bvh.have_motion=1;reason="volumes with motion blur";break;
      case 3:d.data.kernel_features|=KERNEL_FEATURE_LIGHT_LINKING;reason="optional volume integration";break;
      case 4:d.data.film.pass_volume_majorant=PASS_UNUSED;reason="volume film pass";break;
      case 5:d.data.integrator.sampling_pattern=SAMPLING_PATTERN_SOBOL_BURLEY;reason="requires tabulated Sobol";break;
      case 6:d.data.kernel_features|=KERNEL_FEATURE_DENOISING;reason="optional volume integration";break;
      case 7:d.data.kernel_features|=KERNEL_FEATURE_PATH_GUIDING;reason="optional volume integration";break;
      case 8:d.data.kernel_features|=KERNEL_FEATURE_DENOISING;reason="optional volume integration";break;
      case 9:d.data.film.pass_volume_indirect=1<<20;reason="volume film pass";break;
      case 10:d.data.volume_stack_size=1;reason="volume stack size";break;
      case 11:d.data.max_closures=0;reason="volume closure capacity";break;
      case 12:d.data.film.pass_volume_transmit=PASS_UNUSED;reason="volume film pass";break;
      case 13:d.data.film.pass_volume_direct=1<<20;reason="volume film pass";break;
      case 14:for(auto &shader:d.shader_records)if(shader.flags&SD_HAS_VOLUME)shader.flags|=SD_HETEROGENEOUS_VOLUME|SD_HAS_LIGHT_PATH_NODE;reason="heterogeneous volume with a Light Path node";break;
      case 15:d.state.ray.D=nullptr;reason="virtual pointer";break;
      case 16:d.state.volume_stack[0].shader=nullptr;reason="virtual pointer";break;
      case 17:work=0;reason="Incomplete volume dispatch";break;
    }
    std::string error;try{d.enqueue_volume(work,args);}catch(const std::exception &e){error=e.what();}
    d.data=original_data;d.state=original_state;d.shader_records=original_shaders;d.light_records=original_lights;d.light_distribution_records=original_distribution;
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
  // Cases 24-31: volume direct light (VOLUME_LIGHT) with a sphere point light (MIS, distance, equiangular), a
  // disk point light, a sun light, scene rays ending on a triangle and missing, against the pinned kernel.
  // Cases 32-36: a heterogeneous medium (noise density) in a box object, integrated by null scattering through the
  // majorant octree (VOLUME_HETEROGENEOUS): without direct light, then with the light of cases 24 (MIS), 25
  // (distance), 26 (equiangular) and 28 (sun); case 37: camera rays from outside the box (the segment starts at the
  // box entry, a large ray tmin) without direct light.
  for(unsigned test=0;test<75;test++){
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
    const bool tree=test>=66&&test<74;const bool tree_lamps=test>=67&&test<74;
    const unsigned dcase=test==74?24:tree?(test==68?25:test==69?26:24):near_delta?(test==51?25:test==52||test==55?26:test==64?28:test==65?27:24):shape||mesh_lights?24:!het&&!rm?test:test==33||test==39||test==41?24:test==34?25:test==35||test==40?26:test==36?28:0;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;gate_curve_params(params);Scene scene(params,&d);prepare(scene);gate_curve_tufts(scene);
    auto graph=make_unique<ShaderGraph>();auto absorption=graph->create_node<AbsorptionVolumeNode>();absorption->set_density(test==20?0:.04f);absorption->set_color(test==21?make_float3(1,1,1):make_float3(.8,.65,.4));
    auto scatter=graph->create_node<ScatterVolumeNode>();scatter->set_density(test==20?0:test==22?.00001f:test==74?.6f:.035f);scatter->set_color(test==21?make_float3(.8,0,.6):make_float3(.8,.7,.6));scatter->set_anisotropy(test==72?-.999f:near_delta?(test==53?.999f:-.999f):dcase>=24?direct_gs[dcase-24]:gs[test%10]);
    auto add=graph->create_node<AddClosureNode>();graph->connect(absorption->output("Volume"),add->input("Closure1"));graph->connect(scatter->output("Volume"),add->input("Closure2"));ShaderOutput *volume=add->output("Closure");
    if(test>=10&&test<20){auto phase=graph->create_node<ScatterVolumeNode>();phase->set_density(.017f);phase->set_color(make_float3(.2,.9,.7));phase->set_anisotropy(gs[(test+4)%10]);auto mixture=graph->create_node<AddClosureNode>();graph->connect(volume,mixture->input("Closure1"));graph->connect(phase->output("Volume"),mixture->input("Closure2"));volume=mixture->output("Closure");}
    if(het){het_density(*graph,scatter);absorption->set_density(.05f);}
    graph->connect(volume,graph->output()->input("Volume"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);
    Object *box_object=nullptr;
    if(!het)scene.background->set_shader(shader);
    else box_object=het_box(scene,shader);
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
      object->set_tfm((test==74?transform_translate(make_float3(0.f,0.f,0.f)):transform_translate(make_float3(.6f,1.4f,-1.1f)))*transform_rotate(2.6f,make_float3(0,1,0))*transform_rotate(-.5f,make_float3(1,0,0)));
      emitter->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
      if(tree){scene.integrator->set_use_light_tree(true);scene.integrator->set_light_sampling_threshold(0.f);}
      if(tree_lamps){
        auto spot=scene.create_node<SpotLight>();spot->set_radius(.3f);spot->set_is_sphere(true);spot->set_angle(.9f);spot->set_smooth(.3f);
        auto area=scene.create_node<AreaLight>();area->set_sizeu(1.2f);area->set_sizev(.7f);area->set_spread(1.1f);
        auto sun=scene.create_node<SunLight>();sun->set_angle(.12f);sun->set_normalize(false);
        Light *extra[3]={spot,area,sun};const float3 at[3]={make_float3(-2.1f,.8f,1.3f),make_float3(1.9f,-1.2f,-3.f),make_float3(0,0,0)};
        for(unsigned k=0;k<3;k++){extra[k]->set_strength(k==2?make_float3(.4,.5,.6):make_float3(90,80,120));extra[k]->set_use_mis(true);extra[k]->set_used_shaders(slots);auto o=scene.create_node<Object>();o->set_geometry(extra[k]);
          o->set_tfm(transform_translate(at[k])*transform_rotate(1.1f+k,make_float3(0,1,0))*transform_rotate(-.5f,make_float3(1,0,0)));}
      }
    }
    if(rm)scene.integrator->set_volume_ray_marching(true);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Volume scene preparation failed");auto &data=scene.dscene.data;
    if(!direct){data.background.use_mis=0;data.integrator.use_direct_light=0;data.integrator.use_light_mis=0;data.integrator.num_lights=0;}
    else require(data.integrator.use_direct_light&&data.integrator.num_lights>0,"Volume direct light fixture without a light");
    data.integrator.use_caustics=0;data.integrator.filter_glossy=FLT_MAX;data.film.pass_flag=0;
    d.const_copy_to("data",&data,sizeof(data));bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);
    std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> ids;
    for(unsigned j=0;j<512;j++){unsigned i=3*j;ids.push_back(i);auto &s=states[i];s.path.render_pixel_index=j;s.path.sample=j*137u;s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;s.path.rng_offset=uint16_t(65500u+i*79u);
      s.path.bounce=j%3;s.path.volume_bounce=j%3;s.path.queued_kernel=rm?DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME_RAY_MARCHING:DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME;s.path.visibility=s.path.bounce?PATH_RAY_VISIBILITY_VOLUME_SCATTER:PATH_RAY_VISIBILITY_CAMERA;
      s.path.flag=PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND;if(j%13==0)s.path.flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;if(j%17==0)s.path.flag|=PATH_RAY_TERMINATE_IN_NEXT_VOLUME;
      s.path.throughput=make_float3(.6,.8,.4);s.path.continuation_probability=j%23==0?0:.75f;s.path.min_ray_pdf=FLT_MAX;s.path.optical_depth=.125f;
      s.ray.P=make_float3(.1,.2,-2);s.ray.D=normalize(make_float3(.013f*j-.8f,.4f-.007f*j,j%2?1.f:-1.f));s.ray.tmin=.003f*(j%3);s.ray.tmax=FLT_MAX;s.ray.time=float(j%17)/16;s.ray.dP=.003f;s.ray.dD=.0003f*j;
      s.isect.t=s.ray.tmin+.01f+(het?.00004f:.001f)*j*j;s.isect.object=0;s.isect.prim=0;s.isect.type=PRIMITIVE_TRIANGLE;
      if(test==23||test==29||test==31||test==73){s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;s.isect.type=PRIMITIVE_NONE;}
      s.volume_stack[0].object=data.background.object_index;s.volume_stack[0].shader=data.background.volume_shader;
      if(test==37){s.path.bounce=0;s.path.volume_bounce=0;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.ray.P=make_float3(1e-6f*(j%7),-1e-6f*(j%5),-6.99999f);
        s.ray.D=normalize(make_float3(.0009f*(j%23)-.01f,.0011f*(j/23)-.012f,1.f));s.ray.tmin=(-5.f-s.ray.P.z)/s.ray.D.z;s.ray.tmax=1e5f;s.isect.t=s.ray.tmin+.9f+.005f*j;}
      if(het){s.volume_stack[0].object=box_object->get_device_index();s.volume_stack[0].shader=scene.shader_manager->get_shader_id(shader);}s.volume_stack[1].object=OBJECT_NONE;s.volume_stack[1].shader=SHADER_NONE;
    }
    if(test==0)m.rejected=rejections(d,layout,states);
    if(test==32)density_check(d,scene,box_object,scene.shader_manager->get_shader_id(shader),m);
    evaluate(d,scene,layout,states,ids,m,test);
    if(test==36)octree_check(d,stats,profiler,m);
  }
  require(m.scatter>0&&m.transmit>0&&m.terminated>0&&m.shadows>0,"Missing native volume events");require(stats.mem_used==0,"Volume allocation leak");
  std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"originalCpuVolumeIntegrator\":true,\"cases\":"<<m.cases<<",\"invalidCasesRejected\":"<<m.rejected<<",\"paths\":"<<m.cases*512<<",\"scatterEvents\":"<<m.scatter<<",\"transmitEvents\":"<<m.transmit<<",\"terminatedEvents\":"<<m.terminated<<",\"volumeShadowPaths\":"<<m.shadows<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strictFailures\":"<<m.strict<<",\"maximumError\":"<<m.maximum<<",\"maximumErrorActual\":"<<m.maximum_actual<<",\"maximumErrorExpected\":"<<m.maximum_expected<<",\"maximumErrorField\":\""<<m.field<<"\",\"maximumPracticalToleranceRatio\":"<<m.ratio<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
