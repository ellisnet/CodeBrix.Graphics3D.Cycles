// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "wavefront.h"
#include "render_capture.h"
#include "image_fixture.h"
#include "../../parity/native/import_triangles.h"
#include "camera_reference.h"
#include "camera_motion_fixture.h"
#include "closest_reference.h"
#include "surface_reference.h"
#include "surface_dispatch_capture.h"
#include "background_reference.h"
#ifdef CBC_VOLUME_FIXTURE
#include "volume_reference.h"
#endif
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
#include "surface_fixture.h"
#ifdef CBC_CLOSED_VOLUME_FIXTURE
#include "volume_boundary_fixture.h"
#endif
struct Metrics {uint64_t integers=0,floats=0,strict=0,stages=0,paths=0,equivalent_edge_hits=0,volume_scatter=0,volume_transmit=0,volume_terminated=0;double maximum=0,maximum_bound_ratio=0;float maximum_actual=0,maximum_expected=0;std::string maximum_field;unsigned max_bounce=0,max_transparent=0,max_volume_bounds=0;std::map<int,uint64_t> kernels;std::set<uint> shaders;std::vector<std::string> edge_records;};
static void number(float a,float e,const std::string &where,Metrics &m){double error=std::abs(double(a)-e);if(error>m.maximum){m.maximum=error;m.maximum_actual=a;m.maximum_expected=e;m.maximum_field=where;}m.maximum_bound_ratio=std::max(m.maximum_bound_ratio,error/(5e-5+2e-4*std::abs(double(e))));m.floats++;m.strict+=error>1e-7+1e-7*std::abs(double(e));
  require(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)),where+" actual="+std::to_string(a)+" expected="+std::to_string(e));}
static void oracle(DeviceKernel kernel,DeviceScene &scene,const std::vector<int>&ids,std::vector<IntegratorStateCPU>&states,std::vector<float>&film){
  if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)original_closest(scene,ids,states,film);
  else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)original_surface(scene,ids,states,film);
  else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene,ids,states,film);
#ifdef CBC_VOLUME_FIXTURE
  else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME)original_volume(scene,ids,states,film);
  else if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK)original_volume_stack(scene,ids,states);
#endif
  else throw std::runtime_error("Unexpected render kernel "+std::string(device_kernel_as_string(kernel)));
}
#ifdef CBC_VOLUME_FIXTURE
static void volume_trace(const char *side,unsigned sample,DeviceKernel kernel,const std::vector<IntegratorStateCPU>&states){
  const char *target=std::getenv("CODEBRIX_VOLUME_TRACE");if(!target)return;const unsigned i=std::stoul(target);if(i>=states.size())return;
  auto &s=states[i];std::cerr<<std::setprecision(10)<<"VOLTRACE "<<side<<" sample="<<sample<<" kernel="<<device_kernel_as_string(kernel)<<" next="<<s.path.queued_kernel<<" bounce="<<s.path.bounce
    <<" P="<<s.ray.P.x<<','<<s.ray.P.y<<','<<s.ray.P.z<<" D="<<s.ray.D.x<<','<<s.ray.D.y<<','<<s.ray.D.z<<" t="<<s.isect.t<<" prim="<<s.isect.prim<<" object="<<s.isect.object<<" beta="<<s.path.throughput.x<<','<<s.path.throughput.y<<','<<s.path.throughput.z<<" pdf="<<s.path.mis_ray_pdf<<'\n';
}
#endif
// A flat, constant-clay mesh may have two equivalent hits on a shared edge.
// This is a *separate* semantic check: strict primitive/barycentric differences
// remain counted. No mismatched object, material, smooth normal or interior hit
// is accepted by this rule. Independent CPU image comparison remains required.
static bool equivalent_clay_edge(DeviceScene &scene,const IntegratorStateCPU &a,const IntegratorStateCPU &b){
  if(a.isect.prim==b.isect.prim||a.isect.prim==PRIM_NONE||b.isect.prim==PRIM_NONE||a.isect.object!=b.isect.object||a.isect.object==OBJECT_NONE)return false;
  if(a.isect.prim<0||b.isect.prim<0||size_t(a.isect.prim)>=scene.tri_shader.size()||size_t(b.isect.prim)>=scene.tri_shader.size()||a.isect.object<0||size_t(a.isect.object)>=scene.objects.size())return false;
  if(a.isect.type!=PRIMITIVE_TRIANGLE||b.isect.type!=PRIMITIVE_TRIANGLE)return false;
  auto shader=scene.tri_shader[a.isect.prim];if(shader!=scene.tri_shader[b.isect.prim]||(shader&SHADER_SMOOTH_NORMAL))return false;
  if(!(scene.object_flag[a.isect.object]&SD_OBJECT_TRANSFORM_APPLIED))return false;
  auto edge=[](const Intersection &s){return std::min({std::abs(s.u),std::abs(s.v),std::abs(1-s.u-s.v)})<=2e-6f;};
  // Integrator intersection records are a different native type than Intersection.
  Intersection ia{},ib{};ia.u=a.isect.u;ia.v=a.isect.v;ib.u=b.isect.u;ib.v=b.isect.v;if(!edge(ia)||!edge(ib))return false;
  if(std::abs(a.isect.t-b.isect.t)>5e-6f+2e-6f*std::abs(b.isect.t))return false;
  auto vertices=[&](int prim){auto v=scene.tri_vindex[prim];int o=scene.objects[a.isect.object].position_offset;return std::array<float3,3>{scene.tri_verts[o+v.x],scene.tri_verts[o+v.y],scene.tri_verts[o+v.z]};};
  auto va=vertices(a.isect.prim),vb=vertices(b.isect.prim);unsigned shared=0;for(auto x:va)for(auto y:vb)shared+=x.x==y.x&&x.y==y.y&&x.z==y.z;if(shared<2)return false;
  auto na=normalize(cross(va[1]-va[0],va[2]-va[0])),nb=normalize(cross(vb[1]-vb[0],vb[2]-vb[0]));if(!(len(na-nb)<=1e-6f))return false;
  auto pa=(1-a.isect.u-a.isect.v)*va[0]+a.isect.u*va[1]+a.isect.v*va[2];auto pb=(1-b.isect.u-b.isect.v)*vb[0]+b.isect.u*vb[1]+b.isect.v*vb[2];return len(pa-pb)<=1e-5f;
}
static void compare(WebGPUDevice &d,DeviceScene &scene,bool clay,const std::vector<Field>&layout,const std::vector<IntegratorStateCPU>&expected,device_vector<float>&film,const std::vector<float>&wanted,Metrics &m){
  auto actual=download(d,layout,expected.size());std::set<size_t> edge_hits;
  if(clay)for(size_t i=0;i<expected.size();i++)if(equivalent_clay_edge(scene,actual[i],expected[i])){edge_hits.insert(i);m.equivalent_edge_hits++;
    auto &a=actual[i].isect;auto &e=expected[i].isect;std::ostringstream record;record<<std::setprecision(10)<<"{\"stage\":"<<m.stages<<",\"path\":"<<i<<",\"object\":"<<a.object<<",\"gpuPrimitive\":"<<a.prim<<",\"cpuPrimitive\":"<<e.prim<<",\"gpuTuv\":["<<a.t<<','<<a.u<<','<<a.v<<"],\"cpuTuv\":["<<e.t<<','<<e.u<<','<<e.v<<"],\"equivalence\":\"shared edge; matching flat normal, shader and position; constant clay fixture only\"}";m.edge_records.push_back(record.str());}
  for(auto &f:layout)for(size_t i=0;i<expected.size();i++){
    if(edge_hits.contains(i)&&(f.name=="isect.prim"||f.name=="isect.u"||f.name=="isect.v"))continue;
    auto a=reinterpret_cast<const char*>(&actual[i])+f.offset;auto e=reinterpret_cast<const char*>(&expected[i])+f.offset;
    if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);
      if(std::abs(double(av)-ev)>5e-5+2e-4*std::abs(double(ev))){auto &a0=actual[i];auto &e0=expected[i];std::cerr<<std::setprecision(10)<<"Intersection diagnostic actual prim/object/t="<<a0.isect.prim<<'/'<<a0.isect.object<<'/'<<a0.isect.t<<" expected="<<e0.isect.prim<<'/'<<e0.isect.object<<'/'<<e0.isect.t<<" P="<<e0.ray.P.x<<','<<e0.ray.P.y<<','<<e0.ray.P.z<<" D="<<e0.ray.D.x<<','<<e0.ray.D.y<<','<<e0.ray.D.z<<" tmin="<<e0.ray.tmin<<" bounce="<<e0.path.bounce<<'\n';}
      number(av,ev,"stage "+std::to_string(m.stages)+" "+f.name+" path="+std::to_string(i),m);}}
    else{
      if(std::memcmp(a,e,f.stride)){auto &a0=actual[i];auto &e0=expected[i];
        std::cerr<<std::setprecision(10)<<"State diagnostic actual bounce/kernel/pdf="<<a0.path.bounce<<'/'<<a0.path.queued_kernel<<'/'<<a0.path.mis_ray_pdf
          <<" expected="<<e0.path.bounce<<'/'<<e0.path.queued_kernel<<'/'<<e0.path.mis_ray_pdf
          <<" prim/object="<<e0.isect.prim<<'/'<<e0.isect.object<<" actual D="<<a0.ray.D.x<<','<<a0.ray.D.y<<','<<a0.ray.D.z
          <<" expected D="<<e0.ray.D.x<<','<<e0.ray.D.y<<','<<e0.ray.D.z<<'\n';}
      require(!std::memcmp(a,e,f.stride),"stage "+std::to_string(m.stages)+" integer "+f.name+" path="+std::to_string(i));m.integers++;
    }
  }
  film.copy_from_device();healthy(d);for(size_t i=0;i<wanted.size();i++)number(film[i],wanted[i],"Stage "+std::to_string(m.stages)+" film "+std::to_string(i)+" pixel="+std::to_string(i/scene.data.film.pass_stride)+" offset="+std::to_string(i%scene.data.film.pass_stride),m);
  IntegratorQueueCounter counter{},wanted_counter{};for(auto &s:expected)if(s.path.queued_kernel)wanted_counter.num_queued[s.path.queued_kernel]++;
  d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));require(!std::memcmp(&counter,&wanted_counter,sizeof(counter)),"Render queue counters differ");m.stages++;
}
static void fixture(Scene &scene,const char *asset,bool attributes=false,bool images=false,bool normals=false){
  if(!asset)prepare(scene);
  else {
    scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.film->set_use_sample_count(false);
    scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);
  }
  scene.integrator->set_aa_samples(8);scene.integrator->set_min_bounce(3);scene.integrator->set_max_bounce(4);scene.integrator->set_max_diffuse_bounce(4);scene.integrator->set_max_transmission_bounce(4);
  scene.camera->set_fov(1.1f);scene.camera->set_matrix(transform_translate(make_float3(0,0,-10)));scene.camera->compute_auto_viewplane();
  // Open-front enclosure lets secondary rays hit other surfaces before escaping.
  auto g=make_unique<ShaderGraph>();auto diffuse=g->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.6,.65,.7));diffuse->set_roughness(.3f);g->connect(diffuse->output("BSDF"),g->output()->input("Surface"));
  if(images){auto uv=g->create_node<UVMapNode>();auto tex=g->create_node<ImageTextureNode>();ImageParams params;params.extension=EXTENSION_REPEAT;params.interpolation=INTERPOLATION_CUBIC;params.alpha_type=IMAGE_ALPHA_IGNORE;
    tex->handle=scene.image_manager->add_image(make_unique<FixtureImageLoader>(729,IMAGE_DATA_TYPE_BYTE4,13,9),params,false);tex->set_colorspace(u_colorspace_scene_linear);tex->set_alpha_type(IMAGE_ALPHA_IGNORE);
    g->connect(uv->output("UV"),tex->input("Vector"));g->connect(tex->output("Color"),diffuse->input("Color"));
    if(normals){auto normal=g->create_node<NormalMapNode>();normal->set_strength(.35f);normal->set_base(NODE_NORMAL_MAP_BASE_DISPLACED);g->connect(tex->output("Color"),normal->input("Color"));g->connect(normal->output("Normal"),diffuse->input("Normal"));}}
  else if(attributes){auto uv=g->create_node<UVMapNode>();auto checker=g->create_node<CheckerTextureNode>();
    checker->set_color1(make_float3(.8,.15,.09));checker->set_color2(make_float3(.12,.45,.8));checker->set_scale(5.3f);
    g->connect(uv->output("UV"),checker->input("Vector"));g->connect(checker->output("Color"),diffuse->input("Color"));}
  auto wall=scene.create_node<Shader>();wall->set_graph(std::move(g));wall->tag_update(&scene);
  if(asset){
    auto points=import_test_triangles(asset);auto model=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(wall);model->set_used_shaders(slots);model->resize_mesh(points.size(),points.size()/3);
    for(size_t i=0;i<points.size();i++){auto p=points[i];model->get_position_for_write()[i]=make_float3(p[0],p[2]-4,2-p[1]);model->get_triangles()[i]=i;}
    std::ranges::fill(model->get_smooth(),false);std::ranges::fill(model->get_shader(),0);model->tag_triangles_modified();scene.create_node<Object>()->set_geometry(model);
    // Rigid view transform toward the bench from above and its front-right side.
    const float3 origin=make_float3(4,-.6f,-6),target=make_float3(0,-2.7f,1);const float3 forward=normalize(target-origin);const float3 right=normalize(cross(make_float3(0,1,0),forward));const float3 up=cross(forward,right);Transform camera;
    camera.x=make_float4(right.x,up.x,forward.x,origin.x);camera.y=make_float4(right.y,up.y,forward.y,origin.y);camera.z=make_float4(right.z,up.z,forward.z,origin.z);
    scene.camera->set_matrix(camera);scene.camera->set_fov(.68f);scene.camera->compute_auto_viewplane();
  }
  auto mesh=scene.create_node<Mesh>();array<Node*> materials;materials.push_back_slow(wall);mesh->set_used_shaders(materials);mesh->resize_mesh(16,8);
  const float v[][3]={{-6,-4,-4},{6,-4,-4},{6,-4,8},{-6,-4,8},{-6,-4,8},{6,-4,8},{6,5,8},{-6,5,8},{-6,-4,-4},{-6,-4,8},{-6,5,8},{-6,5,-4},{6,-4,8},{6,-4,-4},{6,5,-4},{6,5,8}};
  for(unsigned i=0;i<16;i++)mesh->get_position_for_write()[i]=make_float3(v[i][0],v[i][1],v[i][2]);
  for(unsigned i=0;i<4;i++){const int tri[]={0,1,2,0,2,3};for(unsigned j=0;j<6;j++)mesh->get_triangles()[i*6+j]=i*4+tri[j];}
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(mesh);
  if(attributes){auto uv=mesh->attributes.add(ATTR_STD_UV)->data_for_write<float2>();const float corners[][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
    for(unsigned i=0;i<24;i++)uv[i]=make_float2(corners[i%6][0]+.17f*(i/6),corners[i%6][1]);}
  auto world=make_unique<ShaderGraph>();auto bg=world->create_node<BackgroundNode>();bg->set_color(make_float3(.25,.4,.6));bg->set_strength(.8f);world->connect(bg->output("Background"),world->output()->input("Surface"));auto shader=scene.create_node<Shader>();
#if defined(CBC_VOLUME_FIXTURE) && !defined(CBC_CLOSED_VOLUME_FIXTURE)
  auto absorption=world->create_node<AbsorptionVolumeNode>();absorption->set_color(make_float3(.8,.65,.4));absorption->set_density(.04f);
  ShaderOutput *volume=absorption->output("Volume");
  if(!std::getenv("CODEBRIX_VOLUME_ABSORPTION_ONLY")){
    auto scatter=world->create_node<ScatterVolumeNode>();scatter->set_color(make_float3(.8,.7,.6));scatter->set_density(.035f);scatter->set_anisotropy(std::getenv("CODEBRIX_VOLUME_G")?std::stof(std::getenv("CODEBRIX_VOLUME_G")):.45f);
    auto add=world->create_node<AddClosureNode>();world->connect(volume,add->input("Closure1"));world->connect(scatter->output("Volume"),add->input("Closure2"));volume=add->output("Closure");
  }
  world->connect(volume,world->output()->input("Volume"));scene.integrator->set_max_volume_bounce(4);
#endif
  shader->set_graph(std::move(world));shader->tag_update(&scene);scene.background->set_shader(shader);
#ifdef CBC_CLOSED_VOLUME_FIXTURE
  const unsigned count=std::getenv("CODEBRIX_VOLUME_CLOSED_COUNT")?std::stoul(std::getenv("CODEBRIX_VOLUME_CLOSED_COUNT")):4;
  require(count>=1&&count<=4,"Invalid closed volume fixture count");add_volume_boundaries(scene,std::getenv("CODEBRIX_VOLUME_CAMERA_INSIDE")!=nullptr,count);
#endif
  for(auto sh:scene.shaders)sh->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
}
static void write_image(const std::string &prefix,const std::vector<float>&film,unsigned samples,unsigned width=64,unsigned height=48,unsigned stride=4){
  std::ofstream(prefix+".film.bin",std::ios::binary).write(reinterpret_cast<const char*>(film.data()),film.size()*4);
  std::ofstream ppm(prefix+".ppm",std::ios::binary);ppm<<"P6\n"<<width<<' '<<height<<"\n255\n";
  for(unsigned i=0;i<width*height;i++)for(unsigned c=0;c<3;c++){float linear=std::max(0.f,film[((height-1-i/width)*width+i%width)*stride+c]/samples);float mapped=linear/(1+linear);unsigned char v=std::lround(255*std::pow(mapped,1/2.2f));ppm.write(reinterpret_cast<const char*>(&v),1);}
}
static void preview_render(WebGPUDevice &d,Scheduler &scheduler,RenderBuffers *buffers,const std::string &prefix,unsigned width,unsigned height,unsigned samples){
  buffers->zero();unsigned completed=0;const unsigned total=((width+15)/16)*((height+15)/16)*((samples+15)/16);
  for(unsigned y=0;y<height;y+=16)for(unsigned x=0;x<width;x+=16)for(unsigned sample=0;sample<samples;sample+=16){
    scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Preview reset failed");KernelWorkTile tile{};tile.x=x;tile.y=y;tile.w=std::min(16u,width-x);tile.h=std::min(16u,height-y);tile.stride=width;tile.num_samples=std::min(16u,samples-sample);tile.start_sample=sample;
    scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Preview camera failed: "+d.error_message());
    for(unsigned step=0;step<200;step++){auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<199,"Preview did not terminate");scheduler.enqueue_path_iteration(kernel);require(scheduler.update_queue_counter_and_cache(),"Preview iteration failed: "+d.error_message());}
    completed++;if(completed%64==0||completed==total)std::cerr<<"Preview tiles "<<completed<<'/'<<total<<'\n';
  }
  buffers->buffer.copy_from_device();healthy(d);std::vector<float> image(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
  require(std::all_of(image.begin(),image.end(),[](float v){return std::isfinite(v);}),"Preview contains nonfinite film values");write_image(prefix+".gpu",image,samples,width,height);
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2||argc==3||(argc==7&&std::string(argv[3])=="--preview"),"Usage: render_gate OUTPUT_PREFIX [--attributes | --images | --normals | TRIANGLES [--preview WIDTH HEIGHT SAMPLES]]");
  std::string prefix=argv[1];const bool normal_mode=argc==3&&std::string(argv[2])=="--normals";const bool image_mode=normal_mode||(argc==3&&std::string(argv[2])=="--images");const bool attribute_mode=image_mode||(argc==3&&std::string(argv[2])=="--attributes");
  const bool motion_mode=argc==3&&std::string(argv[2]).starts_with("--motion=");
  const unsigned motion_case=motion_mode?std::stoul(std::string(argv[2]).substr(9)):0;
  require(!motion_mode||motion_case<8,"Invalid motion case");
  const char *asset=argc>=3&&!attribute_mode&&!motion_mode?argv[2]:nullptr;const bool preview_mode=argc==7;
  const unsigned width=preview_mode?std::stoul(argv[4]):64,height=preview_mode?std::stoul(argv[5]):48,samples=preview_mode?std::stoul(argv[6]):8;
  require(width>0&&height>0&&width<=2048&&height<=2048&&samples>0&&samples<=1024,"Invalid render dimensions/samples");auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics m;Metrics image;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);fixture(scene,asset,attribute_mode,image_mode,normal_mode);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);scene.camera->compute_auto_viewplane();scene.integrator->set_aa_samples(samples);if(motion_mode)camera_motion_fixture(scene,scene.camera->get_matrix(),motion_case);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Render scene preparation failed");
    if(motion_mode)require(scene.dscene.data.cam.num_motion_steps>=2&&scene.dscene.data.cam.shuttertime!=-1,"Motion render preparation failed");
    auto &data=scene.dscene.data;data.background.use_mis=0;data.integrator.use_light_mis=0;data.integrator.use_direct_light=0;data.integrator.num_lights=0;data.integrator.use_caustics=0;data.integrator.filter_glossy=FLT_MAX;
    data.film.pass_flag=0;
#ifndef CBC_VOLUME_FIXTURE
    require(data.film.pass_stride==4,"Render requires combined-only film");
#else
    require(data.kernel_features&KERNEL_FEATURE_VOLUME,"Missing native world volume feature");
    std::cerr<<"Volume film stride="<<data.film.pass_stride<<" combined="<<data.film.pass_combined<<" scatter="<<data.film.pass_volume_scatter<<" transmit="<<data.film.pass_volume_transmit<<" majorant="<<data.film.pass_volume_majorant<<" count="<<data.film.pass_volume_majorant_sample_count<<'\n';
#endif
    d.const_copy_to("data",&data,sizeof(data));
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);auto layout=fields(d);
    BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=data.film.pass_stride;auto buffers=scheduler.get_render_buffers();buffers->reset(bp);
    if(preview_mode){preview_render(d,scheduler,buffers,prefix,width,height,samples);}else{
    std::vector<float> gold(buffers->buffer.size()),first;std::vector<IntegratorStateCPU> cpu(scheduler.capacity());
    for(unsigned repeat=0;repeat<2;repeat++){
      buffers->zero();upload(d,layout,std::vector<IntegratorStateCPU>(scheduler.capacity()));
      std::unique_ptr<RenderCapture> capture;if(repeat==1)capture=std::make_unique<RenderCapture>(d,prefix);
      for(unsigned sample=0;sample<samples;sample++){
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Render reset failed");
        KernelWorkTile tile{};tile.w=64;tile.h=48;tile.stride=64;tile.num_samples=1;tile.start_sample=sample;
        if(repeat==0){original_camera(scene.dscene,{tile},0,cpu,gold);for(unsigned step=0;step<100;step++){
          std::map<DeviceKernel,std::vector<int>> queues;for(size_t i=0;i<cpu.size();i++)if(cpu[i].path.queued_kernel)queues[DeviceKernel(cpu[i].path.queued_kernel)].push_back(i);
          if(queues.empty())break;
          require(step<99,"CPU render did not terminate");for(auto &[kernel,ids]:queues){oracle(kernel,scene.dscene,ids,cpu,gold);
#ifdef CBC_VOLUME_FIXTURE
          if(const char *target=std::getenv("CODEBRIX_VOLUME_TRACE"))if(std::find(ids.begin(),ids.end(),std::stoi(target))!=ids.end())volume_trace("cpu",sample,kernel,cpu);
#endif
          }
        }}
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Render camera failed: "+d.error_message());
        for(unsigned step=0;step<100;step++){
          auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<99,"GPU render did not terminate");
          std::vector<IntegratorStateCPU> expected;std::vector<float> wanted;std::vector<int> ids;
          if(repeat==0){expected=download(d,layout,scheduler.capacity());buffers->buffer.copy_from_device();wanted.assign(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
            for(size_t i=0;i<expected.size();i++){auto &s=expected[i];if(s.path.queued_kernel==kernel){ids.push_back(i);m.max_bounce=std::max(m.max_bounce,unsigned(s.path.bounce));m.max_transparent=std::max(m.max_transparent,unsigned(s.path.transparent_bounce));m.max_volume_bounds=std::max(m.max_volume_bounds,unsigned(s.path.volume_bounds_bounce));if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)m.shaders.insert(scene.dscene.tri_shader[s.isect.prim]&SHADER_MASK);}}
            oracle(kernel,scene.dscene,ids,expected,wanted);
            if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST||kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME)for(int i:ids)if(expected[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)expected[i].path.shader_sort_key=(scene.dscene.tri_shader[expected[i].isect.prim]&SHADER_MASK)+data.max_shaders*(i/d.state.sort_partition_divisor);
            if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME)for(int i:ids){auto next=expected[i].path.queued_kernel;
              if(next==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)m.volume_scatter++;
              else if(next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE||next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE||next==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND||next==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)m.volume_transmit++;
              else if(!next)m.volume_terminated++;
            }
            m.paths+=ids.size();m.kernels[kernel]+=ids.size();
          }
          std::unique_ptr<SurfaceDispatchCapture> surface_capture;
          if(repeat==0&&sample==0&&kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)
            if(const char *path=std::getenv("CODEBRIX_SURFACE_DISPATCH_CAPTURE"))surface_capture=std::make_unique<SurfaceDispatchCapture>(d,path);
          scheduler.enqueue_path_iteration(kernel);require(scheduler.update_queue_counter_and_cache(),"Render iteration failed: "+d.error_message());
#ifdef CBC_VOLUME_FIXTURE
          if(repeat==0)if(const char *target=std::getenv("CODEBRIX_VOLUME_TRACE"))if(std::find(ids.begin(),ids.end(),std::stoi(target))!=ids.end())volume_trace("gpu",sample,kernel,download(d,layout,scheduler.capacity()));
#endif
          if(surface_capture)surface_capture->finish(layout,expected,wanted);
          if(repeat==0)compare(d,scene.dscene,asset!=nullptr,layout,expected,buffers->buffer,wanted,m);
        }
        std::cerr<<"Rendered repeat="<<repeat<<" sample="<<sample<<" stages="<<m.stages<<'\n';
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(d.state.path.queued_kernel),scheduler.capacity(),width,height,samples,data.film.pass_stride);
      buffers->buffer.copy_from_device();healthy(d);std::vector<float> result(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      if(repeat==0){first=result;write_image(prefix+".gpu",result,samples,width,height,data.film.pass_stride);write_image(prefix+".cpu",gold,samples,width,height,data.film.pass_stride);for(size_t i=0;i<result.size();i++)number(result[i],gold[i],"Independent CPU image "+std::to_string(i),image);}
      else require(!std::memcmp(first.data(),result.data(),result.size()*4),"GPU render repeat differs");
    }
#ifdef CBC_VOLUME_FIXTURE
    require(m.kernels[DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME]>0&&m.volume_transmit>0,"Missing volume transmission coverage");
    require(std::getenv("CODEBRIX_VOLUME_ABSORPTION_ONLY")?m.volume_scatter==0:m.volume_scatter>0,"Incorrect volume scattering coverage");
#endif
#ifdef CBC_CLOSED_VOLUME_FIXTURE
    require(m.max_volume_bounds>=2,"Missing closed-volume boundary coverage");
    require((m.kernels[DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK]>0)==(std::getenv("CODEBRIX_VOLUME_CAMERA_INSIDE")!=nullptr),"Incorrect native camera-inside volume initialization coverage");
#endif
    require(m.max_bounce>=3,"Missing multiple bounce coverage");if(!asset){require(m.max_transparent>=1,"Missing transparent bounce coverage");require(m.shaders.size()>=10,"Insufficient compiled shader coverage");}
    }
  }
  require(stats.mem_used==0,"Render Cycles allocation leak");std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  {std::ofstream edges(prefix+".shared-edges.json");edges<<'[';for(size_t i=0;i<m.edge_records.size();i++){if(i)edges<<',';edges<<m.edge_records[i];}edges<<"]\n";}
  if(preview_mode)std::cout<<"{\"passed\":true,\"nativeSceneAndScheduler\":true,\"highResolutionPreviewOnly\":true,\"materialSubstitution\":\"rough diffuse clay\",\"width\":"<<width<<",\"height\":"<<height<<",\"samples\":"<<samples<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  else std::cout<<std::setprecision(17)<<"{\"passed\":true,\"nativeSceneAndScheduler\":true,\"independentCpuImage\":true,\"repeatBitwiseIdentical\":true,\"nativeImageTextures\":"<<(image_mode?"true":"false")<<",\"nativeNormalMaps\":"<<(normal_mode?"true":"false")<<",\"width\":64,\"height\":48,\"samples\":"<<samples<<",\"stages\":"<<m.stages<<",\"paths\":"<<m.paths<<",\"volumeScatterEvents\":"<<m.volume_scatter<<",\"volumeTransmitEvents\":"<<m.volume_transmit<<",\"volumeTerminatedEvents\":"<<m.volume_terminated<<",\"volumeStackInitializations\":"<<m.kernels[DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK]<<",\"maxVolumeBoundaryBounce\":"<<m.max_volume_bounds<<",\"maxBounce\":"<<m.max_bounce<<",\"maxTransparentBounce\":"<<m.max_transparent<<",\"surfaceShaders\":"<<m.shaders.size()<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"equivalentSharedEdgeComparisons\":"<<m.equivalent_edge_hits<<",\"strictStageFailures\":"<<m.strict<<",\"maximumStageError\":"<<m.maximum<<",\"maximumStageErrorField\":\""<<m.maximum_field<<"\",\"maximumStageErrorActual\":"<<m.maximum_actual<<",\"maximumStageErrorExpected\":"<<m.maximum_expected<<",\"maximumStagePracticalToleranceRatio\":"<<m.maximum_bound_ratio<<",\"strictImageFailures\":"<<image.strict<<",\"maximumImageError\":"<<image.maximum<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
