// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront.h"
#include "surface_closure.h"
#include "kernel/types.h"
#include "util/types_image.h"
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
  SIZE(Attr);FIELD(Attr,attr);FIELD(Attr,out_offset);FIELD(Attr,output_type);FIELD(Attr,bump_offset);FIELD(Attr,store_derivatives);FIELD(Attr,bump_filter_width);
  SIZE(NormalMap);FIELD(NormalMap,space);FIELD(NormalMap,invert_green);FIELD(NormalMap,use_original_base);FIELD(NormalMap,attr);FIELD(NormalMap,attr_sign);FIELD(NormalMap,color);FIELD(NormalMap,strength);FIELD(NormalMap,normal_offset);
  SIZE(Tangent);FIELD(Tangent,direction_type);FIELD(Tangent,axis);FIELD(Tangent,attr);FIELD(Tangent,tangent_offset);
  SIZE(TexImage);FIELD(TexImage,id);FIELD(TexImage,projection);FIELD(TexImage,flags);FIELD(TexImage,co);FIELD(TexImage,out_offset);FIELD(TexImage,alpha_offset);
  SIZE(TexImageBox);FIELD(TexImageBox,blend);SIZE(TexEnvironment);
#define SHARED_FIELD(member) static_assert(offsetof(SVMNodeTexImage,member)==offsetof(SVMNodeTexImageBox,member));static_assert(offsetof(SVMNodeTexImage,member)==offsetof(SVMNodeTexEnvironment,member));
  SHARED_FIELD(id) SHARED_FIELD(flags) SHARED_FIELD(co) SHARED_FIELD(out_offset) SHARED_FIELD(alpha_offset)
#undef SHARED_FIELD
  static_assert(offsetof(SVMNodeTexImage,projection)==offsetof(SVMNodeTexEnvironment,projection));
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
  SIZE(TexVoronoi);FIELD(TexVoronoi,dimensions);FIELD(TexVoronoi,feature);FIELD(TexVoronoi,metric);FIELD(TexVoronoi,w);FIELD(TexVoronoi,scale);FIELD(TexVoronoi,detail);
  FIELD(TexVoronoi,roughness);FIELD(TexVoronoi,lacunarity);FIELD(TexVoronoi,smoothness);FIELD(TexVoronoi,exponent);FIELD(TexVoronoi,randomness);FIELD(TexVoronoi,normalize);
  FIELD(TexVoronoi,coord);FIELD(TexVoronoi,distance_offset);FIELD(TexVoronoi,color_offset);FIELD(TexVoronoi,position_offset);FIELD(TexVoronoi,w_out_offset);FIELD(TexVoronoi,radius_offset);
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
  source<<"//#endif\n";
#undef ENUM
  source<<"const SVM_SURFACE:bool="<<(surface?"true":"false")<<";\n";
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
  constant("SZ_PackedTransform",sizeof(PackedTransform));
  return source.str();
}
static constexpr char svm_surface_fragment[]=
#include "svm_shader.inc"
;
std::string svm_surface_source(){return shader_data_source()+svm_layout(true)+svm_surface_fragment;}
}
