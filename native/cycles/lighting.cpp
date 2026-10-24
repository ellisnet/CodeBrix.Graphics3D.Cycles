// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
std::string lighting_layout(){
  std::ostringstream out;auto constant=[&](const char *name,size_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
  constant("TRIANGLE_LIGHT_NATIVE_SSE",reference_oracle_sse()?1:0);
#define DATA(member) constant("D_integrator_" #member,offsetof(KernelData,integrator.member))
  DATA(use_direct_light);DATA(use_light_mis);DATA(num_lights);DATA(num_distribution);DATA(distribution_pdf_lights);DATA(distribution_pdf_triangles);DATA(light_inv_rr_threshold);
#undef DATA
#define LIGHT(member) constant("L_" #member,offsetof(KernelLight,member))
  LIGHT(type);LIGHT(co);LIGHT(shader_id);LIGHT(object_id);LIGHT(max_bounces);LIGHT(strength);
#undef LIGHT
  constant("L_radius",offsetof(KernelLight,spot.radius));constant("L_point_eval",offsetof(KernelLight,spot.eval_fac));
  constant("L_is_sphere",offsetof(KernelLight,spot.is_sphere));constant("L_sun_angle",offsetof(KernelLight,sun.one_minus_cosangle));
  constant("L_spot_dir",offsetof(KernelLight,spot.dir));constant("L_spot_cos",offsetof(KernelLight,spot.cos_half_spot_angle));
  constant("L_spot_smooth",offsetof(KernelLight,spot.spot_smooth));constant("L_spot_spread_cos",offsetof(KernelLight,spot.cos_half_larger_spread));
#define AREA(member) constant("L_area_" #member,offsetof(KernelLight,area.member))
  AREA(axis_u);AREA(axis_v);AREA(len_u);AREA(len_v);AREA(dir);AREA(invarea);AREA(tan_half_spread);AREA(normalize_spread);
#undef AREA
  constant("L_sun_eval",offsetof(KernelLight,sun.eval_fac));
  constant("L_sun_half_angle",offsetof(KernelLight,sun.angle));constant("L_sun_pdf",offsetof(KernelLight,sun.pdf));
  constant("LD_totarea",offsetof(KernelLightDistribution,totarea));constant("LD_prim",offsetof(KernelLightDistribution,prim));
  constant("LD_object",offsetof(KernelLightDistribution,object_id));constant("LD_visibility",offsetof(KernelLightDistribution,visibility_flag));
  constant("O_shadow_terminator_geometry_offset",offsetof(KernelObject,shadow_terminator_geometry_offset));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(LIGHT_POINT);ENUM(LIGHT_SUN);ENUM(LIGHT_SPOT);ENUM(LIGHT_AREA);ENUM(SHADER_CAST_SHADOW);ENUM(SHADER_USE_MIS);ENUM(PRNG_LIGHT);ENUM(PRNG_LIGHT_TERMINATE);
#undef ENUM
  constant("K_LIGHT_NEE",DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE);constant("O_primitive_type",offsetof(KernelObject,primitive_type));
  constant("LIGHT_EXCLUDE_CAMERA",SHADER_EXCLUDE_CAMERA);constant("LIGHT_EXCLUDE_SHADOW_CATCHER",SHADER_EXCLUDE_SHADOW_CATCHER);
  return out.str();
}
void WebGPUDevice::validate_lighting(bool sampling) const {
  require(!(sampling||data.integrator.use_light_mis)||!data.integrator.use_light_tree,"WebGPU direct lighting light tree not implemented");
  require(data.integrator.num_lights>=0&&size_t(data.integrator.num_lights)==light_records.size(),"Invalid native light table");
  // Background (world) light: importance-map sampling, MIS and non-constant
  // worlds (shade_light_nee). Portals and sky-texture sun guiding are refused.
  require(data.integrator.num_portals==0&&data.background.portal_weight==0,"WebGPU background light portals not implemented");
  require(data.background.sun_weight==0,"WebGPU background sun guiding (sky texture sun disc) not implemented");
  unsigned background_lights=0;
  for(const auto &light:light_records){
    require(light.type==LIGHT_POINT||light.type==LIGHT_SUN||light.type==LIGHT_SPOT||light.type==LIGHT_AREA||light.type==LIGHT_BACKGROUND,"WebGPU direct lighting emitter type not implemented");
    const auto id=uint(light.shader_id)&SHADER_MASK;
    require(id<shader_records.size(),"Invalid native light shader");
    if(light.type==LIGHT_BACKGROUND){
      require(++background_lights==1,"WebGPU multiple background lights not implemented");
      require(id==(uint(data.background.surface_shader)&SHADER_MASK),"Invalid native background light shader");
      for(float strength:light.strength)require(std::isfinite(strength),"Invalid native light strength");
      require(!data.background.use_mis||(data.background.map_res_x>0&&data.background.map_res_y>0),"Invalid native background importance map");
      continue;
    }
    require(shader_records[id].flags&SD_HAS_CONSTANT_EMISSION,"WebGPU nonconstant point, spot, area or sun light emission not implemented");
    require(std::isfinite(light.co.x)&&std::isfinite(light.co.y)&&std::isfinite(light.co.z),"Invalid native light position/direction");
    for(float strength:light.strength)require(std::isfinite(strength),"Invalid native light strength");
    if(light.type==LIGHT_POINT||light.type==LIGHT_SPOT){require(std::isfinite(light.spot.radius)&&light.spot.radius>=0&&
      std::isfinite(light.spot.eval_fac)&&light.spot.eval_fac>=0,"Invalid native point light");}
    else if(light.type==LIGHT_AREA){
      require(std::isfinite(light.area.len_u)&&light.area.len_u>0&&std::isfinite(light.area.len_v)&&light.area.len_v>0,"WebGPU degenerate area light not implemented");
      require(std::isfinite(light.area.invarea)&&std::isfinite(light.area.tan_half_spread)&&light.area.tan_half_spread>=0&&
        std::isfinite(light.area.normalize_spread)&&light.area.normalize_spread>=0,"Invalid native area light");
      for(const float3 value:{light.area.axis_u,light.area.axis_v,light.area.dir})require(std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z),"Invalid native area basis");
    }
    else require(std::isfinite(light.sun.one_minus_cosangle)&&light.sun.one_minus_cosangle>=0&&light.sun.one_minus_cosangle<=2&&
      std::isfinite(light.sun.eval_fac)&&light.sun.eval_fac>=0&&
      std::isfinite(light.sun.angle)&&light.sun.angle>=0&&std::isfinite(light.sun.pdf)&&light.sun.pdf>=0,"Invalid native sun light");
    if(light.type==LIGHT_SPOT)require(std::isfinite(light.spot.cos_half_spot_angle)&&std::isfinite(light.spot.cos_half_larger_spread)&&
      light.spot.cos_half_larger_spread>=-1&&light.spot.cos_half_larger_spread<=1&&!std::isnan(light.spot.spot_smooth),"Invalid native spot light");
  }
  if(data.integrator.use_light_mis)require(std::isfinite(data.integrator.distribution_pdf_lights)&&data.integrator.distribution_pdf_lights>=0,"Invalid native MIS selection probability");
  require(std::isfinite(data.integrator.distribution_pdf_triangles)&&data.integrator.distribution_pdf_triangles>=0,"Invalid native triangle selection probability");
  if(!sampling)return;
  require(!data.integrator.use_light_tree,"WebGPU direct lighting light tree not implemented");
  const int n=data.integrator.num_distribution;
  require(n>0&&light_distribution_records.size()==size_t(n)+1,"Invalid native light distribution");
  require(std::isfinite(data.integrator.distribution_pdf_lights)&&data.integrator.distribution_pdf_lights>=0&&
    std::isfinite(data.integrator.light_inv_rr_threshold)&&data.integrator.light_inv_rr_threshold>=0,"Invalid native light sampling parameters");
  // Flat-CDF mesh lights: the GPU evaluates their sampled vertices, PDFs and MIS;
  // a triangle whose shader has no constant emission is shaded at the sampled
  // point by shade_light_nee (textured and node emission), as in the pinned kernel.
  auto triangles=globals.find("tri_vindex");const size_t triangle_count=triangles==globals.end()?0:remaining(triangles->second)/sizeof(packed_uint3);
  float previous=0;
  for(int i=0;i<=n;i++){const auto &entry=light_distribution_records[i];
    require(std::isfinite(entry.totarea)&&entry.totarea>=previous&&entry.totarea<=1,"Invalid native light CDF");previous=entry.totarea;
    if(i<n){
      if(entry.prim<0)require(size_t(~entry.prim)<light_records.size()&&data.integrator.distribution_pdf_lights>0,"Invalid native light distribution lamp");
      else {
        require(size_t(entry.prim)<triangle_count&&entry.object_id>=0&&size_t(entry.object_id)<object_records.size()&&
          (object_records[entry.object_id].primitive_type==PRIMITIVE_TRIANGLE||object_records[entry.object_id].primitive_type==PRIMITIVE_MOTION_TRIANGLE)&&
          data.integrator.distribution_pdf_triangles>0,"Invalid native triangle light distribution");
        require(size_t(entry.object_id)<object_flag_records.size(),"Invalid native triangle object flags");
        const auto flags=object_flag_records[entry.object_id];
        // Static instanced emitters pass their independent image since the
        // closest/shadow traversal reproduces the CPU oracle's arithmetic
        // exactly (contraction-proof sums, SSE grouping, RCPSS model; X2).
        (void)flags;
      }
    }
  }
  require(light_distribution_records.front().totarea==0&&previous==1,"Invalid native light CDF endpoints");
}
}
