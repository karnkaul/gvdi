cmake_minimum_required(VERSION 3.23)

project(gvdi VERSION "0.3.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_DEBUG_POSTFIX "-d")

option(GVDI_BUILD_EXAMPLES "Build gvdi example" ${PROJECT_IS_TOP_LEVEL})

add_subdirectory(ext)

add_subdirectory(lib)

if(GVDI_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
