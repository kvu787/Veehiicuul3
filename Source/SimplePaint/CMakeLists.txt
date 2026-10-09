cmake_minimum_required(VERSION 3.21)

# This directory can be copied unchanged into another project or configured
# directly. Only the optional DX12 transform header needs the Windows SDK.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SimplePaint LANGUAGES CXX)
endif()

add_library(SimplePaint STATIC
    Material.cpp
    Material.h
    Geometry.h
    OrthographicTransforms.h
    SimplePaint.hlsl
    SimplePaintCore.hlsli
)
set_source_files_properties(SimplePaint.hlsl SimplePaintCore.hlsli PROPERTIES HEADER_FILE_ONLY TRUE)
# Keep includes stable as <SimplePaint/Material.h> after copying this folder.
target_include_directories(SimplePaint PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(SimplePaint PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(SimplePaint PRIVATE /W4 /WX /permissive- /EHsc /fp:precise)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
    option(SIMPLE_PAINT_BUILD_TESTING "Build SimplePaint verification" ${BUILD_TESTING})
else()
    option(SIMPLE_PAINT_BUILD_TESTING "Build SimplePaint verification" OFF)
endif()
if(SIMPLE_PAINT_BUILD_TESTING)
    enable_testing()
    add_subdirectory(Tests)
endif()
