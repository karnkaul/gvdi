project(gvdi-ext)

find_package(Threads)

file(ARCHIVE_EXTRACT INPUT "${CMAKE_CURRENT_SOURCE_DIR}/src.zip" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(src/vk_dynamic)

set(GLFW_VULKAN_STATIC OFF)
set(GLFW_INSTALL OFF)
set(GLFW_BUILD_DOCS OFF)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
add_subdirectory(src/glfw)
add_library(glfw::glfw ALIAS glfw)

add_subdirectory(src/dear-imgui)

add_library(gvdi-ext INTERFACE)
add_library(gvdi::ext ALIAS gvdi-ext)

target_link_libraries(gvdi-ext INTERFACE
  vk-dynamic::vk-dynamic
  glfw::glfw
  dear_imgui::dear_imgui
  Threads::Threads
)
