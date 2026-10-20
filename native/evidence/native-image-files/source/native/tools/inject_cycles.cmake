# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
set(CODEBRIX_NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
include_directories(BEFORE "${CODEBRIX_CYCLES_OVERLAY}")
add_definitions(-DWITH_WEBGPU)
function(codebrix_add_cycles_device)
  cmake_policy(SET CMP0079 NEW)
  get_target_property(device_sources cycles_device SOURCES)
  list(REMOVE_ITEM device_sources device.cpp)
  set_property(TARGET cycles_device PROPERTY SOURCES "${device_sources}")
  target_sources(cycles_device PRIVATE "${CODEBRIX_CYCLES_OVERLAY}/device/device.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/image.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/device.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/queue.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/background.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/svm_layout.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shader_data.cpp"
    "${CODEBRIX_NATIVE_ROOT}/src/webgpu_device.cpp")
  target_include_directories(cycles_device PRIVATE "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  add_library(codebrix_wgpu SHARED IMPORTED)
  set_target_properties(codebrix_wgpu PROPERTIES IMPORTED_LOCATION
    "${CODEBRIX_WEBGPU_PACKAGE}/runtimes/linux-x64/native/libcodebrix_wgpu.so")
  target_link_libraries(cycles_device PUBLIC codebrix_wgpu ${CMAKE_DL_LIBS})
  get_target_property(integrator_sources cycles_integrator SOURCES)
  list(REMOVE_ITEM integrator_sources path_trace.cpp)
  set_property(TARGET cycles_integrator PROPERTY SOURCES "${integrator_sources}")
  target_sources(cycles_integrator PRIVATE "${CODEBRIX_CYCLES_OVERLAY}/integrator/path_trace.cpp")
  set(codebrix_device_libs cycles_device cycles_kernel cycles_scene cycles_session cycles_integrator
    cycles_bvh cycles_subd cycles_graph cycles_util extern_sky)
  cycles_external_libraries_append(codebrix_device_libs)
  get_directory_property(definitions DIRECTORY "${PROJECT_SOURCE_DIR}/src" COMPILE_DEFINITIONS)
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
  target_compile_options(codebrix_cycles_camera_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_camera_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_camera_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_camera_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_closest_gate "${CODEBRIX_NATIVE_ROOT}/cycles/closest_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp")
  target_compile_features(codebrix_cycles_closest_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_closest_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_closest_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_closest_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_closest_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_background_gate "${CODEBRIX_NATIVE_ROOT}/cycles/background_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
  target_compile_features(codebrix_cycles_background_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_background_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_background_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_background_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_background_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_svm_world_gate "${CODEBRIX_NATIVE_ROOT}/cycles/svm_world_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
  target_compile_features(codebrix_cycles_svm_world_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_svm_world_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_svm_world_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_svm_world_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_svm_world_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_shader_data_gate "${CODEBRIX_NATIVE_ROOT}/cycles/shader_data_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/shader_data_reference.cpp")
  target_compile_features(codebrix_cycles_shader_data_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_shader_data_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_shader_data_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_shader_data_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_shader_data_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_svm_surface_gate "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_reference.cpp")
  target_compile_features(codebrix_cycles_svm_surface_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_svm_surface_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_svm_surface_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_svm_surface_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_svm_surface_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_attribute_gate "${CODEBRIX_NATIVE_ROOT}/cycles/attribute_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_reference.cpp")
  target_compile_features(codebrix_cycles_attribute_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_attribute_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_attribute_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_attribute_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_attribute_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_image_gate "${CODEBRIX_NATIVE_ROOT}/cycles/image_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/image_sampling_reference.cpp" "${CODEBRIX_NATIVE_ROOT}/cycles/svm_surface_reference.cpp")
  target_compile_features(codebrix_cycles_image_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_image_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_image_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_image_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_image_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_image_file_gate "${CODEBRIX_NATIVE_ROOT}/cycles/image_file_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/image_sampling_reference.cpp")
  target_compile_features(codebrix_cycles_image_file_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_image_file_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_image_file_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_image_file_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${CODEBRIX_NATIVE_ROOT}/src" "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_image_file_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_surface_gate "${CODEBRIX_NATIVE_ROOT}/cycles/surface_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp")
  target_compile_features(codebrix_cycles_surface_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_surface_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_surface_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_surface_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_surface_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_render_gate "${CODEBRIX_NATIVE_ROOT}/cycles/render_gate.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/surface_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/camera_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/closest_reference.cpp"
    "${CODEBRIX_NATIVE_ROOT}/cycles/background_reference.cpp")
  target_compile_features(codebrix_cycles_render_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_render_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_render_gate PRIVATE ${definitions})
  target_include_directories(codebrix_cycles_render_gate PRIVATE "${PROJECT_SOURCE_DIR}/src"
     "${CODEBRIX_NATIVE_ROOT}/src"
    "${CODEBRIX_NATIVE_ROOT}/include" "${CODEBRIX_WEBGPU_PACKAGE}/include")
  target_link_libraries(codebrix_cycles_render_gate PRIVATE ${codebrix_device_libs})
  add_executable(codebrix_cycles_atomic_film_gate "${CODEBRIX_NATIVE_ROOT}/../tools/licensing/tests/atomic_contract.cpp")
  target_compile_features(codebrix_cycles_atomic_film_gate PRIVATE cxx_std_20)
  target_compile_options(codebrix_cycles_atomic_film_gate PRIVATE -ffp-contract=off -msse4.2)
  target_compile_definitions(codebrix_cycles_atomic_film_gate PRIVATE ${definitions} CODEBRIX_TEST_CYCLES_FILM)
  target_include_directories(codebrix_cycles_atomic_film_gate PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(codebrix_cycles_atomic_film_gate PRIVATE ${codebrix_device_libs})
endfunction()
cmake_language(DEFER CALL codebrix_add_cycles_device)
