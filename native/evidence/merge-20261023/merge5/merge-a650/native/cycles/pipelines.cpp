// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "pipelines.h"
#include "kernel/svm/types.h"
#include "kernel/svm/node_types.h"
#include "kernel/types.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <unordered_map>
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
    {"SHADOW_TERMINATOR_GEOMETRY",SF_TERMINATOR_GEOMETRY},{"SHADOW_TERMINATOR_SHADING",SF_TERMINATOR_SHADING},
    {"SVM_NODE_TEX_NOISE",SF_TEX_NOISE},{"SVM_NODE_TEX_WHITE_NOISE",SF_TEX_WHITE_NOISE},{"SVM_NODE_TEX_WAVE",SF_TEX_WAVE},
    {"SVM_NODE_TEX_MAGIC",SF_TEX_MAGIC},{"SVM_NODE_TEX_BRICK",SF_TEX_BRICK},{"SVM_NODE_MAPPING",SF_MAPPING},
    {"SVM_NODE_MAP_RANGE",SF_MAP_RANGE},{"SVM_NODE_MIX",SF_MIX},{"SVM_NODE_HSV",SF_HSV},{"SVM_NODE_COLOR_SEPCOMB",SF_COLOR_SEPCOMB},
    {"SVM_NODE_RAMP",SF_RAMP},{"SVM_NODE_FRESNEL",SF_FRESNEL},{"SVM_NODE_VECTOR_ROTATE",SF_VECTOR_ROTATE},
    {"SVM_NODE_BLACKBODY",SF_BLACKBODY},{"SVM_NODE_NORMAL",SF_NORMAL},{"SVM_NODE_SHADING_INFO",SF_SHADING_INFO},{"SVM_NODE_LIGHT_PATH",SF_LIGHT_PATH},
    {"SVM_NODE_TEX_VORONOI",SF_TEX_VORONOI},{"SVM_NODE_WAVELENGTH",SF_WAVELENGTH},{"SVM_NODE_WIREFRAME",SF_WIREFRAME},
    {"SVM_NODE_TEX_GABOR",SF_TEX_GABOR},
    {"SVM_NODE_BUMP",SF_BUMP},{"SVM_NODE_BUMP_STATE",SF_BUMP_STATE},{"SVM_NODE_GEOMETRY_DERIVATIVE",SF_GEOMETRY_DERIVATIVE},
    {"SVM_NODE_TEX_COORD_DERIVATIVE",SF_TEX_COORD_DERIVATIVE},{"SVM_NODE_VALUE_DERIVATIVE",SF_VALUE_DERIVATIVE},
    {"SVM_NODE_VECTOR_MATH_DERIVATIVE",SF_VECTOR_MATH_DERIVATIVE},{"SVM_NODE_MAPPING_DERIVATIVE",SF_MAPPING_DERIVATIVE},
    // Dual-number helpers shared by the derivative variants (wavefront_svm_derivatives.wgsl).
    {"SVM_DERIVATIVE_ANY",SF_GEOMETRY_DERIVATIVE|SF_TEX_COORD_DERIVATIVE|SF_VALUE_DERIVATIVE|SF_VECTOR_MATH_DERIVATIVE|SF_MAPPING_DERIVATIVE},
    // Helper regions shared by several node families (wavefront_svm_nodes.wgsl).
    {"SVM_NODES_ANY",SF_TEX_NOISE|SF_TEX_WHITE_NOISE|SF_TEX_WAVE|SF_TEX_MAGIC|SF_TEX_BRICK|SF_MAPPING|SF_MAP_RANGE|SF_MIX|SF_HSV|SF_COLOR_SEPCOMB|SF_RAMP|
      SF_FRESNEL|SF_VECTOR_ROTATE|SF_BLACKBODY|SF_NORMAL|SF_SHADING_INFO|SF_LIGHT_PATH|SF_TEX_VORONOI|SF_WAVELENGTH|SF_WIREFRAME|SF_MAPPING_DERIVATIVE|SF_TEX_GABOR},
    {"SVM_NODES_DOT",SF_MAPPING|SF_FRESNEL|SF_VECTOR_ROTATE|SF_BLACKBODY|SF_NORMAL|SF_SHADING_INFO|SF_WAVELENGTH|SF_WIREFRAME|SF_MAPPING_DERIVATIVE},
    {"SVM_NODES_HASH",SF_TEX_NOISE|SF_TEX_WHITE_NOISE|SF_TEX_WAVE|SF_TEX_BRICK|SF_TEX_VORONOI|SF_TEX_GABOR},
    {"SVM_NODES_FMOD",SF_TEX_NOISE|SF_TEX_WAVE|SF_TEX_MAGIC},{"SVM_NODES_NOISE",SF_TEX_NOISE|SF_TEX_WAVE},
    {"SVM_NODES_TRANSFORM",SF_MAPPING|SF_VECTOR_ROTATE|SF_MAPPING_DERIVATIVE},{"SVM_NODES_HSV",SF_MIX|SF_HSV|SF_COLOR_SEPCOMB},
    // Region kept when any microfacet family is reachable.
    {"SVM_BSDF_MICROFACET",SF_BSDF_GLOSSY|SF_BSDF_GLASS|SF_BSDF_PRINCIPLED},
    {"SVM_BSDF_PRINCIPLED",SF_BSDF_PRINCIPLED},{"SVM_BSDF_THIN_FILM",SF_BSDF_THIN_FILM},{"SVM_BSDF_THIN_WALL",SF_BSDF_THIN_WALL}};
  return names;
}
std::string shader_feature_list(ShaderFeatures features){
  std::string out;
  for(auto &n:shader_feature_names())if(n.bit.count()==1&&(features&n.bit)){if(!out.empty())out+=',';out+=n.name;}
  return out;
}

// Mask layout invariants (pipelines.h): assigned bits keep their numbers.
static_assert(SF_GEOMETRY.test(0)&&SF_MAPPING_DERIVATIVE.test(61)&&SF_TEX_GABOR.test(63)&&!SF_ALL.test(62));
static_assert(SF_ALL.count()==63&&!(SF_ALL&~ShaderFeatures::range(0,64)).any());
static_assert(ShaderFeatures::bit(64).words[1]==1&&ShaderFeatures::bit(shader_feature_bits-1).test(shader_feature_bits-1));

namespace {
// Record size and handler feature for every opcode the WGSL size switch accepts.
// Zero size: unknown to the GPU interpreter (it reports error 2 there).
struct OpInfo {uint32_t bytes;ShaderFeatures feature;};
OpInfo op_info(uint32_t op){
#define OP(name,type,feature) case name:return {uint32_t(sizeof(SVMNode##type)),feature};
  switch(op){
    OP(NODE_SHADER_JUMP,ShaderJump,SF_NONE) OP(NODE_JUMP_IF_ZERO,JumpIfZero,SF_NONE) OP(NODE_JUMP_IF_ONE,JumpIfOne,SF_NONE)
    OP(NODE_MATH,Math,SF_MATH) OP(NODE_CLAMP,Clamp,SF_CLAMP) OP(NODE_VALUE_F,ValueF,SF_NONE) OP(NODE_VALUE_V,ValueV,SF_NONE)
    OP(NODE_CONVERT,Convert,SF_CONVERT) OP(NODE_GEOMETRY,Geometry,SF_GEOMETRY) OP(NODE_TEX_COORD,TexCoord,SF_TEX_COORD)
    OP(NODE_TEX_IMAGE,TexImage,SF_IMAGE) OP(NODE_TEX_IMAGE_DERIVATIVE,TexImage,SF_IMAGE)
    OP(NODE_TEX_IMAGE_BOX,TexImageBox,SF_IMAGE) OP(NODE_TEX_IMAGE_BOX_DERIVATIVE,TexImageBox,SF_IMAGE)
    OP(NODE_TEX_ENVIRONMENT,TexEnvironment,SF_IMAGE) OP(NODE_TEX_ENVIRONMENT_DERIVATIVE,TexEnvironment,SF_IMAGE)
    OP(NODE_ATTR,Attr,SF_ATTR) OP(NODE_ATTR_DERIVATIVE,Attr,SF_ATTR) OP(NODE_NORMAL_MAP,NormalMap,SF_NORMAL_MAP)
    OP(NODE_TANGENT,Tangent,SF_TANGENT) OP(NODE_TANGENT_DERIVATIVE,Tangent,SF_TANGENT)
    OP(NODE_VERTEX_COLOR,VertexColor,SF_VERTEX_COLOR) OP(NODE_VERTEX_COLOR_DERIVATIVE,VertexColor,SF_VERTEX_COLOR)
    OP(NODE_VECTOR_MATH,VectorMath,SF_VECTOR_MATH) OP(NODE_SEPARATE_VECTOR,SeparateVector,SF_SEPARATE_VECTOR)
    OP(NODE_COMBINE_VECTOR,CombineVector,SF_COMBINE_VECTOR) OP(NODE_CLOSURE_VOLUME,ClosureVolume,SF_CLOSURE_VOLUME)
    OP(NODE_CLOSURE_BSDF,ClosureBsdf,SF_NONE) OP(NODE_CLOSURE_HOLDOUT,ClosureHoldout,SF_CLOSURE_HOLDOUT)
    OP(NODE_CLOSURE_SET_NORMAL,ClosureSetNormal,SF_CLOSURE_SET_NORMAL) OP(NODE_CLOSURE_SET_WEIGHT,ClosureSetWeight,SF_NONE)
    OP(NODE_CLOSURE_WEIGHT,ClosureWeight,SF_NONE) OP(NODE_CLOSURE_BACKGROUND,ClosureBackground,SF_NONE)
    OP(NODE_CLOSURE_EMISSION,ClosureEmission,SF_NONE) OP(NODE_EMISSION_WEIGHT,EmissionWeight,SF_NONE) OP(NODE_MIX_CLOSURE,MixClosure,SF_NONE)
    OP(NODE_TEX_CHECKER,TexChecker,SF_TEX_CHECKER) OP(NODE_TEX_GRADIENT,TexGradient,SF_TEX_GRADIENT)
    OP(NODE_GAMMA,Gamma,SF_GAMMA) OP(NODE_INVERT,Invert,SF_INVERT) OP(NODE_BRIGHTCONTRAST,BrightContrast,SF_BRIGHTCONTRAST)
    OP(NODE_TEX_NOISE,TexNoise,SF_TEX_NOISE) OP(NODE_TEX_VORONOI,TexVoronoi,SF_TEX_VORONOI) OP(NODE_TEX_WHITE_NOISE,TexWhiteNoise,SF_TEX_WHITE_NOISE)
    OP(NODE_TEX_WAVE,TexWave,SF_TEX_WAVE) OP(NODE_TEX_MAGIC,TexMagic,SF_TEX_MAGIC) OP(NODE_TEX_BRICK,TexBrick,SF_TEX_BRICK)
    OP(NODE_MAPPING,Mapping,SF_MAPPING) OP(NODE_TEXTURE_MAPPING,TextureMapping,SF_MAPPING) OP(NODE_MIN_MAX,MinMax,SF_MAPPING)
    OP(NODE_MAP_RANGE,MapRange,SF_MAP_RANGE) OP(NODE_VECTOR_MAP_RANGE,VectorMapRange,SF_MAP_RANGE)
    OP(NODE_MIX,Mix,SF_MIX) OP(NODE_MIX_COLOR,MixColor,SF_MIX) OP(NODE_MIX_FLOAT,MixFloat,SF_MIX)
    OP(NODE_MIX_VECTOR,MixVector,SF_MIX) OP(NODE_MIX_VECTOR_NON_UNIFORM,MixVectorNonUniform,SF_MIX)
    OP(NODE_HSV,HSV,SF_HSV) OP(NODE_SEPARATE_COLOR,SeparateColor,SF_COLOR_SEPCOMB) OP(NODE_COMBINE_COLOR,CombineColor,SF_COLOR_SEPCOMB)
    OP(NODE_RGB_RAMP,RGBRamp,SF_RAMP) OP(NODE_CURVES,Curves,SF_RAMP) OP(NODE_FLOAT_CURVE,FloatCurve,SF_RAMP)
    OP(NODE_FRESNEL,Fresnel,SF_FRESNEL) OP(NODE_LAYER_WEIGHT,LayerWeight,SF_FRESNEL) OP(NODE_VECTOR_ROTATE,VectorRotate,SF_VECTOR_ROTATE)
    OP(NODE_BLACKBODY,Blackbody,SF_BLACKBODY) OP(NODE_NORMAL,Normal,SF_NORMAL)
    OP(NODE_OBJECT_INFO,ObjectInfo,SF_SHADING_INFO) OP(NODE_CAMERA,Camera,SF_SHADING_INFO) OP(NODE_SCENE_TIME,SceneTime,SF_SHADING_INFO)
    OP(NODE_VECTOR_TRANSFORM,VectorTransform,SF_SHADING_INFO) OP(NODE_LIGHT_PATH,LightPath,SF_LIGHT_PATH)
    OP(NODE_WAVELENGTH,Wavelength,SF_WAVELENGTH) OP(NODE_WIREFRAME,Wireframe,SF_WIREFRAME) OP(NODE_TEX_GABOR,TexGabor,SF_TEX_GABOR)
    OP(NODE_SET_BUMP,SetBump,SF_BUMP) OP(NODE_ENTER_BUMP_EVAL,EnterBumpEval,SF_BUMP_STATE) OP(NODE_LEAVE_BUMP_EVAL,LeaveBumpEval,SF_BUMP_STATE)
    OP(NODE_GEOMETRY_DERIVATIVE,Geometry,SF_GEOMETRY_DERIVATIVE) OP(NODE_TEX_COORD_DERIVATIVE,TexCoord,SF_TEX_COORD|SF_TEX_COORD_DERIVATIVE)
    OP(NODE_VALUE_F_DERIVATIVE,ValueF,SF_VALUE_DERIVATIVE) OP(NODE_VALUE_V_DERIVATIVE,ValueV,SF_VALUE_DERIVATIVE)
    OP(NODE_CONVERT_DERIVATIVE,Convert,SF_VALUE_DERIVATIVE) OP(NODE_SEPARATE_VECTOR_DERIVATIVE,SeparateVector,SF_VALUE_DERIVATIVE)
    OP(NODE_COMBINE_VECTOR_DERIVATIVE,CombineVector,SF_VALUE_DERIVATIVE) OP(NODE_VECTOR_MATH_DERIVATIVE,VectorMath,SF_VECTOR_MATH_DERIVATIVE)
    OP(NODE_MAPPING_DERIVATIVE,Mapping,SF_MAPPING_DERIVATIVE) OP(NODE_TEXTURE_MAPPING_DERIVATIVE,TextureMapping,SF_MAPPING_DERIVATIVE)
    default:return {0,SF_NONE};
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
    case CLOSURE_BSDF_PRINCIPLED_ID:return {uint32_t(sizeof(SVMNodePrincipledBsdfData)),SF_BSDF_PRINCIPLED};
    default:return {0,SF_NONE};
  }
}
uint32_t byte_at(const std::vector<uint32_t> &w,size_t word,size_t offset){
  const size_t index=word+offset/4;return index<w.size()?(w[index]>>(8*(offset%4)))&255u:0u;
}
// Principled record census: optional thin-film/thin-wall handlers, and the
// subsurface lobe the WebGPU device does not implement. A linked input may
// enable a feature at run time, so it counts like an enabling constant.
struct PrincipledCensus {ShaderFeatures features;const char *unsupported=nullptr;};
PrincipledCensus principled_census(const std::vector<uint32_t> &w,size_t data){
  PrincipledCensus out;auto field=[&](size_t offset){return w[data+offset/4];};
  auto linked=[](uint32_t bits){return (bits>>8)==(SVM_INPUT_STACK_OFFSET_MASK>>8);};
  auto above=[&](size_t offset,float cutoff,bool saturate){const uint32_t bits=field(offset);if(linked(bits))return true;float v;std::memcpy(&v,&bits,4);
    if(saturate)v=std::min(std::max(v,0.f),1.f);return v>cutoff;};
  if(above(offsetof(SVMNodePrincipledBsdfData,thin_film_thickness),THINFILM_THICKNESS_CUTOFF,false))out.features|=SF_BSDF_THIN_FILM;
  const size_t wall=data+offsetof(SVMNodePrincipledBsdfData,thin_wall)/4;
  if((w[wall]!=0||byte_at(w,wall,offsetof(SVMInputInt,offset))!=SVM_STACK_INVALID)&&above(offsetof(SVMNodePrincipledBsdfData,transmission_weight),CLOSURE_WEIGHT_CUTOFF,true))
    out.features|=SF_BSDF_THIN_WALL;
  if(above(offsetof(SVMNodePrincipledBsdfData,subsurface_weight),CLOSURE_WEIGHT_CUTOFF,true))
    out.unsupported="Unsupported feature: Principled BSDF subsurface scattering (subsurface weight above 0 or linked) is not implemented by the WebGPU device; the scene is rejected, not approximated";
  return out;
}
}

SvmCensus svm_census(const std::vector<uint32_t> &w,size_t shader_count,const std::vector<uint32_t> *roots){
  SvmCensus out;const uint64_t n=w.size();
  constexpr uint32_t header=1+sizeof(SVMNodeShaderJump)/4;
  std::vector<uint32_t> work;std::unordered_set<uint32_t> started,decoded;
  for(size_t shader=0;shader<shader_count;shader++)if(!roots||std::find(roots->begin(),roots->end(),uint32_t(shader))!=roots->end())work.push_back(uint32_t(shader*header));
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
        const uint32_t kind=w[node+offsetof(SVMNodeClosureBsdf,closure_type)/4];const uint64_t data_at=pc;
        const OpInfo data=bsdf_info(kind);
        if(!data.bytes||!take(pc,data.bytes)){out.stopped_paths++;break;}
        out.features|=data.feature;
        if(kind==CLOSURE_BSDF_PRINCIPLED_ID){const auto principled=principled_census(w,data_at);out.features|=principled.features;
          if(principled.unsupported&&out.unsupported.empty())out.unsupported=principled.unsupported;}
      }
      // Ramp and curve tables follow their record (svm_ramp_node: size>=2, sizes it decodes).
      if(op==NODE_RGB_RAMP||op==NODE_CURVES||op==NODE_FLOAT_CURVE){
        const size_t field=op==NODE_RGB_RAMP?offsetof(SVMNodeRGBRamp,table_size):op==NODE_CURVES?offsetof(SVMNodeCurves,table_size):offsetof(SVMNodeFloatCurve,table_size);
        const uint32_t size=w[node+field/4];const uint64_t words=uint64_t(size)*(op==NODE_FLOAT_CURVE?1:4);
        if(size<2||size>0x0fffffffu||!take(pc,uint32_t(words*4))){out.stopped_paths++;break;}
      }
      if((op==NODE_TEX_COORD||op==NODE_TEX_COORD_DERIVATIVE)&&byte_at(w,node,offsetof(SVMNodeTexCoord,texco_type))==NODE_TEXCO_OBJECT_WITH_TRANSFORM){
        if(!take(pc,sizeof(PackedTransform))){out.stopped_paths++;break;}
      }
    }
  }
  return out;
}

std::string specialize_wgsl(const std::string &source,ShaderFeatures enabled){
  std::istringstream in(source);std::ostringstream out;std::string line;
  struct Region {bool keep;std::string fallback;};std::vector<Region> stack;
  auto active=[&]{for(auto &r:stack)if(!r.keep)return false;return true;};
  while(std::getline(in,line)){
    if(line.rfind("//#if ",0)==0){
      const size_t space=line.find(' ',6);const std::string name=line.substr(6,space==std::string::npos?std::string::npos:space-6);
      std::string fallback;
      if(space!=std::string::npos){if(line.compare(space,6," else ")!=0)throw std::runtime_error("Malformed WGSL feature marker: "+line);fallback=line.substr(space+6);}
      ShaderFeatures bit;for(auto &n:shader_feature_names())if(name==n.name)bit=n.bit;
      if(!bit)throw std::runtime_error("Unknown WGSL feature marker: "+name);
      const bool outer=active();stack.push_back({(enabled&bit).any(),fallback});
      if(outer&&!stack.back().keep&&!fallback.empty())out<<fallback<<'\n';
      continue;
    }
    if(line.rfind("//#endif",0)==0){if(stack.empty())throw std::runtime_error("Unbalanced WGSL feature marker");stack.pop_back();continue;}
    if(active())out<<line<<'\n';
  }
  if(!stack.empty())throw std::runtime_error("Unterminated WGSL feature marker");
  return out.str();
}

const char *specialization_mode_name(SpecializationMode mode){
  return mode==SpecializationMode::Scene?"scene":mode==SpecializationMode::Superset?"superset":"off";
}
SpecializationMode specialization_mode_from_environment(unsigned *superset_group){
  const char *mode=std::getenv("CODEBRIX_CYCLES_SHADER_SPECIALIZATION");if(superset_group)*superset_group=0;
  if(!mode||!*mode||std::strcmp(mode,"1")==0)return SpecializationMode::Scene;
  if(std::strcmp(mode,"superset")==0)return SpecializationMode::Superset;
  if(std::strncmp(mode,"superset:",9)==0&&mode[9]>='0'&&mode[9]<='9'){
    char *end=nullptr;const unsigned long group=std::strtoul(mode+9,&end,10);
    if(end&&!*end&&group<1000){if(superset_group)*superset_group=unsigned(group);return SpecializationMode::Superset;}
  }
  if(std::strcmp(mode,"0")==0)return SpecializationMode::Off;
  throw std::runtime_error(std::string("CODEBRIX_CYCLES_SHADER_SPECIALIZATION must be 1, superset or 0, not '")+mode+"'");
}

std::vector<size_t> wgsl_inlined_bytes(const std::string &source,const std::vector<std::string> &entries){
  // Function bodies without line comments; each call to a defined function adds
  // that function's inlined size (recursion is impossible in WGSL).
  std::string text;text.reserve(source.size());
  for(size_t i=0;i<source.size();){
    if(source.compare(i,2,"//")==0){while(i<source.size()&&source[i]!='\n')i++;continue;}
    text+=source[i++];
  }
  auto ident=[](char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_';};
  std::unordered_map<std::string,std::pair<size_t,size_t>> bodies; // name -> [start of "fn", end of body]
  for(size_t at=text.find("fn ");at!=std::string::npos;at=text.find("fn ",at+3)){
    if(at>0&&ident(text[at-1]))continue;
    size_t n=at+3;while(n<text.size()&&text[n]==' ')n++;size_t e=n;while(e<text.size()&&ident(text[e]))e++;
    if(e==n||e>=text.size()||text[e]!='(')continue;
    const size_t open=text.find('{',e);if(open==std::string::npos)break;
    int depth=0;size_t j=open;for(;j<text.size();j++){if(text[j]=='{')depth++;else if(text[j]=='}'&&--depth==0)break;}
    bodies[text.substr(n,e-n)]={at,j+1};
  }
  std::unordered_map<std::string,size_t> memo;
  std::function<size_t(const std::string&,unsigned)> size=[&](const std::string &name,unsigned depth)->size_t{
    if(auto m=memo.find(name);m!=memo.end())return m->second;
    const auto [start,stop]=bodies.at(name);size_t total=stop-start;
    if(depth<64){const size_t open=text.find('{',start);
      for(size_t i=open;i<stop;){
        if(!ident(text[i])||(i>open&&ident(text[i-1]))){i++;continue;}
        size_t e=i;while(e<stop&&ident(text[e]))e++;size_t k=e;while(k<stop&&text[k]==' ')k++;
        if(k<stop&&text[k]=='('){const std::string call=text.substr(i,e-i);if(call!=name&&bodies.count(call))total+=size(call,depth+1);}
        i=e;
      }}
    memo[name]=total;return total;
  };
  std::vector<size_t> out;out.reserve(entries.size());
  for(const std::string &entry:entries)out.push_back(bodies.count(entry)?size(entry,0):0);
  return out;
}
size_t wgsl_inlined_bytes(const std::string &source,const std::string &entry){return wgsl_inlined_bytes(source,std::vector<std::string>{entry})[0];}
size_t pipeline_size_warning_bytes(){
  if(const char *value=std::getenv("CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES");value&&*value)return size_t(std::strtoull(value,nullptr,10));
  return pipeline_size_warning_default_bytes;
}
std::string pipeline_size_warning(const char *module,const char *entry,size_t inlined_bytes,size_t threshold){
  char text[512];
  std::snprintf(text,sizeof(text),"pipeline '%s' (%s module) inlines to %zu bytes of WGSL, at or above the %zu-byte size guard: "
    "about %.0f backend instructions estimated (%.0f to %.0f); Intel Mesa drivers without upstream fix b7e1ac830984 can miscompile "
    "or crash above 65,535; the compiled variant and results are unchanged",entry,module,inlined_bytes,threshold,
    double(inlined_bytes)/pipeline_size_bytes_per_instruction_central,double(inlined_bytes)/pipeline_size_bytes_per_instruction_high,
    double(inlined_bytes)/pipeline_size_bytes_per_instruction_low);
  return text;
}
static size_t max_entry_bytes(const std::vector<std::string> &sources,ShaderFeatures features){
  size_t best=0;
  for(const std::string &source:sources){
    const std::string text=specialize_wgsl(source,features);
    for(size_t at=text.find("@compute");at!=std::string::npos;at=text.find("@compute",at+8)){
      const size_t fn=text.find(" fn ",at);if(fn==std::string::npos)break;const size_t paren=text.find('(',fn+4);
      best=std::max(best,wgsl_inlined_bytes(text,text.substr(fn+4,paren-fn-4)));
    }
  }
  return best;
}
SupersetGroups superset_groups(const std::vector<std::string> &sources,ShaderFeatures scene,ShaderFeatures candidates,size_t budget){
  SupersetGroups out;out.scene_bytes=max_entry_bytes(sources,scene);const size_t limit=std::max(budget,out.scene_bytes);
  // Candidate bits in ascending bit order across every mask word.
  std::vector<ShaderFeatures> todo;const ShaderFeatures open=candidates&~scene;
  for(unsigned b=0;b<shader_feature_bits;b++)if(open.test(b))todo.push_back(ShaderFeatures::bit(b));
  while(!todo.empty()){
    ShaderFeatures current=scene,placed;size_t bytes=out.scene_bytes;std::vector<ShaderFeatures> rest;
    for(const ShaderFeatures &bit:todo){const size_t trial=max_entry_bytes(sources,current|bit);
      if(trial<=limit){current|=bit;placed|=bit;bytes=trial;}else rest.push_back(bit);}
    if(!placed){for(const ShaderFeatures &bit:rest)out.unfit|=bit;break;}
    out.groups.push_back(current);out.group_bytes.push_back(bytes);todo.swap(rest);
  }
  return out;
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
#include "wavefront.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
namespace ccl {
// Adapter descriptions can contain newlines (Adreno driver build strings).
static std::string json_text(const std::string &text){
  std::string out;for(char c:text){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(uint8_t(c)<0x20){out+=' ';}else out+=c;}return out;
}
ShaderFeatures WebGPUDevice::shader_features(){
  // Unimplemented Principled features are refused for every module, with or
  // without specialization, before any pipeline or dispatch uses the scene.
  // Principled handlers count only from shaders the scene references (triangles,
  // lights, world): Cycles' default surface shader is itself a Principled BSDF
  // and would otherwise compile Principled into every scene.
  if(unsupported_generation!=svm_generation){
    std::vector<uint32_t> roots;for(uint32_t s:tri_shader_words)roots.push_back(s&SHADER_MASK);
    for(const KernelLight &light:light_records)roots.push_back(uint32_t(light.shader_id)&SHADER_MASK);
    roots.push_back(uint32_t(data.background.surface_shader)&SHADER_MASK);roots.push_back(uint32_t(data.background.volume_shader)&SHADER_MASK);
    std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
    const SvmCensus referenced=svm_census(svm_node_words,shader_records.size(),&roots);
    unsupported_feature=referenced.unsupported;principled_features=referenced.features&SF_PRINCIPLED_ALL;unsupported_generation=svm_generation;
    // Adapter-class refusal from the known-adapter table (device_tier.cpp), for
    // example texture, colour and utility nodes on the Adreno 650.
    const KnownAdapter *known=tier_policy.known_adapter();
    if(unsupported_feature.empty()&&known&&known->refused_region){
      uint64_t region=0;for(auto &n:shader_feature_names())if(std::strcmp(n.name,known->refused_region)==0)region=n.bit;
      if(const uint64_t refused=referenced.features&region)
        unsupported_feature=std::string("WebGPU: ")+known->refused_text+" are not supported on "+known->device+" GPUs (this scene uses "+
          shader_feature_list(refused)+"): the shader compiler of this GPU class miscompiles or fails to link their shader programs; render the scene on another GPU or on the CPU device";
    }}
  if(!unsupported_feature.empty())throw std::runtime_error(unsupported_feature);
  if(specialization_mode==SpecializationMode::Off)return SF_ALL;
  if(census_generation!=svm_generation){
    census=svm_census(svm_node_words,shader_records.size());census_generation=svm_generation;
    enabled_features|=(census.features&~SF_PRINCIPLED_ALL)|principled_features;
    for(const KernelLight &light:light_records)if(light.type==LIGHT_AREA)enabled_features|=SF_LIGHT_AREA;
    // The last distribution entry is the CDF end marker (prim 0), not an emitter.
    for(size_t i=0;i+1<light_distribution_records.size();i++)if(light_distribution_records[i].prim>=0)enabled_features|=SF_LIGHT_TRIANGLE;
    for(const KernelShader &shader:shader_records)if(shader.flags&(SD_MIS_FRONT|SD_MIS_BACK))enabled_features|=SF_LIGHT_TRIANGLE;
    for(const uint flags:object_flag_records)if(flags&SD_OBJECT_MOTION)enabled_features|=SF_MOTION_OBJECT;
    if(data.cam.num_motion_steps!=0)enabled_features|=SF_MOTION_CAMERA;
    for(const KernelObject &object:object_records){
      if(object.shadow_terminator_shading_offset>1.0f)enabled_features|=SF_TERMINATOR_SHADING;
      if(object.shadow_terminator_geometry_offset>0.0f&&smooth_triangles&&data.integrator.use_direct_light)enabled_features|=SF_TERMINATOR_GEOMETRY;
    }
  }
  if(specialization_mode==SpecializationMode::Superset){
    if(superset_scene!=enabled_features){
      size_t budget=superset_default_budget_bytes;
      if(const char *value=std::getenv("CODEBRIX_CYCLES_SUPERSET_BUDGET_BYTES"))budget=size_t(std::strtoull(value,nullptr,10));
      superset=superset_groups({surface_source(),background_wgsl()},enabled_features,SF_ALL&~SF_SUPERSET_SCENE_ONLY,budget);
      superset_scene=enabled_features;
    }
    // A process can render several scenes with different group counts (the
    // world gate renders 24): a group index past this scene's last group uses its
    // last group, so every requested group still compiles a superset of the scene.
    if(superset.groups.empty())return enabled_features;
    return superset.groups[std::min<size_t>(superset_group,superset.groups.size()-1)];
  }
  return enabled_features;
}
std::string WebGPUDevice::module_wgsl(const std::string &source){return specialize_wgsl(source,shader_features());}
bool WebGPUDevice::pipeline_current(const cbc::Pipeline &pipeline,const char *entry){
  if(!pipeline.value)return false;
  auto it=pipeline_features.find(entry);
  return it!=pipeline_features.end()&&!(shader_features()&~it->second).any();
}
namespace {
struct ProcessPipelines {std::mutex mutex;uint64_t count=0;double max_ms=0,total_ms=0;std::string max_entry;bool conservative=false;std::vector<std::string> warnings;std::string decision;
  // Size guard (pipelines.h): warnings kept apart from the latency warnings so "degraded" keeps its meaning.
  std::vector<std::string> size_warnings;size_t max_inlined_bytes=0;std::string max_inlined_entry;};
ProcessPipelines &process_pipelines(){static ProcessPipelines state;return state;}
}
void pipeline_process_decision(const std::string &json){
  auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.decision=json;
}
void pipeline_process_warning(const std::string &message){
  auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.warnings.push_back(message);
}
uint32_t WebGPUDevice::workgroup(const char *entry) const{
  auto it=pipeline_workgroups.find(entry);if(it==pipeline_workgroups.end())throw std::runtime_error(std::string("No compiled workgroup size for ")+entry);
  return it->second;
}
std::vector<cbc::Pipeline> WebGPUDevice::build_pipelines(const char *module,const std::string &source,std::initializer_list<const char*> entries){
  const ShaderFeatures features=shader_features();const std::string specialized=specialize_wgsl(source,features);
  using clock=std::chrono::steady_clock;auto ms=[](clock::time_point a,clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();};
  auto warn=[&](const std::string &message){
    pipeline_warnings.push_back(message);std::fprintf(stderr,"CodeBrix Cycles WebGPU warning: %s\n",message.c_str());
    auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.warnings.push_back(message);
  };
  std::vector<cbc::Pipeline> out;
  // Size guard (pipelines.h): estimated from the WGSL before the driver sees it,
  // so the warning is on stderr even if the compile then crashes the process.
  const std::vector<size_t> inlined=wgsl_inlined_bytes(specialized,std::vector<std::string>(entries.begin(),entries.end()));
  {size_t index=0;
   for(const char *entry:entries){
     const size_t bytes=inlined[index++];
     if(bytes>max_inlined_bytes){max_inlined_bytes=bytes;max_inlined_entry=entry;}
     auto &process=process_pipelines();std::lock_guard lock(process.mutex);
     if(bytes>process.max_inlined_bytes){process.max_inlined_bytes=bytes;process.max_inlined_entry=entry;}
     if(size_warning_bytes==0||bytes<size_warning_bytes)continue;
     const std::string message=pipeline_size_warning(module,entry,bytes,size_warning_bytes);
     if(std::find(size_warnings.begin(),size_warnings.end(),message)!=size_warnings.end())continue;
     size_warnings.push_back(message);std::fprintf(stderr,"CodeBrix Cycles WebGPU warning: %s\n",message.c_str());
     if(std::find(process.size_warnings.begin(),process.size_warnings.end(),message)==process.size_warnings.end())process.size_warnings.push_back(message);
   }}
  // Optional capture of every module as compiled (maintainer measurements, for
  // example native/tools/intel_compile_envelope.py): CODEBRIX_CYCLES_WGSL_DUMP=DIR
  // writes DIR/<sequence>-<module>.wgsl before the entries are created.
  if(const char *dir=std::getenv("CODEBRIX_CYCLES_WGSL_DUMP");dir&&*dir){
    static std::atomic<unsigned> sequence{0};char name[64];std::snprintf(name,sizeof(name),"/%03u-%s.wgsl",sequence++,module);
    std::ofstream(std::string(dir)+name)<<(conservative?conservative_wgsl(specialized):specialized);
    std::snprintf(name,sizeof(name),"/source-%s.wgsl",module);std::ofstream(std::string(dir)+name)<<source;
  }
  for(const char *entry:entries){
    const size_t entry_inlined=inlined[size_t(std::find(entries.begin(),entries.end(),entry)-entries.begin())];
    for(int attempt=0;;attempt++){
      const std::string code=conservative?conservative_wgsl(specialized):specialized;
      const auto t0=clock::now();
      WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={code.data(),code.size()};
      WGPUShaderModuleDescriptor sd{};sd.nextInChain=&wgsl.chain;
      // Pending breadcrumb for the persisted device record (device_tier.h).
      tier_policy.begin_pipeline(module,entry,conservative);gpu.push_scope();
      cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));
      try{gpu.pop_scope();}catch(const std::exception &e){tier_policy.abandon_pipeline();throw std::runtime_error(std::string("WebGPU shader module '")+module+"' rejected: "+e.what());}
      const double module_ms=ms(t0,clock::now());const auto t1=clock::now();
      try{out.push_back(gpu.pipeline(shader,pipeline_layout,entry,nullptr,tier_policy.pending_descriptor()));}
      catch(const std::exception &e){
        // A lost device cannot compile a retry; other driver failures (for
        // example out-of-memory reported through an error scope) retry once in
        // the conservative tier. The stage name is always reported, and the
        // failure is recorded so later runs start conservative.
        const std::string message=e.what();tier_policy.end_pipeline(false,ms(t1,clock::now()),message);
        if(conservative||attempt>0||message.find("lost")!=std::string::npos){
          pipeline_process_decision(tier_policy.report_json());
          throw std::runtime_error("WebGPU "+std::string(module)+" pipeline '"+entry+"' could not be created ("+(conservative?"conservative":"full")+" tier): "+message);
        }
        conservative=true;tier_policy.switch_conservative("failure",std::string("pipeline '")+entry+"' failed in the full tier: "+message);
        warn(std::string("pipeline '")+entry+"' failed in the full tier ("+message+"); retrying in the conservative tier (workgroup size 1; results may differ in the last bits but must meet the practical criterion)");
        pipeline_process_decision(tier_policy.report_json());
        continue;
      }
      const double pipeline_ms=ms(t1,clock::now());tier_policy.end_pipeline(true,pipeline_ms,"");
      pipeline_records.push_back({module,entry,features,code.size(),entry_inlined,module_ms,pipeline_ms});pipeline_features[entry]=features;
      const uint32_t size=declared_workgroup_size(code,entry);if(!size)throw std::runtime_error(std::string("No workgroup size declared for ")+entry);
      pipeline_workgroups[entry]=size;
      {auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.count++;process.total_ms+=pipeline_ms;
       if(pipeline_ms>process.max_ms){process.max_ms=pipeline_ms;process.max_entry=entry;}process.conservative|=conservative;}
      // Optional JSON-lines log for measurements and gates (one object per pipeline).
      if(const char *path=std::getenv("CODEBRIX_CYCLES_PIPELINE_LOG")){
        std::ofstream log(path,std::ios::app);const auto &r=pipeline_records.back();
        log<<"{\"module\":\""<<r.module<<"\",\"entry\":\""<<r.entry<<"\",\"specialized\":"<<(specialization_enabled?"true":"false")
           <<",\"specializationMode\":\""<<specialization_mode_name(specialization_mode)<<"\""<<superset_json()
           <<",\"tier\":\""<<(conservative?"conservative":"full")<<"\",\"tierReason\":\""<<json_text(tier_policy.reason)<<"\",\"workgroupSize\":"<<size
           <<",\"features\":\""<<shader_feature_list(r.features)<<"\",\"wgslBytes\":"<<r.wgsl_bytes<<",\"inlinedBytes\":"<<r.inlined_bytes<<",\"moduleMs\":"<<r.module_ms
           <<",\"pipelineMs\":"<<r.pipeline_ms<<",\"adapter\":\""<<json_text(gpu.adapter_name)<<"\"}\n";
      }
      if(pipeline_budget_ms>0&&pipeline_ms>pipeline_budget_ms){
        char text[256];std::snprintf(text,sizeof(text),"pipeline '%s' took %.0f ms, over the %.0f ms compile budget",entry,pipeline_ms,pipeline_budget_ms);
        // By default latency only: the variant never switches on timing, so
        // results cannot depend on machine load. An old-hardware deployment may
        // opt in (budget_conservative) to compile later pipelines conservatively.
        if(budget_conservative&&!conservative){conservative=true;tier_policy.switch_conservative("budget-opt-in",text);pipeline_process_decision(tier_policy.report_json());
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
static std::vector<std::string> all_warnings(const std::vector<std::string> &latency,const std::vector<std::string> &size){
  std::vector<std::string> out=latency;out.insert(out.end(),size.begin(),size.end());return out;
}
// "sizeGuard": threshold (0 = disabled), the largest inlined entry compiled so far and the warning count.
static std::string size_guard_json(size_t threshold,size_t largest,const std::string &entry,size_t warnings){
  std::ostringstream o;o<<"{\"thresholdBytes\":"<<threshold<<",\"largestEntryBytes\":"<<largest<<",\"largestEntry\":\""<<json_text(entry)
    <<"\",\"estimatedInstructions\":"<<uint64_t(double(largest)/pipeline_size_bytes_per_instruction_central+0.5)<<",\"warnings\":"<<warnings<<'}';
  return o.str();
}
std::string WebGPUDevice::superset_json() const{
  if(specialization_mode!=SpecializationMode::Superset||superset_scene==~SF_NONE)return "";
  const size_t used=superset.groups.empty()?0:std::min<size_t>(superset_group,superset.groups.size()-1);
  std::ostringstream o;o<<",\"supersetGroup\":"<<superset_group<<",\"supersetGroups\":"<<superset.groups.size()
    <<",\"supersetGroupUsed\":"<<used<<",\"supersetSceneBytes\":"<<superset.scene_bytes<<",\"supersetGroupBytes\":"<<(used<superset.group_bytes.size()?superset.group_bytes[used]:superset.scene_bytes)
    <<",\"supersetGroupAdded\":\""<<shader_feature_list(used<superset.groups.size()?superset.groups[used]&~superset_scene:SF_NONE)
    <<"\",\"supersetUnfit\":\""<<shader_feature_list(superset.unfit)<<'"';
  return o.str();
}
std::string WebGPUDevice::pipeline_report_json() const{
  std::ostringstream o;o<<"{\"specialization\":"<<(specialization_enabled?"true":"false")<<",\"specializationMode\":\""<<specialization_mode_name(specialization_mode)<<"\""<<superset_json()<<",\"tier\":\""<<(conservative?"conservative":"full")
    <<"\",\"degraded\":"<<(pipeline_warnings.empty()?"false":"true")<<",\"budgetMs\":"<<pipeline_budget_ms<<",\"warnings\":"<<json_list(all_warnings(pipeline_warnings,size_warnings))
    <<",\"sizeGuard\":"<<size_guard_json(size_warning_bytes,max_inlined_bytes,max_inlined_entry,size_warnings.size())
    <<",\"enabledFeatures\":\""<<shader_feature_list(enabled_features)<<"\",\"svmDecodedNodes\":"<<census.decoded_nodes
    <<",\"svmStoppedPaths\":"<<census.stopped_paths<<",\"budgetSource\":\""<<json_text(tier_policy.budget_source)<<"\",\"decision\":"<<tier_policy.report_json()<<",\"pipelines\":[";
  for(size_t i=0;i<pipeline_records.size();i++){const auto &r=pipeline_records[i];if(i)o<<',';
    o<<"{\"module\":\""<<r.module<<"\",\"entry\":\""<<r.entry<<"\",\"wgslBytes\":"<<r.wgsl_bytes<<",\"inlinedBytes\":"<<r.inlined_bytes<<",\"moduleMs\":"<<r.module_ms<<",\"pipelineMs\":"<<r.pipeline_ms<<'}';}
  o<<"]}";return o.str();
}
std::string pipeline_process_report_json(){
  auto &process=process_pipelines();std::lock_guard lock(process.mutex);std::ostringstream o;
  o<<"{\"tier\":\""<<(process.conservative?"conservative":"full")<<"\",\"degraded\":"<<(process.warnings.empty()?"false":"true")
   <<",\"pipelines\":"<<process.count<<",\"totalPipelineMs\":"<<process.total_ms<<",\"maxPipelineMs\":"<<process.max_ms
   <<",\"maxPipelineEntry\":\""<<process.max_entry<<"\",\"warnings\":"<<json_list(all_warnings(process.warnings,process.size_warnings))
   <<",\"sizeGuard\":"<<size_guard_json(pipeline_size_warning_bytes(),process.max_inlined_bytes,process.max_inlined_entry,process.size_warnings.size())
   <<",\"decision\":"<<(process.decision.empty()?std::string("null"):process.decision)<<'}';
  return o.str();
}
}
