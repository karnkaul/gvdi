#include <gvdi/context.hpp>
#include <chrono>
#include <iostream>
#include <optional>

namespace {
using namespace std::chrono_literals;

class App {
  public:
	// App requires a GLFWwindow instance to install callbacks.
	// callbacks must be installed before gvdi::Context is constructed,
	// as then Dear ImGui will replace the installed callbacks.
	explicit App(GLFWwindow& window) {
		// setup the data pointer.
		glfwSetWindowUserPointer(&window, this);

		// convenience function to cast the data pointer back to App.
		static auto const self = [](GLFWwindow* window) -> App& {
			return *static_cast<App*>(glfwGetWindowUserPointer(window));
		};

		// install a key callback.
		glfwSetKeyCallback(&window, [](GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
			self(window).on_key(key, action, mods);
		});

		// install more callbacks if desired.
	}

	// take ownership of gvdi::Context and start a main loop.
	void run(gvdi::Context context) {
		// take ownership of the context.
		m_context = std::move(context);

		// keep running until the context doesn't start the next frame.
		// ie, until glfwWindowShouldClose() returns true.
		while (m_context->next_frame()) {
			// update the app.
			update();
			// render the context.
			m_context->render();
		}
	}

	void update() {
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

		// show Dear ImGui demo window.
		ImGui::ShowDemoWindow();

		if (m_show_fps) {
			ImGui::SetNextWindowSize({100.0f, 50.0f});
			if (ImGui::Begin("FPS", &m_show_fps)) { ImGui::Text("FPS: %d", m_fps); }
			ImGui::End();
		}
	}

  private:
	void on_key(int const key, int const action, int const mods) {
		// close on Ctrl + W
		if (key == GLFW_KEY_W && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) == GLFW_MOD_CONTROL) {
			m_context->close();
		}

		// show fps on F
		if (key == GLFW_KEY_F && action == GLFW_RELEASE && mods == 0) { m_show_fps = true; }
	}

	std::optional<gvdi::Context> m_context{};

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
		std::cout << "gvdi version: " << gvdi::build_version_v << "\n";
		// make a gvdi::UniqueWindow.
		auto window = gvdi::Context::create_window({1280.0f, 720.0f}, "Example Window");
		// construct an app and have it install its GLFW callbacks.
		auto app = App{*window};
		// create a gvdi::Context by passing ownership of the window, and run the app.
		app.run(gvdi::Context{std::move(window)});
	} catch (std::exception const& e) {
		std::cerr << "PANIC: " << e.what() << "\n";
		return EXIT_FAILURE;
	}
}
