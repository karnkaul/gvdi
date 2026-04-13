#pragma once
#include <cstdint>
#include <span>

namespace gvdi {
class EventListener {
  public:
	virtual ~EventListener() = default;

	EventListener() = default;
	EventListener(EventListener const&) = delete;
	EventListener(EventListener&&) = delete;
	EventListener& operator=(EventListener const&) = delete;
	EventListener& operator=(EventListener&&) = delete;

	virtual void on_window_reposition([[maybe_unused]] int x, [[maybe_unused]] int y) {}
	virtual void on_window_resize([[maybe_unused]] int x, [[maybe_unused]] int y) {}
	virtual void on_framebuffer_resize([[maybe_unused]] int x, [[maybe_unused]] int y) {}
	virtual void on_window_close() {}
	virtual void on_window_focus([[maybe_unused]] bool focused) {}
	virtual void on_window_iconify([[maybe_unused]] bool iconified) {}
	virtual void on_window_maximize([[maybe_unused]] bool maximized) {}

	virtual void on_key_press([[maybe_unused]] int key, [[maybe_unused]] int scancode, [[maybe_unused]] int mods) {}
	virtual void on_key_release([[maybe_unused]] int key, [[maybe_unused]] int scancode, [[maybe_unused]] int mods) {}
	virtual void on_key_repeat([[maybe_unused]] int key, [[maybe_unused]] int scancode, [[maybe_unused]] int mods) {}
	virtual void on_character([[maybe_unused]] std::uint32_t codepoint) {}

	virtual void on_cursor_reposition([[maybe_unused]] double x, [[maybe_unused]] double y) {}
	virtual void on_cursor_enter([[maybe_unused]] bool entered) {}
	virtual void on_mouse_button_press([[maybe_unused]] int button, [[maybe_unused]] int mods) {}
	virtual void on_mouse_button_release([[maybe_unused]] int button, [[maybe_unused]] int mods) {}
	virtual void on_mouse_scroll([[maybe_unused]] double x, [[maybe_unused]] double y) {}

	virtual void on_path_drop([[maybe_unused]] std::span<char const* const> paths) {}
};
} // namespace gvdi
