// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Included inside the private implementation namespace.
struct MemoryImageData {
  uint64_t identity;
  uint32_t width,height;
  ImageDataType type;
  unsigned channels;
  std::vector<uint8_t> pixels;
};
class MemoryImageLoader final : public ImageLoader {
  std::shared_ptr<const MemoryImageData> data;
 public:
  explicit MemoryImageLoader(std::shared_ptr<const MemoryImageData> source):data(std::move(source)){}
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
  string name() const override {return "CodeBrix caller image "+std::to_string(data->identity);}
  bool equals(const ImageLoader &other) const override {
    auto image=dynamic_cast<const MemoryImageLoader *>(&other);return image && image->data->identity==data->identity;
  }
};
