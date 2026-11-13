cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(AsyncLoad VERSION 1.0.0)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

set(GEODE_CODEGEN_EXTRA_ARGS "--skip-pugixml" CACHE STRING "" FORCE)
set(GEODE_ASP_VERSION "@1.6.4" CACHE STRING "" FORCE)
set(GEODE_NO_PUGIXML_HEADER ON CACHE BOOL "" FORCE)
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

CPMAddPackage("gh:dankmeme01/geobuild#v2")
include("${CMAKE_CURRENT_BINARY_DIR}/geobuild-gen.cmake")

# spng has annoying cmake, so compile it ourselves
CPMAddPackage(
    NAME spng
    GITHUB_REPOSITORY "randy408/libspng"
    GIT_TAG "v0.7.4"
    DOWNLOAD_ONLY ON
)
target_sources(AsyncLoad PRIVATE "${CPM_PACKAGE_spng_SOURCE_DIR}/spng/spng.c")
target_include_directories(AsyncLoad PRIVATE "${CPM_PACKAGE_spng_SOURCE_DIR}/spng")
target_compile_definitions(AsyncLoad PRIVATE SPNG_STATIC)
