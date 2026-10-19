// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
namespace ccl { class Device; }
void evaluate_material_reference(ccl::Device *device,const float *inputs,float *outputs,int count);
