#pragma once
#include <stdexcept>

namespace gvdi {
/// \brief Library exception type.
struct Error : std::runtime_error {
	using std::runtime_error::runtime_error;
};
} // namespace gvdi
