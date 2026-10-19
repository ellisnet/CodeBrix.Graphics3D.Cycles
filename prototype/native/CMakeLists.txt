# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
set(prototype_libs cycles_device cycles_kernel cycles_scene cycles_session
    cycles_bvh cycles_subd cycles_graph cycles_util extern_sky)
cycles_external_libraries_append(prototype_libs)
get_directory_property(cycles_definitions DIRECTORY "${PROJECT_SOURCE_DIR}/src" COMPILE_DEFINITIONS)
add_executable(codebrix_cycles_host_probe "${CODEBRIX_PROTOTYPE_NATIVE_DIR}/host_probe.cpp")
target_compile_features(codebrix_cycles_host_probe PRIVATE cxx_std_20)
target_compile_definitions(codebrix_cycles_host_probe PRIVATE ${cycles_definitions})
target_include_directories(codebrix_cycles_host_probe PRIVATE "${PROJECT_SOURCE_DIR}/src"
    )
target_link_libraries(codebrix_cycles_host_probe PRIVATE ${prototype_libs})
include("${CODEBRIX_PROTOTYPE_NATIVE_DIR}/../../parity/native/CMakeLists.txt")
