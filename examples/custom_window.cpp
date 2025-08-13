#include <gvdi/app.hpp>
#include <chrono>
#include <cstdlib>
#include <print>

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
	// override this to customize window creation and install GLFW callbacks.
	auto create_window() -> GLFWwindow* final {
		// create a standard GLFW window.
		// the NO_CLIENT_API window hint (for Vulkan) is already set, others can be set here.
		auto* ret = glfwCreateWindow(1280, 720, "gvdi custom window", nullptr, nullptr);

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

	Fps m_fps{};
	std::chrono::steady_clock::time_point m_frame_start{std::chrono::steady_clock::now()};
	bool m_show_fps{true};
};
} // namespace

auto main() -> int {
	try {
		auto app = App{};
		app.run();
	} catch (std::exception const& e) {
		std::println("PANIC: {}", e.what());
		return EXIT_FAILURE;
	} catch (...) {
		std::println("PANIC!");
		return EXIT_FAILURE;
	}
}
