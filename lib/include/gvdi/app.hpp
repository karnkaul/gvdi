#pragma once
#include "gvdi/build_version.hpp"
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

	/// \brief Entrypoint. Returns after glfwWindowShouldClose() returns true.
	/// Can be called again after it returns.
	/// Note: reruns can sometimes cause issues if libdecor is enabled on Wayland.
	void run_event_loop() noexcept(false);

  protected:
	/// \brief Customization point that's called before any initialization begins.
	/// Use this to set GLFW init hints etc.
	virtual void pre_init() {}
	/// \brief Customization point for creating a window.
	/// Default implementation returns a 800x600 decorated window on the default monitor.
	virtual auto create_window() -> GLFWwindow*;
	/// \brief List of GPU types in desired selection order.
	[[nodiscard]] virtual auto get_gpu_type_priority() const -> std::span<gpu::Type const> { return gpu_priority_v; }
	/// \brief Customization point that's called after initialization.
	/// Use this to tweak ImGuiIO (eg to set up keyboard / gamepad navigation) etc.
	virtual void post_init() {}

	/// \brief Required customization point, called every frame.
	virtual void update() = 0;

	/// \brief Customization point that's called after the run loop has finished.
	/// Each call of run() will end in a call to post_run() (unless exceptions were thrown).
	/// In contrast, ~App() will only be called once during its lifetime (obviously).
	virtual void post_run() {}

	/// \returns true inside run() loop.
	[[nodiscard]] auto is_running() const -> bool;

	/// \returns Pointer to GLFW window, null until create_window() has returned.
	[[nodiscard]] auto get_window() const -> GLFWwindow*;

	/// \returns Selected gpu::Info, default initialized until select_gpu() has returned.
	[[nodiscard]] auto get_gpu_info() const -> gpu::Info;

  private:
	class Impl;
	struct Deleter {
		void operator()(Impl* ptr) const noexcept;
	};
	std::unique_ptr<Impl, Deleter> m_impl{};
};
} // namespace gvdi
