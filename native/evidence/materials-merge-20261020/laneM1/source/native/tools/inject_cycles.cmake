# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
if(CMAKE_VERSION VERSION_LESS 3.24)
  message(FATAL_ERROR "The CodeBrix native host requires CMake 3.24 or newer")
endif()
set(CODEBRIX_NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(CODEBRIX_WEBGPU_RID "linux-x64" CACHE STRING "Shipped WebGPU runtime RID")
include_directories(BEFORE "${CODEBRIX_CYCLES_OVERLAY}")
add_definitions(-DWITH_WEBGPU)
if(ANDROID)
  # fmt 10's optional consteval parser fails under pinned NDK 29 / Clang 21.
  # Its supported runtime-checked parser remains enabled.
  add_compile_definitions(FMT_CONSTEVAL=)
endif()
function(codebrix_add_cycles_device)
  cmake_policy(SET CMP0079 NEW)
  set(CODEBRIX_REFERENCE_OPTIONS -ffp-contract=off)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    list(APPEND CODEBRIX_REFERENCE_OPTIONS -msse4.2)
  endif()
  get_target_property(device_sources cycles_device SOURCES)
  list(REMOVE_ITEM device_sources device.cpp)
  set_property(TARGET cycles_device PROPERTY SOURCES "${device_sources}")
  target_sources(cycles_device PRIVATE "${CODEBRIX_CYCLES_OVERLAY}/device/device.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/image.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/device.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/queue.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/lighting.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/background.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/volume.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/svm_layout.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/pipelines.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/device_tier.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shader_data.cpp"
    "${CODEBRIX_NATIVE_ROOT}/src/webgpu_device.cpp")
  target_include_directories(cycles_device PRIVATE "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  add_library(codebrix_wgpu SHARED IMPORTED)
  set_target_properties(codebrix_wgpu PROPERTIES IMPORTED_LOCATION
    "${CODEBRIX_WEBGPU_PACKAGE}/runtimes/${CODEBRIX_WEBGPU_RID}/native/libcodebrix_wgpu.so")
  target_link_libraries(cycles_device PUBLIC codebrix_wgpu ${CMAKE_DL_LIBS})
  get_target_property(integrator_sources cycles_integrator SOURCES)
  list(REMOVE_ITEM integrator_sources path_trace.cpp)
  set_property(TARGET cycles_integrator PROPERTY SOURCES "${integrator_sources}")
  target_sources(cycles_integrator PRIVATE "${CODEBRIX_CYCLES_OVERLAY}/integrator/path_trace.cpp")
  get_target_property(util_sources cycles_util SOURCES)
  list(REMOVE_ITEM util_sources colorspace.cpp)
  set_property(TARGET cycles_util PROPERTY SOURCES "${util_sources}")
  target_sources(cycles_util PRIVATE "${CODEBRIX_CYCLES_OVERLAY}/util/colorspace.cpp")
  set(codebrix_device_libs cycles_device cycles_kernel cycles_scene cycles_session cycles_integrator
    cycles_bvh cycles_subd cycles_graph cycles_util extern_sky)
  cycles_external_libraries_append(codebrix_device_libs)
  get_directory_property(definitions DIRECTORY "${PROJECT_SOURCE_DIR}/src" COMPILE_DEFINITIONS)
  include("${CODEBRIX_NATIVE_ROOT}/host/targets.cmake")
  add_executable(codebrix_cycles_device_gate "${CODEBRIX_NATIVE_ROOT}/cycles/gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/control_reference.cpp")
  target_compile_features(codebrix_cycles_device_gate PRIVATE cxx_std_20)
  target_compile_definitions(codebrix_cycles_device_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_device_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_device_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_camera_gate "${CODEBRIX_NATIVE_ROOT}/cycles/camera_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp")
  target_compile_features(codebrix_cycles_camera_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_camera_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_camera_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_camera_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_camera_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_closest_gate "${CODEBRIX_NATIVE_ROOT}/cycles/closest_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp")
  target_compile_features(codebrix_cycles_closest_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_closest_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_closest_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_closest_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_closest_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_shadow_gate "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp")
  target_compile_features(codebrix_cycles_shadow_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_shadow_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_shadow_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_shadow_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_shadow_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_transparent_shadow_gate "${CODEBRIX_NATIVE_ROOT}/cycles/transparent_shadow_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp")
  target_compile_features(codebrix_cycles_transparent_shadow_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_transparent_shadow_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_transparent_shadow_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_transparent_shadow_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_transparent_shadow_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_transparent_shadow_gate PRIVATE CODEBRIX_TRANSPARENT_SHADOW_STATE_GATE)
  add_executable(codebrix_cycles_transparent_lighting_gate "${CODEBRIX_NATIVE_ROOT}/cycles/lighting_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_transparent_lighting_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_transparent_lighting_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_transparent_lighting_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_transparent_lighting_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_transparent_lighting_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_transparent_lighting_gate PRIVATE CODEBRIX_LIGHTING_MIS_GATE CODEBRIX_TRANSPARENT_LIGHT_GATE)
  add_executable(codebrix_cycles_lighting_gate "${CODEBRIX_NATIVE_ROOT}/cycles/lighting_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_lighting_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_lighting_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_lighting_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_lighting_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_lighting_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_lighting_mis_gate "${CODEBRIX_NATIVE_ROOT}/cycles/lighting_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_lighting_mis_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_lighting_mis_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_lighting_mis_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_lighting_mis_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_lighting_mis_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_lighting_mis_gate PRIVATE CODEBRIX_LIGHTING_MIS_GATE)
  add_executable(codebrix_cycles_triangle_lighting_gate "${CODEBRIX_NATIVE_ROOT}/cycles/lighting_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_triangle_lighting_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_triangle_lighting_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_triangle_lighting_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_triangle_lighting_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_triangle_lighting_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_triangle_lighting_gate PRIVATE CODEBRIX_TRIANGLE_LIGHT_GATE CODEBRIX_LIGHTING_MIS_GATE)
  add_executable(codebrix_cycles_broad_lighting_gate "${CODEBRIX_NATIVE_ROOT}/cycles/lighting_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_broad_lighting_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_broad_lighting_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_broad_lighting_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_broad_lighting_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_broad_lighting_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_broad_lighting_gate PRIVATE CODEBRIX_LIGHTING_MIS_GATE CODEBRIX_BROAD_LIGHT_GATE)
  add_executable(codebrix_cycles_object_motion_render_gate "${CODEBRIX_NATIVE_ROOT}/cycles/lighting_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_object_motion_render_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_object_motion_render_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_object_motion_render_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_object_motion_render_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_object_motion_render_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_object_motion_render_gate PRIVATE CODEBRIX_LIGHTING_MIS_GATE CODEBRIX_OBJECT_MOTION_GATE)
  add_executable(codebrix_cycles_deforming_motion_render_gate "${CODEBRIX_NATIVE_ROOT}/cycles/lighting_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_deforming_motion_render_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_deforming_motion_render_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_deforming_motion_render_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_deforming_motion_render_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_deforming_motion_render_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_deforming_motion_render_gate PRIVATE CODEBRIX_LIGHTING_MIS_GATE CODEBRIX_OBJECT_MOTION_GATE CODEBRIX_DEFORMING_MOTION_GATE)
  add_executable(codebrix_cycles_feature_combination_gate "${CODEBRIX_NATIVE_ROOT}/cycles/lighting_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shadow_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_feature_combination_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_feature_combination_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_feature_combination_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_feature_combination_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_feature_combination_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_feature_combination_gate PRIVATE CODEBRIX_LIGHTING_MIS_GATE CODEBRIX_FEATURE_COMBINATION_GATE)
  add_executable(codebrix_cycles_light_state_gate "${CODEBRIX_NATIVE_ROOT}/cycles/light_state_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_light_state_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_light_state_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_light_state_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_light_state_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_light_state_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_broad_light_state_gate "${CODEBRIX_NATIVE_ROOT}/cycles/light_state_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_broad_light_state_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_broad_light_state_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_broad_light_state_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_broad_light_state_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_broad_light_state_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_broad_light_state_gate PRIVATE CODEBRIX_BROAD_LIGHT_GATE)
  add_executable(codebrix_cycles_emitter_sample_gate "${CODEBRIX_NATIVE_ROOT}/cycles/emitter_sample_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_emitter_sample_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_emitter_sample_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_emitter_sample_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_emitter_sample_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_emitter_sample_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_emitter_sample_gate PRIVATE CODEBRIX_BROAD_LIGHT_GATE)
  add_executable(codebrix_cycles_triangle_sample_gate "${CODEBRIX_NATIVE_ROOT}/cycles/emitter_sample_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/light_reference.cpp")
  target_compile_features(codebrix_cycles_triangle_sample_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_triangle_sample_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_triangle_sample_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_triangle_sample_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_triangle_sample_gate PRIVATE ${codebrix_device_libs})
  target_compile_definitions(codebrix_cycles_triangle_sample_gate PRIVATE CODEBRIX_TRIANGLE_LIGHT_GATE)
  add_executable(codebrix_cycles_background_gate "${CODEBRIX_NATIVE_ROOT}/cycles/background_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
  target_compile_features(codebrix_cycles_background_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_background_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_background_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_background_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_background_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_svm_world_gate "${CODEBRIX_NATIVE_ROOT}/cycles/svm_world_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
  target_compile_features(codebrix_cycles_svm_world_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_svm_world_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_svm_world_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_svm_world_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_svm_world_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_shader_data_gate "${CODEBRIX_NATIVE_ROOT}/cycles/shader_data_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shader_data_reference.cpp")
  target_compile_features(codebrix_cycles_shader_data_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_shader_data_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_shader_data_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_shader_data_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_shader_data_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_svm_surface_gate "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_reference.cpp")
  target_compile_features(codebrix_cycles_svm_surface_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_svm_surface_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_svm_surface_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_svm_surface_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_svm_surface_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_attribute_gate "${CODEBRIX_NATIVE_ROOT}/cycles/attribute_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_reference.cpp")
  target_compile_features(codebrix_cycles_attribute_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_attribute_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_attribute_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_attribute_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_attribute_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_normal_gate "${CODEBRIX_NATIVE_ROOT}/cycles/normal_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_reference.cpp")
  target_compile_features(codebrix_cycles_normal_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_normal_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_normal_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_normal_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_normal_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_image_gate "${CODEBRIX_NATIVE_ROOT}/cycles/image_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/image_sampling_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_reference.cpp")
  target_compile_features(codebrix_cycles_image_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_image_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_image_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_image_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_image_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_image_file_gate "${CODEBRIX_NATIVE_ROOT}/cycles/image_file_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/image_sampling_reference.cpp")
  target_compile_features(codebrix_cycles_image_file_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_image_file_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_image_file_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_image_file_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_image_file_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_surface_gate "${CODEBRIX_NATIVE_ROOT}/cycles/surface_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp")
  target_compile_features(codebrix_cycles_surface_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_surface_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_surface_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_surface_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_surface_gate PRIVATE ${codebrix_device_libs})
  foreach(kind IN ITEMS svm bounce render)
    if(kind STREQUAL "svm")
      set(gate_source svm_surface_gate.cpp)
      set(reference_source svm_surface_reference.cpp)
    elseif(kind STREQUAL "bounce")
      set(gate_source surface_gate.cpp)
      set(reference_source surface_reference.cpp)
    else()
      set(gate_source render_gate.cpp)
      set(reference_source surface_reference.cpp)
    endif()
    set(gate_target codebrix_cycles_microfacet_${kind}_gate)
    add_executable(${gate_target} "${CODEBRIX_NATIVE_ROOT}/cycles/${gate_source}"
      "${CODEBRIX_NATIVE_ROOT}/cycles/${reference_source}")
    if(kind STREQUAL "render")
      target_sources(${gate_target} PRIVATE "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp"
        "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
    endif()
    target_compile_features(${gate_target} PRIVATE cxx_std_20)
    target_compile_options(${gate_target} PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
    target_compile_definitions(${gate_target} PRIVATE ${definitions} CBC_MICROFACET_FIXTURE=1)
    target_include_directories(${gate_target} PRIVATE "${PROJECT_SOURCE_DIR}/src"
      "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
    target_link_libraries(${gate_target} PRIVATE ${codebrix_device_libs})
  endforeach()
  foreach(kind IN ITEMS svm bounce render)
    if(kind STREQUAL "svm")
      set(gate_source svm_surface_gate.cpp)
      set(reference_source svm_surface_reference.cpp)
    elseif(kind STREQUAL "bounce")
      set(gate_source surface_gate.cpp)
      set(reference_source surface_reference.cpp)
    else()
      set(gate_source render_gate.cpp)
      set(reference_source surface_reference.cpp)
    endif()
    set(gate_target codebrix_cycles_glass_${kind}_gate)
    add_executable(${gate_target} "${CODEBRIX_NATIVE_ROOT}/cycles/${gate_source}"
      "${CODEBRIX_NATIVE_ROOT}/cycles/${reference_source}")
    if(kind STREQUAL "render")
      target_sources(${gate_target} PRIVATE "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp"
        "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
    endif()
    target_compile_features(${gate_target} PRIVATE cxx_std_20)
    target_compile_options(${gate_target} PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
    target_compile_definitions(${gate_target} PRIVATE ${definitions} CBC_GLASS_FIXTURE=1)
    target_include_directories(${gate_target} PRIVATE "${PROJECT_SOURCE_DIR}/src"
      "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
    target_link_libraries(${gate_target} PRIVATE ${codebrix_device_libs})
  endforeach()
  foreach(kind IN ITEMS svm bounce render)
    if(kind STREQUAL "svm")
      set(gate_source svm_surface_gate.cpp)
      set(reference_source svm_surface_reference.cpp)
    elseif(kind STREQUAL "bounce")
      set(gate_source surface_gate.cpp)
      set(reference_source surface_reference.cpp)
    else()
      set(gate_source render_gate.cpp)
      set(reference_source surface_reference.cpp)
    endif()
    set(gate_target codebrix_cycles_principled_${kind}_gate)
    add_executable(${gate_target} "${CODEBRIX_NATIVE_ROOT}/cycles/${gate_source}"
      "${CODEBRIX_NATIVE_ROOT}/cycles/${reference_source}")
    if(kind STREQUAL "render")
      target_sources(${gate_target} PRIVATE "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp"
        "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
    endif()
    target_compile_features(${gate_target} PRIVATE cxx_std_20)
    target_compile_options(${gate_target} PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
    target_compile_definitions(${gate_target} PRIVATE ${definitions} CBC_PRINCIPLED_FIXTURE=1)
    target_include_directories(${gate_target} PRIVATE "${PROJECT_SOURCE_DIR}/src"
      "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
    target_link_libraries(${gate_target} PRIVATE ${codebrix_device_libs})
  endforeach()
  add_executable(codebrix_cycles_volume_gate "${CODEBRIX_NATIVE_ROOT}/cycles/volume_gate.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/volume_reference.cpp")
  target_compile_features(codebrix_cycles_volume_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_volume_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_volume_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_volume_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_volume_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_volume_render_gate "${CODEBRIX_NATIVE_ROOT}/cycles/render_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/volume_reference.cpp")
  target_compile_features(codebrix_cycles_volume_render_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_volume_render_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_volume_render_gate PRIVATE ${definitions} CBC_VOLUME_FIXTURE=1)
  target_include_directories(codebrix_cycles_volume_render_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_volume_render_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_volume_boundary_gate "${CODEBRIX_NATIVE_ROOT}/cycles/volume_boundary_gate.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/volume_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp")
  target_compile_features(codebrix_cycles_volume_boundary_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_volume_boundary_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_volume_boundary_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_volume_boundary_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_volume_boundary_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_closed_volume_render_gate "${CODEBRIX_NATIVE_ROOT}/cycles/render_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/volume_reference.cpp")
  target_compile_features(codebrix_cycles_closed_volume_render_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_closed_volume_render_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_closed_volume_render_gate PRIVATE ${definitions} CBC_VOLUME_FIXTURE=1 CBC_CLOSED_VOLUME_FIXTURE=1)
  target_include_directories(codebrix_cycles_closed_volume_render_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_closed_volume_render_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_render_gate "${CODEBRIX_NATIVE_ROOT}/cycles/render_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
  target_compile_features(codebrix_cycles_render_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_render_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_render_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_render_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_render_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_atomic_film_gate "${CODEBRIX_NATIVE_ROOT}/../tools/licensing/tests/atomic_contract.cpp")
  target_compile_features(codebrix_cycles_atomic_film_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_atomic_film_gate PRIVATE ${CODEBRIX_REFERENCE_OPTIONS})
  target_compile_definitions(codebrix_cycles_atomic_film_gate PRIVATE ${definitions} CODEBRIX_TEST_CYCLES_FILM)
  target_include_directories(codebrix_cycles_atomic_film_gate PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(codebrix_cycles_atomic_film_gate PRIVATE ${codebrix_device_libs})
endfunction()
cmake_language(DEFER CALL codebrix_add_cycles_device)
