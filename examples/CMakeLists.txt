project(gvdi-examples)

function(add_example target_name main_cpp)
  add_executable(${target_name})
  target_link_libraries(${target_name} PRIVATE gvdi::gvdi)
  target_sources(${target_name} PRIVATE ${main_cpp})

  if(WIN32 AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL GNU)
    target_link_libraries(${target_name} PRIVATE stdc++exp)
  endif()
endfunction()

add_example(quickstart quickstart.cpp)
add_example(custom-window custom_window.cpp)
