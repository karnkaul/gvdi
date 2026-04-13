#pragma once
#include "gvdi/event_listener.hpp"
#include "gvdi/gpu.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <array>
#include <memory>
#include <span>

namespace gvdi {
/// \brief Abstract base class for a windowed app.
/// Having more than one App instance is unsupported.
class App : public EventListener {
  public:
	static constexpr auto gpu_priority_v = std::array{
		gpu::Type::Integrated,
		gpu::Type::Discrete,
		gpu::Type::Cpu,
	};

	App(App const&) = delete;
	App(App&&) = delete;
	auto operator=(App const&) = delete;
	auto operator=(App&&) = delete;

	virtual ~App() = default;

	explicit(false) App();

	/// \brief Entrypoint. Returns after the window and all associated resources have been destroyed.
	/// GLFW remains initialized until App is destroyed.
	///
	/// Note: reruns can sometimes cause issues if libdecor is enabled on Wayland,
	/// prefer using schedule_reboot() when feasible.
	void run_event_loop() noexcept(false);

	[[nodiscard]] auto should_close_window() const -> bool { return glfwWindowShouldClose(get_window()); }
	void set_should_close_window(bool const value) { glfwSetWindowShouldClose(get_window(), value); }

	[[nodiscard]] auto will_reboot() const -> bool;
	void schedule_reboot();

  protected:
	[[nodiscard]] static auto create_windowed_window(char const* title, int width = 800, int height = 600) -> GLFWwindow*;
	[[nodiscard]] static auto create_fullscreen_window(char const* title) -> GLFWwindow*;

	/// \brief Required customization point, called every frame.
	virtual void update() = 0;

	/// \brief Customization point for creating a GLFW window.
	virtual auto create_glfw_window() -> GLFWwindow* { return create_windowed_window("gvdi App"); }
	/// \brief List of GPU types in desired selection order.
	[[nodiscard]] virtual auto get_gpu_type_priority() const -> std::span<gpu::Type const> { return gpu_priority_v; }

	/// \brief Called before the event loop begins.
	virtual void pre_event_loop() {}
	/// \brief Called before first frame for the current window begins.
	virtual void pre_first_frame() {}
	/// \brief Called before run_event_loop() returns.
	virtual void post_event_loop() {}

	/// Customization points for stages in run_event_loop(). If overridden,
	/// the derived type must call the corresponding base implementations.

	/// \brief Initialize GLFW.
	/// Should only be done once per program lifetime.
	virtual void stage_initialize();
	/// \brief Create resources (window, Vulkan, Dear ImGui).
	virtual void stage_create();
	/// \brief Destroy and recreate window and associated resources.
	/// Only called if enqueue_recreate() was called earlier in the frame.
	/// Calls stage_destroy(), stage_create(), and pre_first_frame() in turn.
	virtual void stage_reboot();
	/// \brief Destroy window and associated resources.
	virtual void stage_destroy();

	/// \returns true inside run() loop.
	[[nodiscard]] auto is_running() const -> bool;

	/// \returns Pointer to GLFW window, null until create_window() has returned.
	[[nodiscard]] auto get_window() const -> GLFWwindow*;

	/// \returns Selected gpu::Info, default initialized until create_window() has returned.
	[[nodiscard]] auto get_gpu_info() const -> gpu::Info;

  private:
	class Impl;
	struct Deleter {
		void operator()(Impl* ptr) const noexcept;
	};
	std::unique_ptr<Impl, Deleter> m_impl{};
};
} // namespace gvdi
