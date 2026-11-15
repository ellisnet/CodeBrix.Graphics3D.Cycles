// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Included inside the private implementation namespace.
struct TiledImageSource;
struct MemoryImageData {
  uint64_t identity;
  uint32_t width,height;
  ImageDataType type;
  unsigned channels;
  std::vector<uint8_t> pixels;
  // A tiled image (cbc_host_image_create_tiled, PROVISIONAL): no pixels here; the caller's reader supplies them.
  std::shared_ptr<TiledImageSource> tiled;
};
// A caller image, as one image or as one tile of a UDIM binding (cbc_host_image_bind_tiles, PROVISIONAL; tile 0 for a
// plain binding). ImageManager::add_image_texture returns an existing image whenever ImageLoader::equals and the
// parameters match, and the multi-loader add_image then reads the tile number from that image's loader: equals compares
// the tile, so the same pixels bound at two tiles are two device images, each with its own tile number.
class MemoryImageLoader final : public ImageLoader {
  std::shared_ptr<const MemoryImageData> data;
  int tile;
 public:
  explicit MemoryImageLoader(std::shared_ptr<const MemoryImageData> source,int tile_number=0):data(std::move(source)),tile(tile_number){}
  int get_tile_number() const override {return tile;}
  bool load_metadata(ImageMetaData &metadata,const ImageLoaderParams &params,Progress &) override {
    metadata.width=data->width;metadata.height=data->height;metadata.type=data->type;
    // ImageManager finalizes colorspace, alpha and storage after this callback.
    metadata.channels=data->channels;metadata.colorspace=params.colorspace;return true;
  }
  bool load_pixels(const ImageMetaData &metadata,void *pixels) override {
    if (metadata.width!=data->width || metadata.height!=data->height || metadata.channels!=int(data->channels) || metadata.is_rgba()!=(data->channels==4)) return false;
    if (metadata.type==data->type) std::memcpy(pixels,data->pixels.data(),data->pixels.size());
    else {
      ImageMetaData source;source.type=data->type;
      if (!OIIO::convert_image(int(data->channels),int(data->width),int(data->height),1,
          data->pixels.data(),source.typedesc(),OIIO::AutoStride,OIIO::AutoStride,OIIO::AutoStride,
          pixels,metadata.typedesc(),OIIO::AutoStride,OIIO::AutoStride,OIIO::AutoStride)) return false;
    }
    metadata.conform_pixels(pixels);return true;
  }
  string name() const override {
    return "CodeBrix caller image "+std::to_string(data->identity)+(tile?" (UDIM tile "+std::to_string(tile)+")":std::string());
  }
  bool equals(const ImageLoader &other) const override {
    auto image=dynamic_cast<const MemoryImageLoader *>(&other);return image && image->data->identity==data->identity && image->tile==tile;
  }
};
// Tiled images (cbc_host_image_create_tiled, PROVISIONAL; ABI.md "Tiled images"): Cycles' texture cache (scene/image_cache.cpp)
// loads the tiles (64 x 64 by default) and mip levels a render samples through ImageLoader::load_pixels_tile, which TiledImageLoader
// below answers from the caller's read callback; no file and no image format is involved. Cycles calls the loader from
// several threads at once: the CPU kernel's worker threads on a cache miss (ImageManager::device_cpu_load_requested), the
// parallel_for of a GPU device's cache misses (device_gpu_load_requested), the scene update's image loading (a TaskPool)
// and the session thread (shader evaluation). The caller's callbacks are serialized per host here: one runs at a time,
// in_callback set, and the other threads wait. A cancel request stops every further read (the waiting threads return at
// once), so a render that waits on reads stops after the read in progress, and a read that fails stops the render: Cycles
// would sample the texture's Missing colour for a tile that did not load (KERNEL_TILE_LOAD_FAILED), so the render fails by
// name instead (Host::render_tiles_delivered).
struct TiledReads {
  std::mutex mutex;
  std::condition_variable idle;
  bool busy=false;      // a read or release callback runs
  bool stopped=false;   // a cancel request, a closing host or a failed read: no further read in this render or bake
  bool failed=false;
  std::string failure;  // the first failed read of this render or bake
  std::set<TiledImageSource *> sources;
  std::function<void()> cancel_render;  // the host's cancel request (set at cbc_host_create)
  void begin(){std::lock_guard lock(mutex);stopped=false;failed=false;failure.clear();}
  void stop(){{std::lock_guard lock(mutex);stopped=true;}idle.notify_all();}
  // Takes the callback slot; a read (not always) gives up once the reads are stopped. A release always runs.
  bool enter(bool always){
    std::unique_lock lock(mutex);idle.wait(lock,[&]{return !busy||(!always&&stopped);});
    if(!always&&stopped)return false;
    busy=true;return true;
  }
  void leave(){{std::lock_guard lock(mutex);busy=false;}idle.notify_all();}
  // A read failed: the first failure of the render is kept (unless a cancel came first), every further read stops and the
  // render is cancelled, so it ends without finishing its batch (Host::request_cancel), and fails with the message.
  void fail(const std::string &message){
    bool first=false;
    {std::lock_guard lock(mutex);if(!stopped&&!failed){failed=true;failure=message;first=true;}stopped=true;}
    idle.notify_all();
    if(first&&cancel_render)cancel_render();
  }
};
// Runs one callback into the caller with in_callback naming it (host calls are refused meanwhile); an exception that
// escapes it counts as a failure.
template<class Function> int caller_callback(const char *name,Function &&function) noexcept {
  const char *previous=in_callback;in_callback=name;
  int status=1;
  try{status=function();}catch(...){status=1;}
  in_callback=previous;return status;
}
struct TiledImageSource {
  uint64_t identity=0;
  uint32_t width=0,height=0;
  ImageDataType type=IMAGE_DATA_TYPE_BYTE4;
  unsigned channels=0,pixel_bytes=0;
  uint32_t tile_size=64;  // CBCImageConfig::tile_size (PROVISIONAL), checked at creation (tiled_image_tile_size)
  CBCImageTileReadCallback read=nullptr;
  CBCReleaseCallback release=nullptr;
  void *user_data=nullptr;
  std::shared_ptr<TiledReads> reads;
  // A read that did not deliver (it failed, or a cancel stopped it) left its tile marked failed in Cycles' cache, which
  // Cycles never reads again: the next render gives the image a new generation, which TiledImageLoader::equals compares,
  // so Cycles loads it again (as for cbc_host_image_update), and frees the old image.
  std::atomic<uint32_t> generation{0};
  std::atomic<bool> undelivered{false};
  TiledImageSource(std::shared_ptr<TiledReads> host_reads):reads(std::move(host_reads)){std::lock_guard lock(reads->mutex);reads->sources.insert(this);}
  TiledImageSource(const TiledImageSource &)=delete;
  TiledImageSource &operator=(const TiledImageSource &)=delete;
  // The library no longer uses user_data: release runs once, in the callback slot, on the thread that drops the last
  // reference (an image release, a rebind, Cycles freeing its image at a device update, a destroy, the exit hook).
  ~TiledImageSource(){
    reads->enter(true);
    {std::lock_guard lock(reads->mutex);reads->sources.erase(this);}
    if(release)caller_callback("a tiled image's release callback",[&]{release(user_data);return 0;});
    reads->leave();
  }
  // A read that cannot deliver: the render fails with the message (TiledReads::fail), and the next one reads again.
  bool refuse(const std::string &message){undelivered=true;reads->fail("Tiled image "+std::to_string(identity)+": "+message);return false;}
  // One rectangle of a level, bottom row first (the ABI's rows), row_bytes apart, in the image's format. A float or half
  // value that is not finite is refused, as cbc_host_image_create refuses it (Cycles' conform would set it to zero).
  bool fetch(uint32_t level,uint32_t x,uint32_t y,uint32_t w,uint32_t h,void *pixels,uint64_t row_bytes){
    if(!reads->enter(false)){undelivered=true;return false;}
    const int status=caller_callback("a tiled image's read callback",[&]{return read(user_data,level,x,y,w,h,pixels,row_bytes);});
    reads->leave();
    const std::string where="level "+std::to_string(level)+", x "+std::to_string(x)+", y "+std::to_string(y)+", "+std::to_string(w)+" x "+std::to_string(h);
    if(status!=0)return refuse("the tile reader failed (returned "+std::to_string(status)+"; "+where+")");
    const bool floats=type==IMAGE_DATA_TYPE_FLOAT||type==IMAGE_DATA_TYPE_FLOAT4,halves=type==IMAGE_DATA_TYPE_HALF||type==IMAGE_DATA_TYPE_HALF4;
    if(floats||halves)for(uint32_t row=0;row<h;++row){
      const uint8_t *data=static_cast<const uint8_t *>(pixels)+row*row_bytes;
      for(uint64_t offset=0;offset<uint64_t(w)*pixel_bytes;offset+=floats?4:2){
        if(floats){float value;std::memcpy(&value,data+offset,4);if(!std::isfinite(value))return refuse("the tile reader wrote a nonfinite value ("+where+")");}
        else{uint16_t value;std::memcpy(&value,data+offset,2);if((value&0x7c00)==0x7c00)return refuse("the tile reader wrote a nonfinite half value ("+where+")");}
      }
    }
    return true;
  }
};
// The loader of a tiled image (or of one UDIM tile of tiled images). Cycles' rule for a tiled image: square power-of-two
// tiles of at least 4 * KERNEL_IMAGE_TEX_PADDING, loaded in blocks of DebugFlags().texture_cache.min_tile_size (64), and
// an image smaller than a block in both sides is a plain image (ImageMetaData::detect_tiles, util/image_metadata.cpp).
// The tile size is the image's (TiledImageSource::tile_size: 64 by default, at most 128 on Metal, whose tile pool image is
// (tile + 4) * 64 pixels wide, ImageCache::alloc_tile, and may be at most 16384 wide; tiled_image_tile_size in
// host_scene.inc), and an image smaller than its tile in both sides loads whole.
class TiledImageLoader final : public ImageLoader {
  std::shared_ptr<const MemoryImageData> data;
  int tile;
  uint32_t generation;
  // ABI pixels of the source's format into Cycles' storage type (the metadata's type after ImageMetaData::finalize, which
  // makes byte and ushort images half for a colourspace it converts), count pixels.
  bool convert(const uint8_t *from,uint8_t *to,size_t count,const ImageMetaData &metadata) const {
    if(metadata.type==data->type){std::memcpy(to,from,count*data->tiled->pixel_bytes);return true;}
    ImageMetaData source;source.type=data->type;
    return OIIO::convert_image(int(data->channels),int(count),1,1,from,source.typedesc(),OIIO::AutoStride,OIIO::AutoStride,OIIO::AutoStride,
                               to,metadata.typedesc(),OIIO::AutoStride,OIIO::AutoStride,OIIO::AutoStride);
  }
 public:
  TiledImageLoader(std::shared_ptr<const MemoryImageData> source,int tile_number):data(std::move(source)),tile(tile_number),generation(data->tiled->generation.load()){}
  int get_tile_number() const override {return tile;}
  bool load_metadata(ImageMetaData &metadata,const ImageLoaderParams &params,Progress &) override {
    metadata.width=data->width;metadata.height=data->height;metadata.type=data->type;
    metadata.channels=int(data->channels);metadata.colorspace=params.colorspace;
    const uint32_t tile_size=data->tiled->tile_size;
    const bool small=data->width<tile_size&&data->height<tile_size;
    // Every level down to one block is the caller's (ImageCache::load_image_tiled); the pixels need Cycles' conform (alpha
    // association, colourspace, finite values), as a file that is not a Blender tx file does. Average colour zero: a GPU
    // cache miss restarts the path (SR_CACHE_MISS), so no film shows it.
    metadata.has_tiles_and_mipmaps=!small;metadata.tile_size=small?0:tile_size;metadata.tile_need_conform=true;
    metadata.average_color=zero_float4();
    return true;
  }
  // The whole image, level 0, rows bottom first as Cycles stores a full image (an image smaller than a block).
  bool load_pixels(const ImageMetaData &metadata,void *pixels) override {
    TiledImageSource &source=*data->tiled;
    if(metadata.width!=data->width||metadata.height!=data->height||metadata.channels!=int(data->channels)||metadata.is_rgba()!=(data->channels==4))
      return source.refuse("Cycles asked for pixels of another size or layout");
    const size_t count=size_t(data->width)*data->height,bytes=count*source.pixel_bytes;
    if(metadata.type==data->type){
      if(!source.fetch(0,0,0,data->width,data->height,pixels,uint64_t(data->width)*source.pixel_bytes))return false;
    } else {
      std::vector<uint8_t> read(bytes);
      if(!source.fetch(0,0,0,data->width,data->height,read.data(),uint64_t(data->width)*source.pixel_bytes))return false;
      if(!convert(read.data(),static_cast<uint8_t *>(pixels),count,metadata))return source.refuse("its pixels could not be converted to Cycles' storage type");
    }
    metadata.conform_pixels(pixels);return true;
  }
  // One tile of a level with its padding: a block of (w + 2 * padding) x (h + 2 * padding) pixels whose rows run top
  // down from level row y - padding (Cycles' tiles follow tx files, row 0 at the top: kernel_image_tile_map), each
  // y_stride bytes apart. The layout is that of OIIOImageLoader::load_pixels_tile (scene/image_oiio.cpp of the pinned
  // Cycles, Apache-2.0), whose padding the kernel reads for Linear and Cubic filtering across tile edges; adapted: the
  // padding is filled pixel by pixel, a pixel inside the level from the level, one outside it by the texture's extension
  // with the kernel's own wrap rules for full images (ImageInterpolator in kernel/util/image.h: clamp for Extend, periodic
  // for Repeat, mirror for Mirror, zero for Clip), so a tiled image is filtered across tile and image edges as the same
  // pixels bound as a plain image. The pinned file loader fills a whole neighbouring region at once, and reads before its
  // buffer for Repeat when a level's width or height is one more than a multiple of 64, which this avoids. The caller is
  // asked for every needed rectangle of the level in its bottom-up rows (one call for an inner tile), the pixels are
  // converted to Cycles' storage type and conformed once each (ImageMetaData::conform_pixels), then placed.
  bool load_pixels_tile(const ImageMetaData &metadata,const int miplevel,const int64_t x,const int64_t y,const int64_t w,const int64_t h,
                        const int64_t x_stride,const int64_t y_stride,const int64_t padding,const ExtensionType extension,uint8_t *pixels) override {
    TiledImageSource &source=*data->tiled;
    const int64_t width=std::max<int64_t>(1,int64_t(data->width)>>miplevel),height=std::max<int64_t>(1,int64_t(data->height)>>miplevel);
    const int64_t pixel=int64_t(metadata.typedesc().size())*(metadata.is_rgba()?4:1);
    if(miplevel<0||w<=0||h<=0||padding<0||x_stride!=pixel||x<0||y<0||x+w>width||y+h>height)
      return source.refuse("Cycles asked for a tile outside the image (level "+std::to_string(miplevel)+")");
    // The level column (row) each block column (row) shows; -1 where Clip leaves the pixel zero.
    const auto wrap=[&](int64_t i,int64_t size)->int64_t{
      if(i>=0&&i<size)return i;
      switch(extension){
        case EXTENSION_REPEAT:{const int64_t m=i%size;return m<0?m+size:m;}
        case EXTENSION_MIRROR:{const int64_t m=std::abs(i+(i<0?1:0))%(2*size);return m>=size?2*size-m-1:m;}
        case EXTENSION_CLIP:return -1;
        default:return std::clamp<int64_t>(i,0,size-1);
      }
    };
    const int64_t block_w=w+2*padding,block_h=h+2*padding;
    std::vector<int64_t> columns(static_cast<size_t>(block_w)),rows(static_cast<size_t>(block_h));
    for(int64_t i=0;i<block_w;++i)columns[size_t(i)]=wrap(x-padding+i,width);
    for(int64_t j=0;j<block_h;++j)rows[size_t(j)]=wrap(y-padding+j,height);
    std::vector<int64_t> used_columns,used_rows;
    for(auto c:columns)if(c>=0)used_columns.push_back(c);
    for(auto r:rows)if(r>=0)used_rows.push_back(r);
    for(auto *used:{&used_columns,&used_rows}){std::sort(used->begin(),used->end());used->erase(std::unique(used->begin(),used->end()),used->end());}
    const auto rank=[](const std::vector<int64_t> &used,int64_t value){return size_t(std::lower_bound(used.begin(),used.end(),value)-used.begin());};
    // The needed pixels, gathered: used_rows x used_columns in Cycles' storage type, rows in used_rows' (top-down) order.
    const size_t gathered_w=used_columns.size(),gathered_h=used_rows.size();
    std::vector<uint8_t> gathered(gathered_w*gathered_h*size_t(pixel));
    const auto runs=[](const std::vector<int64_t> &used){
      std::vector<std::pair<int64_t,int64_t>> found;
      for(size_t i=0;i<used.size();){size_t j=i+1;while(j<used.size()&&used[j]==used[j-1]+1)++j;found.emplace_back(used[i],used[j-1]+1);i=j;}
      return found;
    };
    std::vector<uint8_t> read;
    for(auto [r0,r1]:runs(used_rows))for(auto [c0,c1]:runs(used_columns)){
      const int64_t rw=c1-c0,rh=r1-r0,row_bytes=rw*int64_t(source.pixel_bytes);
      read.resize(size_t(rh*row_bytes));
      // Top-down level rows [r0, r1) are the ABI rows [height - r1, height - r0), bottom first.
      if(!source.fetch(uint32_t(miplevel),uint32_t(c0),uint32_t(height-r1),uint32_t(rw),uint32_t(rh),read.data(),uint64_t(row_bytes)))return false;
      for(int64_t j=0;j<rh;++j){
        const int64_t row=r1-1-j;  // the top-down level row of ABI row height - r1 + j
        uint8_t *to=gathered.data()+(rank(used_rows,row)*gathered_w+rank(used_columns,c0))*size_t(pixel);
        if(!convert(read.data()+size_t(j*row_bytes),to,size_t(rw),metadata))return source.refuse("its pixels could not be converted to Cycles' storage type");
      }
    }
    const int64_t elements=metadata.is_rgba()?4:1;
    if(gathered_w&&gathered_h)metadata.conform_pixels(gathered.data(),int64_t(gathered_w),int64_t(gathered_h),elements,int64_t(gathered_w)*elements,int64_t(gathered_w)*elements);
    for(int64_t j=0;j<block_h;++j){
      uint8_t *to=pixels+j*y_stride;
      for(int64_t i=0;i<block_w;++i,to+=pixel){
        if(columns[size_t(i)]<0||rows[size_t(j)]<0){std::memset(to,0,size_t(pixel));continue;}
        std::memcpy(to,gathered.data()+(rank(used_rows,rows[size_t(j)])*gathered_w+rank(used_columns,columns[size_t(i)]))*size_t(pixel),size_t(pixel));
      }
    }
    return true;
  }
  string name() const override {
    return "CodeBrix tiled image "+std::to_string(data->identity)+(tile?" (UDIM tile "+std::to_string(tile)+")":std::string());
  }
  bool equals(const ImageLoader &other) const override {
    auto image=dynamic_cast<const TiledImageLoader *>(&other);
    return image&&image->data->identity==data->identity&&image->tile==tile&&image->generation==generation;
  }
};
// The caller images bound to one texture node: one entry with tile 0 for cbc_host_image_bind, or one entry per UDIM tile,
// in the caller's order, for cbc_host_image_bind_tiles.
using ImageBinding=std::vector<std::pair<int,std::shared_ptr<const MemoryImageData>>>;
// Authored attributes of a geometry captured before the static BVH path may
// bake it with its object transform, and the bake inputs (host_edits.inc).
struct BakeRecord {
  Object *object = nullptr;
  Transform tfm;
  bool to_motion = false;
  std::unique_ptr<AttributeSet> attributes;
};
// Authored attributes of a triangle mesh with true displacement, captured before the render that displaces it, and what
// that displacement read beyond the mesh and its shaders: the object it was evaluated with and the scene's motion type
// (host_edits.inc, reconcile_displacement).
struct DisplacedRecord {
  Object *object = nullptr;
  int motion = 0;
  std::unique_ptr<AttributeSet> attributes;
};
