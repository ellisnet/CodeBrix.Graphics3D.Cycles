// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "render_capture.h"
#include "camera_reference.h"
#include "closest_reference.h"
#include "surface_reference.h"
#include "background_reference.h"
#include "light_reference.h"
#include "shadow_reference.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
using namespace ccl;
using namespace wavefront_test;
#ifdef CODEBRIX_TRANSPARENT_LIGHT_GATE
#include "transparent_shadow_fixture.h"
#endif
#ifdef CODEBRIX_FEATURE_COMBINATION_GATE
#include "glass_fixture.h"
#include "camera_motion_fixture.h"
#endif
#ifdef CODEBRIX_OBJECT_MOTION_GATE
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#endif
#ifdef CODEBRIX_SHADOW_TERMINATOR_GATE
#include "shadow_terminator_fixture.h"
#endif
constexpr unsigned width=32,height=24,samples=8;
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
constexpr int lighting_cases=5;
// Image modes; mode 4 (static instanced emitter) is checked for rejection.
constexpr int lighting_modes[lighting_cases]={0,1,2,3,5};
#elif defined(CODEBRIX_BROAD_LIGHT_GATE)
constexpr int lighting_cases=12;
#elif defined(CODEBRIX_SHADOW_TERMINATOR_GATE)
constexpr int lighting_cases=shadow_terminator_cases;
#else
constexpr int lighting_cases=8;
#endif
#ifdef CODEBRIX_LIGHTING_MIS_GATE
constexpr bool use_mis=true;
#else
constexpr bool use_mis=false;
#endif
struct Metrics {uint64_t unsupported_emitters=0,mesh_mis_paths=0,deforming_paths=0,transparent_hits=0,max_shadow_hits=0,transmissions=0,shadows=0,blocked=0,visible=0,forward=0,stages=0,floats=0,strict=0,repeat_differences=0;double maximum=0,repeat_maximum=0;};
static Shader *material(Scene &scene,int variant){
  auto graph=make_unique<ShaderGraph>();auto diffuse=graph->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.55,.32,.18));diffuse->set_roughness(.31f);
  ShaderOutput *surface=diffuse->output("BSDF");
  if(variant){auto glossy=graph->create_node<GlossyBsdfNode>();glossy->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy->set_color(make_float3(.2,.5,.8));glossy->set_roughness(.35f);
    auto mix=graph->create_node<MixClosureNode>();mix->set_fac(.4f);graph->connect(surface,mix->input("Closure1"));graph->connect(glossy->output("BSDF"),mix->input("Closure2"));surface=mix->output("Closure");}
  graph->connect(surface,graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);return shader;
}
static Object *quad(Scene &scene,Shader *shader,const std::array<float3,4> &vertices){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(4,2);
  std::copy(vertices.begin(),vertices.end(),mesh->get_position_for_write());const int indices[]={0,1,2,0,2,3};std::copy_n(indices,6,mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(mesh);return object;
}
static void fixture(Scene &scene,int mode,bool stationary=false){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(samples);scene.integrator->set_min_bounce(2);scene.integrator->set_max_bounce(3);scene.integrator->set_filter_glossy(0);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);
  scene.camera->set_fov(.9f);scene.camera->set_matrix(transform_translate(make_float3(0,0,-7)));scene.camera->compute_auto_viewplane();
#ifdef CODEBRIX_SHADOW_TERMINATOR_GATE
  {(void)stationary;auto background=make_unique<ShaderGraph>();auto bg=background->create_node<BackgroundNode>();bg->set_color(make_float3(.05f,.06f,.08f));background->connect(bg->output("Background"),background->output()->input("Surface"));
    auto world=scene.create_node<Shader>();world->set_graph(std::move(background));world->tag_update(&scene);scene.background->set_shader(world);
    auto emission_graph=make_unique<ShaderGraph>();auto emission=emission_graph->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(1);
    emission_graph->connect(emission->output("Emission"),emission_graph->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(emission_graph));emitter->tag_update(&scene);
    shadow_terminator_fixture(scene,mode,emitter);for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);return;}
#endif
  auto diffuse=material(scene,0),mixed=material(scene,1);
  quad(scene,diffuse,{make_float3(-4,-3,3),make_float3(4,-3,3),make_float3(4,3,3),make_float3(-4,3,3)});
  quad(scene,mixed,{make_float3(-4,-2,-2),make_float3(4,-2,-2),make_float3(4,-2,3),make_float3(-4,-2,3)});
  Shader *center=mixed;
#ifdef CODEBRIX_FEATURE_COMBINATION_GATE
  auto glass_graph=make_unique<ShaderGraph>();
  const unsigned glass_cases[]={13,14,15,16,18,19,2,6};
  auto closure=glass_material(*glass_graph,glass_cases[mode]);
  glass_graph->connect(closure,glass_graph->output()->input("Surface"));
  center=scene.create_node<Shader>();center->set_graph(std::move(glass_graph));center->tag_update(&scene);
  camera_motion_fixture(scene,scene.camera->get_matrix(),unsigned(mode));
#endif
#ifdef CODEBRIX_OBJECT_MOTION_GATE
  // Authored material exercises local object coordinates and object-space normals.
  auto graph=make_unique<ShaderGraph>();auto tex=graph->create_node<TextureCoordinateNode>();
  auto absolute=graph->create_node<VectorMathNode>();absolute->set_math_type(NODE_VECTOR_MATH_ABSOLUTE);
  graph->connect(tex->output("Object"),absolute->input("Vector1"));
  auto diffuse_motion=graph->create_node<DiffuseBsdfNode>();diffuse_motion->set_roughness(.27f);
  graph->connect(absolute->output("Vector"),diffuse_motion->input("Color"));
  auto normal=graph->create_node<NormalMapNode>();normal->set_space(NODE_NORMAL_MAP_OBJECT);
  normal->set_color(make_float3(.55f,.42f,.97f));normal->set_strength(.35f);
  graph->connect(normal->output("Normal"),diffuse_motion->input("Normal"));
  graph->connect(diffuse_motion->output("BSDF"),graph->output()->input("Surface"));
  center=scene.create_node<Shader>();center->set_graph(std::move(graph));center->tag_update(&scene);
#endif
  auto center_object=quad(scene,center,{make_float3(-.8f,-1.2f,0),make_float3(.8f,-1.2f,0),make_float3(.8f,1.1f,0),make_float3(-.8f,1.1f,0)});
#ifdef CODEBRIX_OBJECT_MOTION_GATE
  scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);
  center_object->set_tfm(transform_scale(make_float3(mode%2?-1.1f:1.f,.9f,1.2f)));
#ifdef CODEBRIX_DEFORMING_MOTION_GATE
  if(!stationary)deforming_motion_fixture(scene,*center_object,unsigned(mode));
  if(!stationary&&(mode%2))object_motion_fixture(scene,*center_object,unsigned(mode));
#else
  if(!stationary)object_motion_fixture(scene,*center_object,unsigned(mode));
#endif
  if(mode>=4){auto instance=scene.create_node<Object>();instance->set_geometry(center_object->get_geometry());
    instance->set_tfm(transform_translate(make_float3(-1.6f,.3f,.7f))*transform_rotate(.15f,make_float3(0,1,0))*transform_scale(make_float3(.6f,.7f,.9f)));
  #ifdef CODEBRIX_DEFORMING_MOTION_GATE
    if(!stationary&&(mode%2))object_motion_fixture(scene,*instance,unsigned(mode+1));
#else
    if(!stationary)object_motion_fixture(scene,*instance,unsigned(mode+1));
#endif
}
#endif
#ifdef CODEBRIX_TRANSPARENT_LIGHT_GATE
  scene.integrator->set_transparent_max_bounce(mode==7?3:16);
  const int layers=mode==4?4:mode==5?7:1;
  auto pane=transparent_material(scene,mode<4?mode:mode==6?4:1);
  for(int layer=0;layer<layers;layer++){const float y=.55f+.13f*layer;
    quad(scene,pane,{make_float3(-2.9f,y,-1.8f),make_float3(2.9f,y,-1.8f),make_float3(2.9f,y,2.8f),make_float3(-2.9f,y,2.8f)});}
#endif
  auto background=make_unique<ShaderGraph>();auto bg=background->create_node<BackgroundNode>();bg->set_color(zero_float3());background->connect(bg->output("Background"),background->output()->input("Surface"));
  auto world=scene.create_node<Shader>();world->set_graph(std::move(background));world->tag_update(&scene);scene.background->set_shader(world);
  auto emission_graph=make_unique<ShaderGraph>();auto emission=emission_graph->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(1);
  emission_graph->connect(emission->output("Emission"),emission_graph->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(emission_graph));emitter->tag_update(&scene);
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
  // Original fixture: emission-only control, two-sided MIS, mirrored static
  // one-sided emitter, a static emitter mixed with a point light, a mirrored
  // emitter plus a second static instance of its mesh (mode 4, retained for its
  // explicit rejection check) and a moving/deforming emitter with a point light.
  emission->set_strength(3);emitter->tag_update(&scene);
  auto panel=quad(scene,emitter,{make_float3(-.8,-.5,0),make_float3(.8,-.5,0),make_float3(.8,.5,0),make_float3(-.8,.5,0)});
  panel->set_tfm(transform_translate(make_float3(1.65,1.55,-1.4))*transform_rotate(.18f,make_float3(0,1,0))*transform_rotate(.5f,make_float3(1,0,0))*transform_scale(make_float3((mode==2||mode==4)?-1.2f:1.f,1.1f,1.f)));
  if(mode==4){auto instance=scene.create_node<Object>();instance->set_geometry(panel->get_geometry());instance->set_tfm(transform_translate(make_float3(-2,1.5,-1))*transform_scale(make_float3(.7,1.2,1)));}
  if(mode==5){deforming_motion_fixture(scene,*panel,1);object_motion_fixture(scene,*panel,1);}
  if(mode==3||mode==5){
    auto point=scene.create_node<PointLight>();point->set_radius(.45f);point->set_is_sphere(true);point->set_use_mis(true);point->set_strength(make_float3(25,35,45));
    array<Node*> slots;slots.push_back_slow(emitter);point->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(point);object->set_tfm(transform_translate(make_float3(-2,2,-1)));
  }
#elif defined(CODEBRIX_BROAD_LIGHT_GATE)
  for(int lamp=0;lamp<(mode==11?3:1);lamp++){
    Light *light;
    if(mode<4||(mode==11&&lamp==1)){auto spot=scene.create_node<SpotLight>();spot->set_radius(mode==0?0.f:mode==3?1.8f:.45f);spot->set_is_sphere(mode!=1);spot->set_angle(mode==3?.25f:1.2f);spot->set_smooth(mode==0?0.f:.35f);light=spot;}
    else if(mode==11&&lamp==2){auto sun=scene.create_node<SunLight>();sun->set_angle(.22f);sun->set_normalize(false);light=sun;}
    else{auto area=scene.create_node<AreaLight>();area->set_sizeu(mode==6||mode==9?1.4f:2.f);area->set_sizev(mode==6||mode==9?1.4f:.9f);area->set_ellipse(mode==5||mode==6||mode==8||mode==9);area->set_spread(mode==10?0.f:mode>=7?.7f:M_PI_F);area->set_normalize(mode!=4);light=area;}
    light->set_strength(light->get_light_type()==LIGHT_SUN?make_float3(.3,.5,.7):make_float3(140,100,70));light->set_use_mis(true);
    array<Node*> slots;slots.push_back_slow(emitter);light->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(light);
    object->set_tfm(transform_translate(make_float3(lamp==1?-2.7f:2.2f,2,-2))*transform_rotate(light->get_light_type()==LIGHT_SUN?2.85f:2.8f,make_float3(0,1,0))*transform_rotate(-.4f,make_float3(1,0,0)));
  }
#else
  for(int lamp=0;lamp<(mode>=4?3:1);lamp++){
    Light *light;
#ifdef CODEBRIX_TRANSPARENT_LIGHT_GATE
    if(mode==2&&lamp==0){auto area=scene.create_node<AreaLight>();area->set_sizeu(1.8f);area->set_sizev(.7f);area->set_spread(1.3f);light=area;}
    else if(mode==3&&lamp==0){auto spot=scene.create_node<SpotLight>();spot->set_radius(.45f);spot->set_is_sphere(true);spot->set_angle(1.2f);spot->set_smooth(.35f);light=spot;}
    else
#endif
    if(mode==3||lamp==2){auto sun=scene.create_node<SunLight>();sun->set_angle(.22f);sun->set_normalize(false);light=sun;}
    else{auto point=scene.create_node<PointLight>();point->set_radius(mode==0?0.f:.45f);point->set_is_sphere(mode!=1);light=point;}
    light->set_strength(light->get_light_type()==LIGHT_SUN?make_float3(.3,.5,.7):make_float3(140,100,70));light->set_use_mis(use_mis);
    #ifndef CODEBRIX_TRANSPARENT_LIGHT_GATE
    if(mode==6)light->set_cast_shadow(false);
#endif
    if(mode==7)light->set_max_bounces(0);
    array<Node*> slots;slots.push_back_slow(emitter);light->set_used_shaders(slots);
    auto object=scene.create_node<Object>();object->set_geometry(light);object->set_tfm(transform_translate(make_float3(lamp==1?-2.7f:2.2f,2,-2))*transform_rotate(light->get_light_type()==LIGHT_SUN?2.85f:(light->get_light_type()==LIGHT_AREA||light->get_light_type()==LIGHT_SPOT)?2.8f:.3f,make_float3(0,1,0))*transform_rotate(-.4f,make_float3(1,0,0)));
  }
  if(mode==5)scene.integrator->set_light_sampling_threshold(.3f);
#endif
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
  if(mode)emitter->set_emission_sampling_method((mode==2||mode==4)?EMISSION_SAMPLING_FRONT:EMISSION_SAMPLING_FRONT_BACK);
#endif
}
static void cpu_render(DeviceScene &scene,std::vector<float> &film,Metrics &metrics){
  std::vector<IntegratorStateCPU> state(width*height);std::vector<IntegratorShadowStateCPU> shadow(width*height);
  for(unsigned sample=0;sample<samples;sample++){
    std::fill(state.begin(),state.end(),IntegratorStateCPU{});KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
    original_camera(scene,{tile},0,state,film);
    for(unsigned step=0;step<100;step++){
      std::map<DeviceKernel,std::vector<int>> queues;
      for(size_t i=0;i<state.size();i++)if(state[i].path.queued_kernel)queues[DeviceKernel(state[i].path.queued_kernel)].push_back(i);
      if(queues.empty()){break;}
      require(step<99,"CPU lighting paths did not terminate");
      for(auto &[kernel,ids]:queues){
        if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)original_closest(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD){metrics.forward+=ids.size();original_light_forward(scene,ids,state,film);}
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
          for(int i:ids)metrics.deforming_paths+=state[i].isect.type==PRIMITIVE_MOTION_TRIANGLE;
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
          for(int i:ids){const auto &path=state[i];const auto shader=scene.tri_shader[path.isect.prim]&SHADER_MASK;
            metrics.mesh_mis_paths+=!(path.path.flag&PATH_RAY_MIS_SKIP)&&(scene.shaders[shader].flags&(SD_MIS_FRONT|SD_MIS_BACK));}
#endif
          original_surface(scene,ids,state,film);std::vector<int> pending;
          for(int i:ids)metrics.transmissions+=(state[i].path.visibility&PATH_RAY_VISIBILITY_TRANSMIT)!=0;
          for(int i:ids)if(state[i].shadow.shadow_path.queued_kernel){shadow[i]=state[i].shadow;pending.push_back(i);}
          metrics.shadows+=pending.size();original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,pending,shadow,film);
          std::vector<int> clear;for(int i:pending){if(shadow[i].shadow_path.queued_kernel)clear.push_back(i);else metrics.blocked++;}
          metrics.visible+=clear.size();
          for(int i:clear)metrics.max_shadow_hits=std::max(metrics.max_shadow_hits,uint64_t(shadow[i].shadow_path.packed_num_hits));
          uint64_t before=0;for(int i:clear)before+=shadow[i].shadow_path.transparent_bounce;
          original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,clear,shadow,film);
          uint64_t after=0;for(int i:clear)after+=shadow[i].shadow_path.transparent_bounce;metrics.transparent_hits+=after-before;
          for(int i:pending)state[i].shadow=shadow[i];
        }else throw std::runtime_error("Unexpected CPU lighting kernel");
      }
    }
  }
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  #ifdef CODEBRIX_OBJECT_MOTION_GATE
  require(argc==2||(argc==3&&std::string(argv[2])=="--stationary"),"Usage: object_motion_render_gate OUTPUT_DIRECTORY [--stationary]");const bool stationary=argc==3;
#else
  require(argc==2,"Usage: lighting_gate OUTPUT_DIRECTORY");const bool stationary=false;
#endif
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics total;
  for(int image=0;image<lighting_cases;image++){
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
    const int mode=lighting_modes[image];
#else
    const int mode=image;
#endif
    std::cerr<<"lighting mode="<<mode<<'\n';auto folder=out/("case-"+std::to_string(image));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics metrics;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&device);fixture(scene,mode,stationary);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Lighting scene preparation failed");
    const auto &data=scene.dscene.data;std::cerr<<"native lights="<<data.integrator.num_lights<<" direct="<<data.integrator.use_direct_light<<" mis="<<data.integrator.use_light_mis<<" transparent="<<data.integrator.transparent_shadows<<"\n";
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
    require(bool(data.integrator.use_direct_light)==(mode!=0)&&!data.integrator.transparent_shadows,"Unexpected native triangle-light configuration");
    require(mode==0||data.integrator.distribution_pdf_triangles>0,"Missing native triangle distribution");
#elif defined(CODEBRIX_TRANSPARENT_LIGHT_GATE)
    require(data.integrator.use_direct_light&&data.integrator.transparent_shadows,"Unexpected native transparent-shadow configuration");
#elif defined(CODEBRIX_BROAD_LIGHT_GATE)
    require(data.integrator.use_direct_light&&!data.integrator.transparent_shadows,"Unexpected native broad-light configuration");
#elif defined(CODEBRIX_SHADOW_TERMINATOR_GATE)
    require(data.integrator.use_direct_light&&!data.integrator.transparent_shadows,"Unexpected native shadow-terminator configuration");
    {bool smooth=false;for(size_t i=0;i<scene.dscene.tri_shader.size();i++)smooth|=(scene.dscene.tri_shader[i]&SHADER_SMOOTH_NORMAL)!=0;require(smooth,"Shadow-terminator fixture lacks smooth triangles");
     unsigned offsets=0;for(size_t i=0;i<scene.dscene.objects.size();i++){const auto &o=scene.dscene.objects[i];
       offsets+=o.shadow_terminator_geometry_offset==shadow_terminator_geometry(mode)&&o.shadow_terminator_shading_offset==1.0f/(1.0f-.5f*shadow_terminator_shading(mode));}
     require(offsets>=(mode==8?2u:1u),"Native shadow terminator offsets not uploaded as authored");}
#else
    require(data.integrator.use_direct_light&&(bool(data.integrator.use_light_mis)==(use_mis&&mode!=0&&mode!=3))&&!data.integrator.transparent_shadows,"Unexpected native light configuration");
#endif
#ifdef CODEBRIX_OBJECT_MOTION_GATE
    require(bool(data.bvh.have_motion)==!stationary,"Object motion scene has incorrect native BVH motion flag");
    require(data.cam.shuttertime!=-1,"Object motion requires native shutter time sampling");
#endif
#ifdef CODEBRIX_DEFORMING_MOTION_GATE
    unsigned deforming_objects=0;for(size_t i=0;i<scene.dscene.objects.size();i++)if(scene.dscene.object_flag[i]&SD_OBJECT_HAS_VERTEX_MOTION){
      const auto &object=scene.dscene.objects[i];require(object.primitive_type==PRIMITIVE_MOTION_TRIANGLE&&(object.num_geom_steps==3||object.num_geom_steps==5),"Missing native deforming triangle frame layout");deforming_objects++;
    }
    require(bool(deforming_objects)==!stationary,"Deforming triangle fixture flag mismatch");
#endif
#ifdef CODEBRIX_FEATURE_COMBINATION_GATE
    require(data.cam.num_motion_steps>=2&&data.cam.shuttertime!=-1,"Combined scene requires camera motion");
#endif
    require(data.film.pass_stride==4,"Lighting combined-only film expected");bool cancelled=false;Scheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);auto buffers=scheduler.get_render_buffers();BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=4;buffers->reset(bp);
    std::vector<float> reference(width*height*4),first;cpu_render(scene.dscene,reference,metrics);
    RenderCapture::file(prefix+".cpu.film.bin",reference.data(),reference.size()*4);
    for(unsigned repeat=0;repeat<2;repeat++){
      buffers->zero();std::unique_ptr<RenderCapture> capture;if(repeat==1)capture=std::make_unique<RenderCapture>(device,prefix);
      for(unsigned sample=0;sample<samples;sample++){
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Lighting reset failed");KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Lighting camera failed");
        for(unsigned step=0;step<150;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<149,"GPU lighting paths did not terminate");
          require(scheduler.enqueue_path_iteration(),"Native lighting scheduler stopped with pending paths");require(scheduler.update_queue_counter_and_cache(),"Lighting stage failed: "+device.error_message());metrics.stages++;}
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples);
      buffers->buffer.copy_from_device();healthy(device);std::vector<float> actual(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
      for(size_t i=0;i<actual.size();i++){double error=std::abs(double(actual[i])-reference[i]);metrics.maximum=std::max(metrics.maximum,error);metrics.floats++;
        metrics.strict+=error>1e-7+1e-7*std::abs(double(reference[i]));
        require(std::isfinite(actual[i])&&std::isfinite(reference[i])&&error<=5e-5+2e-4*std::abs(double(reference[i])),"Lighting film mismatch mode="+std::to_string(mode)+" component="+std::to_string(i)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(reference[i]));
        if(repeat){metrics.repeat_differences+=std::memcmp(&actual[i],&first[i],4)!=0;metrics.repeat_maximum=std::max(metrics.repeat_maximum,std::abs(double(actual[i])-first[i]));}}
      if(!repeat)first=std::move(actual);
    }
#ifdef CODEBRIX_DEFORMING_MOTION_GATE
    require(stationary?metrics.deforming_paths==0:metrics.deforming_paths>100,"Missing original CPU deforming surface paths");
#endif
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
    if(mode!=0)require(metrics.shadows>100&&metrics.visible>100,"Missing mesh direct lighting coverage");
#elif defined(CODEBRIX_BROAD_LIGHT_GATE)
    require(metrics.shadows>0&&metrics.visible>0,"Missing broad direct lighting coverage");
#elif defined(CODEBRIX_SHADOW_TERMINATOR_GATE)
    require(metrics.shadows>100&&metrics.visible>100&&metrics.blocked>10,"Missing shadow-terminator direct lighting coverage");
#else
    require(metrics.shadows>100&&metrics.visible>100,"Missing direct lighting coverage");if(mode!=6)require(metrics.blocked>10,"Missing occlusion coverage");
#endif
    auto report=[&](std::ostream &stream){stream<<std::setprecision(17)<<"{\"passed\":true,\"mode\":"<<mode<<",\"transparentSurfaceHits\":"<<metrics.transparent_hits<<",\"maximumShadowHitCount\":"<<metrics.max_shadow_hits<<",\"forwardLightPaths\":"<<metrics.forward<<",\"meshMisPaths\":"<<metrics.mesh_mis_paths<<",\"deformingSurfacePaths\":"<<metrics.deforming_paths<<",\"shadows\":"<<metrics.shadows<<",\"blocked\":"<<metrics.blocked<<",\"visible\":"<<metrics.visible<<",\"stages\":"<<metrics.stages<<",\"filmChecks\":"<<metrics.floats<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum<<",\"repeatBitwiseDifferences\":"<<metrics.repeat_differences<<",\"repeatMaximumError\":"<<metrics.repeat_maximum<<"}\n";};
    std::ofstream details(folder/"result.json");report(details);report(std::cerr);total.mesh_mis_paths+=metrics.mesh_mis_paths;total.deforming_paths+=metrics.deforming_paths;total.transparent_hits+=metrics.transparent_hits;total.max_shadow_hits=std::max(total.max_shadow_hits,metrics.max_shadow_hits);total.transmissions+=metrics.transmissions;total.forward+=metrics.forward;total.shadows+=metrics.shadows;total.blocked+=metrics.blocked;total.visible+=metrics.visible;total.stages+=metrics.stages;total.floats+=metrics.floats;total.strict+=metrics.strict;total.maximum=std::max(total.maximum,metrics.maximum);total.repeat_differences+=metrics.repeat_differences;
  }
#ifdef CODEBRIX_FEATURE_COMBINATION_GATE
  require(total.transmissions>100,"Combined scene did not exercise transmission");
#endif
#ifdef CODEBRIX_TRANSPARENT_LIGHT_GATE
  require(total.transparent_hits>100&&total.max_shadow_hits>=7,"Missing transparent multi-hit image coverage");
#endif
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
  require(total.mesh_mis_paths>10,"Missing forward emissive triangle MIS paths");
  for(int mode:{4}){
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.use_texture_cache=false;
    Scene scene(params,&device);fixture(scene,mode);Progress progress;scene.device_update(&device,progress);healthy(device);
    require(!progress.get_error(),"Unsupported emitter fixture preparation failed");
    bool rejected=false;try{device.validate_lighting(true);}catch(const std::exception &e){rejected=std::string(e.what()).find("static instanced triangle emitters")!=std::string::npos;}
    require(rejected,"Unvalidated emitter configuration was accepted");total.unsupported_emitters++;
  }
#elif !defined(CODEBRIX_SHADOW_TERMINATOR_GATE)
  if(use_mis)require(total.forward>100,"Missing forward point-light paths");
#endif
  require(stats.mem_used==0,"Lighting test leaked native allocations");std::ofstream(out/"library.txt")<<device.gpu.library_path;std::ofstream(out/"adapter.txt")<<device.gpu.adapter_name;
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"nativeSceneAndScheduler\":true,\"independentOriginalCpuImage\":true,\"fullLightingMisProven\":false,\"cases\":"<<lighting_cases<<",\"pointSunMisEnabled\":"<<(use_mis?"true":"false")<<",\"transparentSurfaceHits\":"<<total.transparent_hits<<",\"maximumShadowHitCount\":"<<total.max_shadow_hits<<",\"forwardLightPaths\":"<<total.forward<<",\"unsupportedEmitterConfigurationsRejected\":"<<total.unsupported_emitters<<",\"meshMisPaths\":"<<total.mesh_mis_paths<<",\"deformingSurfacePaths\":"<<total.deforming_paths<<",\"transmissionPaths\":"<<total.transmissions<<",\"shadowPaths\":"<<total.shadows<<",\"blocked\":"<<total.blocked<<",\"visible\":"<<total.visible<<",\"filmChecks\":"<<total.floats<<",\"strictDifferences\":"<<total.strict<<",\"maximumAbsoluteError\":"<<total.maximum<<",\"repeatBitwiseDifferences\":"<<total.repeat_differences<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
