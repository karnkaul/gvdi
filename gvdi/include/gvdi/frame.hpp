#pragma once
#include <gvdi/instance.hpp>
#include <chrono>

using namespace std::chrono_literals;

namespace gvdi {
struct VulkanContext;

///
/// \brief Represents an active render pass.
///
class Frame {
  public:
	Frame& operator=(Frame&&) = delete;

	///
	/// \brief Construct a Frame and begin render pass.
	///
	[[nodiscard]] explicit Frame(Instance& instance);
	///
	/// \brief Destructor ends render pass and submits command buffer.
	///
	~Frame();

	///
	/// \brief Obtain the Vulkan context.
	///
	VulkanContext vulkan_context() const;

	///
	/// \brief Delta time since last frame.
	///
	std::chrono::duration<float> dt{};

  private:
	Instance& m_instance;
};
} // namespace gvdi
