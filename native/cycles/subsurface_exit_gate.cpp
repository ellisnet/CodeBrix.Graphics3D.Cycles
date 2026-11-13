// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// The subsurface EXIT POINT on the WebGPU device against the unchanged pinned CPU kernels: for each case, walks and disk
// probes from points of a closed sphere run through the pinned integrator_intersect_subsurface (subsurface_reference.cpp),
// and the paths it queues for DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE (PATH_RAY_SUBSURFACE, the exit point) are the input
// of one shade_surface step on both sides: the pinned integrator_shade_surface (surface_reference.cpp) and the device's
// surface stages. Compared: every path state field, every shadow path field (input k's reserved slot k), every film float
// and the queue counters. Cases, each with the random walk and the Christensen-Burley method:
//  0 data, AOV, cryptomatte and denoising feature passes on a bump-mapped BSSRDF (the exit point evaluates the shader: its
//    AOV outputs; it writes no data pass and no denoising feature; PATH_RAY_SINGLE_PASS_DONE and the denoising feature
//    flag as the pinned kernel leaves them);
//  1 AOV passes as the only data passes (film_write_data_passes' PATH_RAY_SINGLE_PASS_DONE bookkeeping);
//  2 light linking (two lamps in different receiver sets: the exit point's light selection);
//  3 shadow linking (the sphere a blocker of one lamp's set: the exit point's bounce looks for its dedicated light);
//  4 a holdout object (the subsurface sphere; integrate_surface applies no holdout at the exit point);
//  5 the shadow catcher (the sphere a catcher, catcher passes, paths with and without the catcher flags);
//  6 a holdout closure mixed with the bump-mapped BSSRDF (the exit point evaluates the shader, its holdout included, and
//    integrate_surface applies no holdout there).
#include "wavefront_test.h"
#include "subsurface_reference.h"
#include "surface_reference.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/light.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/pass.h"
#include "scene/integrator.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace ccl;
using namespace wavefront_test;
struct Metrics {uint64_t floats=0,integers=0,strict=0,practical_failures=0,nonfinite=0,exits=0,shadows=0,film_written=0;unsigned cases=0,printed=0;double maximum=0,ratio=0;std::string field;};
// The standard criterion: practical abs(a-e) <= 5e-5 + 2e-4*abs(e) must hold, strict 1e-7 + 1e-7*abs(e) is reported,
// NaN and Inf fail; integers exact.
static void number(float a,float e,const std::string &field,Metrics &m){
  const double error=std::abs(double(a)-e);m.floats++;m.strict+=error>1e-7+1e-7*std::abs(double(e));
  m.nonfinite+=!(std::isfinite(a)&&std::isfinite(e));
  if(!(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)))){m.practical_failures++;if(m.printed++<12)std::cerr<<"practical "<<field<<" actual "<<std::setprecision(9)<<a<<" expected "<<e<<'\n';}
  if(error>m.maximum){m.maximum=error;m.field=field;}m.ratio=std::max(m.ratio,error/(5e-5+2e-4*std::abs(double(e))));
}
static void integer(const void *a,const void *e,size_t bytes,const std::string &field,Metrics &m){
  m.integers++;if(std::memcmp(a,e,bytes)){m.practical_failures++;if(m.printed++<12){uint32_t x=0,y=0;std::memcpy(&x,a,std::min<size_t>(bytes,4));std::memcpy(&y,e,std::min<size_t>(bytes,4));
    std::cerr<<field<<" integer mismatch actual "<<x<<" expected "<<y<<'\n';}}
}
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
  return out;
}
static Shader *emission(Scene &scene,float strength){
  auto g=make_unique<ShaderGraph>();auto e=g->create_node<EmissionNode>();e->set_color(one_float3());e->set_strength(strength);
  g->connect(e->output("Emission"),g->output()->input("Surface"));auto s=scene.create_node<Shader>();s->set_graph(std::move(g));s->tag_update(&scene);return s;
}
static Object *lamp(Scene &scene,Shader *emitter,float3 at,float3 strength){
  auto point=scene.create_node<PointLight>();point->set_radius(.3f);point->set_is_sphere(true);point->set_use_mis(true);point->set_strength(strength);
  array<Node*> slots;slots.push_back_slow(emitter);point->set_used_shaders(slots);auto o=scene.create_node<Object>();o->set_geometry(point);o->set_tfm(transform_translate(at));return o;
}
static void add_pass(Scene &scene,PassType type,const char *name){auto pass=scene.create_node<Pass>();pass->set_type(type);pass->set_name(ustring(name));}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  // Each case on its own device, as a render session of its scene would be. --one-device runs every case on one device: a
  // device's enabled features only grow (device.h), so cases after the shadow linking case run with its SHADOW_LINKING region.
  const bool one_device=argc==3&&std::string(argv[2])=="--one-device";
  require(argc==2||one_device,"Usage: subsurface_exit_gate OUTPUT_PREFIX [--one-device]");const std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;unique_ptr<Device> owner;Metrics m;std::string adapter,library;
  uint64_t single_pass_set=0,denoising_cleared=0,aov_written=0,dedicated=0,linked_samples=0;
  for(unsigned test=0;test<14;test++){m.printed=0;
    if(!owner||!one_device){owner.reset();owner=Device::create(infos[0],stats,profiler,true);}
    auto &d=require_webgpu_device(owner.get(),"subsurface_exit_gate device creation");adapter=d.gpu.adapter_name;library=d.gpu.library_path;
    const unsigned mode=test/2;const bool disk=test%2;const ClosureType method=disk?CLOSURE_BSSRDF_BURLEY_ID:CLOSURE_BSSRDF_RANDOM_WALK_ID;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);
    scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(16);
    scene.film->set_use_sample_count(false);scene.camera->set_full_width(32);scene.camera->set_full_height(16);scene.camera->set_screen_size(32,16);scene.camera->compute_auto_viewplane();
    // The subsurface material: a Subsurface Scattering node; with passes (modes 0, 1) its Normal from a Bump node over a noise
    // texture (SD_HAS_BSSRDF_BUMP) and two AOV outputs; with the holdout closure (mode 6) the bump and a Mix Closure node.
    auto g=make_unique<ShaderGraph>();auto sss=g->create_node<SubsurfaceScatteringNode>();sss->set_method(method);sss->set_scale(disk?.15f:.4f);
    sss->set_radius(make_float3(1,.6f,.35f));sss->set_color(make_float3(.85f,.7f,.55f));
    if(mode==6){auto holdout=g->create_node<HoldoutNode>();auto mix=g->create_node<MixClosureNode>();mix->set_fac(.35f);
      g->connect(holdout->output("Holdout"),mix->input("Closure1"));g->connect(sss->output("BSSRDF"),mix->input("Closure2"));g->connect(mix->output("Closure"),g->output()->input("Surface"));}
    else g->connect(sss->output("BSSRDF"),g->output()->input("Surface"));
    if(mode<=1||mode==6){auto noise=g->create_node<NoiseTextureNode>();noise->set_scale(3.f);noise->set_detail(1.f);auto bump=g->create_node<BumpNode>();bump->set_strength(.6f);bump->set_distance(.4f);
      g->connect(noise->output("Fac"),bump->input("Height"));g->connect(bump->output("Normal"),sss->input("Normal"));
      if(mode<=1){auto ac=g->create_node<OutputAOVNode>();ac->set_name(ustring("Coords"));g->connect(noise->output("Color"),ac->input("Color"));
        auto av=g->create_node<OutputAOVNode>();av->set_name(ustring("Height"));g->connect(noise->output("Fac"),av->input("Value"));}}
    auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->tag_update(&scene);
    auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);
    const int stacks=8,slices=12;std::vector<float3> verts;std::vector<int> tris;
    for(int i=0;i<=stacks;i++)for(int j=0;j<=slices;j++){const float th=M_PI_F*i/stacks,ph=2*M_PI_F*j/slices;verts.push_back(make_float3(sinf(th)*cosf(ph),cosf(th),sinf(th)*sinf(ph)));}
    for(int i=0;i<stacks;i++)for(int j=0;j<slices;j++){const int a=i*(slices+1)+j,b=a+slices+1;if(i>0)for(int v:{a,b,a+1})tris.push_back(v);if(i<stacks-1)for(int v:{a+1,b,b+1})tris.push_back(v);}
    mesh->resize_mesh(verts.size(),tris.size()/3);for(size_t i=0;i<verts.size();i++)mesh->get_position_for_write()[i]=verts[i];
    for(size_t i=0;i<tris.size();i++)mesh->get_triangles()[i]=tris[i];
    for(size_t t=0;t<tris.size()/3;t++){mesh->get_shader()[t]=0;mesh->get_smooth()[t]=true;}
    mesh->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(mesh);
    // Two lamps, both outside the sphere; the second only where linking (2, 3) needs one.
    auto warm=lamp(scene,emission(scene,1),make_float3(2.2f,2,-2),make_float3(110,90,70));Object *cool=nullptr;
    if(mode==2||mode==3)cool=lamp(scene,emission(scene,1),make_float3(-2.4f,1.8f,-1.6f),make_float3(50,65,90));
    if(mode==2){object->set_receiver_light_set(1);warm->set_light_set_membership(1ull<<1);cool->set_light_set_membership(1ull<<0);}
    if(mode==3){object->set_blocker_shadow_set(1);warm->set_shadow_set_membership(1ull<<0);cool->set_shadow_set_membership((1ull<<0)|(1ull<<1));}
    if(mode==4){object->set_use_holdout(true);scene.background->set_transparent(true);}
    if(mode==6)scene.background->set_transparent(true);
    if(mode==5){object->set_is_shadow_catcher(true);scene.background->set_transparent(true);scene.film->set_use_approximate_shadow_catcher(false);
      add_pass(scene,PASS_SHADOW_CATCHER,"Shadow Catcher");add_pass(scene,PASS_SHADOW_CATCHER_MATTE,"Shadow Catcher Matte");add_pass(scene,PASS_SHADOW_CATCHER_SAMPLE_COUNT,"Shadow Catcher Sample Count");}
    if(mode==0){const std::pair<PassType,const char*> data_passes[]={{PASS_DEPTH,"Depth"},{PASS_POSITION,"Position"},{PASS_NORMAL,"Normal"},{PASS_ROUGHNESS,"Roughness"},{PASS_UV,"UV"},
        {PASS_OBJECT_ID,"IndexOB"},{PASS_MATERIAL_ID,"IndexMA"},{PASS_DIFFUSE_COLOR,"DiffCol"},{PASS_MIST,"Mist"},{PASS_DENOISING_ALBEDO,"Denoising Albedo"},
        {PASS_DENOISING_NORMAL,"Denoising Normal"},{PASS_DENOISING_DEPTH,"Denoising Depth"},{PASS_DENOISING_ROUGHNESS,"Denoising Roughness"},
        {PASS_DENOISING_SPECULAR_ALBEDO,"Denoising Specular Albedo"}};
      for(const auto &[type,name]:data_passes)add_pass(scene,type,name);
      scene.film->set_cryptomatte_passes(CryptomatteType(CRYPT_OBJECT|CRYPT_MATERIAL|CRYPT_ASSET));scene.film->set_cryptomatte_depth(2);
      for(const char *name:{"CryptoObject00","CryptoObject01","CryptoMaterial00","CryptoMaterial01","CryptoAsset00","CryptoAsset01"})add_pass(scene,PASS_CRYPTOMATTE,name);}
    if(mode<=1){add_pass(scene,PASS_AOV_COLOR,"Coords");add_pass(scene,PASS_AOV_VALUE,"Height");}
    {auto wg=make_unique<ShaderGraph>();auto bg=wg->create_node<BackgroundNode>();bg->set_color(make_float3(.3f,.35f,.42f));bg->set_strength(1.f);
      wg->connect(bg->output("Background"),wg->output()->input("Surface"));auto world=scene.create_node<Shader>();world->set_graph(std::move(wg));world->tag_update(&scene);scene.background->set_shader(world);}
    for(auto s:scene.shaders)s->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Subsurface exit scene preparation failed");auto &data=scene.dscene.data;
    require(data.kernel_features&KERNEL_FEATURE_SUBSURFACE,"Subsurface exit fixture without KERNEL_FEATURE_SUBSURFACE");
    if(mode==0)require((data.kernel_features&KERNEL_FEATURE_DENOISING)&&data.film.cryptomatte_passes&&data.film.pass_aov_color!=PASS_UNUSED,"Subsurface exit passes case without its passes");
    if(mode==2)require(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING,"Subsurface exit case without light linking");
    if(mode==3)require(data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING,"Subsurface exit case without shadow linking");
    if(mode==4)require(scene.dscene.object_flag[object->get_device_index()]&SD_OBJECT_HOLDOUT_MASK,"Subsurface exit case without a holdout object");
    if(mode==5)require(data.integrator.has_shadow_catcher,"Subsurface exit case without the shadow catcher");
    if(mode==6){bool holdout=false;for(size_t i=0;i+4<=scene.dscene.svm_nodes.size();i+=4)holdout|=scene.dscene.svm_nodes[i]==NODE_CLOSURE_HOLDOUT;
      bool bump=false;for(size_t i=0;i<scene.dscene.shaders.size();i++)bump|=(scene.dscene.shaders[i].flags&SD_HAS_BSSRDF_BUMP)!=0;
      require(holdout&&bump,"Subsurface exit case without the holdout closure or the BSSRDF bump");}
    data.integrator.use_caustics=0;
    d.const_copy_to("data",&data,sizeof(data));bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
    const auto layout=fields(d);std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> ids;const int prims=int(tris.size()/3);
    // The entry points (as subsurface_gate.cpp's), as a camera path for the pass and holdout cases.
    for(unsigned j=0;j<512;j++){unsigned i=3*j;require(i<states.size(),"Subsurface exit fixture capacity");ids.push_back(i);auto &s=states[i];
      const int prim=int(j*7)%prims;const float3 A=verts[tris[3*prim]],B=verts[tris[3*prim+1]],C=verts[tris[3*prim+2]];
      const float u=.05f+.9f*float((j*37)%101)/101.f*.5f,v=.05f+.9f*float((j*53)%97)/97.f*.5f;
      const float3 P=u*A+v*B+(1-u-v)*C,Ng=normalize(cross(B-A,C-A));
      const float3 tangent=normalize(cross(Ng,fabsf(Ng.x)<.9f?make_float3(1,0,0):make_float3(0,1,0)));
      const float tilt=.95f*sinf(.37f*j);s.ray.D=normalize(-Ng+tilt*tangent);s.ray.P=P;s.ray.tmin=0;s.ray.tmax=FLT_MAX;s.ray.time=.5f;s.ray.dP=.0001f*(j%7);s.ray.dD=0;
      s.isect.object=0;s.isect.prim=prim;s.isect.type=PRIMITIVE_TRIANGLE;s.isect.t=1;s.isect.u=u;s.isect.v=v;
      s.subsurface.N=Ng;s.subsurface.albedo=make_float3(.8f,.55f,.45f);s.subsurface.radius=(disk?make_float3(.08f,.05f,.03f):make_float3(.3f,.135f,.075f))*(1+.25f*float(j%4));
      s.subsurface.anisotropy=disk?2.f:0.f;
      s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE;s.path.render_pixel_index=j;s.path.sample=j*137u;s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;
      s.path.rng_offset=uint16_t(16u*(1+j%5));s.path.bounce=mode==3?1+j%2:0;s.path.diffuse_bounce=0;s.path.transmission_bounce=0;s.path.glossy_bounce=0;s.path.transparent_bounce=0;
      uint32_t flag=disk?PATH_RAY_SUBSURFACE_DISK:PATH_RAY_SUBSURFACE_RANDOM_WALK;if(j%6==0)flag|=PATH_RAY_SUBSURFACE_BACKFACING;
      if(mode!=2&&mode!=3)flag|=PATH_RAY_TRANSPARENT_BACKGROUND;else flag|=PATH_RAY_REFLECT|PATH_RAY_DIFFUSE_ANCESTOR;
      if(mode<=1&&j%3==0)flag|=PATH_RAY_SINGLE_PASS_DONE;
      if(mode==0&&j%2==0)flag|=PATH_RAY_DENOISING_FEATURES;
      if(mode==5&&j%4==1){flag|=PATH_RAY_SHADOW_CATCHER_HIT;}
      if(mode==5&&j%4==2)flag|=PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_PASS;
      s.path.flag=flag;s.path.mnee=0;
      s.path.visibility=uint8_t((mode!=2&&mode!=3)?(PATH_RAY_VISIBILITY_ALL&~PATH_RAY_VISIBILITY_CAMERA):PATH_RAY_VISIBILITY_DIFFUSE);
      s.path.throughput=make_float3(.6f,.8f,.4f);s.path.continuation_probability=1;s.path.min_ray_pdf=FLT_MAX;s.path.mis_ray_pdf=.5f;s.path.mis_origin_n=Ng;
      s.path.denoising_feature_throughput=make_float3(.8f,.7f,.6f);s.path.pass_diffuse_weight=one_spectrum();s.path.pass_glossy_weight=zero_spectrum();
      s.path.mis_ray_object=0;
      for(int k=0;k<MAX_VOLUME_STACK_SIZE;k++){s.volume_stack[k].object=OBJECT_NONE;s.volume_stack[k].shader=SHADER_NONE;}
    }
    // The pinned subsurface kernel gives the exit points; they are the shade_surface input of both sides.
    original_subsurface(scene.dscene,ids,states);std::vector<int> exits;
    for(auto i:ids)if(states[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
      require(states[i].path.flag&PATH_RAY_SUBSURFACE,"Subsurface exit without PATH_RAY_SUBSURFACE");exits.push_back(i);}
    require(exits.size()>64,"Subsurface exit fixture with too few exits");m.exits+=exits.size();
    const auto input=states;
    const size_t count=512*size_t(data.film.pass_stride);std::vector<float> wanted(count,.125f);auto expected=input;
    original_surface(scene.dscene,exits,expected,wanted);
    upload(d,layout,input);device_vector<float> film(&d,"subsurface_exit_film",MEM_READ_WRITE);film.alloc(count);std::fill_n(film.data(),count,.125f);film.copy_to_device();
    device_vector<int> selected(&d,"subsurface_exit_indices",MEM_READ_WRITE);selected.alloc(exits.size());std::copy(exits.begin(),exits.end(),selected.data());selected.copy_to_device();
    IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]=exits.size();d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
    // Every shadow slot starts terminated; the reserved slots start at 0 (next_shadow_path_index).
    const auto shadow_layout=shadow_fields(d);{int zero=0;d.upload(uintptr_t(d.state.next_shadow_path_index),&zero,4);}
    for(auto &f:shadow_layout){std::vector<uint8_t> bytes(size_t(d.remaining(f.address)),0);d.upload(f.address,bytes.data(),bytes.size());}
    for(auto kernel:{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE})if(d.state.sort_key_counter[kernel])d.zero(uintptr_t(d.state.sort_key_counter[kernel]),d.remaining(uintptr_t(d.state.sort_key_counter[kernel])));
    auto queue=d.gpu_queue_create();int n=int(exits.size());DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&film.device_pointer);args.add(&n);
    require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,n+63,args),d.error_message());require(queue->synchronize(),d.error_message());
    const auto actual=download(d,layout,input.size());
    const std::string tag="case="+std::to_string(test);
    for(const auto &field:layout)for(int i:exits){
      auto a=reinterpret_cast<const char*>(&actual[i])+field.offset;auto e=reinterpret_cast<const char*>(&expected[i])+field.offset;const std::string label=tag+" "+field.name+" path="+std::to_string(i);
      if(field.floating)for(size_t k=0;k<field.stride;k+=4){float x,y;std::memcpy(&x,a+k,4);std::memcpy(&y,e+k,4);number(x,y,label+" component="+std::to_string(k/4),m);}
      else integer(a,e,field.stride,label,m);
    }
    for(const auto &field:shadow_layout){
      std::vector<uint8_t> bytes(exits.size()*field.stride);d.download(field.address,bytes.data(),bytes.size());
      for(size_t k=0;k<exits.size();k++){
        const auto &cpu=expected[exits[k]].shadow;if(!cpu.shadow_path.queued_kernel&&field.name!="shadow_path.queued_kernel")continue;
        auto a=reinterpret_cast<const char*>(bytes.data()+k*field.stride);auto e=reinterpret_cast<const char*>(&cpu)+field.offset;const std::string label=tag+" shadow "+field.name+" path="+std::to_string(exits[k]);
        if(field.floating)for(size_t c=0;c<field.stride;c+=4){float x,y;std::memcpy(&x,a+c,4);std::memcpy(&y,e+c,4);number(x,y,label+" component="+std::to_string(c/4),m);}
        else integer(a,e,field.stride,label,m);
      }
    }
    film.copy_from_device();healthy(d);for(size_t i=0;i<count;i++){number(film[i],wanted[i],tag+" film="+std::to_string(i),m);m.film_written+=wanted[i]!=.125f;}
    IntegratorQueueCounter expected_count{};for(int i:exits){if(expected[i].path.queued_kernel)expected_count.num_queued[expected[i].path.queued_kernel]++;
      if(expected[i].shadow.shadow_path.queued_kernel)expected_count.num_queued[expected[i].shadow.shadow_path.queued_kernel]++;}
    d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));integer(&counter,&expected_count,sizeof(counter),tag+" queue counters",m);
    // Events the cases are for, from the pinned kernel's result.
    for(int i:exits){const auto &s=expected[i];const auto &in=input[i];m.shadows+=s.shadow.shadow_path.queued_kernel!=0;
      single_pass_set+=(s.path.flag&PATH_RAY_SINGLE_PASS_DONE)&&!(in.path.flag&PATH_RAY_SINGLE_PASS_DONE);
      denoising_cleared+=(in.path.flag&PATH_RAY_DENOISING_FEATURES)&&!(s.path.flag&PATH_RAY_DENOISING_FEATURES);
      dedicated+=s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT;linked_samples+=mode==2&&s.shadow.shadow_path.queued_kernel!=0;}
    if(mode<=1)for(int i:exits){const size_t at=size_t(input[i].path.render_pixel_index)*data.film.pass_stride+data.film.pass_aov_value;aov_written+=wanted[at]!=.125f;}
    m.cases++;std::cerr<<"Subsurface exit case "<<test<<" exits "<<exits.size()<<" practical failures so far "<<m.practical_failures<<" (totals so far: shadows "<<m.shadows
                       <<" dedicated "<<dedicated<<" filmWritten "<<m.film_written<<" aovWritten "<<aov_written<<" integers "<<m.integers<<" floats "<<m.floats<<" strict "<<m.strict<<")\n";
  }
  require(m.shadows>0&&linked_samples>0&&dedicated>0,"Missing subsurface exit shadow paths, linked light samples or dedicated lights");
  owner.reset();std::ofstream(prefix+".adapter.txt")<<adapter;std::ofstream(prefix+".library.txt")<<library;
  const bool passed=m.practical_failures==0;
  std::cout<<std::setprecision(17)<<"{\"passed\":"<<(passed?"true":"false")<<",\"originalCpuSubsurfaceAndSurfaceKernels\":true,\"oneDevice\":"<<(one_device?"true":"false")<<",\"cases\":"<<m.cases<<",\"exitPaths\":"<<m.exits
           <<",\"shadowPaths\":"<<m.shadows<<",\"dedicatedLightPaths\":"<<dedicated<<",\"singlePassDoneSet\":"<<single_pass_set<<",\"denoisingFeaturesCleared\":"<<denoising_cleared
           <<",\"aovWrittenAtExit\":"<<aov_written<<",\"filmFloatsWritten\":"<<m.film_written<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strictFailures\":"<<m.strict
           <<",\"practicalFailures\":"<<m.practical_failures<<",\"nonfiniteValues\":"<<m.nonfinite<<",\"maximumError\":"<<m.maximum<<",\"maximumErrorField\":\""<<m.field
           <<"\",\"maximumPracticalToleranceRatio\":"<<m.ratio<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  if(!passed)status=1;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
