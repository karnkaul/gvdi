# GLFW Vulkan Dear ImGui (gvdi)

### Minmalist C++20 Dear ImGui implementation over GLFW / Vulkan

#### Requirements

- C++20 compiler (and standard library)
- CMake 3.23+

#### Integration

Use CMake:

```cmake
add_subdirectory(path/to/gvdi)

target_link_libraries(your-target PRIVATE gvdi::gvdi)
```

#### Usage

[example](example/example.cpp) demonstrates usage.

#### Misc

[Original repository](https://github.com/karnkaul/gvdi)

[LICENCE](LICENSE)
