// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront.h"
#include "kernel/types.h"
#include "kernel/svm/node_types.h"
#include <sstream>
namespace ccl {
std::string svm_layout(bool surface){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
#define SIZE(name) constant("SZ_" #name,sizeof(SVMNode##name))
#define FIELD(name,member) constant("N_" #name "_" #member,offsetof(SVMNode##name,member))
  SIZE(ShaderJump);FIELD(ShaderJump,offset_surface);FIELD(ShaderJump,offset_volume);FIELD(ShaderJump,offset_displacement);
  SIZE(JumpIfZero);FIELD(JumpIfZero,jump_offset);FIELD(JumpIfZero,stack_offset);
  SIZE(JumpIfOne);FIELD(JumpIfOne,jump_offset);FIELD(JumpIfOne,stack_offset);
  SIZE(Math);FIELD(Math,math_type);FIELD(Math,value1);FIELD(Math,value2);FIELD(Math,value3);FIELD(Math,result_offset);
  SIZE(Clamp);FIELD(Clamp,clamp_type);FIELD(Clamp,min);FIELD(Clamp,max);FIELD(Clamp,value);FIELD(Clamp,result_offset);
  SIZE(ValueF);FIELD(ValueF,value);FIELD(ValueF,out_offset);
  SIZE(ValueV);FIELD(ValueV,out_offset);FIELD(ValueV,value);
  SIZE(Convert);FIELD(Convert,convert_type);FIELD(Convert,from_offset);FIELD(Convert,to_offset);
  SIZE(Geometry);FIELD(Geometry,geom_type);FIELD(Geometry,bump_offset);FIELD(Geometry,store_derivatives);FIELD(Geometry,out_offset);FIELD(Geometry,bump_filter_width);
  SIZE(TexCoord);FIELD(TexCoord,texco_type);FIELD(TexCoord,bump_offset);FIELD(TexCoord,store_derivatives);FIELD(TexCoord,out_offset);FIELD(TexCoord,bump_filter_width);
  SIZE(VectorMath);FIELD(VectorMath,math_type);FIELD(VectorMath,a);FIELD(VectorMath,b);FIELD(VectorMath,c);FIELD(VectorMath,param1);FIELD(VectorMath,value_offset);FIELD(VectorMath,vector_offset);
  SIZE(SeparateVector);FIELD(SeparateVector,vector);FIELD(SeparateVector,vector_index);FIELD(SeparateVector,out_offset);
  SIZE(CombineVector);FIELD(CombineVector,in);FIELD(CombineVector,vector_index);FIELD(CombineVector,out_offset);
  SIZE(ClosureSetWeight);FIELD(ClosureSetWeight,rgb);
  SIZE(ClosureWeight);FIELD(ClosureWeight,weight_offset);
  SIZE(ClosureBackground);FIELD(ClosureBackground,mix_weight_offset);
  SIZE(ClosureEmission);FIELD(ClosureEmission,mix_weight_offset);
  SIZE(ClosureHoldout);FIELD(ClosureHoldout,mix_weight_offset);
  SIZE(ClosureBsdf);FIELD(ClosureBsdf,closure_type);FIELD(ClosureBsdf,mix_weight_offset);
  SIZE(DiffuseBsdfData);FIELD(DiffuseBsdfData,color);FIELD(DiffuseBsdfData,roughness);FIELD(DiffuseBsdfData,normal_offset);
  SIZE(SimpleBsdfData);FIELD(SimpleBsdfData,param1);FIELD(SimpleBsdfData,normal_offset);
  SIZE(ClosureSetNormal);FIELD(ClosureSetNormal,direction_offset);FIELD(ClosureSetNormal,normal_offset);
  SIZE(EmissionWeight);FIELD(EmissionWeight,color);FIELD(EmissionWeight,strength);
  SIZE(MixClosure);FIELD(MixClosure,fac);FIELD(MixClosure,in_weight_offset);FIELD(MixClosure,weight1_offset);FIELD(MixClosure,weight2_offset);
  SIZE(TexChecker);FIELD(TexChecker,color1);FIELD(TexChecker,color2);FIELD(TexChecker,scale);FIELD(TexChecker,co);FIELD(TexChecker,color_offset);FIELD(TexChecker,fac_offset);
  SIZE(TexGradient);FIELD(TexGradient,gradient_type);FIELD(TexGradient,co);FIELD(TexGradient,fac_offset);FIELD(TexGradient,color_offset);
  SIZE(Gamma);FIELD(Gamma,color);FIELD(Gamma,gamma);FIELD(Gamma,out_offset);
  SIZE(Invert);FIELD(Invert,color);FIELD(Invert,fac);FIELD(Invert,out_offset);
  SIZE(BrightContrast);FIELD(BrightContrast,color);FIELD(BrightContrast,bright);FIELD(BrightContrast,contrast);FIELD(BrightContrast,out_offset);
#undef SIZE
#undef FIELD
#define ENUM(name) constant(#name,uint32_t(name))
  // Enum constants are taken from the same native headers as the bytecode compiler.
#include "svm_enums.inc"
  ENUM(CLOSURE_BSDF_DIFFUSE_ID);ENUM(CLOSURE_BSDF_OREN_NAYAR_ID);ENUM(CLOSURE_BSDF_TRANSLUCENT_ID);
  ENUM(CLOSURE_BSDF_TRANSPARENT_ID);ENUM(CLOSURE_HOLDOUT_ID);
  ENUM(SR_EMISSION);ENUM(SR_BSDF);ENUM(SR_BSDF_HAS_EVAL);ENUM(SR_BSDF_HAS_TRANSMISSION);ENUM(SR_TRANSPARENT);ENUM(SR_HOLDOUT);
  ENUM(SD_USE_BUMP_MAP_CORRECTION);ENUM(PATH_RAY_TERMINATE);ENUM(PATH_RAY_EMISSION);ENUM(PATH_RAY_VISIBILITY_SHADOW);
#undef ENUM
  source<<"const SVM_SURFACE:bool="<<(surface?"true":"false")<<";\n";
  constant("SVM_CLOSURE_CAPACITY",surface?MAX_CLOSURE:1);
  constant("D_max_closures",offsetof(KernelData,max_closures));
  constant("O_dupli_generated",offsetof(KernelObject,dupli_generated));constant("O_dupli_uv",offsetof(KernelObject,dupli_uv));
  constant("D_film_rgb_to_y",offsetof(KernelData,film.rgb_to_y));
  constant("D_cam_type",offsetof(KernelData,cam.type));
  constant("D_cam_worldtocamera",offsetof(KernelData,cam.worldtocamera));
  constant("D_cam_worldtondc",offsetof(KernelData,cam.worldtondc));
  constant("SZ_PackedTransform",sizeof(PackedTransform));
  return source.str();
}
static constexpr char surface_source[]=
#include "svm_shader.inc"
;
std::string svm_surface_source(){return shader_data_source()+svm_layout(true)+surface_source;}
}
