# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# osx-arm64 product profile: pinned Cycles with its own Metal device, the
# CodeBrix C host and no WebGPU device. Linux/Android use inject_cycles.cmake.
if(CMAKE_VERSION VERSION_LESS 3.24)
  message(FATAL_ERROR "The CodeBrix native host requires CMake 3.24 or newer")
endif()
if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
  message(FATAL_ERROR "inject_cycles_macos.cmake builds only the osx-arm64 Metal profile")
endif()
set(CODEBRIX_NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
include_directories(BEFORE "${CODEBRIX_CYCLES_OVERLAY}")
# As with NDK 29's Clang 21: fmt 10's optional consteval format-string parser
# fails under Apple Clang 21; its supported runtime-checked parser stays enabled.
add_compile_definitions(FMT_CONSTEVAL=)
# Repository-relative object paths in the debug map (Cycles appends its own
# linker flags as a list, so they are not passed through CMAKE_*_LINKER_FLAGS).
if(CODEBRIX_OSO_PREFIX)
  add_link_options("LINKER:-oso_prefix,${CODEBRIX_OSO_PREFIX}")
endif()
function(codebrix_add_cycles_metal_host)
  cmake_policy(SET CMP0079 NEW)
  if(NOT WITH_CYCLES_DEVICE_METAL)
    message(FATAL_ERROR "The osx-arm64 profile requires WITH_CYCLES_DEVICE_METAL")
  endif()
  # Device registration names and the color-cache correction, shared with the
  # other RIDs; WITH_WEBGPU stays undefined, so no WebGPU device is compiled.
  get_target_property(device_sources cycles_device SOURCES)
  list(REMOVE_ITEM device_sources device.cpp)
  set_property(TARGET cycles_device PROPERTY SOURCES "${device_sources}")
  target_sources(cycles_device PRIVATE "${CODEBRIX_CYCLES_OVERLAY}/device/device.cpp")
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
  set(CODEBRIX_HOST_NO_IMAGE_FILES ON)
  set(CODEBRIX_HOST_GPU_TEST_BACKEND CBC_HOST_METAL)
  set(CODEBRIX_HOST_GPU_TEST_NAME Metal)
  include("${CODEBRIX_NATIVE_ROOT}/host/targets.cmake")
endfunction()
cmake_language(DEFER CALL codebrix_add_cycles_metal_host)
