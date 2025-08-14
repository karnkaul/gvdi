#include <gvdi/app.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <string_view>

namespace {
using namespace std::chrono_literals;

class Fps {
  public:
	explicit(false) Fps() {
		// set initial fps text.
		update_text();
	}

	void tick(std::chrono::duration<float> const dt) {
		m_elapsed += dt;
		++m_frame_count;

		if (m_elapsed >= 1s) {
			m_value = m_frame_count;
			update_text();
			m_frame_count = 0;
			m_elapsed = 0s;
		}
	}

	void draw(bool& out_open) const {
		ImGui::SetNextWindowSize({100.0f, 50.0f});
		if (ImGui::Begin("FPS", &out_open)) { ImGui::TextUnformatted(m_text.c_str()); }
		ImGui::End();
	}

  private:
	void update_text() {
		m_text.clear();
		std::format_to(std::back_inserter(m_text), "FPS: {}", m_value);
	}

	int m_value{};
	int m_frame_count{};
	std::string m_text{};
	std::chrono::duration<float> m_elapsed{};
};

class App : public gvdi::App {
  public:
	// params to control GLFW init hints.
	struct Params {
		// force X11 instead of Wayland (Linux).
		bool force_x11{false};
		// disable libdecor (Wayland).
		bool nolibdecor{false};
	};

	explicit App(Params const& params) : m_params(params) {}

  private:
	// set GLFW init hints here.
	void pre_init() final {
		if (m_params.force_x11 && glfwPlatformSupported(GLFW_PLATFORM_X11)) {
			std::cout << "-- Forcing X11\n";
			glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
		}
		if (!m_params.force_x11 && m_params.nolibdecor && glfwPlatformSupported(GLFW_PLATFORM_WAYLAND)) {
			std::cout << "-- Disabling libdecor\n";
			glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);
		}
	}

	// set GLFW window hints and install callbacks here.
	auto create_window() -> GLFWwindow* final {
		// the NO_CLIENT_API window hint (for Vulkan) is already set, others can be set here.
		// invisible window.
		glfwInitHint(GLFW_VISIBLE, GLFW_FALSE);
		// create a standard GLFW window.
		auto const title = std::format("gvdi v{}", gvdi::build_version_v);
		auto* ret = glfwCreateWindow(1280, 720, title.c_str(), nullptr, nullptr);

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

	void post_init() final {
		// show the window now.
		glfwShowWindow(get_window());
		// set frame start timestamp.
		m_frame_start = std::chrono::steady_clock::now();
	}

	void update() final {
		// compute delta time.
		auto const now = std::chrono::steady_clock::now();
		auto const dt = now - m_frame_start;
		m_frame_start = now;

		// update fps.
		m_fps.tick(dt);

		// draw stuff.
		ImGui::ShowDemoWindow();
		if (m_show_fps) { m_fps.draw(m_show_fps); }
	}

	void on_key(int const key, int const action, int const mods) {
		// close on Ctrl + W.
		if (key == GLFW_KEY_W && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) == GLFW_MOD_CONTROL) {
			glfwSetWindowShouldClose(get_window(), GLFW_TRUE);
		}

		// toggle fps on F.
		if (key == GLFW_KEY_F && action == GLFW_RELEASE && mods == 0) { m_show_fps = !m_show_fps; }
	}

	Params m_params{};

	Fps m_fps{};
	std::chrono::steady_clock::time_point m_frame_start{std::chrono::steady_clock::now()};
	bool m_show_fps{true};
};
} // namespace

auto main(int argc, char** argv) -> int {
	try {
		auto exe_name = std::string{"<app>"};
		auto args = std::span{argv, static_cast<std::size_t>(argc)};
		if (!args.empty()) {
			exe_name = std::filesystem::path{args.front()}.filename().string();
			args = args.subspan(1);
		}

		auto params = App::Params{};
		for (; !args.empty(); args = args.subspan(1)) {
			std::string_view const arg = args.front();
			if (arg == "--force-x11") {
				params.force_x11 = true;
			} else if (arg == "--nolibdecor") {
				params.nolibdecor = true;
			} else if (arg == "--help") {
				std::cout << std::format("Usage: {} [--force-x11] [--nolibdecor]\n", exe_name);
				return EXIT_SUCCESS;
			} else {
				std::cerr << std::format("Unrecognized option: {}\n", arg);
				return EXIT_FAILURE;
			}
		}

		auto app = App{params};
		app.run();
	} catch (std::exception const& e) {
		std::cout << std::format("PANIC: {}\n", e.what());
		return EXIT_FAILURE;
	} catch (...) {
		std::cout << "PANIC!\n";
		return EXIT_FAILURE;
	}
}
