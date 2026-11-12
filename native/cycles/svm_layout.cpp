// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront.h"
#include "surface_closure.h"
#include "kernel/types.h"
#include "util/types_image.h"
#include "kernel/svm/node_types.h"
#include <sstream>
#include <stdexcept>
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
  SIZE(Attr);FIELD(Attr,attr);FIELD(Attr,out_offset);FIELD(Attr,output_type);FIELD(Attr,bump_offset);FIELD(Attr,store_derivatives);FIELD(Attr,bump_filter_width);
  SIZE(NormalMap);FIELD(NormalMap,space);FIELD(NormalMap,invert_green);FIELD(NormalMap,use_original_base);FIELD(NormalMap,attr);FIELD(NormalMap,attr_sign);FIELD(NormalMap,color);FIELD(NormalMap,strength);FIELD(NormalMap,normal_offset);
  SIZE(Tangent);FIELD(Tangent,direction_type);FIELD(Tangent,axis);FIELD(Tangent,attr);FIELD(Tangent,tangent_offset);
  SIZE(TexImage);FIELD(TexImage,id);FIELD(TexImage,projection);FIELD(TexImage,flags);FIELD(TexImage,co);FIELD(TexImage,out_offset);FIELD(TexImage,alpha_offset);
  SIZE(TexImageBox);FIELD(TexImageBox,blend);SIZE(TexEnvironment);
#define SHARED_FIELD(member) static_assert(offsetof(SVMNodeTexImage,member)==offsetof(SVMNodeTexImageBox,member));static_assert(offsetof(SVMNodeTexImage,member)==offsetof(SVMNodeTexEnvironment,member));
  SHARED_FIELD(id) SHARED_FIELD(flags) SHARED_FIELD(co) SHARED_FIELD(out_offset) SHARED_FIELD(alpha_offset)
#undef SHARED_FIELD
  static_assert(offsetof(SVMNodeTexImage,projection)==offsetof(SVMNodeTexEnvironment,projection));
  // Bump stages (wavefront_svm_derivatives.wgsl): emitted only for scenes that reach them.
  source<<"//#if SVM_NODE_BUMP\n";
  SIZE(SetBump);FIELD(SetBump,scale);FIELD(SetBump,strength);FIELD(SetBump,bump_filter_width);FIELD(SetBump,normal_offset);FIELD(SetBump,invert);
  FIELD(SetBump,use_object_space);FIELD(SetBump,center_offset);FIELD(SetBump,dx_offset);FIELD(SetBump,dy_offset);FIELD(SetBump,out_offset);FIELD(SetBump,bump_state_offset);
  source<<"//#endif\n//#if SVM_NODE_BUMP_STATE\n";
  SIZE(EnterBumpEval);FIELD(EnterBumpEval,state_offset);SIZE(LeaveBumpEval);FIELD(LeaveBumpEval,state_offset);
  source<<"//#endif\n";
  // Ambient Occlusion node (svm/ao.h; ray tracing in wavefront_ao.wgsl).
  source<<"//#if SVM_NODE_AO\n";
  SIZE(AmbientOcclusion);FIELD(AmbientOcclusion,color);FIELD(AmbientOcclusion,dist);FIELD(AmbientOcclusion,flags);FIELD(AmbientOcclusion,samples);
  FIELD(AmbientOcclusion,normal_offset);FIELD(AmbientOcclusion,out_ao_offset);FIELD(AmbientOcclusion,out_color_offset);
  constant("AO_NODE_ONLY_LOCAL",NODE_AO_ONLY_LOCAL);constant("AO_NODE_INSIDE",NODE_AO_INSIDE);constant("AO_NODE_GLOBAL_RADIUS",NODE_AO_GLOBAL_RADIUS);
  source<<"//#endif\n";
  // Bevel and Raycast nodes (svm/bevel.h, svm/raycast.h; ray tracing in wavefront_ao.wgsl).
  // OP_ names: opcode names in the WGSL would join every module's enum list.
  // Both opcodes in either node's modules: one dispatch case and one call site for the
  // shared ray tracing (drivers inline every WGSL call).
  source<<"//#if SVM_RAYTRACE_NODES\n";
  constant("OP_BEVEL",NODE_BEVEL);constant("OP_RAYCAST",NODE_RAYCAST);constant("OP_RAYCAST_ATTR",NODE_ATTR);
  source<<"//#endif\n//#if SVM_NODE_BEVEL\n";
  SIZE(Bevel);FIELD(Bevel,radius);FIELD(Bevel,num_samples);FIELD(Bevel,normal_offset);FIELD(Bevel,out_offset);
  source<<"//#endif\n//#if SVM_NODE_RAYCAST\n";
  SIZE(Raycast);FIELD(Raycast,position);FIELD(Raycast,direction);FIELD(Raycast,distance);
  FIELD(Raycast,bump_filter_width);FIELD(Raycast,only_local);FIELD(Raycast,num_attributes);FIELD(Raycast,is_hit_offset);FIELD(Raycast,is_self_hit_offset);
  FIELD(Raycast,hit_distance_offset);FIELD(Raycast,hit_position_offset);FIELD(Raycast,hit_normal_offset);
  static_assert(offsetof(SVMNodeRaycast,num_attributes)%4<=2);
  source<<"//#endif\n//#if SVM_NODE_RADIAL_TILING\n";
  constant("OP_RADIAL_TILING",NODE_RADIAL_TILING);SIZE(RadialTiling);FIELD(RadialTiling,vector);FIELD(RadialTiling,r_gon_sides);FIELD(RadialTiling,r_gon_roundness);
  FIELD(RadialTiling,normalize_r_gon_parameter);FIELD(RadialTiling,segment_coordinates_offset);FIELD(RadialTiling,segment_id_offset);
  FIELD(RadialTiling,max_unit_parameter_offset);FIELD(RadialTiling,x_axis_A_angle_bisector_offset);
  // Displacement nodes (svm/displace.h; wavefront_svm_derivatives.wgsl) and the true-displacement
  // evaluation (shader_eval_displace). DSP_ names: NODE_ and ATTR_ names in the WGSL would join every
  // module's enum list.
  // AOV output nodes (svm/aov.h, film/aov_passes.h): the AOV writes of film-writing evaluations.
  source<<"//#endif\n//#if SVM_NODE_AOV\n";
  constant("OP_AOV_START",NODE_AOV_START);constant("OP_AOV_COLOR",NODE_AOV_COLOR);constant("OP_AOV_VALUE",NODE_AOV_VALUE);
  SIZE(AOVColor);FIELD(AOVColor,aov_offset);FIELD(AOVColor,color);SIZE(AOVValue);FIELD(AOVValue,aov_offset);FIELD(AOVValue,value);
  constant("AOV_D_pass_aov_color",offsetof(KernelData,film.pass_aov_color));constant("AOV_D_pass_aov_value",offsetof(KernelData,film.pass_aov_value));
  constant("AOV_D_pass_stride",offsetof(KernelData,film.pass_stride));constant("AOV_PASS_UNUSED",uint32_t(PASS_UNUSED));
  constant("AOV_PATH_RAY_TRANSPARENT_BACKGROUND",PATH_RAY_TRANSPARENT_BACKGROUND);constant("AOV_PATH_RAY_SINGLE_PASS_DONE",PATH_RAY_SINGLE_PASS_DONE);
  // AOV passes as the only data passes: film_write_data_passes' PATH_RAY_SINGLE_PASS_DONE bookkeeping (aov_single_pass_done).
  source<<"//#if !FILM_DATA_PASSES\n";
  constant("AOV_D_pass_alpha_threshold",offsetof(KernelData,film.pass_alpha_threshold));constant("AOV_SR_TRANSPARENT",SR_TRANSPARENT);constant("AOV_SR_RAY_PORTAL",SR_RAY_PORTAL);
  source<<"//#endif\n";
  source<<"//#endif\n//#if SVM_NODE_DISPLACEMENT\n";
  constant("OP_SET_DISPLACEMENT",NODE_SET_DISPLACEMENT);constant("OP_DISPLACEMENT",NODE_DISPLACEMENT);constant("OP_VECTOR_DISPLACEMENT",NODE_VECTOR_DISPLACEMENT);
  SIZE(SetDisplacement);FIELD(SetDisplacement,fac_offset);
  SIZE(Displacement);FIELD(Displacement,space);FIELD(Displacement,height);FIELD(Displacement,midlevel);FIELD(Displacement,scale);
  FIELD(Displacement,normal_offset);FIELD(Displacement,out_offset);
  SIZE(VectorDisplacement);FIELD(VectorDisplacement,space);FIELD(VectorDisplacement,vector);FIELD(VectorDisplacement,midlevel);
  FIELD(VectorDisplacement,scale);FIELD(VectorDisplacement,attr);FIELD(VectorDisplacement,attr_sign);FIELD(VectorDisplacement,displacement_offset);
  constant("DSP_SPACE_OBJECT",NODE_NORMAL_MAP_OBJECT);constant("DSP_SPACE_TANGENT",NODE_NORMAL_MAP_TANGENT);constant("DSP_SPACE_WORLD",NODE_NORMAL_MAP_WORLD);
  static_assert(sizeof(NodeNormalMapSpace)==4&&offsetof(SVMNodeDisplacement,space)%4==0&&offsetof(SVMNodeVectorDisplacement,space)%4==0);
  source<<"//#endif\n//#if SHADER_EVAL_DISPLACE\n";
  constant("DSP_INPUT_BYTES",sizeof(KernelShaderEvalInput));constant("DSP_INPUT_object",offsetof(KernelShaderEvalInput,object));
  constant("DSP_INPUT_prim",offsetof(KernelShaderEvalInput,prim));constant("DSP_INPUT_u",offsetof(KernelShaderEvalInput,u));constant("DSP_INPUT_v",offsetof(KernelShaderEvalInput,v));
  source<<"//#endif\n//#if RAYTRACE_ANY\n//#if RAYTRACE_LOCAL\n";
  constant("RAYTRACE_LOCAL_MAX_HITS",LOCAL_MAX_HITS);constant("RAYTRACE_PRNG_SURFACE_BEVEL",PRNG_SURFACE_BEVEL);
  constant("RAYTRACE_VISIBILITY_RAYCAST",PATH_RAY_VISIBILITY_RAYCAST);
  source<<"//#endif\n//#endif\n";
  source<<"//#if RAYTRACE_ANY\n";
  constant("AO_VISIBILITY_ALL",PATH_RAY_VISIBILITY_ALL);constant("AO_VISIBILITY_SHADOW_OPAQUE",PATH_RAY_VISIBILITY_SHADOW_OPAQUE);constant("AO_PRNG_SURFACE_AO",PRNG_SURFACE_AO);
  source<<"//#endif\n";
  SIZE(VertexColor);FIELD(VertexColor,layer_id);FIELD(VertexColor,color_offset);FIELD(VertexColor,alpha_offset);FIELD(VertexColor,bump_offset);FIELD(VertexColor,bump_filter_width);
  SIZE(VectorMath);FIELD(VectorMath,math_type);FIELD(VectorMath,a);FIELD(VectorMath,b);FIELD(VectorMath,c);FIELD(VectorMath,param1);FIELD(VectorMath,value_offset);FIELD(VectorMath,vector_offset);
  SIZE(SeparateVector);FIELD(SeparateVector,vector);FIELD(SeparateVector,vector_index);FIELD(SeparateVector,out_offset);
  SIZE(CombineVector);FIELD(CombineVector,in);FIELD(CombineVector,vector_index);FIELD(CombineVector,out_offset);
  SIZE(ClosureSetWeight);FIELD(ClosureSetWeight,rgb);
  SIZE(ClosureWeight);FIELD(ClosureWeight,weight_offset);
  SIZE(ClosureBackground);FIELD(ClosureBackground,mix_weight_offset);
  SIZE(ClosureEmission);FIELD(ClosureEmission,mix_weight_offset);
  SIZE(ClosureHoldout);FIELD(ClosureHoldout,mix_weight_offset);
  SIZE(ClosureVolume);FIELD(ClosureVolume,closure_type);FIELD(ClosureVolume,density);FIELD(ClosureVolume,param1);FIELD(ClosureVolume,mix_weight_offset);
  // Volume phase functions, the Volume Coefficients and Principled Volume nodes (VOLUME_PHASES, SVM_NODE_* regions).
  source<<"//#if VOLUME_PHASES\n";FIELD(ClosureVolume,param_extra);
  constant("VP_CLOSURE_VOLUME_FOURNIER_FORAND_ID",CLOSURE_VOLUME_FOURNIER_FORAND_ID);constant("VP_CLOSURE_VOLUME_RAYLEIGH_ID",CLOSURE_VOLUME_RAYLEIGH_ID);
  constant("VP_CLOSURE_VOLUME_DRAINE_ID",CLOSURE_VOLUME_DRAINE_ID);constant("VP_CLOSURE_VOLUME_MIE_ID",CLOSURE_VOLUME_MIE_ID);
  source<<"//#endif\n//#if SVM_NODE_VOLUME_COEFFICIENTS\n";
  constant("VC_OP_VOLUME_COEFFICIENTS",NODE_VOLUME_COEFFICIENTS);SIZE(VolumeCoefficients);FIELD(VolumeCoefficients,closure_type);
  FIELD(VolumeCoefficients,absorption_coeffs);FIELD(VolumeCoefficients,emission_coeffs);FIELD(VolumeCoefficients,param1);FIELD(VolumeCoefficients,param_extra);
  FIELD(VolumeCoefficients,mix_weight_offset);
  source<<"//#endif\n//#if SVM_NODE_PRINCIPLED_VOLUME\n";
  constant("VC_OP_PRINCIPLED_VOLUME",NODE_PRINCIPLED_VOLUME);SIZE(PrincipledVolume);FIELD(PrincipledVolume,absorption_color);
  FIELD(PrincipledVolume,emission_color);FIELD(PrincipledVolume,blackbody_tint);FIELD(PrincipledVolume,density);FIELD(PrincipledVolume,anisotropy);
  FIELD(PrincipledVolume,emission);FIELD(PrincipledVolume,blackbody);FIELD(PrincipledVolume,temperature);FIELD(PrincipledVolume,attr_density);
  FIELD(PrincipledVolume,attr_color);FIELD(PrincipledVolume,attr_temperature);FIELD(PrincipledVolume,mix_weight_offset);
  source<<"//#endif\n";
  SIZE(ClosureBsdf);FIELD(ClosureBsdf,closure_type);FIELD(ClosureBsdf,mix_weight_offset);
  SIZE(DiffuseBsdfData);FIELD(DiffuseBsdfData,color);FIELD(DiffuseBsdfData,roughness);FIELD(DiffuseBsdfData,normal_offset);
  SIZE(SimpleBsdfData);FIELD(SimpleBsdfData,param1);FIELD(SimpleBsdfData,normal_offset);
  SIZE(GlossyBsdfData);FIELD(GlossyBsdfData,color);FIELD(GlossyBsdfData,roughness);FIELD(GlossyBsdfData,anisotropy);FIELD(GlossyBsdfData,rotation);FIELD(GlossyBsdfData,normal_offset);FIELD(GlossyBsdfData,tangent_offset);
  SIZE(RefractionBsdfData);FIELD(RefractionBsdfData,roughness);FIELD(RefractionBsdfData,ior);FIELD(RefractionBsdfData,normal_offset);
  SIZE(GlassBsdfData);FIELD(GlassBsdfData,color);FIELD(GlassBsdfData,roughness);FIELD(GlassBsdfData,ior);FIELD(GlassBsdfData,normal_offset);FIELD(GlassBsdfData,thin_film_thickness);FIELD(GlassBsdfData,thin_film_ior);
  SIZE(ClosureSetNormal);FIELD(ClosureSetNormal,direction_offset);FIELD(ClosureSetNormal,normal_offset);
  SIZE(EmissionWeight);FIELD(EmissionWeight,color);FIELD(EmissionWeight,strength);
  SIZE(MixClosure);FIELD(MixClosure,fac);FIELD(MixClosure,in_weight_offset);FIELD(MixClosure,weight1_offset);FIELD(MixClosure,weight2_offset);
  SIZE(TexChecker);FIELD(TexChecker,color1);FIELD(TexChecker,color2);FIELD(TexChecker,scale);FIELD(TexChecker,co);FIELD(TexChecker,color_offset);FIELD(TexChecker,fac_offset);
  SIZE(TexGradient);FIELD(TexGradient,gradient_type);FIELD(TexGradient,co);FIELD(TexGradient,fac_offset);FIELD(TexGradient,color_offset);
  SIZE(Gamma);FIELD(Gamma,color);FIELD(Gamma,gamma);FIELD(Gamma,out_offset);
  SIZE(Invert);FIELD(Invert,color);FIELD(Invert,fac);FIELD(Invert,out_offset);
  SIZE(BrightContrast);FIELD(BrightContrast,color);FIELD(BrightContrast,bright);FIELD(BrightContrast,contrast);FIELD(BrightContrast,out_offset);
  // Texture, colour and utility nodes (wavefront_svm_nodes.wgsl). The region
  // keeps modules of scenes without these nodes byte-identical.
  source<<"//#if SVM_NODES_ANY\n";
  SIZE(TexNoise);FIELD(TexNoise,dimensions);FIELD(TexNoise,noise_type);FIELD(TexNoise,normalize);FIELD(TexNoise,w);FIELD(TexNoise,scale);FIELD(TexNoise,detail);
  FIELD(TexNoise,roughness);FIELD(TexNoise,lacunarity);FIELD(TexNoise,offset);FIELD(TexNoise,gain);FIELD(TexNoise,distortion);FIELD(TexNoise,vector);FIELD(TexNoise,value_offset);FIELD(TexNoise,color_offset);
  SIZE(TexWhiteNoise);FIELD(TexWhiteNoise,dimensions);FIELD(TexWhiteNoise,vector);FIELD(TexWhiteNoise,w);FIELD(TexWhiteNoise,value_offset);FIELD(TexWhiteNoise,color_offset);
  SIZE(TexWave);FIELD(TexWave,wave_type);FIELD(TexWave,bands_direction);FIELD(TexWave,rings_direction);FIELD(TexWave,profile);FIELD(TexWave,scale);FIELD(TexWave,distortion);
  FIELD(TexWave,detail);FIELD(TexWave,dscale);FIELD(TexWave,droughness);FIELD(TexWave,phase);FIELD(TexWave,co);FIELD(TexWave,color_offset);FIELD(TexWave,fac_offset);
  SIZE(TexMagic);FIELD(TexMagic,scale);FIELD(TexMagic,distortion);FIELD(TexMagic,depth);FIELD(TexMagic,co);FIELD(TexMagic,color_offset);FIELD(TexMagic,fac_offset);
  SIZE(TexBrick);FIELD(TexBrick,color1);FIELD(TexBrick,color2);FIELD(TexBrick,mortar);FIELD(TexBrick,scale);FIELD(TexBrick,mortar_size);FIELD(TexBrick,bias);FIELD(TexBrick,brick_width);
  FIELD(TexBrick,row_height);FIELD(TexBrick,mortar_smooth);FIELD(TexBrick,offset_amount);FIELD(TexBrick,squash_amount);FIELD(TexBrick,offset_frequency);FIELD(TexBrick,squash_frequency);
  FIELD(TexBrick,co);FIELD(TexBrick,color_offset);FIELD(TexBrick,fac_offset);
  SIZE(Mapping);FIELD(Mapping,mapping_type);FIELD(Mapping,vector);FIELD(Mapping,location);FIELD(Mapping,rotation);FIELD(Mapping,scale);FIELD(Mapping,result_offset);
  SIZE(TextureMapping);FIELD(TextureMapping,vec_offset);FIELD(TextureMapping,out_offset);FIELD(TextureMapping,tfm);
  SIZE(MinMax);FIELD(MinMax,vec_offset);FIELD(MinMax,out_offset);FIELD(MinMax,mn);FIELD(MinMax,mx);
  SIZE(MapRange);FIELD(MapRange,range_type);FIELD(MapRange,value);FIELD(MapRange,from_min);FIELD(MapRange,from_max);FIELD(MapRange,to_min);FIELD(MapRange,to_max);FIELD(MapRange,steps);FIELD(MapRange,result_offset);
  SIZE(VectorMapRange);FIELD(VectorMapRange,range_type);FIELD(VectorMapRange,use_clamp);FIELD(VectorMapRange,value);FIELD(VectorMapRange,from_min);FIELD(VectorMapRange,from_max);
  FIELD(VectorMapRange,to_min);FIELD(VectorMapRange,to_max);FIELD(VectorMapRange,steps);FIELD(VectorMapRange,result_offset);
  SIZE(Mix);FIELD(Mix,mix_type);FIELD(Mix,c1);FIELD(Mix,c2);FIELD(Mix,fac);FIELD(Mix,result_offset);
  SIZE(MixColor);FIELD(MixColor,blend_type);FIELD(MixColor,a);FIELD(MixColor,b);FIELD(MixColor,fac);FIELD(MixColor,use_clamp);FIELD(MixColor,use_clamp_result);FIELD(MixColor,result_offset);
  SIZE(MixFloat);FIELD(MixFloat,fac);FIELD(MixFloat,a);FIELD(MixFloat,b);FIELD(MixFloat,use_clamp);FIELD(MixFloat,result_offset);
  SIZE(MixVector);FIELD(MixVector,a);FIELD(MixVector,b);FIELD(MixVector,fac);FIELD(MixVector,use_clamp);FIELD(MixVector,result_offset);
  SIZE(MixVectorNonUniform);FIELD(MixVectorNonUniform,a);FIELD(MixVectorNonUniform,b);FIELD(MixVectorNonUniform,fac);FIELD(MixVectorNonUniform,use_clamp);FIELD(MixVectorNonUniform,result_offset);
  SIZE(HSV);FIELD(HSV,color);FIELD(HSV,hue);FIELD(HSV,sat);FIELD(HSV,val);FIELD(HSV,fac);FIELD(HSV,out_color_offset);
  SIZE(SeparateColor);FIELD(SeparateColor,color_type);FIELD(SeparateColor,color);FIELD(SeparateColor,red_offset);FIELD(SeparateColor,green_offset);FIELD(SeparateColor,blue_offset);
  SIZE(CombineColor);FIELD(CombineColor,color_type);FIELD(CombineColor,red);FIELD(CombineColor,green);FIELD(CombineColor,blue);FIELD(CombineColor,color_offset);
  SIZE(RGBRamp);FIELD(RGBRamp,table_size);FIELD(RGBRamp,fac);FIELD(RGBRamp,interpolate);FIELD(RGBRamp,color_offset);FIELD(RGBRamp,alpha_offset);
  SIZE(Curves);FIELD(Curves,color);FIELD(Curves,fac);FIELD(Curves,min_x);FIELD(Curves,max_x);FIELD(Curves,table_size);FIELD(Curves,extrapolate);FIELD(Curves,out_offset);
  SIZE(FloatCurve);FIELD(FloatCurve,fac);FIELD(FloatCurve,value_in);FIELD(FloatCurve,min_x);FIELD(FloatCurve,max_x);FIELD(FloatCurve,table_size);FIELD(FloatCurve,extrapolate);FIELD(FloatCurve,out_offset);
  SIZE(Fresnel);FIELD(Fresnel,ior);FIELD(Fresnel,normal_offset);FIELD(Fresnel,out_offset);
  SIZE(LayerWeight);FIELD(LayerWeight,weight_type);FIELD(LayerWeight,blend);FIELD(LayerWeight,normal_offset);FIELD(LayerWeight,out_offset);
  SIZE(VectorRotate);FIELD(VectorRotate,rotate_type);FIELD(VectorRotate,vector);FIELD(VectorRotate,center);FIELD(VectorRotate,axis);FIELD(VectorRotate,rotation);
  FIELD(VectorRotate,angle);FIELD(VectorRotate,invert);FIELD(VectorRotate,result_offset);
  SIZE(Blackbody);FIELD(Blackbody,temperature);FIELD(Blackbody,color_offset);
  SIZE(Normal);FIELD(Normal,in_normal);FIELD(Normal,out_normal_offset);FIELD(Normal,out_dot_offset);FIELD(Normal,direction_x);
  static_assert(offsetof(SVMNodeNormal,direction_y)==offsetof(SVMNodeNormal,direction_x)+4&&offsetof(SVMNodeNormal,direction_z)==offsetof(SVMNodeNormal,direction_x)+8);
  SIZE(ObjectInfo);FIELD(ObjectInfo,info_type);FIELD(ObjectInfo,out_offset);
  SIZE(Camera);FIELD(Camera,vector_offset);FIELD(Camera,zdepth_offset);FIELD(Camera,distance_offset);
  SIZE(SceneTime);FIELD(SceneTime,seconds_out);FIELD(SceneTime,frame_out);
  SIZE(VectorTransform);FIELD(VectorTransform,transform_type);FIELD(VectorTransform,convert_from);FIELD(VectorTransform,convert_to);FIELD(VectorTransform,vector_in);FIELD(VectorTransform,vector_out_offset);
  SIZE(LightPath);FIELD(LightPath,path_type);FIELD(LightPath,out_offset);
  // The Light Falloff node's opcode and types under their own names (scenes without the node keep their WGSL).
  source<<"//#if SVM_NODE_LIGHT_FALLOFF\n";SIZE(LightFalloff);FIELD(LightFalloff,falloff_type);FIELD(LightFalloff,strength);FIELD(LightFalloff,smooth);
  FIELD(LightFalloff,out_offset);constant("LF_OPCODE",NODE_LIGHT_FALLOFF);constant("LF_LINEAR",NODE_LIGHT_FALLOFF_LINEAR);
  constant("LF_CONSTANT",NODE_LIGHT_FALLOFF_CONSTANT);source<<"//#endif\n";
  source<<"//#if SVM_NODE_IES\n";SIZE(IES);FIELD(IES,strength);FIELD(IES,slot);FIELD(IES,vector_offset);FIELD(IES,fac_offset);
  constant("IES_OPCODE",NODE_IES);source<<"//#endif\n";
  source<<"//#if SVM_TEX_SKY\n";SIZE(TexSky);FIELD(TexSky,sky_type);FIELD(TexSky,dir_offset);FIELD(TexSky,out_offset);
  SIZE(TexSkyPreethamData);FIELD(TexSkyPreethamData,phi);FIELD(TexSkyPreethamData,theta);FIELD(TexSkyPreethamData,radiance_x);
  FIELD(TexSkyPreethamData,radiance_y);FIELD(TexSkyPreethamData,radiance_z);FIELD(TexSkyPreethamData,config_x);FIELD(TexSkyPreethamData,config_y);
  FIELD(TexSkyPreethamData,config_z);SIZE(TexSkyNishitaData);FIELD(TexSkyNishitaData,pixel_bottom_x);FIELD(TexSkyNishitaData,pixel_top_x);
  FIELD(TexSkyNishitaData,sun_elevation);FIELD(TexSkyNishitaData,sun_rotation);FIELD(TexSkyNishitaData,angular_diameter);
  FIELD(TexSkyNishitaData,sun_intensity);FIELD(TexSkyNishitaData,earth_intersection_angle);FIELD(TexSkyNishitaData,texture_id);
  constant("SKY_OPCODE",NODE_TEX_SKY);constant("SKY_PREETHAM",NODE_SKY_PREETHAM);constant("SKY_HOSEK",NODE_SKY_HOSEK);
  constant("SKY_PATH_RAY_IMPORTANCE_BAKE",PATH_RAY_IMPORTANCE_BAKE);constant("SKY_D_use_sun_guiding",offsetof(KernelData,background.use_sun_guiding));
  source<<"//#endif\n";
  SIZE(TexVoronoi);FIELD(TexVoronoi,dimensions);FIELD(TexVoronoi,feature);FIELD(TexVoronoi,metric);FIELD(TexVoronoi,w);FIELD(TexVoronoi,scale);FIELD(TexVoronoi,detail);
  FIELD(TexVoronoi,roughness);FIELD(TexVoronoi,lacunarity);FIELD(TexVoronoi,smoothness);FIELD(TexVoronoi,exponent);FIELD(TexVoronoi,randomness);FIELD(TexVoronoi,normalize);
  FIELD(TexVoronoi,coord);FIELD(TexVoronoi,distance_offset);FIELD(TexVoronoi,color_offset);FIELD(TexVoronoi,position_offset);FIELD(TexVoronoi,w_out_offset);FIELD(TexVoronoi,radius_offset);
  SIZE(TexGabor);FIELD(TexGabor,gabor_type);FIELD(TexGabor,orientation_3d);FIELD(TexGabor,scale);FIELD(TexGabor,frequency);FIELD(TexGabor,anisotropy);
  FIELD(TexGabor,orientation_2d);FIELD(TexGabor,coordinates);FIELD(TexGabor,value_offset);FIELD(TexGabor,phase_offset);FIELD(TexGabor,intensity_offset);
  source<<"//#endif\n//#if SVM_NODE_HAIR_INFO\n";
  SIZE(HairInfo);FIELD(HairInfo,info_type);FIELD(HairInfo,out_offset);
  constant("CV_NODE_HAIR_INFO",NODE_HAIR_INFO);constant("CV_INFO_CURVE_IS_STRAND",NODE_INFO_CURVE_IS_STRAND);constant("CV_INFO_CURVE_THICKNESS",NODE_INFO_CURVE_THICKNESS);constant("CV_INFO_CURVE_TANGENT_NORMAL",NODE_INFO_CURVE_TANGENT_NORMAL);constant("HI_PRIMITIVE_CURVE",PRIMITIVE_CURVE);
  source<<"//#endif\n//#if CURVES\n";constant("CV_ATTR_ELEMENT_CURVE_KEY",ATTR_ELEMENT_CURVE_KEY);constant("CV_ATTR_ELEMENT_CURVE",ATTR_ELEMENT_CURVE);source<<"//#endif\n//#if SVM_NODE_POINT_INFO\n";
  SIZE(PointInfo);FIELD(PointInfo,info_type);FIELD(PointInfo,out_offset);
  constant("PT_NODE_POINT_INFO",NODE_POINT_INFO);constant("PT_INFO_POINT_POSITION",NODE_INFO_POINT_POSITION);constant("PT_INFO_POINT_RADIUS",NODE_INFO_POINT_RADIUS);
  source<<"//#endif\n//#if SVM_NODE_PARTICLE_INFO\n";
  SIZE(ParticleInfo);FIELD(ParticleInfo,info_type);FIELD(ParticleInfo,out_offset);
  constant("PI_NODE_PARTICLE_INFO",NODE_PARTICLE_INFO);constant("PI_INFO_PAR_INDEX",NODE_INFO_PAR_INDEX);constant("PI_INFO_PAR_RANDOM",NODE_INFO_PAR_RANDOM);constant("PI_INFO_PAR_AGE",NODE_INFO_PAR_AGE);constant("PI_INFO_PAR_LIFETIME",NODE_INFO_PAR_LIFETIME);
  constant("PI_INFO_PAR_LOCATION",NODE_INFO_PAR_LOCATION);constant("PI_INFO_PAR_SIZE",NODE_INFO_PAR_SIZE);constant("PI_INFO_PAR_VELOCITY",NODE_INFO_PAR_VELOCITY);constant("PI_INFO_PAR_ANGULAR_VELOCITY",NODE_INFO_PAR_ANGULAR_VELOCITY);
  constant("PI_O_particle_index",offsetof(KernelObject,particle_index));constant("PI_P_index",offsetof(KernelParticle,index));constant("PI_P_age",offsetof(KernelParticle,age));constant("PI_P_lifetime",offsetof(KernelParticle,lifetime));
  constant("PI_P_size",offsetof(KernelParticle,size));constant("PI_P_location",offsetof(KernelParticle,location));constant("PI_P_velocity",offsetof(KernelParticle,velocity));constant("PI_P_angular_velocity",offsetof(KernelParticle,angular_velocity));
  source<<"//#endif\n//#if SVM_NODES_ANY\n";
  SIZE(Wavelength);FIELD(Wavelength,wavelength);FIELD(Wavelength,color_offset);
  SIZE(Wireframe);FIELD(Wireframe,in_size);FIELD(Wireframe,bump_filter_width);FIELD(Wireframe,use_pixel_size);FIELD(Wireframe,bump_offset);FIELD(Wireframe,out_fac_offset);
  source<<"//#endif\n";
#undef SIZE
#undef FIELD
#define ENUM(name) constant(#name,uint32_t(name))
  // Enum constants are taken from the same native headers as the bytecode compiler.
#include "svm_enums.inc"
  source<<"//#if SVM_NODES_ANY\n";
#include "svm_nodes_enums.inc"
  source<<"//#endif\n";
  ENUM(CLOSURE_BSDF_DIFFUSE_ID);ENUM(CLOSURE_BSDF_OREN_NAYAR_ID);ENUM(CLOSURE_BSDF_TRANSLUCENT_ID);
  ENUM(CLOSURE_BSDF_TRANSPARENT_ID);ENUM(CLOSURE_HOLDOUT_ID);
  ENUM(CLOSURE_BSDF_MICROFACET_GGX_ID);ENUM(CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID);
  ENUM(CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID);ENUM(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);
  ENUM(CLOSURE_VOLUME_ABSORPTION_ID);ENUM(CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID);
  ENUM(SR_SCATTER);ENUM(SR_EXTINCTION);ENUM(SR_IS_VOLUME_SHADER_EVAL);ENUM(PATH_RAY_EXTINCTION);
  ENUM(SR_EMISSION);ENUM(SR_BSDF);ENUM(SR_BSDF_HAS_EVAL);ENUM(SR_BSDF_HAS_TRANSMISSION);ENUM(SR_TRANSPARENT);ENUM(SR_HOLDOUT);
  // The Ray Portal BSDF (RAY_PORTAL; bsdf_ray_portal.h, svm/closure.h SVMNodeRayPortalBsdfData).
  source<<"//#if RAY_PORTAL\n";constant("RP_CLOSURE_BSDF_RAY_PORTAL_ID",CLOSURE_BSDF_RAY_PORTAL_ID);constant("RP_SR_RAY_PORTAL",SR_RAY_PORTAL);
  constant("RP_LABEL_RAY_PORTAL",LABEL_RAY_PORTAL);constant("RP_PATH_RAY_MIS_SKIP",PATH_RAY_MIS_SKIP);constant("SZ_RayPortalBsdfData",sizeof(SVMNodeRayPortalBsdfData));
  constant("N_RayPortalBsdfData_direction",offsetof(SVMNodeRayPortalBsdfData,direction));constant("N_RayPortalBsdfData_position_offset",offsetof(SVMNodeRayPortalBsdfData,position_offset));
  source<<"//#endif\n";
  ENUM(SD_USE_BUMP_MAP_CORRECTION);ENUM(PATH_RAY_TERMINATE);ENUM(PATH_RAY_EMISSION);ENUM(PATH_RAY_VISIBILITY_SHADOW);
  // Node-specific copies (prefixed: other stage layouts define some of these names).
  source<<"//#if SVM_NODES_ANY\n";
  constant("SVMN_PATH_RAY_VISIBILITY_CAMERA",PATH_RAY_VISIBILITY_CAMERA);constant("SVMN_PATH_RAY_VISIBILITY_DIFFUSE",PATH_RAY_VISIBILITY_DIFFUSE);
  constant("SVMN_PATH_RAY_VISIBILITY_GLOSSY",PATH_RAY_VISIBILITY_GLOSSY);constant("SVMN_PATH_RAY_VISIBILITY_TRANSMIT",PATH_RAY_VISIBILITY_TRANSMIT);
  constant("SVMN_PATH_RAY_VISIBILITY_VOLUME_SCATTER",PATH_RAY_VISIBILITY_VOLUME_SCATTER);constant("SVMN_PATH_RAY_SINGULAR",PATH_RAY_SINGULAR);
  constant("SVMN_PATH_RAY_REFLECT",PATH_RAY_REFLECT);constant("SVMN_KERNEL_FEATURE_NODE_PORTAL",KERNEL_FEATURE_NODE_PORTAL);
  constant("SVMN_SH_pass_id",offsetof(KernelShader,pass_id));constant("SVMN_O_color",offsetof(KernelObject,color));constant("SVMN_O_alpha",offsetof(KernelObject,alpha));
  constant("SVMN_O_pass_id",offsetof(KernelObject,pass_id));constant("SVMN_O_random_number",offsetof(KernelObject,random_number));
  constant("SVMN_D_scene_time_time",offsetof(KernelData,scene_time.time));constant("SVMN_D_scene_time_frame",offsetof(KernelData,scene_time.frame));
  constant("SVMN_D_film_xyz_to_r",offsetof(KernelData,film.xyz_to_r));constant("SVMN_D_film_xyz_to_g",offsetof(KernelData,film.xyz_to_g));constant("SVMN_D_film_xyz_to_b",offsetof(KernelData,film.xyz_to_b));
  source<<"//#endif\n";
#undef ENUM
  source<<"const SVM_SURFACE:bool="<<(surface?"true":"false")<<";\n";
  // NODE_CLOSURE_BSDF: closure setup in surface modules, error 2 elsewhere (what
  // `if(SVM_SURFACE)` compiled to). Emitting the call only where it can run keeps
  // the dead Principled/microfacet setup out of the background module's text, so
  // the pipeline size guard measures the program the driver compiles.
  source<<"fn svm_closure_bsdf(s:ptr<function,SvmState>,node:u32,flag:u32,visibility:u32){"
        <<(surface?"svm_bsdf(s,node,flag,visibility);":"svm_error(s,2u);")<<"}\n";
  constant("SVM_CLOSURE_CAPACITY",surface?MAX_CLOSURE:1);
  constant("SVM_CLOSURE_BYTES",sizeof(SurfaceClosureRecord));
#define C(member) constant("C_" #member,offsetof(SurfaceClosureRecord,member))
  C(alpha_x);C(alpha_y);C(ior);C(energy_scale);C(T);C(fresnel_f0);C(reflection_tint);C(transmission_tint);
#undef C
  // Pinned FresnelGeneralizedSchlick consumes one native extra closure slot.
  // The independent original-Cycles reference asserts this on every target.
  constant("GLASS_EXTRA_CLOSURES",1);
  constant("D_caustics_refractive",offsetof(KernelData,integrator.caustics_refractive));
  constant("D_ggx_gen_schlick_ior_s",offsetof(KernelData,tables.ggx_gen_schlick_ior_s));
  constant("D_caustics_reflective",offsetof(KernelData,integrator.caustics_reflective));
  constant("D_ggx_E",offsetof(KernelData,tables.ggx_E));
  constant("D_ggx_Eavg",offsetof(KernelData,tables.ggx_Eavg));
  constant("D_max_closures",offsetof(KernelData,max_closures));
  constant("O_volume_density",offsetof(KernelObject,volume_density));
  constant("O_dupli_generated",offsetof(KernelObject,dupli_generated));constant("O_dupli_uv",offsetof(KernelObject,dupli_uv));
  constant("D_film_rgb_to_y",offsetof(KernelData,film.rgb_to_y));
#define II(member) constant("II_" #member,offsetof(KernelImageInfo,member))
  II(data);II(data_type);II(interpolation);II(extension);II(width);II(height);
#undef II
#define IT(member) constant("IT_" #member,offsetof(KernelImageTexture,member))
  IT(image_info_id);IT(tile_descriptor_offset);IT(use_transform_3d);
#undef IT
  constant("IU_tile",offsetof(KernelImageUDIM,tile));constant("IU_image_texture_id",offsetof(KernelImageUDIM,image_texture_id));
  constant("O_attribute_map_offset",offsetof(KernelObject,attribute_map_offset));
  constant("AM_id",offsetof(AttributeMap,id));constant("AM_offset",offsetof(AttributeMap,offset));
  constant("AM_element",offsetof(AttributeMap,element));constant("AM_type",offsetof(AttributeMap,type));
  constant("D_film_is_rec709",offsetof(KernelData,film.is_rec709));
  constant("D_film_rec709_to_r",offsetof(KernelData,film.rec709_to_r));
  constant("D_film_rec709_to_g",offsetof(KernelData,film.rec709_to_g));
  constant("D_film_rec709_to_b",offsetof(KernelData,film.rec709_to_b));
  constant("D_cam_type",offsetof(KernelData,cam.type));
  constant("D_cam_worldtocamera",offsetof(KernelData,cam.worldtocamera));
  constant("D_cam_worldtondc",offsetof(KernelData,cam.worldtondc));
  // The panoramic cameras' inverse mappings (CAMERA_PANORAMA_INVERSE, wavefront_svm.wgsl cpi_direction_to_panorama).
  source<<"//#if CAMERA_PANORAMA_INVERSE\n";
  constant("CPI_CAMERA_PANORAMA",CAMERA_PANORAMA);constant("CPI_D_motion_pass_pre",offsetof(KernelData,cam.motion_pass_pre));
  constant("CPI_D_motion_pass_post",offsetof(KernelData,cam.motion_pass_post));
  source<<"//#endif\n";
  constant("SZ_PackedTransform",sizeof(PackedTransform));
  // Principled BSDF layout, enums and lookup tables: emitted only into modules
  // whose scene can reach the Principled handler (pipelines.h specialization).
  source<<"//#if SVM_BSDF_PRINCIPLED\n";
#define FIELD(member) constant("N_PrincipledBsdfData_" #member,offsetof(SVMNodePrincipledBsdfData,member))
  constant("SZ_PrincipledBsdfData",sizeof(SVMNodePrincipledBsdfData));
  FIELD(distribution);FIELD(ior);FIELD(roughness);FIELD(sheen_weight);FIELD(coat_weight);FIELD(metallic);
  FIELD(transmission_weight);FIELD(subsurface_weight);FIELD(base_color);FIELD(alpha);FIELD(diffuse_roughness);
  FIELD(normal_offset);FIELD(tangent_offset);FIELD(coat_normal_offset);FIELD(specular_tint);FIELD(specular_ior_level);
  FIELD(anisotropic);FIELD(anisotropic_rotation);FIELD(emission_color);FIELD(emission_strength);FIELD(sheen_tint);
  FIELD(sheen_roughness);FIELD(coat_tint);FIELD(coat_roughness);FIELD(coat_ior);FIELD(thin_film_thickness);FIELD(thin_film_ior);FIELD(thin_wall);
#undef FIELD
  // The WGSL reads SVMInputInt's stack offset byte at +4.
  static_assert(offsetof(SVMInputInt,value)==0&&offsetof(SVMInputInt,offset)==4);
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(CLOSURE_BSDF_PRINCIPLED_ID);ENUM(CLOSURE_BSDF_SHEEN_ID);ENUM(CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID);
#undef ENUM
  constant("PRINCIPLED_CLOSURE_NONE",uint32_t(CLOSURE_NONE_ID));
  constant("PRINCIPLED_THIN_GLASS",uint32_t(CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID));
  // Lookup tables of the shared Fresnel albedo and energy preservation helpers
  // (Principled and Metallic BSDF). Principled modules keep the same text.
  source<<"//#endif\n//#if SVM_BSDF_FRESNEL_MODELS\n";
  constant("D_ggx_gen_schlick_s",offsetof(KernelData,tables.ggx_gen_schlick_s));
  constant("D_ggx_glass_E",offsetof(KernelData,tables.ggx_glass_E));constant("D_ggx_glass_Eavg",offsetof(KernelData,tables.ggx_glass_Eavg));
  constant("D_ggx_glass_inv_E",offsetof(KernelData,tables.ggx_glass_inv_E));constant("D_ggx_glass_inv_Eavg",offsetof(KernelData,tables.ggx_glass_inv_Eavg));
  source<<"//#endif\n//#if SVM_BSDF_PRINCIPLED\n";
  constant("D_sheen_ltc",offsetof(KernelData,tables.sheen_ltc));
  source<<"//#endif\n";
  // Subsurface scattering (svm/closure.h: the Principled BSDF's subsurface lobe and the Subsurface Scattering node,
  // closure/bssrdf.h): emitted only into modules whose scene reaches a BSSRDF closure.
  source<<"//#if SUBSURFACE\n";
  constant("N_PrincipledBsdfData_subsurface_method",offsetof(SVMNodePrincipledBsdfData,subsurface_method));
  constant("N_PrincipledBsdfData_subsurface_radius",offsetof(SVMNodePrincipledBsdfData,subsurface_radius));
  constant("N_PrincipledBsdfData_subsurface_scale",offsetof(SVMNodePrincipledBsdfData,subsurface_scale));
  constant("N_PrincipledBsdfData_subsurface_ior",offsetof(SVMNodePrincipledBsdfData,subsurface_ior));
  constant("N_PrincipledBsdfData_subsurface_anisotropy",offsetof(SVMNodePrincipledBsdfData,subsurface_anisotropy));
  constant("SZ_BssrdfData",sizeof(SVMNodeBssrdfData));constant("N_BssrdfData_radius",offsetof(SVMNodeBssrdfData,radius));
  constant("N_BssrdfData_scale",offsetof(SVMNodeBssrdfData,scale));constant("N_BssrdfData_ior",offsetof(SVMNodeBssrdfData,ior));
  constant("N_BssrdfData_anisotropy",offsetof(SVMNodeBssrdfData,anisotropy));constant("N_BssrdfData_roughness",offsetof(SVMNodeBssrdfData,roughness));
  constant("N_BssrdfData_normal_offset",offsetof(SVMNodeBssrdfData,normal_offset));
  constant("SS_CLOSURE_BURLEY",CLOSURE_BSSRDF_BURLEY_ID);constant("SS_CLOSURE_RANDOM_WALK",CLOSURE_BSSRDF_RANDOM_WALK_ID);
  constant("SS_CLOSURE_RANDOM_WALK_LEGACY",CLOSURE_BSSRDF_RANDOM_WALK_LEGACY_ID);constant("SS_CLOSURE_RANDOM_WALK_SKIN",CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID);
  constant("SS_SR_BSSRDF",SR_BSSRDF);constant("SS_CLOSURE_NONE",CLOSURE_NONE_ID);constant("SS_PATH_RAY_DIFFUSE_ANCESTOR",PATH_RAY_DIFFUSE_ANCESTOR);
  static_assert(CLOSURE_BSSRDF_BURLEY_ID==CLOSURE_BSDF_TRANSPARENT_ID+1&&CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID==CLOSURE_BSSRDF_BURLEY_ID+3,"BSSRDF closure range");
  source<<"//#endif\n";
  // Metallic BSDF layout and closure enums (svm/node_types.h SVMNodeMetallicBsdfData).
  source<<"//#if SVM_BSDF_METALLIC\n";
#define FIELD(member) constant("N_MetallicBsdfData_" #member,offsetof(SVMNodeMetallicBsdfData,member))
  constant("SZ_MetallicBsdfData",sizeof(SVMNodeMetallicBsdfData));
  FIELD(distribution);FIELD(base_ior);FIELD(edge_tint_k);FIELD(roughness);FIELD(anisotropy);FIELD(rotation);
  FIELD(thin_film_thickness);FIELD(thin_film_ior);FIELD(normal_offset);FIELD(tangent_offset);
#undef FIELD
  constant("CLOSURE_BSDF_PHYSICAL_CONDUCTOR",uint32_t(CLOSURE_BSDF_PHYSICAL_CONDUCTOR));constant("CLOSURE_BSDF_F82_CONDUCTOR",uint32_t(CLOSURE_BSDF_F82_CONDUCTOR));
  source<<"//#endif\n//#if SVM_BSDF_FAMILIES\n";
  // Closure kinds and the svm/types.h CLOSURE_IS_* range bounds of the shared
  // classification (wavefront_surface.wgsl); FAMILY_ names avoid the enum names
  // other regions emit (CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID, ...).
  constant("CLOSURE_BSDF_MICROFACET_BECKMANN_ID",uint32_t(CLOSURE_BSDF_MICROFACET_BECKMANN_ID));
  constant("CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID",uint32_t(CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID));
  constant("CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID",uint32_t(CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID));
  constant("CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID",uint32_t(CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID));
  constant("CLOSURE_BSDF_ASHIKHMIN_VELVET_ID",uint32_t(CLOSURE_BSDF_ASHIKHMIN_VELVET_ID));
  constant("CLOSURE_BSDF_DIFFUSE_TOON_ID",uint32_t(CLOSURE_BSDF_DIFFUSE_TOON_ID));constant("CLOSURE_BSDF_GLOSSY_TOON_ID",uint32_t(CLOSURE_BSDF_GLOSSY_TOON_ID));
  constant("FAMILY_NONE",uint32_t(CLOSURE_NONE_ID));constant("FAMILY_GLOSSY_LAST",uint32_t(CLOSURE_BSDF_HAIR_REFLECTION_ID));
  constant("FAMILY_TRANSMISSION_LAST",uint32_t(CLOSURE_BSDF_HAIR_TRANSMISSION_ID));constant("FAMILY_THIN_GLASS",uint32_t(CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID));
  constant("FAMILY_GLASS_LAST",uint32_t(CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID));
  source<<"//#endif\n//#if SVM_BSDF_TOON\n";
  constant("SZ_ToonBsdfData",sizeof(SVMNodeToonBsdfData));constant("N_ToonBsdfData_size",offsetof(SVMNodeToonBsdfData,size));
  constant("N_ToonBsdfData_smooth",offsetof(SVMNodeToonBsdfData,smooth));constant("N_ToonBsdfData_normal_offset",offsetof(SVMNodeToonBsdfData,normal_offset));
  source<<"//#endif\n";
  source<<"//#if SVM_BSDF_THIN_FILM\n";
  constant("D_thin_film_table",offsetof(KernelData,tables.thin_film_table));constant("THIN_FILM_TABLE_SIZE",THIN_FILM_TABLE_SIZE);
  source<<"//#endif\n//#if SVM_BSDF_THIN_WALL\n";
  constant("CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID",uint32_t(CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID));
  source<<"//#endif\n//#if SVM_BSDF_HAIR\n";
  // Hair BSDF node (svm/node_types.h SVMNodeHairBsdfData), closure kinds.
#define FIELD(member) constant("N_HairBsdfData_" #member,offsetof(SVMNodeHairBsdfData,member))
  constant("SZ_HairBsdfData",sizeof(SVMNodeHairBsdfData));FIELD(roughness1);FIELD(roughness2);FIELD(offset);FIELD(tangent_offset);
#undef FIELD
  constant("CLOSURE_BSDF_HAIR_REFLECTION_ID",uint32_t(CLOSURE_BSDF_HAIR_REFLECTION_ID));constant("CLOSURE_BSDF_HAIR_TRANSMISSION_ID",uint32_t(CLOSURE_BSDF_HAIR_TRANSMISSION_ID));
  constant("HAIR_PRIMITIVE_CURVE",PRIMITIVE_CURVE);
  source<<"//#endif\n//#if SVM_BSDF_HAIR_CHIANG\n";
  constant("CLOSURE_BSDF_HAIR_CHIANG_ID",uint32_t(CLOSURE_BSDF_HAIR_CHIANG_ID));
  constant("CHIANG_PRIMITIVE_CURVE",PRIMITIVE_CURVE);constant("CHIANG_PRIMITIVE_CURVE_RIBBON",PRIMITIVE_CURVE_RIBBON);
  source<<"//#endif\n//#if SVM_BSDF_HAIR_PRINCIPLED\n";
  // Principled Hair BSDF node, what its models share (svm/node_types.h SVMNodePrincipledHairBsdfData).
#define FIELD(member) constant("N_PrincipledHairBsdfData_" #member,offsetof(SVMNodePrincipledHairBsdfData,member))
  constant("SZ_PrincipledHairBsdfData",sizeof(SVMNodePrincipledHairBsdfData));
  FIELD(parametrization);FIELD(color);FIELD(tint);FIELD(absorption_coefficient);FIELD(roughness);FIELD(random_roughness);FIELD(offset);FIELD(ior);FIELD(random);
  FIELD(melanin);FIELD(melanin_redness);FIELD(coat);FIELD(radial_roughness);FIELD(random_color);FIELD(attr_random);
#undef FIELD
  constant("HAIR_PARAM_REFLECTANCE",uint32_t(NODE_PRINCIPLED_HAIR_REFLECTANCE));constant("HAIR_PARAM_PIGMENT_CONCENTRATION",uint32_t(NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION));
  constant("HAIR_PARAM_DIRECT_ABSORPTION",uint32_t(NODE_PRINCIPLED_HAIR_DIRECT_ABSORPTION));
  constant("FAMILY_HAIR_CHIANG",uint32_t(CLOSURE_BSDF_HAIR_CHIANG_ID));constant("FAMILY_HAIR_HUANG",uint32_t(CLOSURE_BSDF_HAIR_HUANG_ID));
  source<<"//#endif\n";
  // Principled Hair (svm/node_types.h SVMNodePrincipledHairBsdfData).
  source<<"//#if SVM_BSDF_HAIR_HUANG\n";
  // A module with the Chiang model too has the shared names from SVM_BSDF_HAIR_PRINCIPLED above (same values): SHARED leaves them out
  // there, line by line, so a module with the Huang model only keeps this order.
#define SHARED(...) source<<"//#if !SVM_BSDF_HAIR_PRINCIPLED\n";__VA_ARGS__;source<<"//#endif\n"
#define FIELD(member) constant("N_PrincipledHairBsdfData_" #member,offsetof(SVMNodePrincipledHairBsdfData,member))
  SHARED(constant("SZ_PrincipledHairBsdfData",sizeof(SVMNodePrincipledHairBsdfData)));
  SHARED(FIELD(parametrization));SHARED(FIELD(color));SHARED(FIELD(tint));SHARED(FIELD(absorption_coefficient));SHARED(FIELD(roughness));SHARED(FIELD(random_roughness));
  SHARED(FIELD(offset));SHARED(FIELD(ior));SHARED(FIELD(random));SHARED(FIELD(melanin));SHARED(FIELD(melanin_redness));SHARED(FIELD(coat));FIELD(aspect_ratio);
  SHARED(FIELD(radial_roughness));SHARED(FIELD(random_color));FIELD(R);FIELD(TT);FIELD(TRT);SHARED(FIELD(attr_random));FIELD(attr_normal);
#undef FIELD
#undef SHARED
  constant("HH_PARAM_DIRECT_ABSORPTION",NODE_PRINCIPLED_HAIR_DIRECT_ABSORPTION);constant("HH_PARAM_PIGMENT_CONCENTRATION",NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION);constant("HH_PARAM_REFLECTANCE",NODE_PRINCIPLED_HAIR_REFLECTANCE);
  source<<"//#endif\n//#if SVM_BSDF_HAIR_HUANG\n";
  constant("HH_CLOSURE_BSDF_HAIR_HUANG_ID",uint32_t(CLOSURE_BSDF_HAIR_HUANG_ID));constant("HH_PRIMITIVE_CURVE",PRIMITIVE_CURVE);constant("HH_PRIMITIVE_CURVE_RIBBON",PRIMITIVE_CURVE_RIBBON);
  constant("HH_PATH_RAY_VISIBILITY_CAMERA",PATH_RAY_VISIBILITY_CAMERA);
  constant("HH_D_ggx_glass_E",offsetof(KernelData,tables.ggx_glass_E));constant("HH_D_ggx_glass_inv_E",offsetof(KernelData,tables.ggx_glass_inv_E));
  source<<"//#endif\n";
  // A thin-walled Principled BSDF with subsurface: the rough translucent closure (bsdf_thin_subsurface_setup).
  source<<"//#if SVM_BSDF_THIN_SUBSURFACE\n";constant("TS_CLOSURE_BSDF_ROUGH_TRANSLUCENT_ID",uint32_t(CLOSURE_BSDF_ROUGH_TRANSLUCENT_ID));
  constant("TS_PrincipledBsdfData_subsurface_anisotropy",offsetof(SVMNodePrincipledBsdfData,subsurface_anisotropy));source<<"//#endif\n";
  return source.str();
}
static constexpr char svm_surface_fragment[]=
#include "svm_shader.inc"
;
std::string svm_surface_source(){return shader_data_source()+svm_layout(true)+svm_surface_fragment;}
// The volume module (volume.cpp volume_source): every SVM program it runs is a volume shader's (SHADER_TYPE_VOLUME), so
// NODE_CLOSURE_BSDF takes svm_node_closure_bsdf's `shader_type != SHADER_TYPE_SURFACE` branch, svm_node_closure_bsdf_skip
// (svm/closure.h): the data record of the closure type is stepped over and no closure is made. The surface closure
// handler (svm_bsdf) is not called there, so the driver compiles none of its closure setup into the volume entries.
std::string svm_volume_source(){
  std::string layout=svm_layout(true);const std::string call="svm_bsdf(s,node,flag,visibility);";
  const size_t at=layout.find(call);if(at==std::string::npos||layout.find(call,at+1)!=std::string::npos)throw std::runtime_error("Volume SVM layout: closure BSDF call");
  std::ostringstream skip;skip<<"var bytes="<<sizeof(SVMNodeSimpleBsdfData)<<"u;switch(load_word(node+N_ClosureBsdf_closure_type)){";
  auto kinds=[&](std::initializer_list<ClosureType> types,size_t bytes){const char *separator="case ";for(auto t:types){skip<<separator<<uint32_t(t)<<"u";separator=",";}skip<<":{bytes="<<bytes<<"u;}";};
  kinds({CLOSURE_BSDF_PRINCIPLED_ID},sizeof(SVMNodePrincipledBsdfData));
  kinds({CLOSURE_BSDF_HAIR_CHIANG_ID,CLOSURE_BSDF_HAIR_HUANG_ID},sizeof(SVMNodePrincipledHairBsdfData));
  kinds({CLOSURE_BSDF_PHYSICAL_CONDUCTOR,CLOSURE_BSDF_F82_CONDUCTOR},sizeof(SVMNodeMetallicBsdfData));
  kinds({CLOSURE_BSDF_DIFFUSE_ID,CLOSURE_BSDF_OREN_NAYAR_ID,CLOSURE_BSDF_BURLEY_ID},sizeof(SVMNodeDiffuseBsdfData));
  kinds({CLOSURE_BSDF_SHEEN_ID,CLOSURE_BSDF_ASHIKHMIN_VELVET_ID,CLOSURE_BSDF_TRANSLUCENT_ID,CLOSURE_BSDF_TRANSPARENT_ID},sizeof(SVMNodeSimpleBsdfData));
  kinds({CLOSURE_BSDF_RAY_PORTAL_ID},sizeof(SVMNodeRayPortalBsdfData));
  kinds({CLOSURE_BSDF_MICROFACET_GGX_ID,CLOSURE_BSDF_MICROFACET_BECKMANN_ID,CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID,CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID},sizeof(SVMNodeGlossyBsdfData));
  kinds({CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID,CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID},sizeof(SVMNodeRefractionBsdfData));
  kinds({CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID,CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID,CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID},sizeof(SVMNodeGlassBsdfData));
  kinds({CLOSURE_BSDF_GLOSSY_TOON_ID,CLOSURE_BSDF_DIFFUSE_TOON_ID},sizeof(SVMNodeToonBsdfData));
  kinds({CLOSURE_BSDF_HAIR_REFLECTION_ID,CLOSURE_BSDF_HAIR_TRANSMISSION_ID},sizeof(SVMNodeHairBsdfData));
  kinds({CLOSURE_BSSRDF_BURLEY_ID,CLOSURE_BSSRDF_RANDOM_WALK_ID,CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID},sizeof(SVMNodeBssrdfData));
  skip<<"default:{}}_=svm_take(s,bytes);";
  layout.replace(at,call.size(),skip.str());
  return shader_data_source()+layout+svm_surface_fragment;
}
}
