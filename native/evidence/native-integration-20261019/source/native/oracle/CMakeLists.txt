# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# The oracle runner uses only the public C API, so it builds against any RID's
# host library: cmake -S native/oracle -B <dir> -DCODEBRIX_HOST_LIBRARY=<library>
cmake_minimum_required(VERSION 3.24)
project(codebrix_cycles_oracle LANGUAGES CXX)
set(CODEBRIX_HOST_LIBRARY "" CACHE FILEPATH "Built codebrix_cycles_host shared library")
if(NOT EXISTS "${CODEBRIX_HOST_LIBRARY}")
  message(FATAL_ERROR "Set CODEBRIX_HOST_LIBRARY to a built codebrix_cycles_host library")
endif()
add_executable(codebrix_cycles_oracle runner.cpp)
target_compile_features(codebrix_cycles_oracle PRIVATE cxx_std_20)
target_include_directories(codebrix_cycles_oracle PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../host")
target_compile_options(codebrix_cycles_oracle PRIVATE -Wall -Wextra -Werror)
target_link_libraries(codebrix_cycles_oracle PRIVATE "${CODEBRIX_HOST_LIBRARY}")
get_filename_component(host_directory "${CODEBRIX_HOST_LIBRARY}" DIRECTORY)
set_target_properties(codebrix_cycles_oracle PROPERTIES BUILD_RPATH "${host_directory}")
