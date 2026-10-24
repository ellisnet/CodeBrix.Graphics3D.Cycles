# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Included after the Cycles targets and reviewed dependencies exist.
# Native library version, the single source for cbc_host_version():
# "<major>.<minor>.<yyyyMMdd>.<build>". major.minor is set here; the date is the
# UTC configure date (CMake honours SOURCE_DATE_EPOCH for reproducible builds);
# build is CODEBRIX_HOST_BUILD_NUMBER. The Cycles revision is read from the
# pinned import record. See ABI.md "Versioning".
set(CODEBRIX_HOST_VERSION_MAJOR_MINOR "1.0")
set(CODEBRIX_HOST_BUILD_NUMBER "0" CACHE STRING "Fourth component of the native library version")
if(NOT CODEBRIX_HOST_BUILD_NUMBER MATCHES "^[0-9]+$")
  message(FATAL_ERROR "CODEBRIX_HOST_BUILD_NUMBER must be a decimal integer")
endif()
string(TIMESTAMP codebrix_host_date "%Y%m%d" UTC)
set(CODEBRIX_HOST_LIBRARY_VERSION "${CODEBRIX_HOST_VERSION_MAJOR_MINOR}.${codebrix_host_date}.${CODEBRIX_HOST_BUILD_NUMBER}")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../../third_party/cycles-import.json" codebrix_revision_line
  REGEX "^  \"revision\": \"[0-9a-f]+\"" LIMIT_COUNT 1)
string(REGEX REPLACE "^.*\"([0-9a-f]+)\".*$" "\\1" CODEBRIX_CYCLES_REVISION "${codebrix_revision_line}")
string(LENGTH "${CODEBRIX_CYCLES_REVISION}" codebrix_revision_length)
if(NOT codebrix_revision_length EQUAL 40)
  message(FATAL_ERROR "Cannot read the pinned Cycles revision from third_party/cycles-import.json")
endif()
message(STATUS "CodeBrix Cycles host ${CODEBRIX_HOST_LIBRARY_VERSION}, Cycles ${CODEBRIX_CYCLES_REVISION}")
add_library(codebrix_cycles_host SHARED "${CMAKE_CURRENT_LIST_DIR}/cycles_host.cpp")
target_compile_definitions(codebrix_cycles_host PRIVATE
  CBC_HOST_LIBRARY_VERSION="${CODEBRIX_HOST_LIBRARY_VERSION}" CBC_HOST_CYCLES_REVISION="${CODEBRIX_CYCLES_REVISION}")
target_compile_features(codebrix_cycles_host PRIVATE cxx_std_20)
target_compile_definitions(codebrix_cycles_host PRIVATE ${definitions})
target_include_directories(codebrix_cycles_host PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_include_directories(codebrix_cycles_host PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(codebrix_cycles_host PRIVATE ${codebrix_device_libs})
# Retain native registration constructors, including unreferenced node classes.
set_property(TARGET codebrix_cycles_host PROPERTY LINK_LIBRARY_OVERRIDE
  "WHOLE_ARCHIVE,cycles_scene,cycles_session,cycles_graph")
if(WIN32)
  target_sources(codebrix_cycles_host PRIVATE "${CMAKE_CURRENT_LIST_DIR}/exports.def")
elseif(APPLE)
  target_link_options(codebrix_cycles_host PRIVATE
    "LINKER:-exported_symbols_list,${CMAKE_CURRENT_LIST_DIR}/exports.macos")
  set_property(TARGET codebrix_cycles_host APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/exports.macos")
else()
  target_link_options(codebrix_cycles_host PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_LIST_DIR}/exports.map")
  set_property(TARGET codebrix_cycles_host APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/exports.map")
endif()
if(NOT MSVC)
  target_compile_options(codebrix_cycles_host PRIVATE -ffp-contract=off)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    target_compile_options(codebrix_cycles_host PRIVATE -msse4.2)
  endif()
endif()

# A product profile without image file readers rejects texture filenames.
if(CODEBRIX_HOST_NO_IMAGE_FILES)
  target_compile_definitions(codebrix_cycles_host PRIVATE CBC_HOST_NO_IMAGE_FILES)
endif()

# pthread consumers currently cover Linux, Android and macOS. Other platforms still
# require lifecycle execution; compiling a library alone does not establish it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID OR APPLE)
  find_package(Threads REQUIRED)
  # The original and overlaid color implementations must have the same native
  # math options. Cycles sets these in its src/util directory, not on consumers.
  add_library(codebrix_host_reference_color OBJECT "${PROJECT_SOURCE_DIR}/src/util/colorspace.cpp")
  get_directory_property(color_flags DIRECTORY "${PROJECT_SOURCE_DIR}/src/util" DEFINITION CMAKE_CXX_FLAGS)
  separate_arguments(color_options NATIVE_COMMAND "${color_flags}")
  # Cycles' legacy directory flags contain -std=c++17; the target's C++20
  # requirement must retain precedence after moving these to target options.
  list(FILTER color_options EXCLUDE REGEX "^(-std=|/std:)")
  target_compile_options(codebrix_host_reference_color PRIVATE ${color_options})
  target_compile_features(codebrix_host_reference_color PRIVATE cxx_std_20)
  target_compile_definitions(codebrix_host_reference_color PRIVATE ${definitions})
  target_include_directories(codebrix_host_reference_color PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(codebrix_host_reference_color PRIVATE cycles_util)
  set_property(TARGET codebrix_host_reference_color PROPERTY POSITION_INDEPENDENT_CODE ON)
  set(reference_kinds session session64)
  # The image oracle writes and reads EXR/PNG files through OIIO.
  if(NOT CODEBRIX_HOST_NO_IMAGE_FILES)
    list(APPEND reference_kinds image)
  endif()
  foreach(kind IN LISTS reference_kinds)
    if(kind STREQUAL "session64")
      set(source session_reference.cpp)
    else()
      set(source "${kind}_reference.cpp")
    endif()
    set(reference codebrix_host_${kind}_reference)
    # Each oracle uses a fresh native Session without the C API. Compile the
    # original colorspace implementation directly, before the patched archive.
    add_executable(${reference} "${CODEBRIX_NATIVE_ROOT}/tests/host/${source}"
      $<TARGET_OBJECTS:codebrix_host_reference_color>)
    target_compile_features(${reference} PRIVATE cxx_std_20)
    target_compile_definitions(${reference} PRIVATE ${definitions})
    if(kind STREQUAL "session64")
      target_compile_definitions(${reference} PRIVATE CBC_HOST_REFERENCE_SAMPLES=64)
    endif()
    target_include_directories(${reference} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_options(${reference} PRIVATE -ffp-contract=off)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
      target_compile_options(${reference} PRIVATE -msse4.2)
    endif()
    target_link_libraries(${reference} PRIVATE ${codebrix_device_libs})
    if(APPLE)
      target_link_options(${reference} PRIVATE "LINKER:-map,${CMAKE_BINARY_DIR}/${reference}.map")
    else()
      target_link_options(${reference} PRIVATE "LINKER:-Map,${CMAKE_BINARY_DIR}/${reference}.map")
    endif()
  endforeach()
  foreach(kind IN ITEMS cpu gpu reload concurrency tier abi example scene_edit)
    if(kind STREQUAL "cpu")
      set(source consumer.c)
    elseif(kind STREQUAL "example")
      set(source example.c)
    else()
      set(source "${kind}_consumer.c")
    endif()
    add_executable(codebrix_host_${kind}_test "${CODEBRIX_NATIVE_ROOT}/tests/host/${source}")
    target_compile_features(codebrix_host_${kind}_test PRIVATE c_std_11)
    target_compile_options(codebrix_host_${kind}_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(codebrix_host_${kind}_test PRIVATE codebrix_cycles_host Threads::Threads m)
  endforeach()
  if(CODEBRIX_HOST_GPU_TEST_BACKEND)
    target_compile_definitions(codebrix_host_gpu_test PRIVATE
      CBC_TEST_GPU_BACKEND=${CODEBRIX_HOST_GPU_TEST_BACKEND} CBC_TEST_GPU_NAME="${CODEBRIX_HOST_GPU_TEST_NAME}")
  endif()
endif()

# The oracle runner (native/oracle) uses only the public C API; this in-tree
# target builds it against the host so the Linux gates can render the corpus.
if(NOT ANDROID AND NOT CMAKE_CROSSCOMPILING)
  add_executable(codebrix_cycles_oracle "${CODEBRIX_NATIVE_ROOT}/oracle/runner.cpp")
  target_compile_features(codebrix_cycles_oracle PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_oracle PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(codebrix_cycles_oracle PRIVATE codebrix_cycles_host)
endif()
