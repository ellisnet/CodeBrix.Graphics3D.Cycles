// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Tiled images (Cycles' texture cache) on the WGSL device against the pinned CPU kernel: tile and mip level selection
// with derivatives and the stochastic level, the dual-number sphere, tube and environment projections of such a lookup
// (the projected uv and its derivatives exact), the tiles requested and used (the access state, exact), the restart after
// a request (WebGPUDevice::tile_transaction), and the values of a tile that failed to load or is still requested.
#include "wavefront_test.h"
#include "wavefront.h"
#include "image_tile_reference.h"
#include "scene/camera.h"
#include "scene/image.h"
#include "scene/image_loader.h"
#include "scene/integrator.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/image_metadata.h"
#include "util/half.h"
#include "util/progress.h"
#include "util/task.h"
#include "kernel/sample/lcg.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
static constexpr unsigned TILE_IMAGES=12;
// A device word equals the CPU's float bit for bit, but for the sign of a zero, which WGSL does not preserve (a derivative of
// zero; the lookup does not see the sign).
static bool output_word_equal(uint32_t word,float value){return word==std::bit_cast<uint32_t>(value)||(value==0&&std::bit_cast<float>(word)==0);}
// A tiled image of the fixture: 200 x 136 pixels in tiles of 64 (sides that are no multiples of the tile), every mip level
// from a formula of the level and the texel, with the padding the cache asks for (wrapped by the texture's extension).
class FixtureTileLoader final:public ImageLoader {
  unsigned id;ImageDataType type;
 public:
  static constexpr int width=200,height=136;
  FixtureTileLoader(unsigned id,ImageDataType type):id(id),type(type){}
  bool load_metadata(ImageMetaData &m,const ImageLoaderParams &p,Progress&)override{
    m.width=width;m.height=height;m.type=type;m.channels=(type==IMAGE_DATA_TYPE_FLOAT4||type==IMAGE_DATA_TYPE_HALF4||type==IMAGE_DATA_TYPE_BYTE4||type==IMAGE_DATA_TYPE_USHORT4)?4:1;
    m.colorspace=p.colorspace;m.has_tiles_and_mipmaps=true;m.tile_size=64;m.tile_need_conform=false;m.average_color=make_float4(.25f,.5f,.75f,1.0f);
    m.finalize(p.alpha_type);return true;
  }
  bool load_pixels(const ImageMetaData&,void*)override{return false;}
  static int64_t wrap(int64_t i,int64_t size,ExtensionType extension){
    if(i>=0&&i<size)return i;
    switch(extension){
      case EXTENSION_REPEAT:{const int64_t m=i%size;return m<0?m+size:m;}
      case EXTENSION_MIRROR:{const int64_t m=std::abs(i+(i<0?1:0))%(2*size);return m>=size?2*size-m-1:m;}
      case EXTENSION_CLIP:return -1;
      default:return std::clamp<int64_t>(i,0,size-1);
    }
  }
  bool load_pixels_tile(const ImageMetaData &m,const int level,const int64_t x,const int64_t y,const int64_t w,const int64_t h,const int64_t x_stride,
                        const int64_t y_stride,const int64_t padding,const ExtensionType extension,uint8_t *pixels)override{
    const int64_t lw=std::max<int64_t>(1,width>>level),lh=std::max<int64_t>(1,height>>level);
    for(int64_t j=0;j<h+2*padding;j++)for(int64_t i=0;i<w+2*padding;i++){
      const int64_t tx=wrap(x-padding+i,lw,extension),ty=wrap(y-padding+j,lh,extension);uint8_t *to=pixels+j*y_stride+i*x_stride;
      for(int c=0;c<m.channels;c++){
        float v=tx<0||ty<0?0.0f:float((tx*37+ty*91+c*53+level*29+id*7)%251)/250;
        if(v!=0&&c==3)v=float(1+(tx+ty)%4)/4;
        if(type==IMAGE_DATA_TYPE_FLOAT4||type==IMAGE_DATA_TYPE_FLOAT){std::memcpy(to+c*4,&v,4);}
        else if(type==IMAGE_DATA_TYPE_HALF4||type==IMAGE_DATA_TYPE_HALF){const half value=float_to_half_image(v);std::memcpy(to+c*2,&value,2);}
        else if(type==IMAGE_DATA_TYPE_BYTE4||type==IMAGE_DATA_TYPE_BYTE)to[c]=uint8_t(v*255);
        else{const uint16_t value=uint16_t(v*65535);std::memcpy(to+c*2,&value,2);}
      }
    }
    return true;
  }
  string name()const override{return "Tiled image fixture "+std::to_string(id);}
  bool equals(const ImageLoader &other)const override{auto p=dynamic_cast<const FixtureTileLoader*>(&other);return p&&p->id==id&&p->type==type;}
};
static std::vector<ImageHandle> prepare(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(8);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
  array<Node*> shaders;std::vector<ImageHandle> handles;
  const ImageDataType types[]={IMAGE_DATA_TYPE_BYTE4,IMAGE_DATA_TYPE_FLOAT4,IMAGE_DATA_TYPE_HALF,IMAGE_DATA_TYPE_USHORT4,IMAGE_DATA_TYPE_HALF4,IMAGE_DATA_TYPE_BYTE,IMAGE_DATA_TYPE_FLOAT,IMAGE_DATA_TYPE_USHORT};
  for(unsigned i=0;i<TILE_IMAGES;i++){
    // Closest, Linear and Cubic (the interpolation enum's values 1, 0 and 2) with each of the four extensions.
    ImageParams p;p.interpolation=InterpolationType(i%3);p.extension=ExtensionType(i%4);p.alpha_type=IMAGE_ALPHA_ASSOCIATED;
    auto handle=scene.image_manager->add_image(make_unique<FixtureTileLoader>(i,types[i%8]),p,false);handles.push_back(handle);
    auto g=make_unique<ShaderGraph>();auto uv=g->create_node<UVMapNode>();auto tex=g->create_node<ImageTextureNode>();tex->handle=handle;
    tex->set_colorspace(u_colorspace_scene_linear);tex->set_alpha_type(IMAGE_ALPHA_ASSOCIATED);
    auto emission=g->create_node<EmissionNode>();emission->set_strength(.8f);
    g->connect(uv->output("UV"),tex->input("Vector"));g->connect(tex->output("Color"),emission->input("Color"));g->connect(emission->output("Emission"),g->output()->input("Surface"));
    auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);shaders.push_back_slow(sh);
  }
  auto mesh=scene.create_node<Mesh>();mesh->set_used_shaders(shaders);mesh->resize_mesh(TILE_IMAGES*3,TILE_IMAGES);
  for(unsigned i=0;i<TILE_IMAGES;i++){
    float x=(i%4)*2,y=(i/4)*2;auto v=mesh->get_position_for_write();v[3*i]=make_float3(x,y,.1);v[3*i+1]=make_float3(x+1,y,.4);v[3*i+2]=make_float3(x,y+1,-.2);
    for(unsigned j=0;j<3;j++){mesh->get_triangles()[3*i+j]=3*i+j;}mesh->get_shader()[i]=i;mesh->get_smooth()[i]=false;
  }
  mesh->tag_triangles_modified();auto uv=mesh->attributes.add(ATTR_STD_UV)->data_for_write<float2>();
  for(unsigned i=0;i<TILE_IMAGES;i++){uv[3*i]=make_float2(0,0);uv[3*i+1]=make_float2(1,0);uv[3*i+2]=make_float2(0,1);}
  auto ob=scene.create_node<Object>();ob->set_geometry(mesh);
  return handles;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: image_tile_gate OUTPUT_PREFIX");const std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"image_tile_gate device creation");
  uint64_t cases=0,strict=0,restarts=0,tiles_loaded=0,tiles_used=0,projection_cases=0,projection_strict=0,lcg_cases=0;double maximum=0,projection_maximum=0;
  {SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=true;Scene scene(params,&d);auto handles=prepare(scene);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Tiled image scene preparation failed");
    auto &ds=scene.dscene;require(d.tiled_images&&ds.image_texture_tile_descriptors.size()>0,"The fixture's images are not tiled");
    require((d.shader_features()&SF_IMAGE_TILED).any(),"The scene's modules lack the tiled image regions");
    for(auto &h:handles)require(ds.image_textures[h.kernel_id()].tile_descriptor_offset!=KERNEL_TILE_LOAD_NONE&&ds.image_textures[h.kernel_id()].tile_levels==3,"Tiled image fixture layout changed");
    std::vector<ImageTileProbe> probes;
    const float coords[]={-1.3f,-.5f,-.00001f,0,.004f,.21f,.32f,.5f,.64f,.9f,1,1.00001f,1.7f};
    // Derivatives (du/dx, dv/dx, du/dy, dv/dy): none (a lookup without derivatives), then footprints from below a texel to
    // the whole image, isotropic, stretched beyond the anisotropy limit of 16 and skewed.
    const float derivatives[][4]={{0,0,0,0},{.002f,0,0,.002f},{.006f,0,0,.008f},{.011f,.003f,-.002f,.013f},{.03f,0,0,.03f},{.2f,0,0,.001f},{.0005f,0,0,.4f},{.07f,.05f,.06f,-.04f},{1.5f,0,0,2.5f}};
    for(auto &h:handles)for(auto &dv:derivatives)for(unsigned seed=0;seed<3;seed++)for(float u:coords)for(float v:coords)
      probes.push_back({h.kernel_id(),0u,u,v,dv[0],dv[1],dv[2],dv[3],seed?0x9e3779b9u*seed+unsigned(probes.size())*2654435761u+1u:0u,{0,0,0}});
    // Projections (kernel/svm/image.h svm_node_tex_image_mapping and svm_node_tex_environment_projection of a dual3): the
    // sphere and tube projections of the Image Texture node and the equirectangular and mirror ball projections of the
    // Environment Texture node, on a grid of coordinates with the centre of the box (a zero vector after the remapping, or
    // as a direction), its axes and planes, values a few ulps off them (the arctangent's near-axis forms) and the -y
    // direction of the mirror ball (no division there), with five coordinate derivatives and two lcg states, on four images
    // (the three filters, the four extensions).
    std::vector<ImageProjectionProbe> projections;
    const float grid[]={-.3f,0,.49999997f,.5f,.50000003f,.8f,1.2f};
    const float coordinate_derivatives[][6]={{0,0,0,0,0,0},{.004f,0,.001f,0,.005f,-.002f},{.02f,.01f,0,-.01f,.03f,.02f},{.3f,-.2f,.1f,.1f,.4f,-.3f},{1e-6f,0,0,0,0,2e-6f}};
    for(unsigned image=0;image<4;image++)for(unsigned kind:{unsigned(NODE_IMAGE_PROJ_SPHERE),unsigned(NODE_IMAGE_PROJ_TUBE),4u,5u})for(auto &dc:coordinate_derivatives)
      for(unsigned seed=0;seed<2;seed++)for(float x:grid)for(float y:grid)for(float z:grid){
        // An environment projection takes the coordinate minus 0.5 (exact) as its direction.
        const float shift=kind<4?0.0f:0.5f;
        projections.push_back({handles[image].kernel_id(),0u,kind,seed?0x85ebca6bu+unsigned(projections.size())*2246822519u:0u,{x-shift,y-shift,z-shift},{dc[0],dc[1],dc[2]},{dc[3],dc[4],dc[5]},{0,0,0}});
      }
    device_vector<ImageProjectionProbe> projection_input(&d,"image_projection_probe_input",MEM_READ_ONLY);projection_input.alloc(projections.size());
    std::copy(projections.begin(),projections.end(),projection_input.data());projection_input.copy_to_device();
    const size_t projection_record=11;device_vector<uint> projection_output(&d,"image_projection_probe_output",MEM_READ_WRITE);projection_output.alloc(16+projections.size()*projection_record);
    std::fill_n(projection_output.data(),projection_output.size(),0xa5a5a5a5);projection_output.copy_to_device();
    device_vector<ImageTileProbe> input(&d,"image_tile_probe_input",MEM_READ_ONLY);input.alloc(probes.size());std::copy(probes.begin(),probes.end(),input.data());input.copy_to_device();
    const size_t record=5;device_vector<uint> output(&d,"image_tile_probe_output",MEM_READ_WRITE);output.alloc(16+probes.size()*record);
    std::fill_n(output.data(),output.size(),0xa5a5a5a5);output.copy_to_device();
    std::string source=svm_surface_source()+R"WGSL(
@compute @workgroup_size(64) fn image_tile_probe(@builtin(global_invocation_id) id:vec3<u32>){
 if(id.x>=p(1u)){return;}let at=p(2u)+id.x*48u;var s:SvmState;s.output.lcg_state=load_word(at+32u);
 image_tile_duv=vec4<f32>(heap_float(at+16u),heap_float(at+20u),heap_float(at+24u),heap_float(at+28u));
 let color=svm_image_sample(&s,bitcast<i32>(load_word(at)),vec2<f32>(heap_float(at+8u),heap_float(at+12u)),load_word(at+4u),vec4<f32>(1,0,1,1));
 if(s.error!=0u){return;}let out=p(4u)+id.x*20u;for(var i=0u;i<4u;i++){store_word(out+i*4u,bitcast<u32>(color[i]));}store_word(out+16u,s.output.lcg_state);
}
@compute @workgroup_size(64) fn image_projection_probe(@builtin(global_invocation_id) id:vec3<u32>){
 if(id.x>=p(24u)){return;}let at=p(25u)+id.x*64u;var s:SvmState;s.output.lcg_state=load_word(at+12u);
 let uv=image_tile_projection(&s,NormalDual(heap_vec3(at+16u),heap_vec3(at+28u),heap_vec3(at+40u)),load_word(at+8u));let duv=image_tile_duv;
 let color=svm_image_sample(&s,bitcast<i32>(load_word(at)),uv,load_word(at+4u),vec4<f32>(1,0,1,1));
 if(s.error!=0u){return;}let out=p(26u)+id.x*44u;for(var i=0u;i<4u;i++){store_word(out+i*4u,bitcast<u32>(color[i]));}store_word(out+16u,s.output.lcg_state);
 store_word(out+20u,bitcast<u32>(uv.x));store_word(out+24u,bitcast<u32>(uv.y));for(var i=0u;i<4u;i++){store_word(out+28u+i*4u,bitcast<u32>(duv[i]));}
}
@compute @workgroup_size(64) fn image_lcg_probe(@builtin(global_invocation_id) id:vec3<u32>){
 if(id.x>=p(27u)){return;}let at=p(28u)+id.x*12u;let out=p(29u)+id.x*12u;let pixel=load_word(at);let offset=load_word(at+4u);let sample=load_word(at+8u);
 store_word(out,image_tile_lcg_init(pixel,offset,sample,0x15b4f88du));store_word(out+4u,image_tile_lcg_init(pixel,offset,sample,0xd9111870u));
 store_word(out+8u,image_tile_lcg_init(pixel,offset,sample,0xb4bc3953u));
})WGSL";
    // The scene's module with the projections' and the volumes' regions (the fixture's images use the flat projection
    // only, and it has no volume).
    require(!(d.shader_features()&SF_IMAGE_TILED_PROJECTION).any(),"The fixture reads a tiled image through a projection");
    source=specialize_wgsl(source,d.shader_features()|SF_IMAGE_TILED_PROJECTION|SF_IMAGE_TILED_VOLUME);std::ofstream(prefix+".wgsl")<<source;
    WGPUShaderSourceWGSL code{};code.chain.sType=WGPUSType_ShaderSourceWGSL;code.code={source.data(),source.size()};WGPUShaderModuleDescriptor md{};md.nextInChain=&code.chain;
    d.gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&md));d.gpu.pop_scope();
    auto shader=d.gpu.pipeline(module,d.pipeline_layout,"image_tile_probe");auto projection_shader=d.gpu.pipeline(module,d.pipeline_layout,"image_projection_probe");
    // Word 6 is the kernel data (data_float: the mip bias); 24 to 26 are the projection probes' (no SVM code reads them).
    auto queue=d.gpu_queue_create();std::array<uint32_t,32> p{};p[1]=probes.size();p[2]=input.device_pointer;p[4]=output.device_pointer+32;p[6]=d.constants.at("data");p[17]=d.error_word;
    p[24]=projections.size();p[25]=projection_input.device_pointer;p[26]=projection_output.device_pointer+32;
    const auto run=[&]{d.guard([&]{p[14]=d.scene_tables();d.dispatch(p,probes.size()+63,shader);d.dispatch(p,projections.size(),projection_shader);});};
    // Every probe in one dispatch, as a transaction: the tiles it asks for are loaded and it runs again.
    d.tile_transaction(*queue,run,"image_tile_probe");healthy(d);d.guard([&]{d.sync();});healthy(d);restarts=d.tile_restarts;
    require(restarts>=1,"No tile was requested: the gate did not exercise the restart");
    output.copy_from_device();projection_output.copy_from_device();healthy(d);
    for(unsigned i=0;i<8;i++)require(output[i]==0xa5a5a5a5&&output[output.size()-1-i]==0xa5a5a5a5&&projection_output[i]==0xa5a5a5a5&&
                                     projection_output[projection_output.size()-1-i]==0xa5a5a5a5,"Tiled image probe guard overwritten");
    // The device's tables after the run, and the host's descriptors (what the pinned loader copied to the device).
    const size_t table=ds.image_texture_tile_descriptors.size();require(ds.image_texture_tile_access_state.size()==table,"Tile access state size");
    std::vector<KernelTileDescriptor> device_descriptors(table),host_descriptors(ds.image_texture_tile_descriptors.data(),ds.image_texture_tile_descriptors.data()+table);
    std::vector<uint8_t> device_access(table),cpu_access(table,KERNEL_TILE_ACCESS_NONE);
    d.guard([&]{d.download(uint32_t(ds.image_texture_tile_descriptors.device_pointer),device_descriptors.data(),table*sizeof(KernelTileDescriptor));
      d.download(uint32_t(ds.image_texture_tile_access_state.device_pointer),device_access.data(),table);});healthy(d);
    require(device_descriptors==host_descriptors,"The device's tile descriptors differ from the host's after the loads");
    std::vector<KernelTileDescriptor> cpu_descriptors=host_descriptors;
    const auto expected=original_image_tile_samples(ds,probes,cpu_descriptors,cpu_access);
    const auto projected=original_image_projection_samples(ds,projections,cpu_descriptors,cpu_access);
    require(cpu_descriptors==host_descriptors,"The CPU reference changed a tile descriptor");
    // Integers exact: the lcg state after each lookup, and the access state of every tile (KERNEL_TILE_ACCESS_USED where
    // the CPU kernel read a tile, nothing else: the restart puts back the requests of the run that missed).
    for(size_t i=0;i<table;i++){
      require(device_access[i]==cpu_access[i],"Tile access state "+std::to_string(i)+": device "+std::to_string(device_access[i])+", CPU "+std::to_string(cpu_access[i]));
      tiles_used+=cpu_access[i]==KERNEL_TILE_ACCESS_USED;}
    for(auto &h:handles){const auto &tex=ds.image_textures[h.kernel_id()];
      for(int i=0;i<tex.tile_num;i++)tiles_loaded+=kernel_tile_descriptor_loaded(host_descriptors[tex.tile_descriptor_offset+tex.tile_levels+i]);}
    require(tiles_used>=TILE_IMAGES*3&&tiles_loaded>=tiles_used,"Tile coverage changed");
    for(size_t i=0;i<probes.size();i++){
      require(output[8+i*record+4]==expected[i].lcg,"Tiled image probe "+std::to_string(i)+" lcg state: device "+std::to_string(output[8+i*record+4])+", CPU "+std::to_string(expected[i].lcg));
      for(unsigned j=0;j<4;j++){
        const float actual=std::bit_cast<float>(output[8+i*record+j]),wanted=reinterpret_cast<const float*>(&expected[i].rgba)[j];const double error=std::abs(double(actual)-wanted);
        maximum=std::max(maximum,error);if(error>1e-7+1e-7*std::abs(double(wanted)))strict++;
        require(std::isfinite(actual)&&error<=5e-5+2e-4*std::abs(double(wanted)),"Tiled image probe "+std::to_string(i)+" id="+std::to_string(probes[i].id)+" uv="+std::to_string(probes[i].u)+","+
                std::to_string(probes[i].v)+" channel="+std::to_string(j)+" got="+std::to_string(actual)+" expected="+std::to_string(wanted));
      }
    }
    cases=probes.size();
    // The projected uv and its derivatives bit for bit, the lcg state exact, the colour within the practical criterion.
    size_t uv_differences=0;std::string first_difference;
    for(size_t i=0;i<projections.size();i++){
      const size_t at=8+i*projection_record;const auto &probe=projections[i];
      const std::string name="Projection probe "+std::to_string(i)+" kind "+std::to_string(probe.kind)+" co "+std::to_string(probe.co[0])+","+std::to_string(probe.co[1])+","+std::to_string(probe.co[2]);
      for(unsigned j=0;j<6;j++)if(!output_word_equal(projection_output[at+5+j],projected[i].uv[j])){
        if(uv_differences++<20)std::cerr<<name<<" uv component "<<j<<": device "<<std::hexfloat<<std::bit_cast<float>(projection_output[at+5+j])<<", CPU "<<projected[i].uv[j]<<std::defaultfloat<<'\n';
        if(first_difference.empty())first_difference=name+" uv component "+std::to_string(j);}
      require(projection_output[at+4]==projected[i].lcg,name+" lcg state");
      for(unsigned j=0;j<4;j++){
        const float actual=std::bit_cast<float>(projection_output[at+j]),wanted=reinterpret_cast<const float*>(&projected[i].rgba)[j];const double error=std::abs(double(actual)-wanted);
        projection_maximum=std::max(projection_maximum,error);if(error>1e-7+1e-7*std::abs(double(wanted)))projection_strict++;
        if(!(std::isfinite(actual)&&error<=5e-5+2e-4*std::abs(double(wanted)))){
          std::ostringstream uv;uv<<std::hexfloat;for(unsigned k=0;k<6;k++)uv<<' '<<std::bit_cast<float>(projection_output[at+5+k])<<'/'<<projected[i].uv[k];
          require(false,name+" channel "+std::to_string(j)+" got="+std::to_string(actual)+" expected="+std::to_string(wanted)+"; uv device/CPU"+uv.str());}
      }
    }
    require(uv_differences==0,std::to_string(uv_differences)+" projected uv components differ from the CPU's; the first: "+first_difference);
    projection_cases=projections.size();
    // A tile that is not loaded (kernel/util/image.h kernel_image_interp): "return (tile_descriptor ==
    // KERNEL_TILE_LOAD_FAILED) ? missing_rgba : tex.average_color;". One lookup without derivatives (level 0), no transaction.
    const auto &tex=ds.image_textures[handles[1].kernel_id()];const size_t first=tex.tile_descriptor_offset+tex.tile_levels;
    ImageTileProbe single{handles[1].kernel_id(),0u,.3f,.4f,0,0,0,0,0u,{0,0,0}};
    const auto lookup=[&](KernelTileDescriptor state,float4 &rgba,uint32_t &miss,std::vector<KernelTileDescriptor> &after,std::vector<uint8_t> &access){
      std::vector<KernelTileDescriptor> mutated=host_descriptors;for(int i=0;i<tex.tile_num;i++)mutated[first+i]=state;
      std::vector<uint8_t> none(table,KERNEL_TILE_ACCESS_NONE);uint guard[5]={0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5};
      d.guard([&]{d.upload(uint32_t(ds.image_texture_tile_descriptors.device_pointer),mutated.data(),table*sizeof(KernelTileDescriptor));
        d.upload(uint32_t(ds.image_texture_tile_access_state.device_pointer),none.data(),table);d.upload(uint32_t(input.device_pointer),&single,sizeof(single));
        d.upload(p[4],guard,sizeof(guard));p[1]=1;p[14]=d.scene_tables();d.zero(d.tile_miss_word,4);d.dispatch(p,1,shader);d.sync();
        d.download(p[4],guard,sizeof(guard));d.download(d.tile_miss_word,&miss,4);after.resize(table);access.resize(table);
        d.download(uint32_t(ds.image_texture_tile_descriptors.device_pointer),after.data(),table*sizeof(KernelTileDescriptor));
        d.download(uint32_t(ds.image_texture_tile_access_state.device_pointer),access.data(),table);d.zero(d.tile_miss_word,4);});
      healthy(d);rgba=make_float4(std::bit_cast<float>(guard[0]),std::bit_cast<float>(guard[1]),std::bit_cast<float>(guard[2]),std::bit_cast<float>(guard[3]));return mutated;};
    {float4 rgba;uint32_t miss=1;std::vector<KernelTileDescriptor> after;std::vector<uint8_t> access;
      auto mutated=lookup(KERNEL_TILE_LOAD_FAILED,rgba,miss,after,access);std::vector<uint8_t> cpu(table,KERNEL_TILE_ACCESS_NONE);
      const auto cpu_result=original_image_tile_samples(ds,{single},mutated,cpu);
      require(std::memcmp(&rgba,&cpu_result[0].rgba,16)==0&&rgba.x==1&&rgba.y==0&&rgba.z==1&&rgba.w==1,"A failed tile does not sample the Missing colour");
      require(miss==0&&after==mutated&&access==cpu,"A failed tile was requested or marked");}
    {float4 rgba;uint32_t miss=0;std::vector<KernelTileDescriptor> after;std::vector<uint8_t> access;
      auto mutated=lookup(KERNEL_TILE_LOAD_NONE,rgba,miss,after,access);
      // The pinned GPU kernels' request (kernel/util/image_2d.h kernel_image_tile_map, __KERNEL_GPU__): the descriptor becomes
      // KERNEL_TILE_LOAD_REQUEST, the access state KERNEL_TILE_ACCESS_REQUESTED, and the lookup returns the average colour.
      require(rgba.x==.25f&&rgba.y==.5f&&rgba.z==.75f&&rgba.w==1,"A requested tile does not sample the image's average colour");
      size_t requested=0,marked=0;for(size_t i=0;i<table;i++){
        if(after[i]!=mutated[i]){require(after[i]==KERNEL_TILE_LOAD_REQUEST&&mutated[i]==KERNEL_TILE_LOAD_NONE&&i>=first&&i<first+size_t(tex.tile_num),"Unexpected tile descriptor write");requested++;
          require(access[i]==KERNEL_TILE_ACCESS_REQUESTED,"A requested tile's access state");}
        marked+=access[i]!=KERNEL_TILE_ACCESS_NONE;}
      require(miss==1&&requested==1&&marked==1,"A lookup of an unloaded tile must request exactly that tile and mark the dispatch");}
    // The lcg states of volume evaluations (kernel/integrator/shade_volume.h: lcg_state_init(rng_pixel, rng_offset, sample,
    // 0x15b4f88d) on a path, 0xd9111870 on a shadow path; kernel/bake/bake.h: lcg_state_init(offset, 0, 0, 0x15b4f88d)) and of
    // a surface's (0xb4bc3953): every value exact against the pinned lcg_state_init. Paths, then the density evaluation's.
    {std::vector<uint32_t> inputs;for(uint32_t i=0;i<4096;i++){inputs.push_back(i*2654435761u);inputs.push_back((i*40503u)&0xffffu);inputs.push_back(i*7u+(i>>5));}
      for(uint32_t i=0;i<1024;i++){inputs.push_back(i);inputs.push_back(0);inputs.push_back(0);}
      const uint32_t n=uint32_t(inputs.size()/3);device_vector<uint> lcg_input(&d,"image_lcg_probe_input",MEM_READ_ONLY);lcg_input.alloc(inputs.size());
      std::copy(inputs.begin(),inputs.end(),lcg_input.data());lcg_input.copy_to_device();
      device_vector<uint> lcg_output(&d,"image_lcg_probe_output",MEM_READ_WRITE);lcg_output.alloc(n*3);std::fill_n(lcg_output.data(),n*3,0xa5a5a5a5);lcg_output.copy_to_device();
      auto lcg_shader=d.gpu.pipeline(module,d.pipeline_layout,"image_lcg_probe");p[27]=n;p[28]=lcg_input.device_pointer;p[29]=lcg_output.device_pointer;
      d.guard([&]{p[14]=d.scene_tables();d.dispatch(p,n,lcg_shader);d.sync();});healthy(d);lcg_output.copy_from_device();healthy(d);
      const uint scrambles[3]={0x15b4f88du,0xd9111870u,0xb4bc3953u};
      for(uint32_t i=0;i<n;i++)for(unsigned k=0;k<3;k++)require(lcg_output[i*3+k]==lcg_state_init(inputs[i*3],inputs[i*3+1],inputs[i*3+2],scrambles[k]),
        "lcg_state_init "+std::to_string(i)+" scramble "+std::to_string(k)+": device "+std::to_string(lcg_output[i*3+k]));
      lcg_cases=n*3;}
    // The tables as the host has them, for the cleanup's frees.
    d.guard([&]{d.upload(uint32_t(ds.image_texture_tile_descriptors.device_pointer),host_descriptors.data(),table*sizeof(KernelTileDescriptor));});healthy(d);
  }
  std::cout<<"{\"passed\":true,\"tiledImages\":"<<TILE_IMAGES<<",\"samplingCases\":"<<cases<<",\"samplingMaximumError\":"<<maximum<<",\"samplingStrictFailures\":"<<strict
           <<",\"projectionCases\":"<<projection_cases<<",\"projectionMaximumError\":"<<projection_maximum<<",\"projectionStrictFailures\":"<<projection_strict
           <<",\"restarts\":"<<restarts<<",\"tilesLoaded\":"<<tiles_loaded<<",\"tilesUsed\":"<<tiles_used<<",\"failedTileCases\":1,\"requestedTileCases\":1,\"lcgInitCases\":"<<lcg_cases<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
