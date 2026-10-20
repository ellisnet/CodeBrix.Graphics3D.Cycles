// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "pipelines.h"
#include "kernel/svm/types.h"
#include "kernel/svm/node_types.h"
#include "kernel/types.h"
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ccl {
const std::vector<ShaderFeatureName> &shader_feature_names(){
  static const std::vector<ShaderFeatureName> names={
    {"SVM_NODE_GEOMETRY",SF_GEOMETRY},{"SVM_NODE_TEX_COORD",SF_TEX_COORD},{"SVM_NODE_IMAGE",SF_IMAGE},
    {"SVM_NODE_ATTR",SF_ATTR},{"SVM_NODE_NORMAL_MAP",SF_NORMAL_MAP},{"SVM_NODE_TANGENT",SF_TANGENT},
    {"SVM_NODE_VERTEX_COLOR",SF_VERTEX_COLOR},{"SVM_NODE_MATH",SF_MATH},{"SVM_NODE_VECTOR_MATH",SF_VECTOR_MATH},
    {"SVM_NODE_CLAMP",SF_CLAMP},{"SVM_NODE_CONVERT",SF_CONVERT},{"SVM_NODE_SEPARATE_VECTOR",SF_SEPARATE_VECTOR},
    {"SVM_NODE_COMBINE_VECTOR",SF_COMBINE_VECTOR},{"SVM_NODE_CLOSURE_VOLUME",SF_CLOSURE_VOLUME},
    {"SVM_NODE_CLOSURE_HOLDOUT",SF_CLOSURE_HOLDOUT},{"SVM_NODE_CLOSURE_SET_NORMAL",SF_CLOSURE_SET_NORMAL},
    {"SVM_NODE_TEX_CHECKER",SF_TEX_CHECKER},{"SVM_NODE_TEX_GRADIENT",SF_TEX_GRADIENT},{"SVM_NODE_GAMMA",SF_GAMMA},
    {"SVM_NODE_INVERT",SF_INVERT},{"SVM_NODE_BRIGHTCONTRAST",SF_BRIGHTCONTRAST},
    {"SVM_BSDF_DIFFUSE",SF_BSDF_DIFFUSE},{"SVM_BSDF_TRANSLUCENT",SF_BSDF_TRANSLUCENT},
    {"SVM_BSDF_TRANSPARENT",SF_BSDF_TRANSPARENT},{"SVM_BSDF_GLOSSY",SF_BSDF_GLOSSY},{"SVM_BSDF_GLASS",SF_BSDF_GLASS},
    {"LIGHT_AREA",SF_LIGHT_AREA},{"MOTION_OBJECT",SF_MOTION_OBJECT},{"MOTION_CAMERA",SF_MOTION_CAMERA},
    {"LIGHT_TRIANGLE",SF_LIGHT_TRIANGLE},
    // Region kept when either microfacet family is reachable.
    {"SVM_BSDF_MICROFACET",SF_BSDF_GLOSSY|SF_BSDF_GLASS}};
  return names;
}
std::string shader_feature_list(uint64_t features){
  std::string out;
  for(auto &n:shader_feature_names())if((n.bit&(n.bit-1))==0&&(features&n.bit)){if(!out.empty())out+=',';out+=n.name;}
  return out;
}

namespace {
// Record size and handler feature for every opcode the WGSL size switch accepts.
// Zero size: unknown to the GPU interpreter (it reports error 2 there).
struct OpInfo {uint32_t bytes;uint64_t feature;};
OpInfo op_info(uint32_t op){
#define OP(name,type,feature) case name:return {uint32_t(sizeof(SVMNode##type)),feature};
  switch(op){
    OP(NODE_SHADER_JUMP,ShaderJump,0) OP(NODE_JUMP_IF_ZERO,JumpIfZero,0) OP(NODE_JUMP_IF_ONE,JumpIfOne,0)
    OP(NODE_MATH,Math,SF_MATH) OP(NODE_CLAMP,Clamp,SF_CLAMP) OP(NODE_VALUE_F,ValueF,0) OP(NODE_VALUE_V,ValueV,0)
    OP(NODE_CONVERT,Convert,SF_CONVERT) OP(NODE_GEOMETRY,Geometry,SF_GEOMETRY) OP(NODE_TEX_COORD,TexCoord,SF_TEX_COORD)
    OP(NODE_TEX_IMAGE,TexImage,SF_IMAGE) OP(NODE_TEX_IMAGE_DERIVATIVE,TexImage,SF_IMAGE)
    OP(NODE_TEX_IMAGE_BOX,TexImageBox,SF_IMAGE) OP(NODE_TEX_IMAGE_BOX_DERIVATIVE,TexImageBox,SF_IMAGE)
    OP(NODE_TEX_ENVIRONMENT,TexEnvironment,SF_IMAGE) OP(NODE_TEX_ENVIRONMENT_DERIVATIVE,TexEnvironment,SF_IMAGE)
    OP(NODE_ATTR,Attr,SF_ATTR) OP(NODE_ATTR_DERIVATIVE,Attr,SF_ATTR) OP(NODE_NORMAL_MAP,NormalMap,SF_NORMAL_MAP)
    OP(NODE_TANGENT,Tangent,SF_TANGENT) OP(NODE_TANGENT_DERIVATIVE,Tangent,SF_TANGENT)
    OP(NODE_VERTEX_COLOR,VertexColor,SF_VERTEX_COLOR) OP(NODE_VERTEX_COLOR_DERIVATIVE,VertexColor,SF_VERTEX_COLOR)
    OP(NODE_VECTOR_MATH,VectorMath,SF_VECTOR_MATH) OP(NODE_SEPARATE_VECTOR,SeparateVector,SF_SEPARATE_VECTOR)
    OP(NODE_COMBINE_VECTOR,CombineVector,SF_COMBINE_VECTOR) OP(NODE_CLOSURE_VOLUME,ClosureVolume,SF_CLOSURE_VOLUME)
    OP(NODE_CLOSURE_BSDF,ClosureBsdf,0) OP(NODE_CLOSURE_HOLDOUT,ClosureHoldout,SF_CLOSURE_HOLDOUT)
    OP(NODE_CLOSURE_SET_NORMAL,ClosureSetNormal,SF_CLOSURE_SET_NORMAL) OP(NODE_CLOSURE_SET_WEIGHT,ClosureSetWeight,0)
    OP(NODE_CLOSURE_WEIGHT,ClosureWeight,0) OP(NODE_CLOSURE_BACKGROUND,ClosureBackground,0)
    OP(NODE_CLOSURE_EMISSION,ClosureEmission,0) OP(NODE_EMISSION_WEIGHT,EmissionWeight,0) OP(NODE_MIX_CLOSURE,MixClosure,0)
    OP(NODE_TEX_CHECKER,TexChecker,SF_TEX_CHECKER) OP(NODE_TEX_GRADIENT,TexGradient,SF_TEX_GRADIENT)
    OP(NODE_GAMMA,Gamma,SF_GAMMA) OP(NODE_INVERT,Invert,SF_INVERT) OP(NODE_BRIGHTCONTRAST,BrightContrast,SF_BRIGHTCONTRAST)
    default:return {0,0};
  }
#undef OP
}
// Data record and feature for each closure svm_bsdf accepts (wavefront_svm_closures.wgsl).
OpInfo bsdf_info(uint32_t kind){
  switch(kind){
    case CLOSURE_BSDF_DIFFUSE_ID:return {uint32_t(sizeof(SVMNodeDiffuseBsdfData)),SF_BSDF_DIFFUSE};
    case CLOSURE_BSDF_TRANSLUCENT_ID:return {uint32_t(sizeof(SVMNodeSimpleBsdfData)),SF_BSDF_TRANSLUCENT};
    case CLOSURE_BSDF_TRANSPARENT_ID:return {uint32_t(sizeof(SVMNodeSimpleBsdfData)),SF_BSDF_TRANSPARENT};
    case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:return {uint32_t(sizeof(SVMNodeRefractionBsdfData)),SF_BSDF_GLASS};
    case CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID:return {uint32_t(sizeof(SVMNodeGlassBsdfData)),SF_BSDF_GLASS};
    case CLOSURE_BSDF_MICROFACET_GGX_ID:case CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID:return {uint32_t(sizeof(SVMNodeGlossyBsdfData)),SF_BSDF_GLOSSY};
    default:return {0,0};
  }
}
uint32_t byte_at(const std::vector<uint32_t> &w,size_t word,size_t offset){
  const size_t index=word+offset/4;return index<w.size()?(w[index]>>(8*(offset%4)))&255u:0u;
}
}

SvmCensus svm_census(const std::vector<uint32_t> &w,size_t shader_count){
  SvmCensus out;const uint64_t n=w.size();
  constexpr uint32_t header=1+sizeof(SVMNodeShaderJump)/4;
  std::vector<uint32_t> work;std::unordered_set<uint32_t> started,decoded;
  for(size_t shader=0;shader<shader_count;shader++)work.push_back(uint32_t(shader*header));
  // svm_take(bytes): fails when pc>count or count(bytes)>count-pc (word units).
  auto take=[&](uint64_t &pc,uint32_t bytes)->bool{const uint64_t words=bytes/4;if(pc>n||words>n-pc)return false;pc+=words;return true;};
  while(!work.empty()){
    uint64_t pc=work.back();work.pop_back();
    if(!started.insert(uint32_t(pc)).second)continue;
    for(;;){
      const uint64_t opcode_at=pc;if(!take(pc,4)){out.stopped_paths++;break;}
      const uint32_t op=w[opcode_at];if(op==NODE_END)break;
      const OpInfo info=op_info(op);if(!info.bytes){out.stopped_paths++;break;}
      const uint64_t node=pc;if(!take(pc,info.bytes)){out.stopped_paths++;break;}
      if(decoded.insert(uint32_t(opcode_at)).second)out.decoded_nodes++;
      out.features|=info.feature;
      if(op==NODE_SHADER_JUMP){
        work.push_back(w[node+offsetof(SVMNodeShaderJump,offset_surface)/4]);
        work.push_back(w[node+offsetof(SVMNodeShaderJump,offset_volume)/4]);break;
      }
      if(op==NODE_JUMP_IF_ZERO||op==NODE_JUMP_IF_ONE){
        const size_t field=op==NODE_JUMP_IF_ZERO?offsetof(SVMNodeJumpIfZero,jump_offset):offsetof(SVMNodeJumpIfOne,jump_offset);
        work.push_back(uint32_t(pc)+w[node+field/4]);  // u32 wrap, as in WGSL.
      }
      if(op==NODE_CLOSURE_BSDF){
        const OpInfo data=bsdf_info(w[node+offsetof(SVMNodeClosureBsdf,closure_type)/4]);
        if(!data.bytes||!take(pc,data.bytes)){out.stopped_paths++;break;}
        out.features|=data.feature;
      }
      if(op==NODE_TEX_COORD&&byte_at(w,node,offsetof(SVMNodeTexCoord,texco_type))==NODE_TEXCO_OBJECT_WITH_TRANSFORM){
        if(!take(pc,sizeof(PackedTransform))){out.stopped_paths++;break;}
      }
    }
  }
  return out;
}

std::string specialize_wgsl(const std::string &source,uint64_t enabled){
  std::istringstream in(source);std::ostringstream out;std::string line;
  struct Region {bool keep;std::string fallback;};std::vector<Region> stack;
  auto active=[&]{for(auto &r:stack)if(!r.keep)return false;return true;};
  while(std::getline(in,line)){
    if(line.rfind("//#if ",0)==0){
      const size_t space=line.find(' ',6);const std::string name=line.substr(6,space==std::string::npos?std::string::npos:space-6);
      std::string fallback;
      if(space!=std::string::npos){if(line.compare(space,6," else ")!=0)throw std::runtime_error("Malformed WGSL feature marker: "+line);fallback=line.substr(space+6);}
      uint64_t bit=0;for(auto &n:shader_feature_names())if(name==n.name)bit=n.bit;
      if(!bit)throw std::runtime_error("Unknown WGSL feature marker: "+name);
      const bool outer=active();stack.push_back({(enabled&bit)!=0,fallback});
      if(outer&&!stack.back().keep&&!fallback.empty())out<<fallback<<'\n';
      continue;
    }
    if(line.rfind("//#endif",0)==0){if(stack.empty())throw std::runtime_error("Unbalanced WGSL feature marker");stack.pop_back();continue;}
    if(active())out<<line<<'\n';
  }
  if(!stack.empty())throw std::runtime_error("Unterminated WGSL feature marker");
  return out.str();
}

std::string conservative_wgsl(const std::string &source){
  static const std::string from="@workgroup_size(64)",to="@workgroup_size(1)";
  std::string out=source;for(size_t at=out.find(from);at!=std::string::npos;at=out.find(from,at+to.size()))out.replace(at,from.size(),to);
  return out;
}
uint32_t declared_workgroup_size(const std::string &source,const char *entry){
  const std::string needle=std::string(" fn ")+entry+"(";const size_t at=source.find(needle);if(at==std::string::npos)return 0;
  const size_t open=source.rfind("@workgroup_size(",at);if(open==std::string::npos||source.find(" fn ",open)!=at)return 0;
  return uint32_t(std::strtoul(source.c_str()+open+16,nullptr,10));
}
}

#include "device.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
namespace ccl {
// Adapter descriptions can contain newlines (Adreno driver build strings).
static std::string json_text(const std::string &text){
  std::string out;for(char c:text){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(uint8_t(c)<0x20){out+=' ';}else out+=c;}return out;
}
uint64_t WebGPUDevice::shader_features(){
  if(!specialization_enabled)return SF_ALL;
  if(census_generation!=svm_generation){
    census=svm_census(svm_node_words,shader_records.size());census_generation=svm_generation;
    enabled_features|=census.features;
    for(const KernelLight &light:light_records)if(light.type==LIGHT_AREA)enabled_features|=SF_LIGHT_AREA;
    // The last distribution entry is the CDF end marker (prim 0), not an emitter.
    for(size_t i=0;i+1<light_distribution_records.size();i++)if(light_distribution_records[i].prim>=0)enabled_features|=SF_LIGHT_TRIANGLE;
    for(const KernelShader &shader:shader_records)if(shader.flags&(SD_MIS_FRONT|SD_MIS_BACK))enabled_features|=SF_LIGHT_TRIANGLE;
    for(const uint flags:object_flag_records)if(flags&SD_OBJECT_MOTION)enabled_features|=SF_MOTION_OBJECT;
    if(data.cam.num_motion_steps!=0)enabled_features|=SF_MOTION_CAMERA;
  }
  return enabled_features;
}
std::string WebGPUDevice::module_wgsl(const std::string &source){return specialize_wgsl(source,shader_features());}
bool WebGPUDevice::pipeline_current(const cbc::Pipeline &pipeline,const char *entry){
  if(!pipeline.value)return false;
  auto it=pipeline_features.find(entry);
  return it!=pipeline_features.end()&&(shader_features()&~it->second)==0;
}
namespace {
struct ProcessPipelines {std::mutex mutex;uint64_t count=0;double max_ms=0,total_ms=0;std::string max_entry;bool conservative=false;std::vector<std::string> warnings;};
ProcessPipelines &process_pipelines(){static ProcessPipelines state;return state;}
}
void pipeline_process_warning(const std::string &message){
  auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.warnings.push_back(message);
}
uint32_t WebGPUDevice::workgroup(const char *entry) const{
  auto it=pipeline_workgroups.find(entry);if(it==pipeline_workgroups.end())throw std::runtime_error(std::string("No compiled workgroup size for ")+entry);
  return it->second;
}
std::vector<cbc::Pipeline> WebGPUDevice::build_pipelines(const char *module,const std::string &source,std::initializer_list<const char*> entries){
  const uint64_t features=shader_features();const std::string specialized=specialize_wgsl(source,features);
  using clock=std::chrono::steady_clock;auto ms=[](clock::time_point a,clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();};
  auto warn=[&](const std::string &message){
    pipeline_warnings.push_back(message);std::fprintf(stderr,"CodeBrix Cycles WebGPU warning: %s\n",message.c_str());
    auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.warnings.push_back(message);
  };
  std::vector<cbc::Pipeline> out;
  for(const char *entry:entries){
    for(int attempt=0;;attempt++){
      const std::string code=conservative?conservative_wgsl(specialized):specialized;
      const auto t0=clock::now();
      WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={code.data(),code.size()};
      WGPUShaderModuleDescriptor sd{};sd.nextInChain=&wgsl.chain;gpu.push_scope();
      cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));
      try{gpu.pop_scope();}catch(const std::exception &e){throw std::runtime_error(std::string("WebGPU shader module '")+module+"' rejected: "+e.what());}
      const double module_ms=ms(t0,clock::now());const auto t1=clock::now();
      try{out.push_back(gpu.pipeline(shader,pipeline_layout,entry));}
      catch(const std::exception &e){
        // A lost device cannot compile a retry; other driver failures (for
        // example out-of-memory reported through an error scope) retry once in
        // the conservative tier. The stage name is always reported.
        const std::string message=e.what();
        if(conservative||attempt>0||message.find("lost")!=std::string::npos)
          throw std::runtime_error("WebGPU "+std::string(module)+" pipeline '"+entry+"' could not be created ("+(conservative?"conservative":"full")+" tier): "+message);
        conservative=true;warn(std::string("pipeline '")+entry+"' failed in the full tier ("+message+"); retrying in the conservative tier (workgroup size 1; results may differ in the last bits but must meet the practical criterion)");
        continue;
      }
      const double pipeline_ms=ms(t1,clock::now());
      pipeline_records.push_back({module,entry,features,code.size(),module_ms,pipeline_ms});pipeline_features[entry]=features;
      const uint32_t size=declared_workgroup_size(code,entry);if(!size)throw std::runtime_error(std::string("No workgroup size declared for ")+entry);
      pipeline_workgroups[entry]=size;
      {auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.count++;process.total_ms+=pipeline_ms;
       if(pipeline_ms>process.max_ms){process.max_ms=pipeline_ms;process.max_entry=entry;}process.conservative|=conservative;}
      // Optional JSON-lines log for measurements and gates (one object per pipeline).
      if(const char *path=std::getenv("CODEBRIX_CYCLES_PIPELINE_LOG")){
        std::ofstream log(path,std::ios::app);const auto &r=pipeline_records.back();
        log<<"{\"module\":\""<<r.module<<"\",\"entry\":\""<<r.entry<<"\",\"specialized\":"<<(specialization_enabled?"true":"false")
           <<",\"tier\":\""<<(conservative?"conservative":"full")<<"\",\"workgroupSize\":"<<size
           <<",\"features\":\""<<shader_feature_list(r.features)<<"\",\"wgslBytes\":"<<r.wgsl_bytes<<",\"moduleMs\":"<<r.module_ms
           <<",\"pipelineMs\":"<<r.pipeline_ms<<",\"adapter\":\""<<json_text(gpu.adapter_name)<<"\"}\n";
      }
      if(pipeline_budget_ms>0&&pipeline_ms>pipeline_budget_ms){
        char text[256];std::snprintf(text,sizeof(text),"pipeline '%s' took %.0f ms, over the %.0f ms compile budget",entry,pipeline_ms,pipeline_budget_ms);
        // By default latency only: the variant never switches on timing, so
        // results cannot depend on machine load. An old-hardware deployment may
        // opt in (budget_conservative) to compile later pipelines conservatively.
        if(budget_conservative&&!conservative){conservative=true;
          warn(std::string(text)+"; later pipelines use the conservative tier (workgroup size 1; results may differ in the last bits but must meet the practical criterion)");}
        else warn(std::string(text)+" ("+(conservative?"conservative":"full")+" tier); first-use latency is degraded on this driver");
      }
      break;
    }
  }
  return out;
}
static std::string json_list(const std::vector<std::string> &items){
  std::string out="[";for(size_t i=0;i<items.size();i++){if(i)out+=',';out+='"'+json_text(items[i])+'"';}return out+"]";
}
std::string WebGPUDevice::pipeline_report_json() const{
  std::ostringstream o;o<<"{\"specialization\":"<<(specialization_enabled?"true":"false")<<",\"tier\":\""<<(conservative?"conservative":"full")
    <<"\",\"degraded\":"<<(pipeline_warnings.empty()?"false":"true")<<",\"budgetMs\":"<<pipeline_budget_ms<<",\"warnings\":"<<json_list(pipeline_warnings)
    <<",\"enabledFeatures\":\""<<shader_feature_list(enabled_features)<<"\",\"svmDecodedNodes\":"<<census.decoded_nodes
    <<",\"svmStoppedPaths\":"<<census.stopped_paths<<",\"pipelines\":[";
  for(size_t i=0;i<pipeline_records.size();i++){const auto &r=pipeline_records[i];if(i)o<<',';
    o<<"{\"module\":\""<<r.module<<"\",\"entry\":\""<<r.entry<<"\",\"wgslBytes\":"<<r.wgsl_bytes<<",\"moduleMs\":"<<r.module_ms<<",\"pipelineMs\":"<<r.pipeline_ms<<'}';}
  o<<"]}";return o.str();
}
std::string pipeline_process_report_json(){
  auto &process=process_pipelines();std::lock_guard lock(process.mutex);std::ostringstream o;
  o<<"{\"tier\":\""<<(process.conservative?"conservative":"full")<<"\",\"degraded\":"<<(process.warnings.empty()?"false":"true")
   <<",\"pipelines\":"<<process.count<<",\"totalPipelineMs\":"<<process.total_ms<<",\"maxPipelineMs\":"<<process.max_ms
   <<",\"maxPipelineEntry\":\""<<process.max_entry<<"\",\"warnings\":"<<json_list(process.warnings)<<'}';
  return o.str();
}
}
