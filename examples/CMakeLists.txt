project(gvdi-examples)

function(add_example target_name main_cpp)
  add_executable(${target_name})
  target_link_libraries(${target_name} PRIVATE gvdi::gvdi)
  target_sources(${target_name} PRIVATE ${main_cpp})
endfunction()

add_example(quickstart quickstart.cpp)
add_example(custom-window custom_window.cpp)
