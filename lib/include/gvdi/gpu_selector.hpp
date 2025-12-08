#pragma once
#include "gvdi/gpu_info.hpp"
#include <optional>
#include <span>

namespace gvdi {
/// \brief Represents a viable GPU.
enum struct GpuHandle : std::int8_t {};

/// \brief Opaque interface for GPU selection.
class GpuSelector {
  public:
	GpuSelector(GpuSelector const&) = delete;
	GpuSelector(GpuSelector&&) = delete;
	GpuSelector& operator=(GpuSelector const&) = delete;
	GpuSelector& operator=(GpuSelector&&) = delete;

	GpuSelector() = default;
	virtual ~GpuSelector() = default;

	/// \returns List of viable GPU handles.
	[[nodiscard]] virtual auto enumerate_handles() const -> std::span<GpuHandle const> = 0;

	/// \returns Selected GPU handle.
	[[nodiscard]] virtual auto get_selected() const -> GpuHandle = 0;
	/// \brief Select a GPU.
	/// \param handle Handle to desired GPU.
	/// \returns true if handle is a valid viable GPU.
	virtual auto set_selected(GpuHandle handle) -> bool = 0;

	/// \returns GPU metadata for given handle.
	[[nodiscard]] virtual auto get_info(GpuHandle handle) const -> std::optional<GpuInfo> = 0;
};
} // namespace gvdi
