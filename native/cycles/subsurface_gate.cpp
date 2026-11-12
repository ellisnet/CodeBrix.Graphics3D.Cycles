// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE on the WebGPU device against the unchanged pinned CPU kernel:
// random walks from points on a closed sphere into it (the random walk, the skin model and the legacy random walk,
// several anisotropies and radii), every path state field and the queue counters compared. Christensen-Burley disk
// paths (subsurface_disk) on the sphere, on the sphere with four concentric shells inside it (probes crossing more
// than BSSRDF_MAX_HITS surfaces: the lcg_state reservoir picks the hits), on an instanced object (transform not
// applied) and on a negatively scaled object; the exit hit (object, prim, type, u, v, t) is an integer-exact field.
// Both methods on a moving object (object motion, five transforms) and on a deforming one (motion triangles), at ray
// times 0.35 to 0.65, and inside a scene with a volume box that cuts the sphere: integrator_volume_stack_update_for_subsurface
// enters or leaves the box on the segment from the entry to the exit point (every volume stack slot is compared). Both
// methods on a caustics receiver with use_caustics: subsurface_scatter queues the exits for INTERSECT_MNEE (unsorted).
#include "wavefront_test.h"
#include "subsurface_reference.h"
#include "curve_gate_tuft.h"
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/film.h"
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
struct Metrics {uint64_t floats=0,integers=0,strict=0,exits=0,terminated=0,practical_failures=0,nonfinite=0;unsigned cases=0;double maximum=0,ratio=0;std::string field;};
static void number(float a,float e,const std::string &field,Metrics &m){
  const double error=std::abs(double(a)-e);m.floats++;m.strict+=error>1e-7+1e-7*std::abs(double(e));
  m.nonfinite+=!(std::isfinite(a)&&std::isfinite(e));
  if(!(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)))){m.practical_failures++;if(m.practical_failures<=20)std::cerr<<"practical "<<field<<" actual "<<std::setprecision(9)<<a<<" expected "<<e<<'\n';}
  if(error>m.maximum){m.maximum=error;m.field=field;}m.ratio=std::max(m.ratio,error/(5e-5+2e-4*std::abs(double(e))));
}
struct Case {ClosureType method;float anisotropy;float3 radius;float3 albedo;int shells=0;int transform=0;int motion=0;int volume=0;int caustics=0;};
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: subsurface_gate OUTPUT_PREFIX");const std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"subsurface_gate device creation");Metrics m;
  // The Bssrdf radius and albedo as subsurface_bounce stores them (after bssrdf_setup_radius).
  const Case cases[]={{CLOSURE_BSSRDF_RANDOM_WALK_ID,0,make_float3(.3f,.135f,.075f),make_float3(.8f,.55f,.45f)},
                      {CLOSURE_BSSRDF_RANDOM_WALK_ID,.6f,make_float3(.36f,.3f,.18f),make_float3(.9f,.75f,.45f)},
                      {CLOSURE_BSSRDF_RANDOM_WALK_ID,-.3f,make_float3(.25f,.22f,.2f),make_float3(.88f,.87f,.84f)},
                      {CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID,0,make_float3(.2f,.08f,.05f),make_float3(.8f,.55f,.45f)},
                      {CLOSURE_BSSRDF_RANDOM_WALK_LEGACY_ID,.2f,make_float3(.03f,.02f,.011f),make_float3(.85f,.7f,.55f)},
                      {CLOSURE_BSSRDF_RANDOM_WALK_LEGACY_ID,-.3f,make_float3(.03f,.02f,.011f),make_float3(.85f,.7f,.55f)},
                      {CLOSURE_BSSRDF_RANDOM_WALK_ID,.3f,make_float3(.01f,.008f,.004f),make_float3(.6f,.5f,.4f)},
                      {CLOSURE_BSSRDF_RANDOM_WALK_ID,0,make_float3(4,2.5f,1.5f),make_float3(.95f,.9f,.2f)},
                      // Christensen-Burley: the Bssrdf radius after bssrdf_setup_radius (mean free path times 0.25/pi).
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.05f,.03f,.02f),make_float3(.8f,.55f,.45f)},
                      {CLOSURE_BSSRDF_BURLEY_ID,.4f,make_float3(.1f,.06f,.02f),make_float3(.9f,.75f,.45f),1},
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.2f,0,.1f),make_float3(.85f,.7f,.55f),1},
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.04f,.03f,.01f),make_float3(.8f,.55f,.45f),0,1},
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.08f,.05f,.03f),make_float3(.8f,.55f,.45f),1,2},
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.6f,.4f,.3f),make_float3(.6f,.5f,.4f)},
                      // Motion blur: object motion (1) and deformation motion (2); volumes: a volume box across the sphere (1).
                      {CLOSURE_BSSRDF_RANDOM_WALK_ID,.2f,make_float3(.36f,.3f,.18f),make_float3(.9f,.75f,.45f),0,0,1},
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.1f,.06f,.02f),make_float3(.9f,.75f,.45f),0,0,1},
                      {CLOSURE_BSSRDF_RANDOM_WALK_ID,0,make_float3(.3f,.135f,.075f),make_float3(.8f,.55f,.45f),0,0,2},
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.08f,.05f,.03f),make_float3(.8f,.55f,.45f),1,0,2},
                      {CLOSURE_BSSRDF_RANDOM_WALK_ID,.3f,make_float3(.5f,.4f,.3f),make_float3(.9f,.8f,.6f),0,0,0,1},
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.3f,.2f,.1f),make_float3(.85f,.7f,.55f),0,0,0,1},
                      // MNEE caustics: the sphere is a caustics receiver, so its exits go to INTERSECT_MNEE (unsorted).
                      {CLOSURE_BSSRDF_RANDOM_WALK_ID,0,make_float3(.3f,.135f,.075f),make_float3(.8f,.55f,.45f),0,0,0,0,1},
                      {CLOSURE_BSSRDF_BURLEY_ID,0,make_float3(.1f,.06f,.02f),make_float3(.9f,.75f,.45f),0,0,0,0,1}};
  uint64_t disk_paths=0,disk_over_max=0,disk_without_hits=0,disk_exits=0,disk_later_hit=0,volume_updates=0,motion_exits=0,mnee_exits=0;
  for(unsigned test=0;test<sizeof(cases)/sizeof(cases[0]);test++){
    const Case &c=cases[test];
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;gate_curve_params(params);Scene scene(params,&d);
    scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(16);
    scene.film->set_use_sample_count(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
    auto g=make_unique<ShaderGraph>();auto sss=g->create_node<SubsurfaceScatteringNode>();sss->set_method(c.method);
    g->connect(sss->output("BSSRDF"),g->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->tag_update(&scene);
    // A closed UV sphere (radius 1, smooth), its transform applied; with shells, four concentric spheres inside it
    // (radii .85, .7, .55, .4) in the same mesh.
    auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);
    const int stacks=8,slices=12;std::vector<float3> verts;std::vector<int> tris;
    for(int shell=0;shell<=4*c.shells;shell++){const float scale=1-.15f*shell;const int first=int(verts.size());
      for(int i=0;i<=stacks;i++)for(int j=0;j<=slices;j++){const float th=M_PI_F*i/stacks,ph=2*M_PI_F*j/slices;
        verts.push_back(scale*make_float3(sinf(th)*cosf(ph),cosf(th),sinf(th)*sinf(ph)));}
      for(int i=0;i<stacks;i++)for(int j=0;j<slices;j++){const int a=first+i*(slices+1)+j,b=a+slices+1;
        if(i>0)for(int v:{a,b,a+1})tris.push_back(v);
        if(i<stacks-1)for(int v:{a+1,b,b+1})tris.push_back(v);}}
    mesh->resize_mesh(verts.size(),tris.size()/3);for(size_t i=0;i<verts.size();i++)mesh->get_position_for_write()[i]=verts[i];
    for(size_t i=0;i<tris.size();i++)mesh->get_triangles()[i]=tris[i];
    for(size_t t=0;t<tris.size()/3;t++){mesh->get_shader()[t]=0;mesh->get_smooth()[t]=true;}
    mesh->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(mesh);
    // transform 1: a rotated, non-uniformly scaled instance of a mesh two objects use (SD_OBJECT_TRANSFORM_APPLIED unset);
    // transform 2: a single-user object with a negative scale (applied, SD_OBJECT_NEGATIVE_SCALE).
    Transform tfm=transform_identity();
    if(c.transform==1){tfm=transform_rotate(.6f,normalize(make_float3(.3f,1,.2f)))*transform_scale(1.25f,.8f,1.1f);object->set_tfm(tfm);
      auto twin=scene.create_node<Object>();twin->set_geometry(mesh);twin->set_tfm(transform_translate(6,0,0));}
    if(c.transform==2){tfm=transform_scale(-1.2f,1,.9f);object->set_tfm(tfm);}
    const Transform itfm=transform_inverse(tfm);
    if(c.caustics)object->set_is_caustics_receiver(true);
    if(c.motion==1)object_motion_fixture(scene,*object,2);
    if(c.motion==2)deforming_motion_fixture(scene,*object,1);
    // A volume box x in [.15, 2.5] (absorption), cutting the sphere: the sphere intersects a volume.
    Shader *medium=nullptr;Object *box=nullptr;
    if(c.volume){auto vg=make_unique<ShaderGraph>();auto absorption=vg->create_node<AbsorptionVolumeNode>();absorption->set_color(make_float3(.8f,.65f,.4f));absorption->set_density(.3f);
      vg->connect(absorption->output("Volume"),vg->output()->input("Volume"));medium=scene.create_node<Shader>();medium->set_graph(std::move(vg));medium->tag_update(&scene);
      auto cube=scene.create_node<Mesh>();array<Node*> cs;cs.push_back_slow(medium);cube->set_used_shaders(cs);
      const float3 lo=make_float3(.15f,-2.5f,-2.5f),hi=make_float3(2.5f,2.5f,2.5f);cube->resize_mesh(8,12);
      for(int k=0;k<8;k++)cube->get_position_for_write()[k]=make_float3(k&1?hi.x:lo.x,k&2?hi.y:lo.y,k&4?hi.z:lo.z);
      const int faces[]={0,2,1,1,2,3, 4,5,6,5,7,6, 0,1,4,1,5,4, 2,6,3,3,6,7, 0,4,2,2,4,6, 1,3,5,3,7,5};
      for(int k=0;k<36;k++){cube->get_triangles()[k]=faces[k];}
      for(int k=0;k<12;k++){cube->get_shader()[k]=0;cube->get_smooth()[k]=false;}
      cube->tag_triangles_modified();box=scene.create_node<Object>();box->set_geometry(cube);}
    gate_curve_tufts(scene);
    for(auto s:scene.shaders)s->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Subsurface scene preparation failed");auto &data=scene.dscene.data;
    require(data.kernel_features&KERNEL_FEATURE_SUBSURFACE,"Subsurface fixture without KERNEL_FEATURE_SUBSURFACE");
    if(c.motion==1)require(scene.dscene.object_flag[object->get_device_index()]&SD_OBJECT_MOTION,"Subsurface object motion case without a moving object");
    if(c.motion==2)require(scene.dscene.object_flag[object->get_device_index()]&SD_OBJECT_HAS_VERTEX_MOTION,"Subsurface deformation case without vertex motion");
    if(c.volume)require((data.kernel_features&KERNEL_FEATURE_VOLUME)&&data.integrator.use_volumes&&(scene.dscene.object_flag[object->get_device_index()]&SD_OBJECT_INTERSECTS_VOLUME),
                        "Subsurface volume case without a sphere that intersects a volume");
    data.integrator.use_caustics=c.caustics;data.film.pass_flag=0;
    if(c.caustics)require(scene.dscene.object_flag[object->get_device_index()]&SD_OBJECT_CAUSTICS_RECEIVER,"Subsurface caustics case without a receiver");
    d.const_copy_to("data",&data,sizeof(data));bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
    const auto layout=fields(d);std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> ids;
    const int prims=int(tris.size()/3);
    for(unsigned j=0;j<512;j++){unsigned i=3*j;require(i<states.size(),"Subsurface fixture capacity");ids.push_back(i);auto &s=states[i];
      const int prim=int(j*7)%prims;const float3 A=verts[tris[3*prim]],B=verts[tris[3*prim+1]],C=verts[tris[3*prim+2]];
      const float u=.05f+.9f*float((j*37)%101)/101.f*.5f,v=.05f+.9f*float((j*53)%97)/97.f*.5f;
      const float3 P0=u*A+v*B+(1-u-v)*C,N0=normalize(cross(B-A,C-A));
      const float3 P=c.transform?transform_point(&tfm,P0):P0;const float3 Ng=c.transform?normalize(transform_direction_transposed(&itfm,N0)):N0;
      const float3 tangent=normalize(cross(Ng,fabsf(Ng.x)<.9f?make_float3(1,0,0):make_float3(0,1,0)));
      const float tilt=.95f*sinf(.37f*j);s.ray.D=normalize(-Ng+tilt*tangent);s.ray.P=P;s.ray.tmin=0;s.ray.tmax=FLT_MAX;s.ray.time=c.motion?.35f+.05f*float(j%7):.5f;s.ray.dP=.0001f*(j%7);s.ray.dD=0;
      // The volume stack: empty, or the box for an entry point inside it; every slot set (fields() compares them all).
      for(int k=0;k<MAX_VOLUME_STACK_SIZE;k++){s.volume_stack[k].object=OBJECT_NONE;s.volume_stack[k].shader=SHADER_NONE;}
      if(c.volume&&P.x>.15f){s.volume_stack[0].object=int(box->get_device_index());s.volume_stack[0].shader=scene.shader_manager->get_shader_id(medium);}
      s.isect.object=0;s.isect.prim=prim;s.isect.type=PRIMITIVE_TRIANGLE;s.isect.t=1;s.isect.u=u;s.isect.v=v;
      const bool disk=c.method==CLOSURE_BSSRDF_BURLEY_ID;
      s.subsurface.N=(j%5==0&&!disk)?normalize(Ng+make_float3(.05f,-.03f,.02f)):Ng;s.subsurface.albedo=c.albedo;s.subsurface.radius=c.radius*(1+.25f*float(j%4));
      s.subsurface.anisotropy=c.method==CLOSURE_BSSRDF_RANDOM_WALK_ID?c.anisotropy:c.anisotropy+2;
      s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE;s.path.render_pixel_index=j;s.path.sample=j*137u;s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;
      s.path.rng_offset=uint16_t(16u*(1+j%5));s.path.bounce=j%3;
      s.path.flag=(disk?PATH_RAY_SUBSURFACE_DISK:PATH_RAY_SUBSURFACE_RANDOM_WALK|PATH_RAY_DIFFUSE_ANCESTOR)|(j%6==0?PATH_RAY_SUBSURFACE_BACKFACING:0);
      s.path.visibility=PATH_RAY_VISIBILITY_DIFFUSE;s.path.throughput=make_float3(.6f,.8f,.4f);s.path.continuation_probability=1;s.path.min_ray_pdf=FLT_MAX;
    }
    std::vector<DiskHits> disk;if(c.method==CLOSURE_BSSRDF_BURLEY_ID)disk=original_disk_hits(scene.dscene,ids,states);
    for(const auto &hits:disk){disk_paths++;disk_over_max+=hits.count>BSSRDF_MAX_HITS;disk_without_hits+=hits.count==0;}
    auto expected=states;original_subsurface(scene.dscene,ids,expected);
    // Exits whose volume stack the pinned kernel changed (the box entered or left), and exits on a moving object.
    {uint64_t routed=0;for(auto i:ids)routed+=expected[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE;
      require(c.caustics?routed>0:routed==0,"Subsurface MNEE case without MNEE exits, or MNEE exits without a caustics receiver");mnee_exits+=routed;}
    for(auto i:ids){const auto &e=expected[i];if(e.path.queued_kernel!=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)continue;motion_exits+=c.motion!=0;
      if(c.volume)for(int k=0;k<MAX_VOLUME_STACK_SIZE;k++)if(e.volume_stack[k].object!=states[i].volume_stack[k].object||e.volume_stack[k].shader!=states[i].volume_stack[k].shader){volume_updates++;break;}}
    // The sorted hit the pinned kernel's resampling selected for each disk exit (its prim and t are the exit's isect).
    for(size_t k=0;k<disk.size();k++){const auto &s=expected[ids[k]];if(s.path.queued_kernel!=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)continue;disk_exits++;int at=-1;
      for(size_t h=0;h<disk[k].t.size();h++)if(disk[k].prim[h]==s.isect.prim&&disk[k].t[h]==s.isect.t){at=int(h);break;}
      require(at>=0,"Christensen-Burley exit hit not among the probe's hits");disk_later_hit+=at>0;}
    for(auto i:ids){auto &s=expected[i];if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
        s.path.shader_sort_key=(scene.dscene.tri_shader[s.isect.prim]&SHADER_MASK)+data.max_shaders*(i/d.state.sort_partition_divisor);m.exits++;}
      else if(!s.path.queued_kernel)m.terminated++;}
    upload(d,layout,states);
    device_vector<int> selected(&d,"subsurface_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
    IntegratorQueueCounter count{};count.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&count,sizeof(count));
    for(auto kernel:{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE})if(d.state.sort_key_counter[kernel])d.zero(uintptr_t(d.state.sort_key_counter[kernel]),d.remaining(uintptr_t(d.state.sort_key_counter[kernel])));
    auto queue=d.gpu_queue_create();int n=ids.size();DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&n);
    require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE,n,args),d.error_message());require(queue->synchronize(),d.error_message());
    const auto actual=download(d,layout,states.size());
    for(const auto &field:layout)for(int i:ids){
      auto a=reinterpret_cast<const char*>(&actual[i])+field.offset;auto e=reinterpret_cast<const char*>(&expected[i])+field.offset;
      const std::string label="case="+std::to_string(test)+" "+field.name+" path="+std::to_string(i);
      if(field.floating)for(size_t k=0;k<field.stride;k+=4){float x,y;std::memcpy(&x,a+k,4);std::memcpy(&y,e+k,4);number(x,y,label+" component="+std::to_string(k/4),m);}
      else{if(std::memcmp(a,e,field.stride)){std::cerr<<label<<" integer mismatch\n";m.practical_failures++;}m.integers++;}
    }
    IntegratorQueueCounter expected_count{};for(int i:ids)if(expected[i].path.queued_kernel)expected_count.num_queued[expected[i].path.queued_kernel]++;
    d.download(uintptr_t(d.state.queue_counter),&count,sizeof(count));require(!std::memcmp(&count,&expected_count,sizeof(count)),"Subsurface queue counter mismatch");m.cases++;
    std::cerr<<"Subsurface case "<<test<<" exits "<<m.exits<<" practical failures so far "<<m.practical_failures<<'\n';
  }
  require(m.exits>0&&m.terminated>0,"Missing subsurface events");require(volume_updates>0&&motion_exits>0&&mnee_exits>0,"Missing subsurface volume stack updates, motion exits or MNEE exits");require(disk_over_max>0&&disk_paths>disk_without_hits&&disk_later_hit>0,"Missing Christensen-Burley reservoir or resampling events");
  std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  const bool passed=m.practical_failures==0;
  std::cout<<std::setprecision(17)<<"{\"passed\":"<<(passed?"true":"false")<<",\"originalCpuSubsurfaceKernel\":true,\"cases\":"<<m.cases<<",\"paths\":"<<m.cases*512<<",\"exits\":"<<m.exits<<",\"terminated\":"<<m.terminated<<",\"diskPaths\":"<<disk_paths<<",\"diskProbesOverMaxHits\":"<<disk_over_max<<",\"diskProbesWithoutHits\":"<<disk_without_hits<<",\"diskExits\":"<<disk_exits<<",\"diskExitsAfterFirstHit\":"<<disk_later_hit<<",\"volumeStackUpdates\":"<<volume_updates<<",\"motionExits\":"<<motion_exits<<",\"mneeExits\":"<<mnee_exits
           <<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strictFailures\":"<<m.strict<<",\"practicalFailures\":"<<m.practical_failures<<",\"nonfiniteValues\":"<<m.nonfinite<<",\"maximumError\":"<<m.maximum
           <<",\"maximumErrorField\":\""<<m.field<<"\",\"maximumPracticalToleranceRatio\":"<<m.ratio<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  if(!passed)status=1;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
