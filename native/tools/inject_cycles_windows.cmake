# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Windows product profile (win-x64, win-arm64): pinned Cycles with the CodeBrix
# WGSL/WebGPU device on the shipped CodeBrix.WebGpu.Native runtime, the CPU
# device and the C host, built with clang-cl. It reuses the Linux/Android
# injection (inject_cycles.cmake), whose MSVC branches pass the fixed-baseline
# math options in clang-cl's /clang: form, and adds only what Windows needs: the
# runtime is a DLL linked through an import library the recipe generates, the
# hybrid C runtime, fmt's runtime-checked parser, and no image file readers.
if(CMAKE_VERSION VERSION_LESS 3.24)
  message(FATAL_ERROR "The CodeBrix native host requires CMake 3.24 or newer")
endif()
if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT MSVC)
  message(FATAL_ERROR "inject_cycles_windows.cmake builds only the Windows WGSL profile with clang-cl")
endif()
if(NOT CODEBRIX_WEBGPU_RID MATCHES "^win-(x64|arm64)$")
  message(FATAL_ERROR "The Windows profile requires -DCODEBRIX_WEBGPU_RID=win-x64 or win-arm64")
endif()
if(NOT EXISTS "${CODEBRIX_WEBGPU_IMPLIB}")
  message(FATAL_ERROR "The Windows profile requires -DCODEBRIX_WEBGPU_IMPLIB (the generated codebrix_wgpu.lib)")
endif()
# As on macOS and NDK 29: fmt 10's optional consteval format-string parser fails
# with Clang 21 and later; its supported runtime-checked parser stays enabled.
add_compile_definitions(FMT_CONSTEVAL=)
# Hybrid C runtime, as in the dependency prefix (tools/windows_toolchain.py):
# Cycles' configure_build.cmake forces /MD into CMAKE_<LANG>_FLAGS_RELEASE after
# this file runs; a directory option comes later on every command line and wins.
add_compile_options(/MT)
# Debug line tables and the build-path map, which a CMAKE_<LANG>_FLAGS value would
# lose to configure_build.cmake (build_cycles_windows.py passes them).
if(CODEBRIX_WINDOWS_COMPILE_OPTIONS)
  add_compile_options(${CODEBRIX_WINDOWS_COMPILE_OPTIONS})
endif()
# The product binary reads no image files: callers bind decoded pixels, and the
# host rejects texture filenames (the macOS rule). targets.cmake reads this
# variable when the deferred device function includes it.
set(CODEBRIX_HOST_NO_IMAGE_FILES ON)
include("${CMAKE_CURRENT_LIST_DIR}/inject_cycles.cmake")
# Deferred calls run in the order they are registered, so this runs after
# codebrix_add_cycles_device has created the imported runtime target with the
# Linux file name. Point it at the Windows DLL and its import library; the
# recipe copies the DLL beside the host library.
function(codebrix_use_windows_webgpu_runtime)
  if(NOT TARGET codebrix_wgpu)
    message(FATAL_ERROR "inject_cycles.cmake did not create the codebrix_wgpu runtime target")
  endif()
  set(runtime "${CODEBRIX_WEBGPU_PACKAGE}/runtimes/${CODEBRIX_WEBGPU_RID}/native/codebrix_wgpu.dll")
  if(NOT EXISTS "${runtime}")
    message(FATAL_ERROR "The shipped ${CODEBRIX_WEBGPU_RID} WebGPU runtime is missing: ${runtime}")
  endif()
  set_target_properties(codebrix_wgpu PROPERTIES IMPORTED_LOCATION "${runtime}" IMPORTED_IMPLIB "${CODEBRIX_WEBGPU_IMPLIB}")
  message(STATUS "CodeBrix ${CODEBRIX_WEBGPU_RID} WebGPU runtime: ${runtime}")
endfunction()
cmake_language(DEFER CALL codebrix_use_windows_webgpu_runtime)
