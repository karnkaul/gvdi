cmake_minimum_required(VERSION 3.22)

project(gvdi VERSION "0.2.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_DEBUG_POSTFIX "-d")

if(CMAKE_VERSION VERSION_LESS "3.21")
  # https://cmake.org/cmake/help/latest/variable/PROJECT_IS_TOP_LEVEL.html
  string(COMPARE EQUAL ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} PROJECT_IS_TOP_LEVEL)
endif()

option(GVDI_BUILD_EXAMPLE "Build gvdi example" ${PROJECT_IS_TOP_LEVEL})

add_subdirectory(ext)

configure_file(lib/build_version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/include/gvdi/build_version.hpp" @ONLY)

add_library(${PROJECT_NAME})
add_library(gvdi::gvdi ALIAS ${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} PUBLIC
  gvdi::ext
)

target_sources(${PROJECT_NAME} PRIVATE
  lib/include/gvdi/context.hpp
  "${CMAKE_CURRENT_BINARY_DIR}/include/gvdi/build_version.hpp"
  lib/src/context.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  lib/include
  "${CMAKE_CURRENT_BINARY_DIR}/include"
)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wunused -Werror=return-type
  )
endif()

if(GVDI_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()
