// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "scene/scene.h"
#include <vector>
// One lookup of a tiled image: the uv, its derivatives (the dual part of the pinned dual2) and the lcg state of the
// shader data (0: no stochastic mip level). 48 bytes, the probe entry's record.
struct ImageTileProbe {int id;unsigned flags;float u,v,dudx,dvdx,dudy,dvdy;unsigned lcg,pad[3];};
struct ImageTileResult {ccl::float4 rgba;unsigned lcg;};
// The pinned CPU kernel's svm_image_texture for every probe, over the scene's tables with the given tile descriptors (the
// host's, after the device's requests were loaded) and tile access state (written as the kernel writes it). A lookup that
// asks for a tile that is not loaded throws: the CPU would load it at once, so the device must have requested it.
std::vector<ImageTileResult> original_image_tile_samples(ccl::DeviceScene &scene,const std::vector<ImageTileProbe> &probes,
                                                         std::vector<ccl::KernelTileDescriptor> &descriptors,std::vector<uint8_t> &access);
// One lookup through a projection with derivatives: the node's coordinate as a dual3 (value, d/dx, d/dy) and kind, the
// Image Texture node's projection (NODE_IMAGE_PROJ_SPHERE, NODE_IMAGE_PROJ_TUBE) or 4 plus the Environment Texture node's.
// 64 bytes, the projection probe entry's record. The result holds the projected uv and its derivatives (u, v, du/dx,
// dv/dx, du/dy, dv/dy).
struct ImageProjectionProbe {int id;unsigned flags,kind,lcg;float co[3],dx[3],dy[3];unsigned pad[3];};
struct ImageProjectionResult {ccl::float4 rgba;unsigned lcg;float uv[6];};
// The pinned svm_node_tex_image_mapping<dual3> or svm_node_tex_environment_projection<dual3> and svm_image_texture for
// every probe, with the same tables and rules as original_image_tile_samples.
std::vector<ImageProjectionResult> original_image_projection_samples(ccl::DeviceScene &scene,const std::vector<ImageProjectionProbe> &probes,
                                                                     std::vector<ccl::KernelTileDescriptor> &descriptors,std::vector<uint8_t> &access);
