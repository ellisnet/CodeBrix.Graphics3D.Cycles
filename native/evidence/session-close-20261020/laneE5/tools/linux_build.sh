#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase E5: clean Linux build of the final tree (reviewed dependency prefix reused), then every gate target.
cd /home/jeremy/ClaudeHome/cycles-worktrees/e5-pixel-sampler
python3 tools/build.py --jobs 6 --skip-dependencies || exit 1
targets="codebrix_cycles_device_gate codebrix_cycles_camera_gate codebrix_cycles_closest_gate codebrix_cycles_shadow_gate codebrix_cycles_transparent_shadow_gate codebrix_cycles_transparent_lighting_gate codebrix_cycles_lighting_gate codebrix_cycles_lighting_mis_gate codebrix_cycles_triangle_lighting_gate codebrix_cycles_broad_lighting_gate codebrix_cycles_shadow_terminator_gate codebrix_cycles_object_motion_render_gate codebrix_cycles_deforming_motion_render_gate codebrix_cycles_feature_combination_gate codebrix_cycles_light_state_gate codebrix_cycles_broad_light_state_gate codebrix_cycles_emitter_sample_gate codebrix_cycles_triangle_sample_gate codebrix_cycles_background_gate codebrix_cycles_svm_world_gate codebrix_cycles_shader_data_gate codebrix_cycles_svm_surface_gate codebrix_cycles_attribute_gate codebrix_cycles_normal_gate codebrix_cycles_image_gate codebrix_cycles_image_file_gate codebrix_cycles_surface_gate codebrix_cycles_volume_gate codebrix_cycles_volume_render_gate codebrix_cycles_volume_boundary_gate codebrix_cycles_closed_volume_render_gate codebrix_cycles_render_gate codebrix_cycles_atomic_film_gate $(cmake --build native/work/cycles-build --target help | grep -oE 'codebrix_cycles_(glass|microfacet)[a-z_]*_gate' | sort -u | tr '\n' ' ')"
echo "gate targets: $targets"
python3 native/tools/build_cycles.py --jobs 6 $(for t in $targets; do echo --target $t; done)
