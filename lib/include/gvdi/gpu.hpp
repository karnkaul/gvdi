#pragma once
#include <cstdint>
#include <string_view>

namespace gvdi::gpu {
enum class Type : std::int8_t { Other, Discrete, Integrated, Cpu, Virtual };

struct Info {
	Type type{Type::Other};
	std::string_view name{};
};
} // namespace gvdi::gpu
