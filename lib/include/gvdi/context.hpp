#pragma once
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <gvdi/build_version.hpp>
#include <memory>
#include <stdexcept>

namespace gvdi {
/// \brief Type of all exceptions thrown by gvdi.
struct Error : std::runtime_error {
	using runtime_error::runtime_error;
};

/// \brief Custom deleter for GLFWwindow.
struct WindowDeleter {
	void operator()(GLFWwindow* window) const noexcept {
		glfwDestroyWindow(window);
		glfwTerminate();
	}
};

/// \brief A unique GLFW window.
using UniqueWindow = std::unique_ptr<GLFWwindow, WindowDeleter>;

/// \brief Context and renderer.
///
/// Owns UniqueWindow, Vulkan device, and Dear ImGui.
class Context {
  public:
	/// \brief Create a UniqueWindow given its size and title.
	/// \param size Window size.
	/// \param title Window title.
	/// \returns UniqueWindow if successful.
	/// \throws Error on failure.
	[[nodiscard]] static auto create_window(ImVec2 size, char const* title) noexcept(false) -> UniqueWindow;

	/// \brief Constructor.
	/// \param window Unique GLFW window to take ownership of.
	/// \throws Error on failure.
	explicit Context(UniqueWindow window) noexcept(false);

	/// \brief Start next frame.
	/// \returns true unless the GLFW window close flag has been set.
	auto next_frame() -> bool;

	/// \brief Render the frame.
	/// \param clear Clear colour for the render pass.
	/// \pre next_frame() must have returned true.
	void render(ImVec4 const& clear = {});

	/// \brief Close the context and its window.
	void close();

	/// \brief Recreates ImGui's fonts texture (image and descriptor set).
	/// Call this after adding custom fonts to ImGuiIO::Fonts.
	void rebuild_imgui_fonts();

	[[nodiscard]] auto get_window() const -> GLFWwindow*;
	[[nodiscard]] auto get_framebuffer_size() const -> ImVec2;

  private:
	struct Impl;
	struct Deleter {
		void operator()(Impl* ptr) const noexcept;
	};

	std::unique_ptr<Impl, Deleter> m_impl{};
};
} // namespace gvdi
