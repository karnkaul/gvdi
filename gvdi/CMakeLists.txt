add_library(${PROJECT_NAME})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
  include/gvdi/event_handler.hpp
  include/gvdi/frame.hpp
  include/gvdi/gvdi.hpp
  include/gvdi/instance.hpp
  include/gvdi/vulkan_context.hpp

  src/gvdi.cpp
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
  include
)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
  gvdi::ext
)

if(CMAKE_CXX_COMPILER_ID STREQUAL Clang OR CMAKE_CXX_COMPILER_ID STREQUAL GNU)
  target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Werror=return-type
  )
endif()
