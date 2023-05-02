project(gvdi-example)

add_executable(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} PRIVATE gvdi::gvdi)

target_sources(${PROJECT_NAME} PRIVATE example.cpp)
