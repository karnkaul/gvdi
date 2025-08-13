#pragma once
#include <stdexcept>

namespace gvdi {
struct Error : std::runtime_error {
	using std::runtime_error::runtime_error;
};
} // namespace gvdi
