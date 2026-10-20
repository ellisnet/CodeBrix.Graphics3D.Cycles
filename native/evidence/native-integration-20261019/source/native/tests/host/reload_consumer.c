// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#define main baseline_lifecycle_main
#include "consumer.c"
#undef main

static CBCImage make_image(CBCHost host,const char *root,unsigned format) {
  char path[4096];REQUIRE(snprintf(path,sizeof(path),"%s/input.%u.bin",root,format)>0);
  FILE *file=fopen(path,"rb");REQUIRE(file);REQUIRE(fseek(file,0,SEEK_END)==0);long size=ftell(file);REQUIRE(size>0);
  REQUIRE(fseek(file,0,SEEK_SET)==0);void *pixels=malloc((size_t)size);REQUIRE(pixels);
  REQUIRE(fread(pixels,1,(size_t)size,file)==(size_t)size);REQUIRE(fclose(file)==0);
  CBCImageConfig config={sizeof(config),7,5,format+1,{0}};CBCImage image=0;
  CHECK(cbc_host_image_create(host,&config,pixels,(uint64_t)size,&image));memset(pixels,0,(size_t)size);free(pixels);return image;
}
static void bind_copy(CBCHost host,CBCNode texture,const char *root,unsigned format) {
  CBCImage image=make_image(host,root,format);CHECK(cbc_host_image_bind(host,texture,image));CHECK(cbc_host_image_release(host,image));
}
static void save_render(CBCHost host,const char *root,unsigned index) {
  CBCRenderConfig config={sizeof(config),32,24,8,{0}};float pixels[FLOAT_COUNT];
  CHECK(cbc_host_render(host,&config,pixels,FLOAT_COUNT));
  for(unsigned i=0;i<FLOAT_COUNT;++i)REQUIRE(isfinite(pixels[i]));
  char path[4096];REQUIRE(snprintf(path,sizeof(path),"%s/case.%u.rgba.bin",root,index)>0);
  FILE *file=fopen(path,"wb");REQUIRE(file);REQUIRE(fwrite(pixels,sizeof(float),FLOAT_COUNT,file)==FLOAT_COUNT);REQUIRE(fclose(file)==0);
}
static int image_cases(int argc,char **argv) {
  REQUIRE(argc==3);
  char config_path[4096],missing[4096];REQUIRE(snprintf(config_path,sizeof(config_path),"%s/config.ocio",argv[1])>0);
  REQUIRE(snprintf(missing,sizeof(missing),"%s/missing-config.ocio",argv[1])>0);
  REQUIRE(cbc_host_color_config_path("")==CBC_INVALID);CHECK(cbc_host_color_config_path(config_path));
  REQUIRE(cbc_host_color_config_path(missing)==CBC_FAILED);
  CBCHostConfig config={sizeof(config),CBC_HOST_ABI,CBC_HOST_CPU_REFERENCE,1,{0}};CBCHost host=0;CHECK(cbc_host_create(&config,&host));
  REQUIRE(cbc_host_color_config_path(config_path)==CBC_NOT_READY);
  CBCNode mesh=0,material=0;uint64_t count=0;CBCNode original_color=fixture(host,&mesh);
  CHECK(cbc_host_get_nodes(host,mesh,"used_shaders",&material,1,&count));REQUIRE(count==1);
  CBCNode diffuse=shader_node(host,material,"diffuse_bsdf"),texture=shader_node(host,material,"image_texture"),uv=shader_node(host,material,"uvmap"),output=0;
  CHECK(cbc_host_shader_output(host,material,&output));scalar(host,diffuse,"roughness",.3f);
  CHECK(cbc_host_connect(host,uv,"UV",texture,"Vector"));CHECK(cbc_host_connect(host,texture,"Color",diffuse,"Color"));CHECK(cbc_host_connect(host,diffuse,"BSDF",output,"Surface"));
  CHECK(cbc_host_set_string(host,texture,"interpolation","linear"));CHECK(cbc_host_set_string(host,texture,"extension","periodic"));
  CBCAttributeConfig attr={sizeof(attr),CBC_VALUE_POINT2,CBC_ATTR_CORNER,CBC_ATTR_STANDARD,{0}};
  const float coords[]={-.3f,-.2f,1.7f,.1f,.1f,1.4f};CHECK(cbc_host_attribute_floats(host,mesh,"uv",&attr,coords,6));
  const char *spaces[]={"data","scene_linear","scene_linear_srgb","__builtin_srgb","","cbc_scaled"};
  const char *alphas[]={"unassociated","associated","channel_packed","ignore","auto"};
  unsigned completed=0;
  for(unsigned format=0;format<8;++format) {
    bind_copy(host,texture,argv[1],format);
    for(unsigned space=0;space<6;++space)for(unsigned alpha=0;alpha<5;++alpha) {
      CHECK(cbc_host_set_string(host,texture,"colorspace",spaces[space]));CHECK(cbc_host_set_string(host,texture,"alpha_type",alphas[alpha]));
      save_render(host,argv[2],completed++);
    }
  }
  CHECK(cbc_host_connect(host,original_color,"Color",diffuse,"Color"));
  CBCNode world=0;CHECK(cbc_host_get_nodes(host,singleton(host,"background"),"shader",&world,1,&count));REQUIRE(count==1);
  CBCNode environment=shader_node(host,world,"environment_texture"),background=shader_node(host,world,"background_shader");
  scalar(host,background,"strength",.8f);CHECK(cbc_host_shader_output(host,world,&output));
  CHECK(cbc_host_connect(host,environment,"Color",background,"Color"));CHECK(cbc_host_connect(host,background,"Background",output,"Surface"));
  CHECK(cbc_host_set_string(host,environment,"colorspace","cbc_scaled"));CHECK(cbc_host_set_string(host,environment,"alpha_type","channel_packed"));
  CHECK(cbc_host_set_string(host,environment,"interpolation","linear"));
  for(unsigned format=0;format<8;++format) {
    bind_copy(host,environment,argv[1],format);
    for(unsigned projection=0;projection<2;++projection) {
      CHECK(cbc_host_set_string(host,environment,"projection",projection?"mirror_ball":"equirectangular"));save_render(host,argv[2],completed++);
    }
  }
  CBCNode world_color=shader_node(host,world,"color");color(host,world_color,"value",.25f,.4f,.6f);
  CHECK(cbc_host_connect(host,world_color,"Color",background,"Color"));CHECK(cbc_host_connect(host,texture,"Color",diffuse,"Color"));
  CHECK(cbc_host_set_string(host,texture,"colorspace","cbc_scaled"));CHECK(cbc_host_set_string(host,texture,"alpha_type","channel_packed"));
  const unsigned file_formats[]={5,7};
  for(unsigned i=0;i<2;++i) {
    unsigned format=file_formats[i];char path[4096];REQUIRE(snprintf(path,sizeof(path),"%s/input.%u.png",argv[1],format)>0);
    CHECK(cbc_host_set_string(host,texture,"filename",path));CHECK(cbc_host_image_bind(host,texture,0));save_render(host,argv[2],completed++);
    bind_copy(host,texture,argv[1],format);save_render(host,argv[2],completed++);
  }
  CHECK(cbc_host_destroy(host));CHECK(cbc_host_color_config_path(config_path));REQUIRE(completed==260);
  puts("{\"passed\":true,\"cases\":260,\"imageFormats\":8,\"colorspaces\":6,\"alphaModes\":5,\"environmentCases\":16,\"fileAndRebindCases\":4,\"copiedInputs\":true,\"releasedBindingModeEdits\":true}");return 0;
}

int main(int argc,char **argv) {
  REQUIRE(argc==4);unsigned cycles=(unsigned)strtoul(argv[3],NULL,10);REQUIRE(cycles>0 && cycles<=16);
  for(unsigned round=0;round<cycles;++round) {
    char input[4096],output[4096];const char variant=(char)('a'+round%3);
    REQUIRE(snprintf(input,sizeof(input),"%s/%c/output",argv[1],variant)>0);
    REQUIRE(snprintf(output,sizeof(output),"%s/round.%u",argv[2],round)>0);
    char *args[]={argv[0],input,output};REQUIRE(image_cases(3,args)==0);
  }
  printf("{\"passed\":true,\"configurationRounds\":%u,\"films\":%u}\n",cycles,260*cycles);return 0;
}
