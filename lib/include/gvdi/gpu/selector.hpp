#pragma once
#include "gvdi/gpu/info.hpp"
#include "gvdi/polymorphic.hpp"
#include <optional>
#include <span>

namespace gvdi::gpu {
/// \brief Represents a viable GPU.
enum struct Handle : std::int8_t {};

/// \brief Opaque interface for GPU selection.
class Selector : public Polymorphic {
  public:
	/// \returns List of viable GPU handles.
	[[nodiscard]] virtual auto enumerate_handles() const -> std::span<Handle const> = 0;

	/// \returns Selected GPU handle.
	[[nodiscard]] virtual auto get_selected() const -> Handle = 0;
	/// \brief Select a GPU.
	/// \param handle Handle to desired GPU.
	/// \returns true if handle is a valid viable GPU.
	virtual auto set_selected(Handle handle) -> bool = 0;

	/// \returns GPU metadata for given handle.
	[[nodiscard]] virtual auto get_info(Handle handle) const -> std::optional<Info> = 0;
};
} // namespace gvdi::gpu
