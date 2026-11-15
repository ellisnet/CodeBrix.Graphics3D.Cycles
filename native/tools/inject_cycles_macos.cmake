# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# osx-arm64 product profile: pinned Cycles with its own Metal device, the
# CodeBrix C host and no WebGPU device. Linux/Android use inject_cycles.cmake.
# CMake 4.2 or newer: OBJECT_NAME (codebrix_checkout_independent_objects below) is what makes the host library, its
# executables and its dSYM the same at every checkout path and gives dsymutil the line tables of every object.
if(CMAKE_VERSION VERSION_LESS 4.2)
  message(FATAL_ERROR "The osx-arm64 recipe requires CMake 4.2 or newer (this is CMake ${CMAKE_VERSION}): it names the host's "
    "objects with the OBJECT_NAME source property (CMake 4.2) for a build that is the same at every checkout path and a dSYM "
    "with the line tables of every object")
endif()
if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
  message(FATAL_ERROR "inject_cycles_macos.cmake builds only the osx-arm64 Metal profile")
endif()
set(CODEBRIX_NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
include_directories(BEFORE "${CODEBRIX_CYCLES_OVERLAY}")
# As with NDK 29's Clang 21: fmt 10's optional consteval format-string parser
# fails under Apple Clang 21; its supported runtime-checked parser stays enabled.
add_compile_definitions(FMT_CONSTEVAL=)
# CPU oracle arithmetic, the same definition as inject_cycles.cmake (Linux x64,
# Linux ARM64, macOS x64): no floating-point contraction (Apple Clang's arm64
# default is on) and no reciprocal math, at the architecture's base level (no
# -mcpu; build_cycles_macos.py keeps Cycles' native-only mode off). Directory
# compile options follow Cycles' CMAKE_CXX_FLAGS, so they take precedence. The
# Metal kernels are compiled at run time and are not affected.
add_compile_options(-ffp-contract=off -fno-reciprocal-math)
set(CODEBRIX_CPU_FP_CONTRACT "off")
# Repository-relative object paths in the debug map (Cycles appends its own
# linker flags as a list, so they are not passed through CMAKE_*_LINKER_FLAGS).
if(CODEBRIX_OSO_PREFIX)
  add_link_options("LINKER:-oso_prefix,${CODEBRIX_OSO_PREFIX}")
endif()
# Object names for a debug map that is the same at every checkout path and that dsymutil
# reads unambiguously (OBJECT_NAME, CMake 4.2). The debug map (which the UUID covers)
# names each object the host library and the executables link, by its path for an
# object linked directly and as archive(member) for one taken from a static library;
# with ZERO_AR_DATE (build_cycles_macos.py) it records no modification times.
# - CMake names the object of a source outside the Cycles source and build trees after
#   its absolute path: the targets of native/host/targets.cmake name theirs after the
#   repository-relative path instead.
# - A static library keeps only each object's file name, and Cycles' device library has
#   several of one name (device.cpp in cpu/, multi/, the overlay, ...). Without times
#   dsymutil takes the first member of a name for every entry of that name, so each
#   object of a repeated name is named after its path relative to its target's source
#   directory (or to the repository), with '_' for '/'.
function(codebrix_checkout_independent_objects existing_targets)
  cmake_path(SET repository NORMALIZE "${CODEBRIX_NATIVE_ROOT}/..")
  get_property(host_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  list(REMOVE_ITEM host_targets ${existing_targets})
  foreach(target IN LISTS host_targets)
    get_target_property(sources ${target} SOURCES)
    foreach(source IN LISTS sources)
      if(source MATCHES "^\\$<" OR NOT IS_ABSOLUTE "${source}")
        continue()
      endif()
      cmake_path(SET path NORMALIZE "${source}")
      cmake_path(IS_PREFIX repository "${path}" in_repository)
      cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${path}" in_source)
      cmake_path(IS_PREFIX CMAKE_BINARY_DIR "${path}" in_build)
      if(in_repository AND NOT in_source AND NOT in_build)
        cmake_path(RELATIVE_PATH path BASE_DIRECTORY "${repository}" OUTPUT_VARIABLE object)
        set_source_files_properties("${source}" TARGET_DIRECTORY ${target} PROPERTIES OBJECT_NAME "${object}")
      endif()
    endforeach()
  endforeach()
  set(directories "${CMAKE_SOURCE_DIR}")
  while(directories)
    list(POP_FRONT directories directory)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    list(APPEND directories ${subdirectories})
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type ${target} TYPE)
      if(NOT type STREQUAL "STATIC_LIBRARY")
        continue()
      endif()
      get_target_property(source_dir ${target} SOURCE_DIR)
      get_target_property(sources ${target} SOURCES)
      set(compiled "")
      set(members "")
      foreach(source IN LISTS sources)
        if(source MATCHES "^\\$<" OR NOT source MATCHES "\\.(c|cc|cpp|cxx|m|mm)$")
          continue()
        endif()
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${source_dir}" NORMALIZE OUTPUT_VARIABLE path)
        cmake_path(GET path FILENAME member)
        list(APPEND compiled "${path}")
        list(APPEND members "${member}")
      endforeach()
      set(seen "")
      foreach(path IN LISTS compiled)
        cmake_path(GET path FILENAME member)
        set(count 0)
        foreach(other IN LISTS members)
          if(other STREQUAL member)
            math(EXPR count "${count} + 1")
          endif()
        endforeach()
        if(count LESS 2)
          continue()
        endif()
        cmake_path(IS_PREFIX source_dir "${path}" in_target)
        if(in_target)
          cmake_path(RELATIVE_PATH path BASE_DIRECTORY "${source_dir}" OUTPUT_VARIABLE object)
        else()
          cmake_path(RELATIVE_PATH path BASE_DIRECTORY "${repository}" OUTPUT_VARIABLE object)
        endif()
        string(REPLACE "/" "_" object "${object}")
        if(object IN_LIST seen)
          message(FATAL_ERROR "Two objects of ${target} would both be named ${object}")
        endif()
        list(APPEND seen "${object}")
        set_source_files_properties("${path}" TARGET_DIRECTORY ${target} PROPERTIES OBJECT_NAME "${object}")
      endforeach()
    endforeach()
  endwhile()
endfunction()
function(codebrix_add_cycles_metal_host)
  cmake_policy(SET CMP0079 NEW)
  if(NOT WITH_CYCLES_DEVICE_METAL)
    message(FATAL_ERROR "The osx-arm64 profile requires WITH_CYCLES_DEVICE_METAL")
  endif()
  # The overlay shared with the other RIDs (device registration names, the
  # scheduling additions, util/colorspace.cpp handed through unchanged);
  # WITH_WEBGPU stays undefined, so no WebGPU device is compiled and the
  # overlay copy of util/types_gpu_compressed.h preprocesses to the pinned text
  # (Cycles' half-precision integrator state, as its Metal kernels expect).
  # The CPU device's arithmetic for the capability report (see inject_cycles.cmake).
  target_sources(cycles_kernel_cpu PRIVATE "${CODEBRIX_NATIVE_ROOT}/cycles/cpu_arithmetic.cpp")
  set_source_files_properties("${CODEBRIX_NATIVE_ROOT}/cycles/cpu_arithmetic.cpp" TARGET_DIRECTORY cycles_kernel_cpu
    PROPERTIES COMPILE_DEFINITIONS "CODEBRIX_CPU_FP_CONTRACT=\"${CODEBRIX_CPU_FP_CONTRACT}\"")
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
  get_property(codebrix_cycles_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  include("${CODEBRIX_NATIVE_ROOT}/host/targets.cmake")
  codebrix_checkout_independent_objects("${codebrix_cycles_targets}")
endfunction()
cmake_language(DEFER CALL codebrix_add_cycles_metal_host)
