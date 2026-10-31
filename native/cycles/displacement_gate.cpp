// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Displacement gate: DEVICE_KERNEL_SHADER_EVAL_DISPLACE on the WebGPU device (shader_eval_displace,
// SHADER_EVAL_DISPLACE) against the unmodified kernel_displace_evaluate (bake/bake.h) on the same
// device-prepared scene, at the three corners and the centre of every triangle of every displaced
// grid. The scenes are prepared on the WebGPU device, so GeometryManager::displace has already
// displaced the grids through the same kernel before the BVH build (the gate also checks that the
// vertices moved). Cases: Displacement node height in object space (rotated, scaled object) and in
// world space (flat grid), Vector Displacement in object, world and tangent space (without a UV map:
// dPdu; with a UV map: its tangent and sign attributes), and displacement method Both.
#include "wavefront_test.h"
#include "reference_policy.h"
#include "environment_reference.h"
#include "integrator/shader_eval.h"
#include "scene/integrator.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
struct Case {const char *name;int node;NodeNormalMapSpace space;DisplacementMethod method;bool smooth,uv,transform;};
// node: 0 Displacement (noise height), 1 Vector Displacement (wave colour).
static const Case cases[]={
  {"height-object-transformed",0,NODE_NORMAL_MAP_OBJECT,DISPLACE_TRUE,true,false,true},
  {"height-world-flat",0,NODE_NORMAL_MAP_WORLD,DISPLACE_TRUE,false,false,false},
  {"vector-object",1,NODE_NORMAL_MAP_OBJECT,DISPLACE_TRUE,true,false,true},
  {"vector-world",1,NODE_NORMAL_MAP_WORLD,DISPLACE_TRUE,true,false,false},
  {"vector-tangent-dpdu",1,NODE_NORMAL_MAP_TANGENT,DISPLACE_TRUE,true,false,true},
  {"vector-tangent-uv",1,NODE_NORMAL_MAP_TANGENT,DISPLACE_TRUE,true,true,true},
  {"height-object-both",0,NODE_NORMAL_MAP_OBJECT,DISPLACE_BOTH,true,false,true},
};
constexpr unsigned case_count=sizeof(cases)/sizeof(cases[0]);
constexpr int grid_n=12;
static Shader *displacement_shader(Scene &scene,const Case &c){
  auto g=make_unique<ShaderGraph>();auto d=g->create_node<DiffuseBsdfNode>();d->set_color(make_float3(.6f,.5f,.4f));
  g->connect(d->output("BSDF"),g->output()->input("Surface"));
  if(c.node==0){auto noise=g->create_node<NoiseTextureNode>();noise->set_scale(2.5f);noise->set_detail(2.f);
    auto disp=g->create_node<DisplacementNode>();disp->set_space(c.space);disp->set_scale(.45f);disp->set_midlevel(.5f);
    g->connect(noise->output("Fac"),disp->input("Height"));g->connect(disp->output("Displacement"),g->output()->input("Displacement"));}
  else{auto wave=g->create_node<WaveTextureNode>();wave->set_scale(1.7f);wave->set_distortion(1.5f);
    auto disp=g->create_node<VectorDisplacementNode>();disp->set_space(c.space);disp->set_scale(.3f);disp->set_midlevel(.4f);
    if(c.uv)disp->set_attribute(ustring("UVMap"));
    g->connect(wave->output("Color"),disp->input("Vector"));g->connect(disp->output("Displacement"),g->output()->input("Displacement"));}
  auto s=scene.create_node<Shader>();s->set_graph(std::move(g));s->set_displacement_method(c.method);s->tag_update(&scene);return s;
}
static Object *grid(Scene &scene,Shader *shader,const Case &c,std::vector<float3> &authored){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);
  const int n=grid_n;mesh->resize_mesh((n+1)*(n+1),n*n*2);auto P=mesh->get_position_for_write();
  for(int j=0;j<=n;j++)for(int i=0;i<=n;i++)P[j*(n+1)+i]=make_float3(2.6f*(float(i)/n-.5f),.1f*std::sin(.7f*i),2.6f*(float(j)/n-.5f));
  authored.assign(P,P+(n+1)*(n+1));
  auto tris=mesh->get_triangles().data();int t=0;
  for(int j=0;j<n;j++)for(int i=0;i<n;i++){const int a=j*(n+1)+i;const int q[6]={a,a+n+1,a+1,a+1,a+n+1,a+n+2};for(int k=0;k<6;k++)tris[t++]=q[k];}
  std::ranges::fill(mesh->get_smooth(),c.smooth);std::ranges::fill(mesh->get_shader(),0);
  if(c.uv){auto uv=mesh->attributes.add(ATTR_STD_UV,ustring("UVMap"))->data_for_write<float2>();
    for(int k=0;k<n*n*6;k++){const float3 p=authored[tris[k]];uv[k]=make_float2(p.x/2.6f+.5f,p.z/2.6f+.5f);}}
  mesh->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(mesh);
  if(c.transform)object->set_tfm(transform_translate(make_float3(.2f,-1.4f,.4f))*transform_rotate(.5f,make_float3(0,1,0))*transform_scale(make_float3(1.1f,.8f,.9f)));
  return object;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  reference_policy::use_reference_oracle_arithmetic();
  require(argc==2,"Usage: displacement_gate OUTPUT_DIRECTORY");
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"displacement_gate device creation");
  uint64_t total_checks=0,total_strict=0,moved_total=0;double total_maximum=0;std::ostringstream reports;
  for(unsigned index=0;index<case_count;index++){
    const Case &c=cases[index];std::cerr<<"displacement case="<<index<<" "<<c.name<<"\n";
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;
    Scene scene(params,&device);scene.integrator->set_use_light_tree(false);std::vector<float3> authored;
    auto object=grid(scene,displacement_shader(scene,c),c,authored);
    Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Displacement scene preparation failed: "+progress.get_error_message());
    auto mesh=static_cast<Mesh*>(object->get_geometry());uint64_t moved=0;
    for(size_t i=0;i<authored.size();i++)moved+=len(make_float3(mesh->get_position()[i])-authored[i])>1e-4f;
    require(moved>authored.size()/2,"Displacement did not move the grid vertices");
    require(c.method!=DISPLACE_BOTH||mesh->attributes.find(ATTR_STD_POSITION_UNDISPLACED)!=nullptr,"Missing the undisplaced position attribute");
    std::vector<KernelShaderEvalInput> inputs;const float corners[4][2]={{0,0},{1,0},{0,1},{1.f/3,1.f/3}};
    for(size_t t=0;t<mesh->num_triangles();t++)for(auto &uv:corners){KernelShaderEvalInput in;in.object=object->get_device_index();in.prim=int(mesh->prim_offset+t);in.u=uv[0];in.v=uv[1];inputs.push_back(in);}
    const auto expected=original_displace_evaluate(scene.dscene,inputs);std::vector<float> actual;
    ShaderEval eval(&device,progress);
    require(eval.eval(SHADER_EVAL_DISPLACE,int(inputs.size()),3,[&](device_vector<KernelShaderEvalInput> &in){std::copy(inputs.begin(),inputs.end(),in.data());return int(inputs.size());},
      [&](device_vector<float> &o){actual.assign(o.data(),o.data()+o.size());}),"Displacement evaluation failed");
    healthy(device);require(actual.size()==expected.size(),"Displacement evaluation size");
    uint64_t strict=0,nonzero=0;double maximum=0;
    for(size_t i=0;i<actual.size();i++){const double e=expected[i],a=actual[i],error=std::abs(a-e);maximum=std::max(maximum,error);strict+=error>1e-7+1e-7*std::abs(e);nonzero+=e!=0;
      require(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(e),std::string("Displacement case ")+c.name+" component="+std::to_string(i)+" actual="+std::to_string(a)+" expected="+std::to_string(e));}
    require(nonzero>actual.size()/2,"Displacement offsets are mostly zero");
    total_checks+=actual.size();total_strict+=strict;total_maximum=std::max(total_maximum,maximum);moved_total+=moved;
    if(reports.tellp()>0)reports<<',';
    reports<<std::setprecision(17)<<"{\"case\":"<<index<<",\"name\":\""<<c.name<<"\",\"inputs\":"<<inputs.size()<<",\"movedVertices\":"<<moved<<",\"floatChecks\":"<<actual.size()<<",\"strict\":"<<strict<<",\"maximumAbsoluteError\":"<<maximum<<"}";
    std::cerr<<"Passed displacement case "<<index<<" ("<<inputs.size()<<" inputs, strict "<<strict<<", max "<<maximum<<")\n";
  }
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"cases\":"<<case_count<<",\"floatChecks\":"<<total_checks<<",\"strictDifferences\":"<<total_strict<<",\"maximumAbsoluteError\":"<<total_maximum
           <<",\"movedVertices\":"<<moved_total<<",\"originalCpuKernelDisplaceEvaluate\":true,\"details\":["<<reports.str()<<"]}\n";
}catch(const std::exception &error){std::cerr<<"displacement_gate: "<<error.what()<<"\n";status=1;}
TaskScheduler::exit();return status;}
