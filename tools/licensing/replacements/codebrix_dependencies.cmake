# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Original target adapters from the requirements of Apache-licensed Cycles
# CMake call sites and the variables produced by its external_libs.cmake.
# The excluded dependency_targets.cmake implementation was not used.

function(codebrix_dependency name)
  add_library(codebrix_dependency_${name} INTERFACE)
  add_library(bf::dependencies::${name} ALIAS codebrix_dependency_${name})
  target_link_libraries(codebrix_dependency_${name} INTERFACE ${ARGN})
endfunction()

codebrix_dependency(openimageio OpenImageIO::OpenImageIO OpenImageIO::OpenImageIO_Util)
target_include_directories(codebrix_dependency_openimageio SYSTEM INTERFACE ${OPENIMAGEIO_INCLUDE_DIRS} ${OPENIMAGEIO_INCLUDE_DIR})
codebrix_dependency(opencolorio OpenColorIO::OpenColorIO)
target_include_directories(codebrix_dependency_opencolorio SYSTEM INTERFACE ${OPENCOLORIO_INCLUDE_DIRS})
codebrix_dependency(openexr OpenEXR::OpenEXR Imath::Imath)
target_include_directories(codebrix_dependency_openexr SYSTEM INTERFACE ${OPENEXR_INCLUDE_DIRS} ${IMATH_INCLUDE_DIRS})
codebrix_dependency(zlib ${ZLIB_LIBRARIES})
target_include_directories(codebrix_dependency_zlib SYSTEM INTERFACE ${ZLIB_INCLUDE_DIRS})
codebrix_dependency(zstd ${ZSTD_LIBRARIES})
target_include_directories(codebrix_dependency_zstd SYSTEM INTERFACE ${ZSTD_INCLUDE_DIRS})
codebrix_dependency(pthreads ${PTHREADS_LIBRARIES})

# Optional target names exist even when a feature is off. Enabled features must
# have reviewed adapters; silently dropping a requested dependency is forbidden.
foreach(name embree openimagedenoise openpgl opensubdiv openvdb nanovdb osl pugixml python tbb)
  add_library(codebrix_optional_${name} INTERFACE)
  add_library(bf::dependencies::optional::${name} ALIAS codebrix_optional_${name})
endforeach()
if(WITH_TBB)
  target_compile_definitions(codebrix_optional_tbb INTERFACE WITH_TBB)
  target_link_libraries(codebrix_optional_tbb INTERFACE ${TBB_LIBRARIES})
  target_include_directories(codebrix_optional_tbb SYSTEM INTERFACE ${TBB_INCLUDE_DIRS})
endif()
if(WITH_CYCLES_PUGIXML OR OPENIMAGEIO_PUGIXML_FOUND)
  target_link_libraries(codebrix_optional_pugixml INTERFACE ${PUGIXML_LIBRARIES})
  target_include_directories(codebrix_optional_pugixml SYSTEM INTERFACE ${PUGIXML_INCLUDE_DIR})
endif()
# Open Image Denoise: the reviewed snapshot (third_party/deps/oidn) built into the
# dependency prefix; FindOpenImageDenoise.cmake supplies the variables.
if(WITH_OPENIMAGEDENOISE)
  target_compile_definitions(codebrix_optional_openimagedenoise INTERFACE WITH_OPENIMAGEDENOISE)
  target_link_libraries(codebrix_optional_openimagedenoise INTERFACE ${OPENIMAGEDENOISE_LIBRARIES})
  target_include_directories(codebrix_optional_openimagedenoise SYSTEM INTERFACE ${OPENIMAGEDENOISE_INCLUDE_DIRS})
endif()
# Embree: the reviewed snapshot (third_party/deps/embree) built as a static library into
# the dependency prefix; FindEmbree.cmake supplies the variables. Its tasking system is
# the reviewed oneTBB.
if(WITH_CYCLES_EMBREE)
  target_compile_definitions(codebrix_optional_embree INTERFACE WITH_EMBREE)
  target_link_libraries(codebrix_optional_embree INTERFACE ${EMBREE_LIBRARIES} ${TBB_LIBRARIES})
  target_include_directories(codebrix_optional_embree SYSTEM INTERFACE ${EMBREE_INCLUDE_DIRS})
endif()
foreach(feature WITH_CYCLES_PATH_GUIDING
                WITH_OPENSUBDIV WITH_OPENVDB WITH_NANOVDB WITH_CYCLES_OSL)
  if(${feature})
    message(FATAL_ERROR "${feature} needs a reviewed CodeBrix dependency adapter before enabling")
  endif()
endforeach()
