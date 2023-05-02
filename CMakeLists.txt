cmake_minimum_required(VERSION 3.18 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DEBUG_POSTFIX "-d")

project(gvdi VERSION "0.1.0")

set(is_root_project OFF)

if(PROJECT_NAME STREQUAL CMAKE_PROJECT_NAME)
  set(is_root_project ON)
endif()

add_subdirectory(ext)
add_subdirectory(gvdi)

option(GVDI_BUILD_EXAMPLE "Build gvdi example" ${is_root_project})

if(GVDI_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()
