add_library(${PROJECT_NAME})
add_library(gvdi::gvdi ALIAS ${PROJECT_NAME})

configure_file(src/build_version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/include/gvdi/build_version.hpp" @ONLY)

target_link_libraries(${PROJECT_NAME} PUBLIC
  gvdi::ext
)

target_sources(${PROJECT_NAME} PUBLIC FILE_SET HEADERS
  BASE_DIRS include FILES
  include/gvdi/app.hpp
  include/gvdi/error.hpp
)

target_sources(${PROJECT_NAME} PUBLIC FILE_SET HEADERS
  BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/include"
  FILES "${CMAKE_CURRENT_BINARY_DIR}/include/gvdi/build_version.hpp"
)

target_sources(${PROJECT_NAME} PRIVATE
  src/app.cpp
)
