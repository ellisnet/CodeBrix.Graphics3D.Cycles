// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
namespace ccl { class Device; }
void render_path_reference(ccl::Device *device,const std::string &directory,
                           unsigned width,unsigned height,unsigned samples,unsigned bounces);
