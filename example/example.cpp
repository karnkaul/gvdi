#include <gvdi/app.hpp>
#include <chrono>
#include <print>

namespace {
using namespace std::chrono_literals;

class App : public gvdi::App {
	auto create_window() -> GLFWwindow* final {
		auto* ret = glfwCreateWindow(1280, 720, "example", nullptr, nullptr);

		glfwSetWindowUserPointer(ret, this);

		// convenience function to cast the data pointer back to App.
		static auto const self = [](GLFWwindow* window) -> App& {
			return *static_cast<App*>(glfwGetWindowUserPointer(window));
		};

		// install a key callback.
		glfwSetKeyCallback(ret, [](GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
			self(window).on_key(key, action, mods);
		});

		return ret;
	}

	void update() final {
		// update fps.
		auto const now = std::chrono::steady_clock::now();
		auto const dt = now - m_start;
		m_start = now;

		m_elapsed += dt;
		++m_frame_count;
		if (m_elapsed >= 1s) {
			m_fps = m_frame_count;
			m_frame_count = {};
			m_elapsed = {};
		}

		// TEMP
		static auto s_elapsed = std::chrono::duration<float>{};
		s_elapsed += dt;
		if (s_elapsed > 5s) { glfwSetWindowShouldClose(get_window(), GLFW_TRUE); }
		// TEMP

		// show Dear ImGui demo window.
		ImGui::ShowDemoWindow();

		// if (m_show_fps) {
		// 	ImGui::SetNextWindowSize({100.0f, 50.0f});
		// 	if (ImGui::Begin("FPS", &m_show_fps)) { ImGui::Text("FPS: %d", m_fps); }
		// 	ImGui::End();
		// }
	}

	void on_key(int const key, int const action, int const mods) {
		// close on Ctrl + W
		if (key == GLFW_KEY_W && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) == GLFW_MOD_CONTROL) {
			glfwSetWindowShouldClose(get_window(), GLFW_TRUE);
		}

		// show fps on F
		if (key == GLFW_KEY_F && action == GLFW_RELEASE && mods == 0) { m_show_fps = true; }
	}

	bool m_show_fps{true};
	int m_frame_count{};
	int m_fps{};
	std::chrono::duration<float> m_elapsed{};
	std::chrono::steady_clock::time_point m_start{std::chrono::steady_clock::now()};
};
} // namespace

auto main() -> int {
	try {
		// print the build version.
		std::println("gvdi version: {}", gvdi::build_version_v);

		auto app = App{};
		app.run();
	} catch (std::exception const& e) {
		std::println(stderr, "PANIC: {}", e.what());
		return EXIT_FAILURE;
	}
}
