// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Renders a cbcOracleScene file through the public C host API and writes raw
// float32 RGBA films plus meta.json. The same source builds on every platform.
#include "cycles_host.h"
#include "json.h"
#include "sha256.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using oracle::Json;

namespace {

[[noreturn]] void fail(const std::string &message) { throw std::runtime_error(message); }

void check(int status, const std::string &call) {
  if (status == CBC_OK) return;
  char error[4096] = {}; uint64_t required = 0;
  cbc_host_error(error, sizeof(error), &required);
  fail(call + " returned " + std::to_string(status) + ": " + error);
}

std::string read_file(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) fail("Cannot read " + path.string());
  std::ostringstream content; content << file.rdbuf(); return content.str();
}

struct Loader {
  CBCHost host;
  fs::path directory;
  std::map<std::string, CBCNode> nodes;
  std::map<std::string, CBCImage> images;

  CBCNode node(const std::string &handle, const std::map<std::string, CBCNode> *local = nullptr) const {
    if (local) if (auto it = local->find(handle); it != local->end()) return it->second;
    auto it = nodes.find(handle);
    if (it == nodes.end()) fail("Unknown node handle: " + handle);
    return it->second;
  }

  void image(const Json &entry) {
    static const std::map<std::string, std::pair<uint32_t, uint32_t>> formats = {
      {"float", {CBC_IMAGE_FLOAT, 4}}, {"float4", {CBC_IMAGE_FLOAT4, 16}}, {"half", {CBC_IMAGE_HALF, 2}},
      {"half4", {CBC_IMAGE_HALF4, 8}}, {"byte", {CBC_IMAGE_BYTE, 1}}, {"byte4", {CBC_IMAGE_BYTE4, 4}},
      {"ushort", {CBC_IMAGE_USHORT, 2}}, {"ushort4", {CBC_IMAGE_USHORT4, 8}}};
    const auto &handle = entry.at("handle").string();
    auto format = formats.find(entry.at("format").string());
    if (format == formats.end()) fail("Unknown image format for " + handle);
    CBCImageConfig config = {sizeof(config), uint32_t(entry.at("width").as_int()), uint32_t(entry.at("height").as_int()), format->second.first, {0}};
    auto pixels = read_file(directory / entry.at("file").string());
    if (pixels.size() != uint64_t(config.width) * config.height * format->second.second)
      fail("Raw pixel file size does not match " + handle);
    CBCImage image = 0;
    check(cbc_host_image_create(host, &config, pixels.data(), pixels.size(), &image), "cbc_host_image_create(" + handle + ")");
    if (!images.emplace(handle, image).second) fail("Duplicate image handle: " + handle);
  }

  // Each value is written with the setter that matches the native socket kind.
  void set(CBCNode target, const std::string &socket, const Json &value, const std::map<std::string, CBCNode> *local) {
    const std::string where = "socket '" + socket + "'";
    if (socket == "image") {
      auto it = images.find(value.string());
      if (it == images.end()) fail("Unknown image handle for " + where);
      check(cbc_host_image_bind(host, target, it->second), "cbc_host_image_bind");
      return;
    }
    CBCPropertyInfo info = {sizeof(info), 0, 0, 0, 0, 0};
    check(cbc_host_property_info(host, target, socket.c_str(), &info), "cbc_host_property_info(" + socket + ")");
    const bool array = info.flags & CBC_PROPERTY_ARRAY;
    std::vector<const Json *> items;
    if (value.is(Json::Array)) for (auto &item : value.items) items.push_back(&item); else items.push_back(&value);
    auto floats = [&] {
      std::vector<float> out;
      std::function<void(const Json &)> flatten = [&](const Json &v) {
        if (v.is(Json::Array)) for (auto &item : v.items) flatten(item); else out.push_back(v.as_float());
      };
      flatten(value); return out;
    };
    switch (info.value_type) {
      case CBC_VALUE_BOOLEAN: case CBC_VALUE_INT: case CBC_VALUE_UINT: case CBC_VALUE_UINT64: {
        std::vector<int64_t> ints;
        for (auto item : items) {
          if (info.value_type == CBC_VALUE_BOOLEAN && !item->is(Json::Boolean)) fail(where + " expects booleans");
          ints.push_back(item->as_int());
        }
        if (!array && ints.size() != 1) fail(where + " expects one value");
        check(cbc_host_set_ints(host, target, socket.c_str(), ints.data(), ints.size()), "cbc_host_set_ints(" + socket + ")");
        return;
      }
      case CBC_VALUE_ENUM:
        if (value.is(Json::String)) check(cbc_host_set_string(host, target, socket.c_str(), value.text.c_str()), "cbc_host_set_string(" + socket + ")");
        else { int64_t v = value.as_int(); check(cbc_host_set_ints(host, target, socket.c_str(), &v, 1), "cbc_host_set_ints(" + socket + ")"); }
        return;
      case CBC_VALUE_FLOAT: case CBC_VALUE_COLOR: case CBC_VALUE_VECTOR: case CBC_VALUE_POINT: case CBC_VALUE_NORMAL:
      case CBC_VALUE_POINT2: case CBC_VALUE_TRANSFORM: case CBC_VALUE_FLOAT4: {
        auto values = floats();
        check(cbc_host_set_floats(host, target, socket.c_str(), values.data(), values.size()), "cbc_host_set_floats(" + socket + ")");
        return;
      }
      case CBC_VALUE_STRING:
        if (array) {
          std::vector<const char *> strings; for (auto item : items) strings.push_back(item->string().c_str());
          check(cbc_host_set_strings(host, target, socket.c_str(), strings.data(), strings.size()), "cbc_host_set_strings(" + socket + ")");
        } else check(cbc_host_set_string(host, target, socket.c_str(), value.string().c_str()), "cbc_host_set_string(" + socket + ")");
        return;
      case CBC_VALUE_NODE: {
        std::vector<CBCNode> handles;
        for (auto item : items) handles.push_back(item->is(Json::Null) ? 0 : node(item->string(), local));
        check(cbc_host_set_nodes(host, target, socket.c_str(), handles.data(), handles.size()), "cbc_host_set_nodes(" + socket + ")");
        return;
      }
      default: fail(where + " has a value kind this loader cannot express (" + std::to_string(info.value_type) + ")");
    }
  }

  void apply(CBCNode target, const Json *values, const std::map<std::string, CBCNode> *local) {
    if (!values) return;
    if (!values->is(Json::Object)) fail("'set' must be an object");
    for (auto &[socket, value] : values->fields) set(target, socket, value, local);
  }

  void graph(CBCNode shader, const Json &graph) {
    std::map<std::string, CBCNode> local;
    CBCNode output = 0; check(cbc_host_shader_output(host, shader, &output), "cbc_host_shader_output");
    local.emplace("output", output);
    std::vector<std::pair<CBCNode, const Json *>> pending;
    for (auto &entry : graph.at("nodes").items) {
      const auto &handle = entry.at("handle").string();
      CBCNode created = 0;
      check(cbc_host_shader_node(host, shader, entry.at("type").string().c_str(), &created), "cbc_host_shader_node(" + entry.at("type").string() + ")");
      if (!local.emplace(handle, created).second) fail("Duplicate graph handle: " + handle);
      pending.emplace_back(created, entry.find("set"));
    }
    for (auto &[created, values] : pending) apply(created, values, &local);
    if (auto links = graph.find("connect")) for (auto &link : links->items) {
      if (link.items.size() != 4) fail("Each connection is [from, output, to, input]");
      check(cbc_host_connect(host, node(link.items[0].string(), &local), link.items[1].string().c_str(),
                             node(link.items[2].string(), &local), link.items[3].string().c_str()),
            "cbc_host_connect(" + link.items[0].string() + "." + link.items[1].string() + " -> " +
                link.items[2].string() + "." + link.items[3].string() + ")");
    }
  }

  // Raw little-endian 32-bit arrays beside the scene (positionsFile, indicesFile, attribute file).
  template<typename T> std::vector<T> raw_array(const Json &name) const {
    const auto bytes = read_file(directory / name.string());
    if (bytes.size() % sizeof(T)) fail("Raw array file " + name.string() + " is not a whole number of 32-bit values");
    std::vector<T> values(bytes.size() / sizeof(T));
    if (!values.empty()) std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
  }

  void mesh(CBCNode target, const Json &mesh) {
    std::vector<float> xyz; std::vector<uint32_t> indices;
    if (auto file = mesh.find("positionsFile")) xyz = raw_array<float>(*file);
    else for (auto &v : mesh.at("positions").items) xyz.push_back(v.as_float());
    if (auto file = mesh.find("indicesFile")) indices = raw_array<uint32_t>(*file);
    else for (auto &v : mesh.at("indices").items) indices.push_back(uint32_t(v.as_int()));
    if (xyz.size() % 3 || indices.size() % 3) fail("Mesh positions and indices must be complete triples");
    check(cbc_host_mesh(host, target, xyz.data(), xyz.size() / 3, indices.data(), indices.size() / 3), "cbc_host_mesh");
    if (auto attributes = mesh.find("attributes")) for (auto &attribute : attributes->items) {
      static const std::map<std::string, uint32_t> values = {{"float", CBC_VALUE_FLOAT}, {"color", CBC_VALUE_COLOR},
        {"vector", CBC_VALUE_VECTOR}, {"point", CBC_VALUE_POINT}, {"normal", CBC_VALUE_NORMAL}, {"point2", CBC_VALUE_POINT2},
        {"transform", CBC_VALUE_TRANSFORM}, {"float4", CBC_VALUE_FLOAT4}, {"byte4", CBC_VALUE_BYTE4}};
      static const std::map<std::string, uint32_t> domains = {{"mesh", CBC_ATTR_MESH}, {"face", CBC_ATTR_FACE},
        {"vertex", CBC_ATTR_VERTEX}, {"corner", CBC_ATTR_CORNER}, {"vertex_normal", CBC_ATTR_VERTEX_NORMAL},
        {"corner_normal", CBC_ATTR_CORNER_NORMAL}, {"corner_byte", CBC_ATTR_CORNER_BYTE}};
      const auto &name = attribute.at("name").string();
      auto value = values.find(attribute.at("value").string()); auto domain = domains.find(attribute.at("domain").string());
      if (value == values.end() || domain == domains.end()) fail("Unknown attribute value or domain for " + name);
      uint32_t flags = 0;
      if (auto f = attribute.find("flags")) for (auto &flag : f->items)
        flags |= flag.string() == "standard" ? uint32_t(CBC_ATTR_STANDARD) : flag.string() == "subdivision" ? uint32_t(CBC_ATTR_SUBDIVISION) : (fail("Unknown attribute flag"), 0u);
      CBCAttributeConfig config = {sizeof(config), value->second, domain->second, flags, {0}};
      if (auto data = attribute.find("data")) {
        std::vector<float> floats; for (auto &v : data->items) floats.push_back(v.as_float());
        check(cbc_host_attribute_floats(host, target, name.c_str(), &config, floats.data(), floats.size()), "cbc_host_attribute_floats(" + name + ")");
      } else if (auto file = attribute.find("file")) {
        if (value->second == CBC_VALUE_BYTE4) {
          auto bytes = read_file(directory / file->string());
          check(cbc_host_attribute_bytes(host, target, name.c_str(), &config, reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()), "cbc_host_attribute_bytes(" + name + ")");
        } else {
          auto floats = raw_array<float>(*file);
          check(cbc_host_attribute_floats(host, target, name.c_str(), &config, floats.data(), floats.size()), "cbc_host_attribute_floats(" + name + ")");
        }
      } else {
        std::vector<uint8_t> bytes; for (auto &v : attribute.at("bytes").items) bytes.push_back(uint8_t(v.as_int()));
        check(cbc_host_attribute_bytes(host, target, name.c_str(), &config, bytes.data(), bytes.size()), "cbc_host_attribute_bytes(" + name + ")");
      }
    }
    // Optional deformation motion: steps time-ordered position arrays, the
    // center step equal to "positions" (cbc_host_motion_positions).
    if (auto motion = mesh.find("motion")) {
      std::vector<float> positions;
      for (auto &v : motion->at("positions").items) positions.push_back(v.as_float());
      check(cbc_host_motion_positions(host, target, uint32_t(motion->at("steps").as_int()), positions.data(), positions.size()),
            "cbc_host_motion_positions");
    }
  }

  void entry(const Json &entry) {
    const auto &handle = entry.at("handle").string();
    CBCNode target = 0;
    if (auto singleton = entry.find("scene")) check(cbc_host_scene_node(host, singleton->string().c_str(), &target), "cbc_host_scene_node(" + singleton->string() + ")");
    else check(cbc_host_create_node(host, entry.at("create").string().c_str(), &target), "cbc_host_create_node(" + entry.at("create").string() + ")");
    if (!nodes.emplace(handle, target).second) fail("Duplicate node handle: " + handle);
    if (auto g = entry.find("graph")) graph(target, *g);
    if (auto m = entry.find("mesh")) mesh(target, *m);
    apply(target, entry.find("set"), nullptr);
  }
};

struct Options { fs::path scene, output; std::string backend, label; uint32_t threads = 4; };

Options parse(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string key = argv[i];
    if (i + 1 >= argc) fail("Missing value for " + key);
    std::string value = argv[++i];
    if (key == "--scene") options.scene = value; else if (key == "--output") options.output = value;
    else if (key == "--backend") options.backend = value; else if (key == "--label") options.label = value;
    else if (key == "--threads") options.threads = uint32_t(std::stoul(value));
    else fail("Unknown option " + key);
  }
  if (options.scene.empty() || options.output.empty() || options.backend.empty())
    fail("Usage: codebrix_cycles_oracle --scene FILE --backend cpu|metal|webgpu --output DIR [--label NAME] [--threads N]");
  if (options.label.empty()) options.label = options.backend;
  return options;
}

int run(int argc, char **argv) {
  static_assert(std::endian::native == std::endian::little, "Films are written as little-endian float32");
  auto options = parse(argc, argv);
  const std::map<std::string, uint32_t> backends = {{"cpu", CBC_HOST_CPU_REFERENCE}, {"metal", CBC_HOST_METAL}, {"webgpu", CBC_HOST_WEBGPU}};
  auto backend = backends.find(options.backend);
  if (backend == backends.end()) fail("Unknown backend " + options.backend);
  const auto text = read_file(options.scene);
  const auto scene_sha = oracle::sha256(text.data(), text.size());
  const auto scene = oracle::JsonReader(text).document();
  if (scene.at("cbcOracleScene").as_int() != 1) fail("Unsupported cbcOracleScene version");
  // Results are never overwritten: every run needs a new or empty directory.
  if (fs::exists(options.output) && !fs::is_empty(options.output))
    fail("Output directory is not empty; use a new directory: " + options.output.string());
  fs::create_directories(options.output);
  const auto &render = scene.at("render");
  const uint32_t width = uint32_t(render.at("width").as_int()), height = uint32_t(render.at("height").as_int());
  const uint32_t samples = uint32_t(render.at("samples").as_int());
  const uint32_t repeats = render.find("repeats") ? uint32_t(render.at("repeats").as_int()) : 2;
  if (!repeats) fail("render.repeats must be positive");
  const fs::path directory = options.scene.parent_path();
  if (auto color = scene.find("colorConfig"); color && !color->is(Json::Null))
    check(cbc_host_color_config_path((directory / color->string()).string().c_str()), "cbc_host_color_config_path");

  CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, backend->second, options.threads, {0}};
  CBCHost host = 0; check(cbc_host_create(&config, &host), "cbc_host_create(" + options.backend + ")");
  Loader loader{host, directory, {}, {}};
  std::vector<std::vector<float>> films;
  std::vector<double> seconds;
  try {
    if (auto images = scene.find("images")) for (auto &image : images->items) loader.image(image);
    for (auto &entry : scene.at("nodes").items) loader.entry(entry);
    CBCRenderConfig render_config = {sizeof(render_config), width, height, samples, {0}};
    for (uint32_t i = 0; i < repeats; ++i) {
      std::vector<float> film(size_t(width) * height * 4);
      auto start = std::chrono::steady_clock::now();
      check(cbc_host_render(host, &render_config, film.data(), film.size()), "cbc_host_render");
      seconds.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
      films.push_back(std::move(film));
    }
  } catch (...) { cbc_host_destroy(host); throw; }
  check(cbc_host_destroy(host), "cbc_host_destroy");

  std::vector<std::string> hashes;
  for (size_t i = 0; i < films.size(); ++i) {
    std::ofstream file(options.output / ("film-" + std::to_string(i) + ".f32"), std::ios::binary);
    file.write(reinterpret_cast<const char *>(films[i].data()), std::streamsize(films[i].size() * sizeof(float)));
    if (!file) fail("Cannot write film " + std::to_string(i));
    hashes.push_back(oracle::sha256(films[i].data(), films[i].size() * sizeof(float)));
  }
  bool identical = true; double maximum = 0;
  for (size_t i = 1; i < films.size(); ++i) for (size_t j = 0; j < films[i].size(); ++j) {
    if (std::memcmp(&films[i][j], &films[0][j], sizeof(float)) != 0) identical = false;
    maximum = std::max(maximum, double(std::fabs(films[i][j] - films[0][j])));
  }
  auto list = [](auto &values, auto format) {
    std::string out = "["; for (size_t i = 0; i < values.size(); ++i) out += (i ? "," : "") + format(values[i]); return out + "]";
  };
  std::ostringstream meta;
  meta.precision(17);
  meta << "{\n  \"sceneId\": " << oracle::json_quote(scene.at("id").string())
       << ",\n  \"sceneSha256\": \"" << scene_sha << "\",\n  \"backend\": " << oracle::json_quote(options.label)
       << ",\n  \"hostBackend\": " << oracle::json_quote(options.backend) << ",\n  \"hostAbi\": " << cbc_host_abi_version()
       << ",\n  \"threads\": " << options.threads << ",\n  \"width\": " << width << ",\n  \"height\": " << height
       << ",\n  \"samples\": " << samples << ",\n  \"repeats\": " << repeats
       << ",\n  \"repeatBitwiseIdentical\": " << (identical ? "true" : "false")
       << ",\n  \"repeatMaxAbsDifference\": " << maximum
       << ",\n  \"renderSeconds\": " << list(seconds, [](double s) { std::ostringstream o; o.precision(6); o << s; return o.str(); })
       << ",\n  \"filmSha256\": " << list(hashes, [](const std::string &h) { return "\"" + h + "\""; })
       << ",\n  \"filmLayout\": \"float32 little-endian RGBA, width*height*4 values, lower-left origin\""
       << ",\n  \"warnings\": []\n}\n";
  std::ofstream(options.output / "meta.json") << meta.str();
  std::cout << meta.str();
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  try { return run(argc, argv); }
  catch (const std::exception &error) { std::cerr << "codebrix_cycles_oracle: " << error.what() << "\n"; return 1; }
}
