#pragma once
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <cstdint>
#include <span>
#include <string_view>

namespace gvdi {
///
/// \brief Abstract customization point for GLFW event callbacks.
///
struct EventHandler {
	struct Default;

	virtual ~EventHandler() = default;

	virtual void on_close() {}
	virtual void on_framebuffer_resize(ImVec2 extent) = 0;
	virtual void on_window_resize(ImVec2 extent) = 0;
	virtual void on_focus(bool gained) = 0;
	virtual void on_position(ImVec2 position) = 0;
	virtual void on_key(int key, int action, int mods) = 0;
	virtual void on_char(std::uint32_t codepoint) = 0;
	virtual void on_mouse_button(int button, int action, int mods) = 0;
	virtual void on_scroll(ImVec2 value) = 0;
	virtual void on_cursor(ImVec2 position) = 0;
	virtual void on_drop(std::span<std::string_view const> paths) = 0;
};

///
/// \brief Concrete EventHandler that does nothing.
///
// This is mainly for convenience, to not have to override all the pure virtual functions.
/// Performance-first code can derive from the abstract class instead.
///
struct EventHandler::Default : EventHandler {
	void on_close() override {}
	void on_framebuffer_resize(ImVec2) override {}
	void on_window_resize(ImVec2) override {}
	void on_focus(bool) override {}
	void on_position(ImVec2) override {}
	void on_key(int, int, int) override {}
	void on_char(std::uint32_t) override {}
	void on_mouse_button(int, int, int) override {}
	void on_scroll(ImVec2) override {}
	void on_cursor(ImVec2) override {}
	void on_drop(std::span<std::string_view const>) override {}
};
} // namespace gvdi
