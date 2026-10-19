# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(CodeBrixPystring LANGUAGES CXX)
include(GNUInstallDirs)
add_library(pystring STATIC "${PYSTRING_SOURCE_ROOT}/pystring.cpp")
set_target_properties(pystring PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pystring PUBLIC "${PYSTRING_SOURCE_ROOT}")
install(TARGETS pystring ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "${PYSTRING_SOURCE_ROOT}/pystring.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pystring)
