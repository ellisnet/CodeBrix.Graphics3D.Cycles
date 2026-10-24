// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Environment-light and non-constant-emitter fixtures (environment gate): worlds
// (constant, procedural noise, HDRI environment texture), the background light,
// and textured/procedural emissive quads. Images are generated in memory.
// scene/shader.h first: Background::set_shader must see the complete Shader
// (an incomplete one selects Node::set(bool) inside the inline setter).
#include "scene/shader.h"
#include "scene/background.h"
#include "scene/image.h"
#include "scene/image_loader.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/scene.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/image_metadata.h"
#include <cmath>
// A smooth sky with a bright compact source: float RGBA, equirectangular.
class EnvironmentImageLoader final:public ccl::ImageLoader {
  unsigned width,height,variant;
 public:
  EnvironmentImageLoader(unsigned width,unsigned height,unsigned variant):width(width),height(height),variant(variant){}
  bool load_metadata(ccl::ImageMetaData &m,const ccl::ImageLoaderParams &p,ccl::Progress&)override{
    m.width=width;m.height=height;m.type=variant==2?ccl::IMAGE_DATA_TYPE_BYTE4:ccl::IMAGE_DATA_TYPE_FLOAT4;m.channels=4;m.colorspace=p.colorspace;m.finalize(p.alpha_type);return true;
  }
  bool load_pixels(const ccl::ImageMetaData &m,void *pixels)override{
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
      const float u=(x+.5f)/width,v=(y+.5f)/height;float rgb[3];
      if(variant==2){ // texture for emitters: checker with a gradient (sRGB bytes)
        const bool c=((x*4/width)+(y*4/height))%2;rgb[0]=c?.9f:.15f*u;rgb[1]=.2f+.6f*v;rgb[2]=c?.1f:.8f;
        auto *b=static_cast<ccl::uchar*>(pixels)+4*(y*width+x);for(int k=0;k<3;k++)b[k]=ccl::uchar(rgb[k]*255);b[3]=255;continue;
      }
      const float sky=.25f+.75f*v;rgb[0]=.2f*sky+.05f;rgb[1]=.35f*sky+.05f;rgb[2]=.6f*sky+.1f;
      const float du=u-.3f,dv=v-.35f,d2=du*du*4+dv*dv;
      if(variant==3){ // a compact, very bright sun (bench-like: ~1.5 degree disc, radiance 2000) and a halo
        const float sun=(u-.62f)*(u-.62f)*4+(v-.3f)*(v-.3f);
        if(sun<1.7e-5f){rgb[0]+=2000.f;rgb[1]+=1900.f;rgb[2]+=1700.f;}else if(sun<4e-4f){rgb[0]+=8.f;rgb[1]+=7.f;rgb[2]+=5.f;}}
      else if(d2<.004f){const float s=variant==1?60.f:25.f;rgb[0]+=s;rgb[1]+=s*.9f;rgb[2]+=s*.7f;}
      if(std::fabs(u-.75f)<.05f&&v>.55f&&v<.7f){rgb[0]+=3.f;rgb[1]+=.5f;}
      auto *f=static_cast<float*>(pixels)+4*(y*width+x);f[0]=rgb[0];f[1]=rgb[1];f[2]=rgb[2];f[3]=1;
    }
    m.conform_pixels(pixels);return true;
  }
  ccl::string name()const override{return "Environment fixture "+std::to_string(variant);}
  bool equals(const ccl::ImageLoader &other)const override{auto p=dynamic_cast<const EnvironmentImageLoader*>(&other);return p&&p->width==width&&p->height==height&&p->variant==variant;}
};
enum class EnvironmentWorld {Constant,Procedural,Hdri};
inline ccl::Shader *environment_world(ccl::Scene &scene,EnvironmentWorld kind,unsigned variant=0){
  using namespace ccl;auto g=make_unique<ShaderGraph>();auto bg=g->create_node<BackgroundNode>();
  if(kind==EnvironmentWorld::Constant){bg->set_color(make_float3(.35f,.45f,.6f));bg->set_strength(1.2f);}
  else if(kind==EnvironmentWorld::Procedural){
    auto coords=g->create_node<TextureCoordinateNode>();auto noise=g->create_node<NoiseTextureNode>();noise->set_scale(2.5f);noise->set_detail(2.f);
    g->connect(coords->output("Generated"),noise->input("Vector"));g->connect(noise->output("Color"),bg->input("Color"));bg->set_strength(1.6f);
  }else{
    ImageParams p;p.interpolation=INTERPOLATION_LINEAR;p.extension=EXTENSION_REPEAT;p.alpha_type=IMAGE_ALPHA_ASSOCIATED;
    const unsigned w=variant==3?512:64,h=variant==3?256:32;
    auto env=g->create_node<EnvironmentTextureNode>();env->handle=scene.image_manager->add_image(make_unique<EnvironmentImageLoader>(w,h,variant),p,false);
    env->set_colorspace(u_colorspace_scene_linear);env->set_alpha_type(IMAGE_ALPHA_ASSOCIATED);
    g->connect(env->output("Color"),bg->input("Color"));bg->set_strength(1.f);
  }
  g->connect(bg->output("Background"),g->output()->input("Surface"));
  auto world=scene.create_node<Shader>();world->set_graph(std::move(g));world->tag_update(&scene);scene.background->set_shader(world);return world;
}
// The world light that Blender's sync creates for world MIS.
inline ccl::Object *environment_light(ccl::Scene &scene,ccl::Shader *world,bool use_mis,int resolution){
  using namespace ccl;auto light=scene.create_node<BackgroundLight>();light->set_use_mis(use_mis);light->set_map_resolution(resolution);
  array<Node*> slots;slots.push_back_slow(world);light->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(light);return object;
}
// Emission shaders whose colour varies over the surface: an sRGB image through
// UV (variant 0), a procedural checker through generated coordinates (1), and a
// mixed emission/diffuse surface (2).
inline ccl::Shader *environment_emitter(ccl::Scene &scene,unsigned variant){
  using namespace ccl;auto g=make_unique<ShaderGraph>();auto emission=g->create_node<EmissionNode>();emission->set_strength(variant==1?4.f:6.f);
  if(variant==1){auto coords=g->create_node<TextureCoordinateNode>();auto checker=g->create_node<CheckerTextureNode>();checker->set_scale(3.f);
    checker->set_color1(make_float3(1.f,.8f,.3f));checker->set_color2(make_float3(.1f,.3f,1.f));
    g->connect(coords->output("Generated"),checker->input("Vector"));g->connect(checker->output("Color"),emission->input("Color"));}
  else{ImageParams p;p.interpolation=INTERPOLATION_LINEAR;p.extension=EXTENSION_REPEAT;p.alpha_type=IMAGE_ALPHA_ASSOCIATED;
    auto uv=g->create_node<UVMapNode>();auto tex=g->create_node<ImageTextureNode>();tex->handle=scene.image_manager->add_image(make_unique<EnvironmentImageLoader>(16,16,2),p,false);
    tex->set_colorspace(u_colorspace_srgb);tex->set_alpha_type(IMAGE_ALPHA_ASSOCIATED);
    g->connect(uv->output("UV"),tex->input("Vector"));g->connect(tex->output("Color"),emission->input("Color"));}
  ShaderOutput *surface=emission->output("Emission");
  if(variant==2){auto diffuse=g->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.6f,.5f,.4f));
    auto mix=g->create_node<MixClosureNode>();mix->set_fac(.45f);g->connect(surface,mix->input("Closure1"));g->connect(diffuse->output("BSDF"),mix->input("Closure2"));surface=mix->output("Closure");}
  g->connect(surface,g->output()->input("Surface"));
  auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->set_emission_sampling_method(EMISSION_SAMPLING_FRONT_BACK);shader->tag_update(&scene);return shader;
}
