# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Add prototype targets after upstream has declared its native libraries.
set(CODEBRIX_PROTOTYPE_NATIVE_DIR "${CMAKE_CURRENT_LIST_DIR}/../native")
function(codebrix_add_native_prototype)
  include("${CODEBRIX_PROTOTYPE_NATIVE_DIR}/CMakeLists.txt")
endfunction()
cmake_language(DEFER CALL codebrix_add_native_prototype)
