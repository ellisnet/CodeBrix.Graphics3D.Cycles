# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
add_executable(codebrix_cycles_material_host
  "${CODEBRIX_PROTOTYPE_NATIVE_DIR}/../../parity/native/material_host.cpp"
  "${CODEBRIX_PROTOTYPE_NATIVE_DIR}/../../parity/native/material_reference.cpp")
target_compile_features(codebrix_cycles_material_host PRIVATE cxx_std_20)
target_compile_definitions(codebrix_cycles_material_host PRIVATE ${cycles_definitions})
target_include_directories(codebrix_cycles_material_host PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/third_party/atomic")
target_compile_options(codebrix_cycles_material_host PRIVATE -ffp-contract=off -msse4.2)
target_link_libraries(codebrix_cycles_material_host PRIVATE ${prototype_libs})
add_executable(codebrix_cycles_path_host
  "${CODEBRIX_PROTOTYPE_NATIVE_DIR}/../../parity/native/path_host.cpp"
  "${CODEBRIX_PROTOTYPE_NATIVE_DIR}/../../parity/native/path_reference.cpp")
target_compile_features(codebrix_cycles_path_host PRIVATE cxx_std_20)
target_compile_definitions(codebrix_cycles_path_host PRIVATE ${cycles_definitions})
target_include_directories(codebrix_cycles_path_host PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/third_party/atomic")
target_compile_options(codebrix_cycles_path_host PRIVATE -ffp-contract=off -msse4.2)
target_link_libraries(codebrix_cycles_path_host PRIVATE ${prototype_libs})
