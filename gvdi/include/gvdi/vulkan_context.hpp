#pragma once
#include <vulkan/vulkan.hpp>

namespace gvdi {
///
/// \brief The active Vulkan context.
///
/// Note: the command buffer is executing the render pass.
///
struct VulkanContext {
	vk::Instance instance{};
	vk::PhysicalDevice gpu{};
	vk::SurfaceKHR surface{};
	std::uint32_t queue_family{};
	vk::Device device{};
	vk::Queue queue{};
	vk::RenderPass render_pass{};
	vk::CommandBuffer command_buffer{};
};
} // namespace gvdi
