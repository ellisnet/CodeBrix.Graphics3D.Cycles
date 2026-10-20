// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "wavefront.h"
#include "image_sampling_reference.h"
#include "scene/image.h"
#include "util/half.h"
#include "util/progress.h"
#include "util/task.h"
#include <OpenImageIO/imageio.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

using namespace ccl;
using namespace wavefront_test;
namespace fs = std::filesystem;

struct FileImage {
  fs::path path;
  OIIO::TypeDesc type;
  int channels, width, height;
  bool lossless;
  std::vector<float> pixels;
};

static FileImage write_image(const fs::path &path, OIIO::TypeDesc type,
                            int channels, int width, int height, unsigned seed) {
  FileImage image{path,type,channels,width,height,path.extension()!=".jpg",{}};
  image.pixels.resize(size_t(width)*height*channels);
  for (int y=0;y<height;y++) for (int x=0;x<width;x++) for (int c=0;c<channels;c++) {
    float value=float((x*17+y*31+c*47+seed*13)%256)/255.0f;
    if (type.is_floating_point()) value=float((x*7+y*11+c*19+seed)%97)/16.0f-.5f;
    if ((channels==2||channels==4)&&c==channels-1) value=float((x+y+seed)%5)/4;
    image.pixels[(size_t(y)*width+x)*channels+c]=value;
  }
  auto output=OIIO::ImageOutput::create(path.string());
  require(bool(output),"Missing reviewed OIIO writer: "+path.string());
  OIIO::ImageSpec spec(width,height,channels,type);
  spec.attribute("oiio:UnassociatedAlpha",1);
  spec.attribute("oiio:ColorSpace",type.is_floating_point()?"Linear":"sRGB");
  spec.attribute("CompressionQuality",100);
  require(output->open(path.string(),spec),"Cannot open fixture output: "+output->geterror());
  require(output->write_image(OIIO::TypeFloat,image.pixels.data()),"Cannot encode fixture: "+output->geterror());
  require(output->close(),"Cannot close fixture: "+output->geterror());
  return image;
}

struct Checks {
  uint64_t batches=0,samples=0,strict=0,known_texels=0,queued_reads=0;
  double maximum=0,known_maximum=0;
};

static const char probe_entry[] = R"WGSL(
@compute @workgroup_size(64) fn image_file_probe(@builtin(global_invocation_id) id:vec3<u32>){
 if(id.x>=p(1u)){return;}let at=p(2u)+id.x*16u;var s:SvmState;
 let color=svm_image_sample(&s,bitcast<i32>(load_word(at)),vec2<f32>(heap_float(at+8u),heap_float(at+12u)),load_word(at+4u));
 if(s.error!=0u){return;}let out=p(4u)+id.x*16u;
 for(var i=0u;i<4u;i++){store_word(out+i*4u,bitcast<u32>(color[i]));}
})WGSL";

static cbc::Pipeline make_pipeline(WebGPUDevice &device, const std::string &source) {
  WGPUShaderSourceWGSL code{};code.chain.sType=WGPUSType_ShaderSourceWGSL;
  code.code={source.data(),source.size()};WGPUShaderModuleDescriptor desc{};desc.nextInChain=&code.chain;
  device.gpu.push_scope();
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(device.gpu.get(),&desc));
  device.gpu.pop_scope();return device.gpu.pipeline(module,device.pipeline_layout,"image_file_probe");
}

// CPU answers are taken before enqueue, so replacement cannot change an oracle
// for work already queued against the previous image.
class SampleBatch {
  WebGPUDevice &device;
  device_vector<ImageProbe> input;
  device_vector<uint> output;
  std::array<uint32_t,32> parameters{};
  std::vector<float4> reference;
 public:
  SampleBatch(WebGPUDevice &d, Scene &scene, const std::vector<ImageProbe> &probes)
      :device(d),input(&d,"file_probe_input",MEM_READ_ONLY),output(&d,"file_probe_output",MEM_READ_WRITE),
       reference(original_image_samples(scene.dscene,probes)) {
    require(!probes.empty(),"Empty image file sample batch");
    input.alloc(probes.size());std::copy(probes.begin(),probes.end(),input.data());input.copy_to_device();
    output.alloc(16+probes.size()*4);std::fill_n(output.data(),output.size(),0xa5a5a5a5);output.copy_to_device();
    parameters[1]=probes.size();parameters[2]=input.device_pointer;parameters[4]=output.device_pointer+32;
    parameters[14]=d.scene_tables();parameters[17]=d.error_word;
  }
  void enqueue(cbc::Pipeline &pipeline) {device.dispatch(parameters,parameters[1],pipeline);}
  std::vector<float4> verify(Checks &checks) {
    device.sync();output.copy_from_device();healthy(device);
    std::vector<float4> result(reference.size());std::memcpy(result.data(),output.data()+8,result.size()*sizeof(float4));
    for (size_t i=0;i<reference.size()*4;i++) {
      float actual=reinterpret_cast<const float*>(result.data())[i];
      float expected=reinterpret_cast<const float*>(reference.data())[i];
      double error=std::abs(double(actual)-expected);checks.maximum=std::max(checks.maximum,error);
      require(std::isfinite(actual)&&std::isfinite(expected)&&error<=5e-5+2e-4*std::abs(double(expected)),
              "File image sample "+std::to_string(i)+" GPU="+std::to_string(actual)+" CPU="+std::to_string(expected));
      checks.strict+=error>1e-7+1e-7*std::abs(double(expected));
    }
    for (unsigned i=0;i<8;i++) require(output[i]==0xa5a5a5a5&&output[output.size()-1-i]==0xa5a5a5a5,"File sample output guard");
    checks.batches++;checks.samples+=reference.size();return result;
  }
  void capture(const std::string &prefix,const std::string &source) {
    auto save=[&](const char *suffix,const void *data,size_t bytes) {
      std::ofstream f(prefix+suffix,std::ios::binary);f.write(static_cast<const char*>(data),bytes);
      require(bool(f),"File sampling capture write failed");
    };
    auto staging=device.gpu.buffer(device.arena_bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    wgpuCommandEncoderCopyBufferToBuffer(device.commands(),device.arena,0,staging,0,device.arena_bytes);device.flush();
    std::vector<uint8_t> arena(device.arena_bytes);device.gpu.read(staging,arena.data(),arena.size());
    save(".arena.bin",arena.data(),arena.size());save(".params.bin",parameters.data(),sizeof(parameters));
    save(".reference.bin",reference.data(),reference.size()*sizeof(float4));std::ofstream(prefix+".wgsl")<<source;
    std::ofstream(prefix+".capture.json")<<"{\"entry\":\"image_file_probe\",\"recordSize\":16,\"count\":"<<reference.size()
      <<",\"outputOffset\":"<<parameters[4]<<",\"errorOffset\":"<<parameters[17]
      <<",\"fields\":[{\"name\":\"rgba\",\"offset\":0,\"size\":16,\"floating\":true}]}\n";
  }
};

static std::vector<ImageProbe> probes_for(int id) {
  const float values[]={-.5f,-.0001f,0,.1f,.5f,.9999f,1,1.5f};std::vector<ImageProbe> probes;
  for (unsigned flags=0;flags<4;flags++) for (float u:values) for (float v:values) probes.push_back({id,flags,u,v});
  return probes;
}

static void update_images(WebGPUDevice &d, Scene &scene) {
  Progress progress;scene.image_manager->device_update(&d,&scene,progress);
  require(!progress.get_error(),"Native image preparation failed");healthy(d);d.load_image_info();
}

// Known lossless texels verify channel expansion, vertical orientation and raw
// storage precision independently of the original CPU sampling implementation.
static void known_pixels(WebGPUDevice &d, Scene &scene, cbc::Pipeline &pipeline,
                         ImageHandle &handle, const FileImage &file, Checks &checks) {
  require(file.lossless,"Lossy files are not exact source-pixel oracles");
  std::vector<ImageProbe> probes;
  for (int y=0;y<file.height;y++) for (int x=0;x<file.width;x++)
    probes.push_back({handle.kernel_id(),0,(x+.5f)/file.width,(y+.5f)/file.height});
  SampleBatch batch(d,scene,probes);batch.enqueue(pipeline);auto actual=batch.verify(checks);
  auto stored=[&](float value) {
    if (file.type==OIIO::TypeUInt8) return float(std::lround(value*255))/255;
    if (file.type==OIIO::TypeUInt16) return float(std::lround(value*65535))/65535;
    if (file.type==OIIO::TypeHalf) return half_to_float_image(float_to_half_image(value));
    return value;
  };
  for (int y=0;y<file.height;y++) for (int x=0;x<file.width;x++) {
    size_t index=size_t(y)*file.width+x;
    const float *source=file.pixels.data()+(size_t(file.height-1-y)*file.width+x)*file.channels;
    const float *color=reinterpret_cast<const float*>(&actual[index]);
    for (int c=0;c<4;c++) {
      float expected=c==3?((file.channels==2||file.channels==4)?stored(source[file.channels-1]):1):stored(source[file.channels<=2?0:c]);
      double error=std::abs(double(color[c])-expected);checks.known_maximum=std::max(checks.known_maximum,error);
      require(error<=5e-5+2e-4*std::abs(double(expected)),"Known texel mismatch: "+file.path.string()+" texel="+std::to_string(index)+" channel="+std::to_string(c)+" GPU="+std::to_string(color[c])+" source="+std::to_string(expected));
    }
    checks.known_texels++;
  }
}

int main(int argc,char **argv) {
  TaskScheduler::init(4);ColorSpaceManager::init_fallback_config();int status=0;
  try {
    require(argc==2,"Usage: image_file_gate OUTPUT_PREFIX");const std::string prefix=argv[1];
    const fs::path directory=fs::path(prefix).parent_path()/"files";fs::create_directories(directory);
    std::vector<FileImage> files;
    for (auto type:{OIIO::TypeUInt8,OIIO::TypeUInt16}) for (int channels=1;channels<=4;channels++)
      files.push_back(write_image(directory/("png-"+std::to_string(type.basesize())+"-"+std::to_string(channels)+".png"),type,channels,7,5,files.size()));
    for (int channels=1;channels<=4;channels++)
      files.push_back(write_image(directory/("float-"+std::to_string(channels)+".tif"),OIIO::TypeFloat,channels,7,5,files.size()));
    for (auto type:{OIIO::TypeHalf,OIIO::TypeFloat}) for (int channels:{1,4})
      files.push_back(write_image(directory/("exr-"+std::to_string(type.basesize())+"-"+std::to_string(channels)+".exr"),type,channels,7,5,files.size()));
    for (int channels:{1,3})
      files.push_back(write_image(directory/("jpeg-"+std::to_string(channels)+".jpg"),OIIO::TypeUInt8,channels,7,5,files.size()));
    auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
    Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);
    auto pointer=dynamic_cast<WebGPUDevice*>(owner.get());require(pointer,"WebGPU device creation failed");auto &d=*pointer;
    const std::string source=svm_surface_source()+probe_entry;auto pipeline=make_pipeline(d,source);
    Checks checks;uint64_t capture_samples=0;unsigned duplicate_handles=0,replacements=0,missing_files=0,udim_tiles=0;
    std::set<unsigned> formats;unsigned parameter_variants=0;
    {
      SceneParams scene_params;scene_params.use_texture_cache=false;Scene scene(scene_params,&d);
      std::vector<ImageHandle> handles;std::vector<std::pair<size_t,size_t>> known;
      const ustring spaces[]={u_colorspace_data,u_colorspace_scene_linear,u_colorspace_scene_linear_srgb,u_colorspace_auto};
      for (size_t f=0;f<files.size();f++) for (unsigned space=0;space<4;space++) for (unsigned alpha=0;alpha<IMAGE_ALPHA_NUM_TYPES;alpha++) {
        ImageParams params;params.colorspace=spaces[space];params.alpha_type=ImageAlphaType(alpha);
        params.interpolation=INTERPOLATION_CLOSEST;params.extension=EXTENSION_REPEAT;
        if (files[f].lossless&&space==0&&alpha==IMAGE_ALPHA_CHANNEL_PACKED) known.emplace_back(handles.size(),f);
        handles.push_back(scene.image_manager->add_image(files[f].path.string(),params));parameter_variants++;
      }
      update_images(d,scene);
      require(d.images.size()==parameter_variants,"File variant load count changed");
      for (auto &[id,image]:d.images) formats.insert(image->info.data_type);
      for (auto [handle,file]:known) known_pixels(d,scene,pipeline,handles[handle],files[file],checks);

      // Keep one real image untouched throughout edits of another image.
      const auto anchor_id=scene.dscene.image_textures[handles[0].kernel_id()].image_info_id;
      const auto anchor=d.images.at(anchor_id);const auto anchor_address=anchor->device_pointer;
      std::vector<uint8_t> anchor_bytes(anchor->memory_size());d.download(anchor_address,anchor_bytes.data(),anchor_bytes.size());
      ImageParams edit_params;edit_params.colorspace=u_colorspace_data;edit_params.alpha_type=IMAGE_ALPHA_CHANNEL_PACKED;
      edit_params.interpolation=INTERPOLATION_CLOSEST;edit_params.extension=EXTENSION_EXTEND;edit_params.animated=true;
      const fs::path edit_path=directory/"edit.png";
      FileImage edited=write_image(edit_path,OIIO::TypeUInt8,4,7,5,99);
      ImageHandle editing=scene.image_manager->add_image(edit_path.string(),edit_params);update_images(d,scene);
      const int logical_slot=editing.kernel_id();
      for (unsigned step=0;step<16;step++) {
        ImageHandle duplicate=scene.image_manager->add_image(edit_path.string(),edit_params);
        require(duplicate==editing&&duplicate.kernel_id()==logical_slot,"Duplicate image was reloaded");duplicate_handles++;
        duplicate.clear();require(!scene.image_manager->need_update(),"Live duplicate release invalidated image");
        SampleBatch queued(d,scene,probes_for(editing.kernel_id()));queued.enqueue(pipeline);
        const auto old_info=scene.dscene.image_textures[logical_slot].image_info_id;
        editing.clear();update_images(d,scene);
        require(!d.images.contains(old_info),"Freed image still owns GPU storage");
        const int widths[]={1,129,17,7};const int heights[]={1,65,3,5};
        edited=write_image(edit_path,step%2?OIIO::TypeUInt16:OIIO::TypeUInt8,4,widths[step%4],heights[step%4],100+step);
        edit_params.frame=step+1;editing=scene.image_manager->add_image(edit_path.string(),edit_params);
        require(editing.kernel_id()==logical_slot,"Native logical image slot was not reused");
        require(scene.image_manager->set_animation_frame_update(step+1),"Native animated image did not request update");
        require(!scene.image_manager->set_animation_frame_update(step+1),"Same animation frame requested update");
        update_images(d,scene);queued.verify(checks);checks.queued_reads++;replacements++;
        known_pixels(d,scene,pipeline,editing,edited,checks);
        require(d.images.at(anchor_id)==anchor&&anchor->device_pointer==anchor_address,"Unrelated image allocation changed");
        std::vector<uint8_t> now(anchor_bytes.size());d.download(anchor_address,now.data(),now.size());
        require(now==anchor_bytes,"Unrelated image pixels changed");
      }
      handles.push_back(std::move(editing));

      // Use the native filename/UDIM loader and table builder, not a manually
      // manufactured logical mapping. A deliberately absent tile stays magenta.
      array<int> tiles;for (int tile:{1001,1002,1012}) {
        tiles.push_back_slow(tile);write_image(directory/("tile."+std::to_string(tile)+".png"),OIIO::TypeUInt8,4,5,3,tile);
      }
      ImageParams tiled; tiled.colorspace=u_colorspace_data;tiled.alpha_type=IMAGE_ALPHA_CHANNEL_PACKED;
      auto udim=scene.image_manager->add_image((directory/"tile.<UDIM>.png").string(),tiled,tiles);
      udim_tiles=udim.num_tiles();require(udim_tiles==3,"Native UDIM tile count");
      auto uvtiles=scene.image_manager->add_image((directory/"tile.<UVTILE>.png").string(),tiled,tiles);
      // Exercise the alternate pattern with the same known pixel content.
      for (int tile:tiles) fs::copy_file(directory/("tile."+std::to_string(tile)+".png"),
        directory/("tile.u"+std::to_string((tile-1001)%10+1)+"_v"+std::to_string((tile-1001)/10+1)+".png"),fs::copy_options::overwrite_existing);
      std::ofstream(directory/"corrupt.png",std::ios::binary)<<"This is a deliberately invalid PNG fixture.\n";
      for (const char *name:{"missing.png","corrupt.png"}) {handles.push_back(scene.image_manager->add_image((directory/name).string(),tiled));missing_files++;}
      update_images(d,scene);
      for (size_t i=handles.size()-2;i<handles.size();i++) {
        auto id=handles[i].kernel_id();require(scene.dscene.image_textures[id].image_info_id==KERNEL_IMAGE_NONE,"Missing image did not retain native missing descriptor");
      }
      std::vector<ImageProbe> probes;
      for (auto &handle:handles) {auto p=probes_for(handle.kernel_id());probes.insert(probes.end(),p.begin(),p.end());}
      for (int id:{udim.kernel_id(),uvtiles.kernel_id()}) for (unsigned flags=0;flags<4;flags++)
        for (float u:{-.5f,0.f,.3f,.9999f,1.f,1.3f,2.3f,10.1f}) for (float v:{-.5f,0.f,.4f,1.f,1.4f,2.3f}) probes.push_back({id,flags,u,v});
      SampleBatch batch(d,scene,probes);capture_samples=probes.size();batch.capture(prefix,source);batch.enqueue(pipeline);batch.verify(checks);
    }
    d.load_image_info();healthy(d);
    require(stats.mem_used==0&&d.images.empty()&&!d.globals.contains("image_info")&&!d.globals.contains("image_bounds"),"Image cleanup retained allocation/descriptor");
    std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
    std::cout<<std::setprecision(12)<<"{\"passed\":true,\"nativeFileImageManager\":true,\"fileVariants\":"<<files.size()
      <<",\"parameterVariants\":"<<parameter_variants<<",\"residentFormats\":"<<formats.size()<<",\"nativeUdimTiles\":"<<udim_tiles
      <<",\"missingFiles\":"<<missing_files<<",\"duplicateHandles\":"<<duplicate_handles<<",\"replacementCycles\":"<<replacements
      <<",\"queuedOldImageReads\":"<<checks.queued_reads<<",\"knownLosslessTexels\":"<<checks.known_texels
      <<",\"knownMaximumError\":"<<checks.known_maximum<<",\"samplingCases\":"<<checks.samples<<",\"captureCases\":"<<capture_samples
      <<",\"maximumError\":"<<checks.maximum<<",\"strictFailures\":"<<checks.strict<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  } catch (const std::exception &e) {std::cerr<<e.what()<<'\n';status=1;}
  ColorSpaceManager::free_memory();TaskScheduler::exit();return status;
}
