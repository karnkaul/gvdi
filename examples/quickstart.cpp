#include <gvdi/app.hpp>
#include <cstdlib>
#include <print>

namespace {
class App : public gvdi::App {
	// if no customization is desired (like window size / title, GLFW hints, ImGuiIO),
	// this is the only function that needs to be overridden and implemented.
	void update() final {
		// for this example we just show the demo window.
		ImGui::ShowDemoWindow();
	}
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
