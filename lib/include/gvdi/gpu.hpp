#pragma once
#include <cstdint>
#include <string_view>

namespace gvdi::gpu {
enum class Type : std::int8_t { Other, Discrete, Integrated, Cpu, Virtual };

[[nodiscard]] constexpr auto to_string_view(Type const type) -> std::string_view {
	switch (type) {
	case Type::Discrete: return "Discrete";
	case Type::Integrated: return "Integrated";
	case Type::Cpu: return "Cpu";
	case Type::Virtual: return "Virtual";
	default: return "Other";
	}
}

struct Info {
	Type type{Type::Other};
	std::string_view name{};
};
} // namespace gvdi::gpu
