#pragma once
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <memory>

namespace gvdi {
///
/// \brief GPU type: discrete or integrated.
///
enum class GpuType : std::uint8_t { eDiscrete, eIntegrated };

///
/// \brief Core instance owning GLFW, Vulkan, and Dear ImGui resources.
///
/// Only a single instance of Instance is permitted to be active at a time.
/// Supports being moved.
/// Note: Both library and Dear ImGui GLFW callbacks are installed on successful construction.
/// Customize via EventHandler, do not replace installed callbacks via glfwSetCursorCallback etc!
///
class Instance {
  public:
	Instance() = default;

	Instance(ImVec2 extent, char const* title, GpuType preferred = GpuType::eIntegrated);

	///
	/// \brief Check if instance is running.
	/// \returns true if active and glfwWindowShouldClose returns false
	///
	bool is_running() const;
	///
	/// \brief Close instance and stop running.
	///
	/// Calls glfwSetWindowShouldClose
	///
	void close();

	ImVec2 framebuffer_extent() const;
	ImVec2 window_extent() const;
	ImVec2 window_position() const;
	ImVec2 cursor_position() const;

	GLFWwindow* window() const;
	void set_event_handler(struct EventHandler* event_handler);

	///
	/// \brief Check if instance is active.
	/// \returns true if instance is active
	///
	explicit operator bool() const { return m_impl != nullptr; }

  private:
	struct Impl;
	struct Deleter {
		void operator()(Impl const* ptr) const;
	};
	std::unique_ptr<Impl, Deleter> m_impl{};

	friend class Frame;
	friend struct VulkanContext;
};
} // namespace gvdi
