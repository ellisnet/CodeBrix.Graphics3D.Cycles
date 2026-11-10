// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Bake state gate: DEVICE_KERNEL_INTEGRATOR_INIT_FROM_BAKE on the WebGPU device, dispatched by the unmodified
// PathTraceWorkGPU::enqueue_work_tiles, judged against the unmodified pinned CPU integrator_init_from_bake
// (bake_reference.cpp) on the same scene tables, the same render buffer and the same input states.
// The map in the render buffer is written by this gate, not by a rasterizer: interior points, points on an
// edge, exact vertices (the nudge towards the centre), wide differentials (the jitter's retries and its give-up)
// and empty texels. Cases: an instanced target (transform not applied) and a single-user target with a negative
// scale (transform applied, winding swapped); the view direction along the normal and towards a perspective or
// orthographic camera; tabulated Sobol, Sobol-Burley and both blue-noise patterns, scrambling distance below 0.9;
// the position and normal fast paths, the normal through shading (bump), environment baking, the seed pass,
// adaptive sampling with the sample count pass, the shadow catcher split, the raytrace queue (AO node) and a
// world volume (volume stack). Every integer, flag, sort key, queue counter, seed and untouched guard byte is
// exact; the ray, its differentials and the barycentrics are judged by the practical criterion
// (abs(a-e) <= 5e-5 + 2e-4*abs(e)) with the strict one (1e-7 + 1e-7*abs(e)) reported; film floats are exact.
// Usage: codebrix_cycles_bake_state_gate OUTPUT_DIRECTORY   (writes OUTPUT_DIRECTORY/status.json)
#include "wavefront_test.h"
#include "bake_reference.h"
// scene/shader.h first: background.h only declares Shader, and its inline set_shader would otherwise take the bool overload of Node::set.
#include "scene/shader.h"
#include "scene/background.h"
#include "scene/bake.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/pass.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "session/buffers.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
constexpr unsigned W=16,H=12;
struct Case {const char *name;int target;PassType pass;bool use_camera;CameraType camera;SamplingPattern pattern;unsigned samples;
  bool seed,adaptive,catcher;int shader;bool volume;float scrambling;};
// shader: 0 diffuse, 1 Ambient Occlusion node (SD_HAS_RAYTRACE), 2 bump (SD_HAS_BUMP), 3 diffuse mixed with a Ray Portal BSDF.
static const Case cases[]={
  {"instanced combined along the normal",0,PASS_COMBINED,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,4,false,false,false,0,false,1},
  {"instanced towards a perspective camera, Sobol-Burley, scrambled order",0,PASS_COMBINED,true,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_SOBOL_BURLEY,3,false,false,false,0,false,.25f},
  {"applied negative scale towards an orthographic camera, blue noise",1,PASS_COMBINED,true,CAMERA_ORTHOGRAPHIC,SAMPLING_PATTERN_BLUE_NOISE_PURE,2,false,false,false,0,false,1},
  {"position fast path",1,PASS_POSITION,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,1,false,false,false,0,false,1},
  {"normal fast path, smooth",0,PASS_NORMAL,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,4,false,false,false,0,false,1},
  {"normal through shading (bump)",1,PASS_NORMAL,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,2,false,false,false,2,false,1},
  {"environment baking",0,PASS_BACKGROUND,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,2,false,false,false,0,false,1},
  {"seed pass",0,PASS_COMBINED,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,2,true,false,false,0,false,1},
  {"adaptive sampling and the sample count pass",0,PASS_COMBINED,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,1,false,true,false,0,false,1},
  {"shadow catcher split",0,PASS_COMBINED,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,2,false,false,true,0,false,1},
  {"raytrace queue (Ambient Occlusion node)",1,PASS_COMBINED,true,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,2,false,false,false,1,false,1},
  {"world volume, diffuse direct pass, blue noise first",0,PASS_DIFFUSE_DIRECT,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_BLUE_NOISE_FIRST,2,false,false,false,0,true,1},
  {"Ray Portal BSDF on the baked object (KERNEL_FEATURE_NODE_PORTAL: the portal bounce)",0,PASS_COMBINED,false,CAMERA_PERSPECTIVE,SAMPLING_PATTERN_TABULATED_SOBOL,2,false,false,false,3,false,1}};
struct Metrics {uint64_t paths=0,queued=0,empty=0,splits=0,integers=0,floats=0,strict=0,bit_differences=0,film_floats=0,vertex_texels=0,jitter_given_up=0;double maximum=0;std::string maximum_field;
  std::map<std::string,uint64_t> strict_by_field;std::map<std::string,uint64_t> kernels;std::vector<std::string> layouts;};
static Shader *graph_shader(Scene &scene,std::unique_ptr<ShaderGraph> g){auto s=scene.create_node<Shader>();s->set_graph(std::move(g));s->tag_update(&scene);return s;}
static Shader *material(Scene &scene,int kind,float3 color){
  auto g=make_unique<ShaderGraph>();auto d=g->create_node<DiffuseBsdfNode>();d->set_color(color);
  if(kind==1){auto ao=g->create_node<AmbientOcclusionNode>();ao->set_distance(.8f);g->connect(ao->output("Color"),d->input("Color"));}
  if(kind==2){auto bump=g->create_node<BumpNode>();bump->set_height(.5f);bump->set_strength(1.f);g->connect(bump->output("Normal"),d->input("Normal"));}
  if(kind==3){auto portal=g->create_node<RayPortalBsdfNode>();portal->set_color(make_float3(.9,.8,.7));portal->set_direction(make_float3(0,0,1));
    auto mix=g->create_node<MixClosureNode>();mix->set_fac(.5f);g->connect(d->output("BSDF"),mix->input("Closure1"));g->connect(portal->output("BSDF"),mix->input("Closure2"));
    g->connect(mix->output("Closure"),g->output()->input("Surface"));return graph_shader(scene,std::move(g));}
  g->connect(d->output("BSDF"),g->output()->input("Surface"));return graph_shader(scene,std::move(g));
}
// A tent of four triangles around a raised centre vertex: smooth vertex normals that vary over each triangle.
static Mesh *tent(Scene &scene,Shader *shader,float lift){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(5,4);
  const float3 v[]={make_float3(-1,-1,0),make_float3(1,-1,.3f),make_float3(1,1,0),make_float3(-1,1,.4f),make_float3(.1f,-.05f,lift)};
  std::copy(std::begin(v),std::end(v),mesh->get_position_for_write());const int t[]={0,1,4,1,2,4,2,3,4,3,0,4};std::copy_n(t,12,mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),true);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();return mesh;
}
static Object *object(Scene &scene,Mesh *mesh,const Transform &tfm){auto o=scene.create_node<Object>();o->set_geometry(mesh);o->set_tfm(tfm);return o;}
// The texel map: per pixel a triangle (or -1), Blender-convention barycentrics and their differentials.
static void write_map(const KernelFilm &f,unsigned stride,unsigned c,std::vector<float> &film,Metrics &m){
  std::mt19937 random(1234+c);std::uniform_real_distribution<float> unit(0.f,1.f);
  for(unsigned pixel=0;pixel<W*H;pixel++){
    float *at=film.data()+size_t(pixel)*stride;
    float *primitive=at+f.pass_bake_primitive,*differential=at+f.pass_bake_differential;
    const unsigned kind=(pixel*7+c)%11;
    if(kind==0){primitive[0]=primitive[1]=0;primitive[2]=std::bit_cast<float>(-1);m.empty++;continue;}
    float u=unit(random),v=unit(random);if(u+v>1){u=1-u;v=1-v;}
    if(kind==1){u=0;v=0;m.vertex_texels++;}else if(kind==2){u=1;v=0;m.vertex_texels++;}else if(kind==3){u=0;v=1;m.vertex_texels++;}
    else if(kind==4){u=0;}else if(kind==5){v=1-u;}
    float scale=kind==6?4.f:kind==7?1e-6f:.08f;
    primitive[0]=u;primitive[1]=v;primitive[2]=std::bit_cast<float>(int(pixel%4));
    for(int k=0;k<4;k++)differential[k]=(unit(random)-.5f)*scale;
    if(kind==6)m.jitter_given_up++;
  }
}
static void judge(const char *label,const std::string &field,float a,float e,Metrics &m){
  const double error=std::abs(double(a)-double(e));m.floats++;
  if(std::memcmp(&a,&e,4))m.bit_differences++;
  if(error>1e-7+1e-7*std::abs(double(e))){m.strict++;m.strict_by_field[field]++;}
  if(error>m.maximum){m.maximum=error;m.maximum_field=field;}
  require(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)),std::string(label)+": "+field+" actual="+std::to_string(a)+" expected="+std::to_string(e));
}
static void run_case(WebGPUDevice &d,unsigned c,Metrics &m){
  const Case &k=cases[c];
  SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;
  Scene scene(params,&d);
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(k.adaptive);scene.integrator->set_use_denoise(false);
  scene.integrator->set_aa_samples(8);scene.integrator->set_seed(17+c*31);scene.integrator->set_sampling_pattern(k.pattern);
  scene.film->set_use_sample_count(k.adaptive);
  scene.camera->set_full_width(W);scene.camera->set_full_height(H);scene.camera->set_screen_size(W,H);scene.camera->set_camera_type(k.camera);
  scene.camera->set_matrix(transform_translate(make_float3(.4f,-.3f,5.f))*transform_rotate(M_PI_F,make_float3(0,1,0))*transform_rotate(.15f,make_float3(1,0,0)));
  scene.camera->compute_auto_viewplane();
  auto world=make_unique<ShaderGraph>();auto bg=world->create_node<BackgroundNode>();bg->set_color(make_float3(.3,.4,.5));bg->set_strength(1.f);
  world->connect(bg->output("Background"),world->output()->input("Surface"));
  if(k.volume){auto absorption=world->create_node<AbsorptionVolumeNode>();absorption->set_color(make_float3(.8,.65,.4));absorption->set_density(.04f);
    world->connect(absorption->output("Volume"),world->output()->input("Volume"));}
  scene.background->set_shader(graph_shader(scene,std::move(world)));scene.background->set_transparent(k.catcher);
  // An unrelated object first, so that the bake mesh's triangles start after an offset (kernel_data.bake.tri_offset).
  object(scene,tent(scene,material(scene,0,make_float3(.5,.5,.5)),.2f),transform_translate(make_float3(0,0,-3)));
  Mesh *shared=tent(scene,material(scene,k.target==0?k.shader:0,make_float3(.7,.4,.2)),.8f);
  Object *a=object(scene,shared,transform_translate(make_float3(.2f,.1f,.3f))*transform_rotate(.4f,make_float3(0,0,1))*transform_scale(make_float3(1.3f,.9f,1.1f)));
  object(scene,shared,transform_translate(make_float3(3,0,0)));
  Object *b=object(scene,tent(scene,material(scene,k.target==1?k.shader:0,make_float3(.2,.6,.4)),.6f),
                   transform_translate(make_float3(-.3f,.2f,.1f))*transform_rotate(-.3f,make_float3(1,0,0))*transform_scale(make_float3(-1.2f,1.f,.9f)));
  Object *target=k.target?b:a;target->set_is_bake_target(true);target->set_is_shadow_catcher(k.catcher);
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
  {auto pass=scene.create_node<Pass>();pass->set_type(k.pass);pass->set_name(ustring(k.pass==PASS_COMBINED?"Combined":"Bake"));pass->set_mode(PassMode::NOISY);}
  scene.bake_manager->set_use_seed(k.seed);scene.bake_manager->set_use_camera(k.use_camera);scene.bake_manager->set_baking(&scene,true);scene.bake_manager->tag_update();
  Progress progress;scene.device_update(&d,progress);healthy(d);
  auto &data=scene.dscene.data;
  require(data.bake.use&&data.bake.tri_offset>0&&data.bake.object_index==(k.target?3:1)&&data.bake.use_camera==int(k.use_camera),"Bake kernel data: tri_offset "+std::to_string(data.bake.tri_offset)+" object "+std::to_string(data.bake.object_index));
  require(((d.object_flag_records.at(size_t(data.bake.object_index))&SD_OBJECT_TRANSFORM_APPLIED)!=0)==(k.target==1),"Bake target transform state");
  require((data.film.pass_bake_seed!=0)==k.seed,"Bake seed pass");
  if(k.scrambling!=1){data.integrator.scrambling_distance=k.scrambling;d.const_copy_to("data",&data,sizeof(data));healthy(d);}
  bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
  const auto layout=fields(d);
  if(k.shader==3)require((data.kernel_features&KERNEL_FEATURE_NODE_PORTAL)&&std::any_of(layout.begin(),layout.end(),[](const Field &f){return f.name=="path.portal_bounce";}),
                         "Ray Portal case without the portal feature or its path state");
  {const auto &f=data.film;std::ostringstream o;o<<"{\"case\":"<<c<<",\"stride\":"<<f.pass_stride<<",\"combined\":"<<f.pass_combined<<",\"bakePrimitive\":"<<f.pass_bake_primitive
    <<",\"bakeDifferential\":"<<f.pass_bake_differential<<",\"bakeSeed\":"<<f.pass_bake_seed<<",\"sampleCount\":"<<f.pass_sample_count<<",\"adaptiveAux\":"<<f.pass_adaptive_aux_buffer<<"}";m.layouts.push_back(o.str());}
  BufferParams buffer_params;buffer_params.width=buffer_params.full_width=buffer_params.window_width=W;buffer_params.height=buffer_params.full_height=buffer_params.window_height=H;
  buffer_params.update_passes(scene.passes);
  auto buffers=scheduler.get_render_buffers();buffers->reset(buffer_params);buffers->zero();healthy(d);
  const unsigned stride=unsigned(data.film.pass_stride);require(buffer_params.pass_stride==int(stride),"Bake buffer stride");
  std::vector<float> film(size_t(W)*H*stride,0.f);write_map(data.film,stride,c,film,m);
  if(k.seed)for(unsigned pixel=0;pixel<W*H;pixel++)film[size_t(pixel)*stride+data.film.pass_bake_seed]=std::bit_cast<float>(pixel*2654435761u+c);
  if(k.adaptive){require(data.film.pass_adaptive_aux_buffer!=PASS_UNUSED&&data.film.pass_sample_count!=PASS_UNUSED,"Adaptive bake passes");
    for(unsigned pixel=0;pixel<W*H;pixel++){film[size_t(pixel)*stride+data.film.pass_sample_count]=std::bit_cast<float>(pixel%7);
      if(pixel%5==0)film[size_t(pixel)*stride+data.film.pass_adaptive_aux_buffer+3]=1.f;}}
  std::memcpy(buffers->buffer.data(),film.data(),film.size()*4);buffers->buffer.copy_to_device();healthy(d);
  const unsigned n=unsigned(scheduler.capacity());std::vector<IntegratorStateCPU> expected(n);
  for(const auto &f:layout){std::vector<uint8_t> bytes(size_t(n)*f.stride,0x3e);
    for(unsigned i=0;i<n;i++)std::memcpy(reinterpret_cast<uint8_t*>(&expected[i])+f.offset,bytes.data()+i*f.stride,f.stride);
    d.upload(f.address,bytes.data(),bytes.size());}
  scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Bake scheduler reset");
  for(auto &s:expected)s.path.queued_kernel=0;
  KernelWorkTile tiles[2]{};
  for(int t=0;t<2;t++){tiles[t].x=0;tiles[t].y=t*6;tiles[t].w=W;tiles[t].h=6;tiles[t].num_samples=k.samples;tiles[t].start_sample=t?5:0;tiles[t].offset=0;tiles[t].stride=W;tiles[t].sample_offset=k.adaptive?11:0;}
  constexpr unsigned first=5;const unsigned work=W*6*k.samples;require(first+2*work*2<=n,"Bake gate state capacity");
  uint32_t next=first+2*work;
  if(k.catcher){require(d.state.next_main_path_index!=nullptr,"Missing shadow catcher path index");d.upload(uint32_t(uintptr_t(d.state.next_main_path_index)),&next,4);}
  // The CPU kernel, work item by work item in ascending path order (get_work_pixel's two orders).
  const unsigned divisor=unsigned(d.state.sort_partition_divisor);require(divisor>0,"Sort divisor");
  auto sort_key=[&](const IntegratorStateCPU &s,unsigned index){return (scene.dscene.tri_shader[s.isect.prim]&SHADER_MASK)+uint32_t(data.max_shaders)*(index/divisor);};
  for(int t=0;t<2;t++){KernelWorkTile tile=tiles[t];tile.path_index_offset=first+t*work;tile.work_size=work;
    for(unsigned item=0;item<work;item++){
      unsigned pixel=item/tile.num_samples,sample=item%tile.num_samples;
      if(data.integrator.scrambling_distance<.9f){sample=item/(tile.w*tile.h);pixel=item%(tile.w*tile.h);}
      const unsigned x=tile.x+pixel%tile.w,y=tile.y+pixel/tile.w,path=tile.path_index_offset+item;
      IntegratorStateCPU pair[2]={expected[path],expected[path]};
      original_bake(scene.dscene,tile,x,y,tile.start_sample+sample,pair,film);expected[path]=pair[0];
      const auto kernel=DeviceKernel(expected[path].path.queued_kernel);
      if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE||kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE)expected[path].path.shader_sort_key=sort_key(expected[path],path);
      // The kernel resets the flag (path_state_init_integrator): PATH_RAY_SHADOW_CATCHER_HIT afterwards is this call's split.
      if(kernel&&(pair[0].path.flag&PATH_RAY_SHADOW_CATCHER_HIT)){
        const uint32_t j=next++;require(j<n,"Shadow catcher split slot");IntegratorStateCPU copy=pair[1];
        if(copy.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)copy.path.shader_sort_key=sort_key(copy,j);
        expected[j]=copy;m.splits++;
      }
    }}
  scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_BAKE,tiles,2,first,0);
  require(scheduler.update_queue_counter_and_cache(),std::string("Bake dispatch: ")+d.error_message());healthy(d);
  // States: every byte exact except the ray and the barycentrics (practical, strict reported).
  for(const auto &f:layout){std::vector<uint8_t> actual(size_t(n)*f.stride);d.download(f.address,actual.data(),actual.size());
    const bool approximate=(f.name.starts_with("ray.")&&f.name!="ray.previous_dt")||f.name=="isect.u"||f.name=="isect.v";
    for(unsigned i=0;i<n;i++){
      auto *want=reinterpret_cast<const uint8_t*>(&expected[i])+f.offset;auto *got=actual.data()+size_t(i)*f.stride;
      if(approximate&&f.floating&&expected[i].path.queued_kernel){for(size_t b=0;b<f.stride;b+=4){float a,e;std::memcpy(&a,got+b,4);std::memcpy(&e,want+b,4);
        judge(k.name,f.name+"["+std::to_string(b/4)+"] path "+std::to_string(i),a,e,m);}}
      else{require(std::memcmp(got,want,f.stride)==0,std::string(k.name)+": state "+f.name+" path "+std::to_string(i));m.integers++;}
    }
  }
  IntegratorQueueCounter counter{};d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
  std::vector<int> counts(DEVICE_GPU_KERNEL_INTEGRATOR_NUM);
  for(auto &s:expected)if(s.path.queued_kernel){counts.at(s.path.queued_kernel)++;m.queued++;m.kernels[device_kernel_as_string(DeviceKernel(s.path.queued_kernel))]++;}
  for(unsigned i=0;i<counts.size();i++)require(counter.num_queued[i]==counts[i],std::string(k.name)+": queue counter "+device_kernel_as_string(DeviceKernel(i)));
  if(k.catcher){uint32_t after=0;d.download(uint32_t(uintptr_t(d.state.next_main_path_index)),&after,4);require(after==next&&m.splits>0,std::string(k.name)+": shadow catcher split count");}
  buffers->buffer.copy_from_device();healthy(d);
  for(size_t i=0;i<film.size();i++){require(std::memcmp(&buffers->buffer.data()[i],&film[i],4)==0,std::string(k.name)+": film float "+std::to_string(i));m.film_floats++;}
  m.paths+=2*work;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;Metrics m;std::string failure;unsigned passed=0;
  try{
    require(argc==2,"Usage: codebrix_cycles_bake_state_gate OUTPUT_DIRECTORY");std::filesystem::create_directories(argv[1]);
    auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
    Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"Bake state gate");
    for(unsigned c=0;c<std::size(cases);c++){run_case(d,c,m);passed++;std::cerr<<"PASS "<<cases[c].name<<"\n";}
    require(stats.mem_used==0,"Bake gate leaked Cycles allocations");
  }catch(const std::exception &e){failure=e.what();std::cerr<<"FAIL "<<failure<<'\n';status=1;}
  std::ostringstream out;out<<"{\"complete\":true,\"passed\":"<<(status==0?"true":"false")<<",\"cases\":"<<std::size(cases)<<",\"casesPassed\":"<<passed
    <<",\"failure\":\""<<failure.substr(0,400)<<"\",\"paths\":"<<m.paths<<",\"queuedPaths\":"<<m.queued<<",\"shadowCatcherSplits\":"<<m.splits
    <<",\"emptyTexelsWritten\":"<<m.empty<<",\"vertexTexelsWritten\":"<<m.vertex_texels<<",\"wideDifferentialTexels\":"<<m.jitter_given_up
    <<",\"exactStateFieldChecks\":"<<m.integers<<",\"filmFloatsExact\":"<<m.film_floats<<",\"practicalFloatChecks\":"<<m.floats
    <<",\"strictFailures\":"<<m.strict<<",\"bitDifferences\":"<<m.bit_differences<<",\"maximumAbsoluteError\":"<<m.maximum<<",\"maximumField\":\""<<m.maximum_field<<"\",\"strictByField\":{";
  {bool first=true;for(auto &[name,count]:m.strict_by_field){out<<(first?"":",")<<'"'<<name.substr(0,name.find(' '))<<"\":"<<count;first=false;}}
  out<<"},\"queuedKernels\":{";{bool first=true;for(auto &[name,count]:m.kernels){out<<(first?"":",")<<'"'<<name<<"\":"<<count;first=false;}}
  out<<"},\"filmLayouts\":[";for(size_t i=0;i<m.layouts.size();i++)out<<(i?",":"")<<m.layouts[i];
  out<<"],\"absoluteTolerance\":0.00005,\"relativeTolerance\":0.0002,\"strictAbsoluteTolerance\":0.0000001,\"strictRelativeTolerance\":0.0000001}\n";
  if(argc==2)std::ofstream(std::string(argv[1])+"/status.json")<<out.str();std::cout<<out.str();
  TaskScheduler::exit();return status;}
