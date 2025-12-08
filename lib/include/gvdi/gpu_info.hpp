#pragma once
#include <cstdint>
#include <string_view>

namespace gvdi {
/// \brief GPU metadata, can be inspected during selection.
struct GpuInfo {
	enum class Type : std::int8_t { Other, Discrete, Integrated, Cpu, Virtual };

	Type type{Type::Other};
	std::string_view name{};
};
} // namespace gvdi
