# GLFW Vulkan Dear ImGui (gvdi)

**Minmalist C++20 Dear ImGui implementation over GLFW / Vulkan**

## Requirements

- C++20 compiler (and standard library)
- CMake 3.23+

## Integration

Use CMake:

```cmake
add_subdirectory(path/to/gvdi)

target_link_libraries(your-target PRIVATE gvdi::gvdi)
```

## Usage

[examples](examples) demonstrate detailed usage.

### Quickstart

Inherit from `gvdi::App` and implement `update()` (and override any other desired virtual functions).

```cpp
class App : public gvdi::App {
  void update() final {
    ImGui::ShowDemoWindow();
  }
};
```

Create an instance and call `App::run()`.

```cpp
auto app = App{};
app.run();
```

## Misc

[Original repository](https://github.com/karnkaul/gvdi)

[LICENSE](LICENSE)
