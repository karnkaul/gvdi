#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <gvdi/event_handler.hpp>
#include <gvdi/gvdi.hpp>
#include <gvdi/vulkan_context.hpp>
#include <vulkan/vulkan.hpp>
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

namespace gvdi {
namespace {
EventHandler* g_event_handler{};
GLFWwindow* g_window{};
void* g_instance{};

template <typename T, typename U>
constexpr ImVec2 to_vec2(T x, U y) {
	return {static_cast<float>(x), static_cast<float>(y)};
}

struct Glfw {
	struct Deleter {
		void operator()(GLFWwindow* ptr) const {
			if (ptr) {
				g_window = {};
				glfwDestroyWindow(ptr);
				glfwTerminate();
			}
		}
	};

	std::unique_ptr<GLFWwindow, Deleter> window{};

	static bool match(GLFWwindow* w) { return w == g_window; }

	static Glfw make(ImVec2 extent, char const* title) {
		if (!glfwInit()) { return {}; }
		if (!glfwVulkanSupported()) {
			glfwTerminate();
			return {};
		}
		auto ret = Glfw{};
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		auto const width = static_cast<int>(extent.x);
		auto const height = static_cast<int>(extent.y);
		ret.window = std::unique_ptr<GLFWwindow, Deleter>{glfwCreateWindow(width, height, title, {}, {})};
		g_window = ret.window.get();

		glfwSetWindowCloseCallback(ret.window.get(), [](GLFWwindow* w) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_close(); }
			glfwSetWindowShouldClose(w, GLFW_TRUE);
		});
		glfwSetWindowSizeCallback(ret.window.get(), [](GLFWwindow* w, int x, int y) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_window_resize(to_vec2(x, y)); }
		});
		glfwSetFramebufferSizeCallback(ret.window.get(), [](GLFWwindow* w, int x, int y) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_framebuffer_resize(to_vec2(x, y)); }
		});
		glfwSetWindowFocusCallback(ret.window.get(), [](GLFWwindow* w, int gained) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_focus(gained == GLFW_TRUE); }
		});
		glfwSetWindowPosCallback(ret.window.get(), [](GLFWwindow* w, int x, int y) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_position(to_vec2(x, y)); }
		});
		glfwSetKeyCallback(ret.window.get(), [](GLFWwindow* w, int key, int, int action, int mods) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_key(key, action, mods); }
		});
		glfwSetCharCallback(ret.window.get(), [](GLFWwindow* w, std::uint32_t codepoint) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_char(codepoint); }
		});
		glfwSetMouseButtonCallback(ret.window.get(), [](GLFWwindow* w, int button, int action, int mods) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_mouse_button(button, action, mods); }
		});
		glfwSetScrollCallback(ret.window.get(), [](GLFWwindow* w, double x, double y) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_scroll(to_vec2(x, y)); }
		});
		glfwSetCursorPosCallback(ret.window.get(), [](GLFWwindow* w, double x, double y) {
			if (!match(w)) { return; }
			if (g_event_handler) { g_event_handler->on_cursor(to_vec2(x, y)); }
		});
		glfwSetDropCallback(ret.window.get(), [](GLFWwindow* w, int count, char const** paths) {
			if (!match(w)) { return; }
			if (g_event_handler) {
				auto drops = std::vector<std::string_view>{};
				drops.reserve(static_cast<std::size_t>(count));
				for (int i = 0; i < count; ++i) { drops.push_back(paths[i]); }
				g_event_handler->on_drop(drops);
			}
		});
		if (glfwRawMouseMotionSupported()) { glfwSetInputMode(ret.window.get(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE); }

		return ret;
	}
};

vk::UniqueImageView make_image_view(vk::Device device, vk::Image const image, vk::Format const format) {
	vk::ImageViewCreateInfo info;
	info.viewType = vk::ImageViewType::e2D;
	info.format = format;
	info.components.r = info.components.g = info.components.b = info.components.a = vk::ComponentSwizzle::eIdentity;
	info.subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	info.image = image;
	return device.createImageViewUnique(info);
}

struct DeviceBlock {
	vk::Device device{};

	~DeviceBlock() {
		if (device) { device.waitIdle(); }
	}
};

struct Vulkan {
	vk::UniqueInstance instance{};
	vk::PhysicalDevice gpu{};
	vk::UniqueSurfaceKHR surface{};
	std::uint32_t queue_family{};
	vk::UniqueDevice device{};
	vk::Queue queue{};

	static vk::PhysicalDevice select_gpu(std::span<vk::PhysicalDevice const> gpus, GpuType preferred) {
		auto const type = preferred == GpuType::eDiscrete ? vk::PhysicalDeviceType::eIntegratedGpu : vk::PhysicalDeviceType::eDiscreteGpu;
		for (auto const& gpu : gpus) {
			if (gpu.getProperties().deviceType == type) { return gpu; }
		}
		assert(!gpus.empty());
		return gpus.front();
	}

	static std::uint32_t get_queue_family(vk::SurfaceKHR surface, vk::PhysicalDevice const& device, std::uint32_t& out_family) {
		static constexpr auto queue_flags_v = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
		auto const properties = device.getQueueFamilyProperties();
		for (std::size_t i = 0; i < properties.size(); ++i) {
			auto const family = static_cast<std::uint32_t>(i);
			if (!device.getSurfaceSupportKHR(family, surface)) { continue; }
			if (!(properties[i].queueFlags & queue_flags_v)) { continue; }
			out_family = family;
			return true;
		}
		return false;
	}

	static Vulkan make(GLFWwindow* window, GpuType preferred) {
		auto dynamic_loader = vk::DynamicLoader{};
		VULKAN_HPP_DEFAULT_DISPATCHER.init(dynamic_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
		auto ici = vk::InstanceCreateInfo{};
		auto extension_count = std::uint32_t{};
		auto** glfw_extensions = glfwGetRequiredInstanceExtensions(&extension_count);
		ici.enabledExtensionCount = extension_count;
		ici.ppEnabledExtensionNames = glfw_extensions;
#if defined(__APPLE__)
		ici.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif
		auto ai = vk::ApplicationInfo{};
		ai.apiVersion = VK_API_VERSION_1_1;
		ici.pApplicationInfo = &ai;
		auto ret = Vulkan{};
		ret.instance = vk::createInstanceUnique(ici);
		if (!ret.instance) { return {}; }
		VULKAN_HPP_DEFAULT_DISPATCHER.init(*ret.instance);

		ret.gpu = select_gpu(ret.instance->enumeratePhysicalDevices(), preferred);
		if (!ret.gpu) { return {}; }
		auto surface = VkSurfaceKHR{};
		auto const surface_result = glfwCreateWindowSurface(*ret.instance, window, {}, &surface);
		if (surface_result != VK_SUCCESS) { return {}; }
		ret.surface = vk::UniqueSurfaceKHR{surface, *ret.instance};
		if (!get_queue_family(*ret.surface, ret.gpu, ret.queue_family)) { return {}; }

		static constexpr float priority_v = 1.0f;
		auto dqci = vk::DeviceQueueCreateInfo{{}, ret.queue_family, 1, &priority_v};
		auto dci = vk::DeviceCreateInfo{};
		static constexpr std::array required_extensions_v = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_MAINTENANCE1_EXTENSION_NAME,

#if defined(__APPLE__)
			"VK_KHR_portability_subset",
#endif
		};
		dci.queueCreateInfoCount = 1u;
		dci.pQueueCreateInfos = &dqci;
		dci.enabledExtensionCount = static_cast<std::uint32_t>(required_extensions_v.size());
		dci.ppEnabledExtensionNames = required_extensions_v.data();
		ret.device = ret.gpu.createDeviceUnique(dci);
		if (!ret.device) { return {}; }
		VULKAN_HPP_DEFAULT_DISPATCHER.init(*ret.device);

		ret.queue = ret.device->getQueue(ret.queue_family, 0u);

		return ret;
	}
};

struct DearImGui {
	static vk::UniqueDescriptorPool make_descriptor_pool(vk::Device device, std::uint32_t count) {
		vk::DescriptorPoolSize pool_sizes[] = {
			{vk::DescriptorType::eSampler, count},
			{vk::DescriptorType::eCombinedImageSampler, count},
			{vk::DescriptorType::eSampledImage, count},
			{vk::DescriptorType::eStorageImage, count},
			{vk::DescriptorType::eUniformTexelBuffer, count},
			{vk::DescriptorType::eStorageTexelBuffer, count},
			{vk::DescriptorType::eUniformBuffer, count},
			{vk::DescriptorType::eStorageBuffer, count},
			{vk::DescriptorType::eUniformBufferDynamic, count},
			{vk::DescriptorType::eStorageBufferDynamic, count},
			{vk::DescriptorType::eInputAttachment, count},
		};
		auto dpci = vk::DescriptorPoolCreateInfo{};
		dpci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		dpci.poolSizeCount = std::size(pool_sizes);
		dpci.maxSets = count * dpci.poolSizeCount;
		dpci.pPoolSizes = pool_sizes;
		return device.createDescriptorPoolUnique(dpci);
	}

	struct Deleter {
		void operator()(void const*) const {
			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
		}
	};

	enum class State { eBegin, eEnd };

	vk::UniqueDescriptorPool descriptor_pool{};
	std::unique_ptr<void, Deleter> instance{};
	State next{State::eBegin};

	static DearImGui make(Vulkan const& vulkan, vk::RenderPass render_pass, GLFWwindow* window, std::uint32_t min_image_count, std::uint32_t image_count) {
		auto ret = DearImGui{};
		ret.descriptor_pool = make_descriptor_pool(*vulkan.device, 1024u);

		struct Data {
			int magic{42};
			vk::Instance instance{};
			vk::DynamicLoader dynamic_loader{};
		} data{.instance = *vulkan.instance};
		auto const fn = [](char const* f, void* ptr) {
			auto const* data = reinterpret_cast<Data const*>(ptr);
			assert(data && data->magic == 42);
			return data->dynamic_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr")(data->instance, f);
		};
		ImGui_ImplVulkan_LoadFunctions(fn, &data);
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		ImGui_ImplGlfw_InitForVulkan(window, true);

		auto initInfo = ImGui_ImplVulkan_InitInfo{};
		initInfo.Instance = *vulkan.instance;
		initInfo.Device = *vulkan.device;
		initInfo.PhysicalDevice = vulkan.gpu;
		initInfo.Queue = vulkan.queue;
		initInfo.QueueFamily = vulkan.queue_family;
		initInfo.MinImageCount = min_image_count;
		initInfo.ImageCount = image_count;
		initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		initInfo.DescriptorPool = *ret.descriptor_pool;
		if (!ImGui_ImplVulkan_Init(&initInfo, render_pass)) { return {}; }

		ret.instance = std::unique_ptr<void, Deleter>{reinterpret_cast<void*>(42)};
		return ret;
	}

	void begin_frame() {
		if (next == State::eEnd) { end_frame(); }
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		next = State::eEnd;
	}

	void end_frame() {
		if (next == State::eBegin) { begin_frame(); }
		ImGui::Render();
		next = State::eBegin;
	}

	void render(vk::CommandBuffer cb) { ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb); }
};

struct Renderer {
	struct Swapchain {
		vk::UniqueSwapchainKHR swapchain{};
		std::vector<vk::Image> images{};
		std::vector<vk::UniqueImageView> image_views{};
	};

	vk::Device device{};
	vk::Queue queue{};
	vk::PhysicalDevice gpu{};
	vk::SurfaceKHR surface{};
	Swapchain swapchain{};
	Swapchain previous_swapchain{};
	vk::SwapchainCreateInfoKHR sci{};
	vk::UniqueRenderPass render_pass{};
	vk::UniqueFramebuffer framebuffer{};
	vk::UniqueSemaphore sempahore_draw{};
	vk::UniqueSemaphore sempahore_present{};
	vk::UniqueFence fence_drawn{};
	vk::UniqueCommandPool command_pool{};
	vk::CommandBuffer command_buffer{};
	DearImGui dear_imgui{};
	DeviceBlock device_block{};

	static constexpr auto fence_timeout_v{std::numeric_limits<std::uint64_t>::max()};

	static vk::SurfaceFormatKHR get_surface_format(std::span<vk::SurfaceFormatKHR const> available) {
		for (auto const& format : available) {
			if (format.format == vk::Format::eR8G8B8Snorm || format.format == vk::Format::eB8G8R8A8Snorm) { return format; }
		}
		assert(!available.empty());
		return available.front();
	}

	static vk::SwapchainCreateInfoKHR make_sci(Vulkan const& vulkan, vk::SurfaceFormatKHR format) {
		auto const caps = vulkan.gpu.getSurfaceCapabilitiesKHR(*vulkan.surface);
		auto ret = vk::SwapchainCreateInfoKHR{};
		ret.surface = *vulkan.surface;
		ret.presentMode = vk::PresentModeKHR::eFifo;
		ret.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
		ret.queueFamilyIndexCount = 1u;
		ret.pQueueFamilyIndices = &vulkan.queue_family;
		ret.imageColorSpace = format.colorSpace;
		ret.imageArrayLayers = 1u;
		ret.imageFormat = format.format;
		if (caps.maxImageCount == 0) {
			ret.minImageCount = std::max(3u, caps.minImageCount);
		} else {
			ret.minImageCount = std::clamp(3u, caps.minImageCount, caps.maxImageCount);
		}
		return ret;
	}

	static vk::UniqueRenderPass make_render_pass(vk::Device device, vk::Format colour) {
		auto attachment = vk::AttachmentDescription{};
		attachment.format = colour;
		attachment.samples = vk::SampleCountFlagBits::e1;
		attachment.loadOp = vk::AttachmentLoadOp::eClear;
		attachment.storeOp = vk::AttachmentStoreOp::eStore;
		attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attachment.initialLayout = vk::ImageLayout::eUndefined;
		attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
		auto colour_attachment = vk::AttachmentReference{};
		colour_attachment.attachment = 0;
		colour_attachment.layout = vk::ImageLayout::eColorAttachmentOptimal;
		auto subpass = vk::SubpassDescription{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1u;
		subpass.pColorAttachments = &colour_attachment;
		auto rpci = vk::RenderPassCreateInfo{};
		rpci.attachmentCount = 1u;
		rpci.pAttachments = &attachment;
		rpci.subpassCount = 1u;
		rpci.pSubpasses = &subpass;
		return device.createRenderPassUnique(rpci);
	}

	static Renderer make(Vulkan const& vulkan, GLFWwindow* window, vk::Extent2D framebuffer_extent) {
		auto ret = Renderer{.device = *vulkan.device, .queue = vulkan.queue, .gpu = vulkan.gpu, .surface = *vulkan.surface};
		ret.device_block.device = ret.device;
		auto const format = get_surface_format(vulkan.gpu.getSurfaceFormatsKHR(ret.surface));
		ret.sci = make_sci(vulkan, format);
		ret.refresh(framebuffer_extent);
		if (!ret.swapchain.swapchain) { return {}; }
		ret.fence_drawn = ret.device.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
		ret.sempahore_draw = ret.device.createSemaphoreUnique({});
		ret.sempahore_present = ret.device.createSemaphoreUnique({});

		ret.render_pass = make_render_pass(ret.device, ret.sci.imageFormat);
		if (!ret.render_pass) { return {}; }
		static constexpr vk::CommandPoolCreateFlags pool_flags_v =
			vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		static constexpr vk::CommandBufferLevel cb_lvl_v = vk::CommandBufferLevel::ePrimary;
		ret.command_pool = ret.device.createCommandPoolUnique({pool_flags_v, vulkan.queue_family});
		auto const cbai = vk::CommandBufferAllocateInfo{*ret.command_pool, cb_lvl_v, 1u};
		if (ret.device.allocateCommandBuffers(&cbai, &ret.command_buffer) != vk::Result::eSuccess) { return {}; }

		auto const image_count = static_cast<std::uint32_t>(ret.swapchain.images.size());
		ret.dear_imgui = DearImGui::make(vulkan, *ret.render_pass, window, ret.sci.minImageCount, image_count);
		if (!ret.dear_imgui.instance) { return {}; }

		ret.command_buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
		ImGui_ImplVulkan_CreateFontsTexture(ret.command_buffer);
		ret.command_buffer.end();
		auto si = vk::SubmitInfo{};
		si.commandBufferCount = 1u;
		si.pCommandBuffers = &ret.command_buffer;
		auto fence = ret.device.createFenceUnique({});
		ret.queue.submit(si, *fence);
		if (ret.device.waitForFences(*fence, true, fence_timeout_v) != vk::Result::eSuccess) { return {}; }
		ImGui_ImplVulkan_DestroyFontUploadObjects();

		return ret;
	}

	void refresh(vk::Extent2D framebuffer_extent) {
		auto new_swapchain = Swapchain{};
		auto const caps = gpu.getSurfaceCapabilitiesKHR(surface);
		if (caps.currentExtent.width == 0xffffffff && caps.currentExtent.height == 0xffffffff) {
			sci.imageExtent.width = std::clamp(framebuffer_extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
			sci.imageExtent.height = std::clamp(framebuffer_extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
		} else {
			sci.imageExtent = caps.currentExtent;
		}
		sci.oldSwapchain = *swapchain.swapchain;
		new_swapchain.swapchain = device.createSwapchainKHRUnique(sci);
		if (!new_swapchain.swapchain) { return; }
		new_swapchain.images = device.getSwapchainImagesKHR(*new_swapchain.swapchain);
		for (auto const& image : new_swapchain.images) { new_swapchain.image_views.push_back(make_image_view(device, image, sci.imageFormat)); }
		previous_swapchain = std::exchange(swapchain, std::move(new_swapchain));
	}

	std::optional<std::uint32_t> acquire(vk::Extent2D extent) {
		if (extent.width == 0 || extent.height == 0) { return {}; }
		if (device.waitForFences(*fence_drawn, true, fence_timeout_v) != vk::Result::eSuccess) { return {}; }
		device.resetFences(*fence_drawn);
		auto ret = std::uint32_t{};
		auto const result = device.acquireNextImageKHR(*swapchain.swapchain, fence_timeout_v, *sempahore_draw, {}, &ret);
		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
			refresh(extent);
			return {};
		}
		auto fbci = vk::FramebufferCreateInfo{
			{}, *render_pass, 1u, &*swapchain.image_views[ret], sci.imageExtent.width, sci.imageExtent.height, 1u,
		};
		framebuffer = device.createFramebufferUnique(fbci);
		command_buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
		auto const render_area = vk::Rect2D{{}, sci.imageExtent};
		auto const clear_value = vk::ClearValue{vk::ClearColorValue{std::array{0.1f, 0.1f, 0.1f, 1.0f}}};
		auto const rpbi = vk::RenderPassBeginInfo{*render_pass, *framebuffer, render_area, 1u, &clear_value};
		command_buffer.beginRenderPass(rpbi, vk::SubpassContents::eInline);
		return ret;
	}

	void submit(vk::Extent2D extent, std::uint32_t image_index) {
		if (extent.width == 0 || extent.height == 0) { return; }
		dear_imgui.render(command_buffer);
		command_buffer.endRenderPass();
		command_buffer.end();
		static constexpr vk::PipelineStageFlags wait_stage_dst_mask_v = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		auto si = vk::SubmitInfo{};
		si.commandBufferCount = 1u;
		si.pCommandBuffers = &command_buffer;
		si.waitSemaphoreCount = 1u;
		si.pWaitSemaphores = &*sempahore_draw;
		si.signalSemaphoreCount = 1u;
		si.pSignalSemaphores = &*sempahore_present;
		si.pWaitDstStageMask = &wait_stage_dst_mask_v;
		auto result = queue.submit(1u, &si, *fence_drawn);
		auto pi = vk::PresentInfoKHR{};
		pi.waitSemaphoreCount = 1u;
		pi.pWaitSemaphores = &*sempahore_present;
		pi.swapchainCount = 1u;
		pi.pSwapchains = &*swapchain.swapchain;
		pi.pImageIndices = &image_index;
		result = queue.presentKHR(&pi);
		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) { refresh(extent); }
	}
};
} // namespace

struct Instance::Impl {
	Glfw glfw{};
	Vulkan vulkan{};
	Renderer renderer{};

	std::chrono::steady_clock::time_point frame_start{};
	std::optional<std::uint32_t> acquired_index{};

	vk::Extent2D framebuffer_extent() const {
		int x, y{};
		glfwGetFramebufferSize(glfw.window.get(), &x, &y);
		return vk::Extent2D{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};
	}

	void init(ImVec2 extent, char const* title, GpuType preferred) noexcept(false) {
		glfw = Glfw::make(extent, title);
		if (!glfw.window) { throw std::runtime_error{"Failed to create GLFW Window"}; }

		vulkan = Vulkan::make(glfw.window.get(), preferred);
		if (!vulkan.device) { throw std::runtime_error{"Failed to create Vulkan device"}; }

		renderer = Renderer::make(vulkan, glfw.window.get(), framebuffer_extent());
		if (!renderer.render_pass) { throw std::runtime_error{"Failed to create Renderer"}; }
	}
};

void Instance::Deleter::operator()(Impl const* ptr) const {
	g_instance = {};
	g_event_handler = {};
	delete ptr;
}

Instance::Instance(ImVec2 extent, char const* title, GpuType preferred) : m_impl(new Impl{}) {
	try {
		if (g_instance) { throw std::runtime_error{"Duplicate instance"}; }
		m_impl->init(extent, title, preferred);
		glfwShowWindow(m_impl->glfw.window.get());
		g_instance = m_impl.get();

	} catch (std::exception const& e) {
		std::cerr << e.what() << "\n";
		m_impl.reset();
	}
}

bool Instance::is_running() const {
	if (!m_impl) { return false; }
	return glfwWindowShouldClose(m_impl->glfw.window.get()) != GLFW_TRUE;
}

void Instance::close() {
	if (!m_impl) { return; }
	glfwSetWindowShouldClose(m_impl->glfw.window.get(), GLFW_TRUE);
}

ImVec2 Instance::framebuffer_extent() const {
	if (!m_impl) { return {}; }
	int x, y;
	glfwGetFramebufferSize(m_impl->glfw.window.get(), &x, &y);
	return to_vec2(x, y);
}

ImVec2 Instance::window_extent() const {
	if (!m_impl) { return {}; }
	int x, y;
	glfwGetWindowSize(m_impl->glfw.window.get(), &x, &y);
	return to_vec2(x, y);
}

ImVec2 Instance::window_position() const {
	if (!m_impl) { return {}; }
	int x, y;
	glfwGetWindowPos(m_impl->glfw.window.get(), &x, &y);
	return to_vec2(x, y);
}

ImVec2 Instance::cursor_position() const {
	if (!m_impl) { return {}; }
	double x, y;
	glfwGetCursorPos(m_impl->glfw.window.get(), &x, &y);
	return to_vec2(x, y);
}

GLFWwindow* Instance::window() const {
	if (!m_impl) { return {}; }
	return m_impl->glfw.window.get();
}

void Instance::set_event_handler(EventHandler* event_handler) { g_event_handler = event_handler; }

Frame::Frame(Instance& instance) : m_instance(instance) {
	glfwPollEvents();
	if (!m_instance.m_impl || m_instance.m_impl->acquired_index) { return; }
	auto const now = std::chrono::steady_clock::now();
	dt = now - m_instance.m_impl->frame_start;
	m_instance.m_impl->frame_start = now;
	m_instance.m_impl->acquired_index = m_instance.m_impl->renderer.acquire(m_instance.m_impl->framebuffer_extent());
	if (!m_instance.m_impl->acquired_index) { return; }
	m_instance.m_impl->renderer.dear_imgui.begin_frame();
}

Frame::~Frame() {
	if (!m_instance.m_impl || !m_instance.m_impl->acquired_index) { return; }
	m_instance.m_impl->renderer.dear_imgui.end_frame();
	m_instance.m_impl->renderer.submit(m_instance.m_impl->framebuffer_extent(), *m_instance.m_impl->acquired_index);
	m_instance.m_impl->acquired_index.reset();
}

VulkanContext Frame::vulkan_context() const {
	auto const* impl = m_instance.m_impl.get();
	if (!impl) { return {}; }
	return VulkanContext{
		.instance = *impl->vulkan.instance,
		.gpu = impl->vulkan.gpu,
		.surface = *impl->vulkan.surface,
		.queue_family = impl->vulkan.queue_family,
		.device = *impl->vulkan.device,
		.queue = impl->vulkan.queue,
		.render_pass = *impl->renderer.render_pass,
		.command_buffer = impl->renderer.command_buffer,
	};
}
} // namespace gvdi
