# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# osx-x64 product profile: pinned Cycles with the CodeBrix WGSL/WebGPU device on
# the shipped CodeBrix.WebGpu.Native osx-x64 runtime, the CPU device and the C
# host. It reuses the Linux/Android injection (inject_cycles.cmake) unchanged and
# adds only what macOS needs: the runtime is a .dylib, Apple Clang 21 needs fmt's
# runtime-checked parser, and the debug map gets repository-relative paths. The
# osx-arm64 product (Cycles' Metal device) uses inject_cycles_macos.cmake.
if(CMAKE_VERSION VERSION_LESS 3.24)
  message(FATAL_ERROR "The CodeBrix native host requires CMake 3.24 or newer")
endif()
if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
  message(FATAL_ERROR "inject_cycles_macos_x64.cmake builds only the osx-x64 WGSL profile")
endif()
if(NOT CODEBRIX_WEBGPU_RID STREQUAL "osx-x64")
  message(FATAL_ERROR "The osx-x64 profile requires -DCODEBRIX_WEBGPU_RID=osx-x64")
endif()
if(WITH_CYCLES_DEVICE_METAL)
  message(FATAL_ERROR "The osx-x64 profile uses the WGSL/WebGPU device; configure with WITH_CYCLES_DEVICE_METAL=OFF")
endif()
# As on osx-arm64 and NDK 29 (Clang 21): fmt 10's optional consteval
# format-string parser fails; its supported runtime-checked parser stays enabled.
add_compile_definitions(FMT_CONSTEVAL=)
# The product binary reads no image files: callers bind decoded pixels, and the
# host rejects texture filenames (same rule as osx-arm64). targets.cmake reads
# this variable when the deferred device function includes it.
set(CODEBRIX_HOST_NO_IMAGE_FILES ON)
# Repository-relative object paths in the debug map (Cycles appends its own
# linker flags as a list, so they are not passed through CMAKE_*_LINKER_FLAGS).
if(CODEBRIX_OSO_PREFIX)
  add_link_options("LINKER:-oso_prefix,${CODEBRIX_OSO_PREFIX}")
endif()
# The linker signs arm64 output ad hoc by itself but leaves x86_64 output
# unsigned; sign here so the build tree carries the same ad-hoc signatures as
# on osx-arm64 (the host gate verifies them; the stager re-signs its copies).
add_link_options("LINKER:-adhoc_codesign")
include("${CMAKE_CURRENT_LIST_DIR}/inject_cycles.cmake")
# Deferred calls run in the order they are registered, so this runs after
# codebrix_add_cycles_device has created the imported runtime target with the
# Linux file name. Point it at the osx-x64 dylib, whose install name is
# @rpath/libcodebrix_wgpu.dylib; the recipe copies it beside the host library.
function(codebrix_use_osx_x64_webgpu_runtime)
  if(NOT TARGET codebrix_wgpu)
    message(FATAL_ERROR "inject_cycles.cmake did not create the codebrix_wgpu runtime target")
  endif()
  set(runtime "${CODEBRIX_WEBGPU_PACKAGE}/runtimes/osx-x64/native/libcodebrix_wgpu.dylib")
  if(NOT EXISTS "${runtime}")
    message(FATAL_ERROR "The shipped osx-x64 WebGPU runtime is missing: ${runtime}")
  endif()
  set_target_properties(codebrix_wgpu PROPERTIES IMPORTED_LOCATION "${runtime}"
    IMPORTED_SONAME "@rpath/libcodebrix_wgpu.dylib")
  message(STATUS "CodeBrix osx-x64 WebGPU runtime: ${runtime}")
endfunction()
cmake_language(DEFER CALL codebrix_use_osx_x64_webgpu_runtime)
