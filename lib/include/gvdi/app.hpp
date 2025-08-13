#pragma once
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <gvdi/build_version.hpp>
#include <memory>

namespace gvdi {
/// \brief Abstract base class for a windowed app.
/// Having more than one App instance is unsupported.
class App {
  public:
	App(App const&) = delete;
	App(App&&) = delete;
	auto operator=(App const&) = delete;
	auto operator=(App&&) = delete;

	virtual ~App() = default;

	explicit(false) App();

	/// \brief Entrypoint. Returns after glfwWindowShouldClose() returns true.
	void run();

  protected:
	/// \brief Customization point that's called before any initialization begins.
	/// Use this to set GLFW init hints etc.
	virtual void pre_init() {}
	/// \brief Customization point for creating a window.
	/// Default implementation returns a 800x600 decorated window on the default monitor.
	/// Use this to install GLFW callbacks etc before returning.
	virtual auto create_window() -> GLFWwindow*;
	/// \brief Customization point that's called after initialization.
	/// Use this to tweak ImGuiIO (eg to set up keyboard / gamepad navigation) etc.
	virtual void post_init() {}

	/// \brief Required customization point, called every frame.
	virtual void update() = 0;

	/// \returns Pointer to GLFW window, null until create_window() has been called.
	[[nodiscard]] auto get_window() const -> GLFWwindow*;

  private:
	class Impl;
	struct Deleter {
		void operator()(Impl* ptr) const noexcept;
	};
	std::unique_ptr<Impl, Deleter> m_impl{};
};
} // namespace gvdi
