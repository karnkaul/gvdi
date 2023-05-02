#include <gvdi/gvdi.hpp>

namespace {
struct State {
	bool show_fps{true};
};

// custom event hander
struct EventHandler : gvdi::EventHandler::Default {
	gvdi::Instance& instance;
	State& state;

	EventHandler(gvdi::Instance& instance, State& state) : instance(instance), state(state) {}

	void on_key(int key, int action, int mods) final {
		if (action == GLFW_RELEASE) {
			// close on Ctrl + W
			if (key == GLFW_KEY_W && (mods & GLFW_MOD_CONTROL)) { instance.close(); }
			// show fps on F
			if (key == GLFW_KEY_F && !mods) { state.show_fps = true; }
		}
	}
};
} // namespace

int main() {
	// make an instance
	auto instance = gvdi::Instance{{1280, 720}, "Example Window"};
	// ensure it's active
	if (!instance) { return EXIT_FAILURE; }
	auto state = State{};
	// make a custom event handler
	auto event_handler = EventHandler{instance, state};
	// set the event handler on the instance
	instance.set_event_handler(&event_handler);
	// track Fps
	auto frame_count = int{};
	auto fps = int{};
	auto elapsed = std::chrono::duration<float>{};

	// keep looping while instance is running
	while (instance.is_running()) {
		// make a new frame (starts a render pass)
		auto frame = gvdi::Frame{instance};
		// execute ImGui (or other) code here
		elapsed += frame.dt;
		++frame_count;
		if (elapsed >= 1s) {
			fps = frame_count;
			frame_count = {};
			elapsed = {};
		}
		ImGui::ShowDemoWindow();
		ImGui::SetNextWindowSize({100.0f, 50.0f});
		if (state.show_fps) {
			if (ImGui::Begin("FPS", &state.show_fps)) { ImGui::Text("FPS: %d", fps); }
			ImGui::End();
		}
	} // frame's destructor ends render pass, submits command buffer, and presents swapchain image
}
