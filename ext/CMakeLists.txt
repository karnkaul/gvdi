project(gvdi-ext)

file(ARCHIVE_EXTRACT INPUT "${CMAKE_CURRENT_SOURCE_DIR}/src.zip" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT TARGET glfw)
  set(GLFW_INSTALL OFF)
  set(GLFW_BUILD_DOCS OFF)
  message(STATUS "[glfw]")
  add_subdirectory(src/glfw)
  add_library(glfw::glfw ALIAS glfw)
endif()

if(NOT TARGET dyvk::dyvk)
  message(STATUS "[dyvk]")
  add_subdirectory(src/dyvk)
endif()


add_library(${PROJECT_NAME} INTERFACE)
add_library(gvdi::ext ALIAS ${PROJECT_NAME})

# Dear ImGui

message(STATUS "[dear imgui]")
add_library(dear_imgui)
add_library(dear_imgui::dear_imgui ALIAS dear_imgui)

target_include_directories(dear_imgui SYSTEM PUBLIC src/dear_imgui)

target_sources(dear_imgui PRIVATE
  src/dear_imgui/imconfig.h
  src/dear_imgui/imgui_demo.cpp
  src/dear_imgui/imgui_draw.cpp
  src/dear_imgui/imgui_internal.h
  src/dear_imgui/imgui_tables.cpp
  src/dear_imgui/imgui_widgets.cpp
  src/dear_imgui/imgui.cpp
  src/dear_imgui/imgui.h

  src/dear_imgui/backends/imgui_impl_glfw.cpp
  src/dear_imgui/backends/imgui_impl_glfw.h
  src/dear_imgui/backends/imgui_impl_vulkan.cpp
  src/dear_imgui/backends/imgui_impl_vulkan.h

  src/dear_imgui/misc/cpp/imgui_stdlib.cpp
)

target_compile_definitions(dear_imgui PUBLIC
  GLFW_INCLUDE_VULKAN
)

target_link_libraries(dear_imgui PRIVATE
  glfw::glfw
  dyvk::dyvk
)

if(CMAKE_CXX_COMPILER_ID STREQUAL Clang OR CMAKE_CXX_COMPILER_ID STREQUAL GNU)
  target_compile_options(dear_imgui PRIVATE
    -Wno-conversion -Wno-extra
  )
endif()

target_link_libraries(${PROJECT_NAME} INTERFACE
  dear_imgui::dear_imgui
  glfw::glfw
  dyvk::dyvk
)

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  target_link_libraries(${PROJECT_NAME} INTERFACE dl)
endif()

target_compile_definitions(${PROJECT_NAME} INTERFACE
  VK_ENABLE_BETA_EXTENSIONS
  GLFW_INCLUDE_VULKAN
)
