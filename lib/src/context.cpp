#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <gvdi/build_version.hpp>
#include <gvdi/context.hpp>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace gvdi {
namespace {
constexpr auto vk_api_v = VK_API_VERSION_1_1;

[[nodiscard]] auto get_glfw_instance_extensions() -> std::vector<char const*> {
	auto count = std::uint32_t{};
	auto const* extensions_ptr = glfwGetRequiredInstanceExtensions(&count);
	auto const extensions = std::span{extensions_ptr, count};
	return {extensions.begin(), extensions.end()};
}

constexpr auto is_linear_format(vk::Format const format) {
	using enum vk::Format;
	constexpr auto linear_formats_v = std::array{eR8G8B8A8Unorm, eR8G8B8A8Snorm,	   eB8G8R8A8Unorm,
												 eB8G8R8A8Snorm, eA8B8G8R8UnormPack32, eA8B8G8R8SnormPack32};
	return std::find(linear_formats_v.begin(), linear_formats_v.end(), format) != linear_formats_v.end();
}

constexpr auto get_linear_surface_format(std::span<vk::SurfaceFormatKHR const> available) -> vk::SurfaceFormatKHR {
	for (auto const format : available) {
		if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
			if (is_linear_format(format.format)) { return format; }
		}
	}
	return available.front();
}

constexpr auto image_extent(vk::SurfaceCapabilitiesKHR const& caps, vk::Extent2D const fb) noexcept -> vk::Extent2D {
	constexpr auto limitless_v = std::numeric_limits<std::uint32_t>::max();
	if (caps.currentExtent.width < limitless_v && caps.currentExtent.height < limitless_v) {
		return caps.currentExtent;
	}
	auto const x = std::clamp(fb.width, caps.minImageExtent.width, caps.maxImageExtent.width);
	auto const y = std::clamp(fb.height, caps.minImageExtent.height, caps.maxImageExtent.height);
	return vk::Extent2D{x, y};
}

constexpr auto image_count(vk::SurfaceCapabilitiesKHR const& caps) noexcept -> std::uint32_t {
	if (caps.maxImageCount < caps.minImageCount) { return std::max(3u, caps.minImageCount); }
	return std::clamp(3u, caps.minImageCount, caps.maxImageCount);
}

template <typename T>
constexpr auto to_vk_extent(T const width, T const height) {
	return vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

constexpr auto to_vk_extent(ImVec2 const vec2) { return to_vk_extent(vec2.x, vec2.y); }

[[nodiscard]] auto to_vk_version(std::string_view const ver_str) -> std::uint32_t {
	struct {
		int major{};
		int minor{};
		int patch{};
	} version{};
	auto str = std::istringstream{std::string{ver_str}};
	char ch{};
	str >> ch >> version.major >> ch >> version.minor >> ch >> version.patch;
	return VK_MAKE_VERSION(version.major, version.minor, version.patch);
}

struct MakeImageView {
	vk::Image image;
	vk::Format format;

	vk::ImageSubresourceRange subresource{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	vk::ImageViewType type{vk::ImageViewType::e2D};

	[[nodiscard]] auto operator()(vk::Device device) const -> vk::UniqueImageView {
		auto ivci = vk::ImageViewCreateInfo{};
		ivci.viewType = type;
		ivci.format = format;
		ivci.subresourceRange = subresource;
		ivci.image = image;
		return device.createImageViewUnique(ivci);
	}
};

template <typename... Args>
auto str_format(Args const&... args) {
	auto str = std::stringstream{};
	((str << args), ...); // NOLINT
	return str.str();
}
} // namespace

struct Context::Impl {
	Impl(UniqueWindow window) : m_window(std::move(window)) {
		if (!m_window) { throw Error{"Null window passed to Context"}; }
		create_instance();
		create_surface();
		select_gpu();
		create_device();
		create_swapchain();
		create_frame();
		create_render_pass();
		create_dear_imgui();
	}

	auto next_frame() -> bool {
		glfwPollEvents();
		m_imgui->begin_frame();
		return glfwWindowShouldClose(m_window.get()) != GLFW_TRUE;
	}

	void render(ImVec4 const& clear) {
		m_imgui->end_frame();
		auto const acquired_image = acquire_next_image();
		if (!acquired_image) { return; }

		m_framebuffer = create_framebuffer(*acquired_image);
		auto const render_area = vk::Rect2D{vk::Offset2D{}, m_swapchain.create_info.imageExtent};

		auto const vk_clear_colour = vk::ClearColorValue{clear.x, clear.y, clear.z, clear.w};
		auto const clear_values = std::array<vk::ClearValue, 2>{
			vk_clear_colour,
			vk::ClearDepthStencilValue{1.0f, 0},
		};
		auto const rpbi = vk::RenderPassBeginInfo{*m_render_pass, *m_framebuffer, render_area, 2, clear_values.data()};

		m_frame.command.buffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
		m_frame.command.buffer.beginRenderPass(rpbi, vk::SubpassContents::eInline);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_frame.command.buffer);
		m_frame.command.buffer.endRenderPass();
		m_frame.command.buffer.end();

		submit_and_present();
	}

	// NOLINTNEXTLINE(readability-make-member-function-const)
	void close() { glfwSetWindowShouldClose(get_window(), GLFW_TRUE); }

	[[nodiscard]] auto get_window() const -> GLFWwindow* { return m_window.get(); }

	[[nodiscard]] auto get_framebuffer_size() const -> ImVec2 {
		auto width = int{};
		auto height = int{};
		glfwGetFramebufferSize(m_window.get(), &width, &height);
		return {static_cast<float>(width), static_cast<float>(height)};
	}

  private:
	static constexpr auto max_timeout_v{std::numeric_limits<std::uint64_t>::max()};

	struct DeviceBlocker {
		vk::Device device{};
	};

	struct Gpu {
		vk::PhysicalDevice device{};
		vk::PhysicalDeviceProperties properties{};
		std::uint32_t queue_family{};
	};

	struct DearImGui {
		enum class State : std::int8_t { eBegin, eEnd };

		vk::UniqueDescriptorPool descriptor_pool{};
		State state{};

		void begin_frame() {
			if (state == State::eEnd) { end_frame(); }
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			state = State::eEnd;
		}

		auto end_frame() -> void {
			if (state == State::eBegin) { return; }
			// ImGui::Render calls ImGui::EndFrame
			ImGui::Render();
			state = State::eBegin;
		}
	};

	struct Deleter {
		void operator()(GLFWwindow* window) const noexcept {
			glfwDestroyWindow(window);
			glfwTerminate();
		}

		void operator()(DeviceBlocker* blocker) const noexcept {
			blocker->device.waitIdle();
			std::default_delete<DeviceBlocker>{}(blocker);
		}

		void operator()(DearImGui* dear_imgui) const noexcept {
			ImGui_ImplVulkan_DestroyFontsTexture();
			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			std::default_delete<DearImGui>{}(dear_imgui);
		}
	};

	struct Command {
		vk::UniqueCommandPool pool{};
		vk::CommandBuffer buffer{};

		[[nodiscard]] static auto make(vk::Device device, std::uint32_t queue_family) -> Command {
			auto const cpci = vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eTransient |
															vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
														queue_family};
			auto ret = Command{};
			ret.pool = device.createCommandPoolUnique(cpci);
			auto const cbai = vk::CommandBufferAllocateInfo{*ret.pool, vk::CommandBufferLevel::ePrimary, 1};
			if (device.allocateCommandBuffers(&cbai, &ret.buffer) != vk::Result::eSuccess) {
				throw Error{"Failed to allocate Vulkan Command Buffer"};
			}
			return ret;
		}
	};

	struct Swapchain {
		vk::SwapchainCreateInfoKHR create_info{};
		vk::UniqueSwapchainKHR swapchain{};
		std::vector<vk::Image> images{};
		std::vector<vk::UniqueImageView> image_views{};
		std::optional<std::uint32_t> image_index{};
	};

	struct Frame {
		Command command{};
		vk::UniqueSemaphore draw{};
		vk::UniqueFence drawn{};
	};

	void create_instance() {
		VULKAN_HPP_DEFAULT_DISPATCHER.init();

		auto const api_version = vk::enumerateInstanceVersion();
		if (api_version < vk_api_v) { throw Error{"Vulkan 1.1 not supported by loader"}; }

		auto extensions = get_glfw_instance_extensions();

		auto const version = to_vk_version(build_version_v);
		auto const vai = vk::ApplicationInfo{"bave", version, "bave", version, vk_api_v};
		auto ici = vk::InstanceCreateInfo{};
		ici.pApplicationInfo = &vai;
#if defined(__APPLE__)
		ici.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
		ici.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
		ici.ppEnabledExtensionNames = extensions.data();

		try {
			m_instance = vk::createInstanceUnique(ici);
		} catch (vk::LayerNotPresentError const& e) {
			ici.enabledLayerCount = 0;
			m_instance = vk::createInstanceUnique(ici);
		}
		if (!m_instance) { throw Error{"Failed to create Vulkan Instance"}; }

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance.get());
	}

	void create_surface() {
		VkSurfaceKHR surface{};
		auto const result = glfwCreateWindowSurface(*m_instance, m_window.get(), nullptr, &surface);
		if (result != VK_SUCCESS || surface == VkSurfaceKHR{}) { throw Error{"Failed to create surface"}; }
		m_surface = vk::UniqueSurfaceKHR{surface, *m_instance};
	}

	void select_gpu() {
		auto const get_queue_family = [surface = *m_surface](vk::PhysicalDevice const& device,
															 std::uint32_t& out_family) {
			static constexpr auto queue_flags_v = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
			auto const properties = device.getQueueFamilyProperties();
			for (size_t i = 0; i < properties.size(); ++i) {
				auto const family = static_cast<std::uint32_t>(i);
				if (device.getSurfaceSupportKHR(family, surface) == 0) { continue; }
				if (!(properties[i].queueFlags & queue_flags_v)) { continue; }
				out_family = family;
				return true;
			}
			return false;
		};

		for (auto const& device : m_instance->enumeratePhysicalDevices()) {
			auto gpu = Gpu{.device = device, .properties = device.getProperties()};
			if (gpu.properties.apiVersion < vk_api_v) { continue; }
			if (!get_queue_family(device, gpu.queue_family)) { continue; }

			m_gpu = gpu;
			return;
		}

		throw Error{"Failed to select suitable GPU"};
	}

	void create_device() {
		static constexpr float priority_v = 1.0f;
		static constexpr std::array required_extensions_v = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if defined(__APPLE__)
															 VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#endif
		};

		auto const available_extensions = m_gpu.device.enumerateDeviceExtensionProperties();
		for (auto const* ext : required_extensions_v) {
			auto const found = [ext](vk::ExtensionProperties const& props) {
				return std::string_view{props.extensionName} == ext;
			};
			if (std::find_if(available_extensions.begin(), available_extensions.end(), found) ==
				available_extensions.end()) {
				auto const err = str_format("Required extension '", ext, "' not supported by selected GPU '",
											m_gpu.properties.deviceName.data(), "'");
				throw Error{err};
			}
		}

		auto qci = vk::DeviceQueueCreateInfo{{}, m_gpu.queue_family, 1, &priority_v};
		auto dci = vk::DeviceCreateInfo{};
		dci.queueCreateInfoCount = 1;
		dci.pQueueCreateInfos = &qci;
		dci.enabledExtensionCount = static_cast<std::uint32_t>(required_extensions_v.size());
		dci.ppEnabledExtensionNames = required_extensions_v.data();

		m_device = m_gpu.device.createDeviceUnique(dci);
		m_queue = m_device->getQueue(m_queue_family, 0);
		if (!m_device || !m_queue) { throw Error{"Failed to create Vulkan Device"}; }

		VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_device);

		m_blocker = std::unique_ptr<DeviceBlocker, Deleter>{new DeviceBlocker{.device = *m_device}};
	}

	void create_swapchain() {
		auto const format = get_linear_surface_format(m_gpu.device.getSurfaceFormatsKHR(*m_surface));
		m_swapchain.create_info.surface = *m_surface;
		m_swapchain.create_info.presentMode = vk::PresentModeKHR::eFifo;
		m_swapchain.create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
		m_swapchain.create_info.queueFamilyIndexCount = 1u;
		m_swapchain.create_info.pQueueFamilyIndices = &m_queue_family;
		m_swapchain.create_info.imageColorSpace = format.colorSpace;
		m_swapchain.create_info.imageArrayLayers = 1u;
		m_swapchain.create_info.imageFormat = format.format;

		if (!recreate_swapchain(to_vk_extent(get_framebuffer_size()))) {
			throw Error{"Failed to create Vulkan Swapchain"};
		}
	}

	void create_frame() {
		m_frame.draw = m_device->createSemaphoreUnique({});
		m_frame.drawn = m_device->createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
		m_frame.command = Command::make(*m_device, m_queue_family);
		m_swapchain.image_index.reset();
	}

	void create_render_pass() {
		auto rpci = vk::RenderPassCreateInfo{};

		auto const attachment_ref = vk::AttachmentReference{0, vk::ImageLayout::eColorAttachmentOptimal};

		auto sd = vk::SubpassDescription{};
		sd.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		sd.colorAttachmentCount = 1;
		sd.pColorAttachments = &attachment_ref;

		auto attachment_descs = std::array<vk::AttachmentDescription, 2>{};
		attachment_descs[0].format = m_swapchain.create_info.imageFormat;
		attachment_descs[0].loadOp = vk::AttachmentLoadOp::eClear;
		attachment_descs[0].storeOp = vk::AttachmentStoreOp::eStore;
		attachment_descs[0].initialLayout = vk::ImageLayout::eUndefined;
		attachment_descs[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;
		attachment_descs[0].samples = vk::SampleCountFlagBits::e1;

		auto dep = vk::SubpassDependency{};
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.srcAccessMask = dep.dstAccessMask =
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

		rpci.pAttachments = attachment_descs.data();
		rpci.attachmentCount = 1;
		rpci.pSubpasses = &sd;
		rpci.subpassCount = 1;
		rpci.pDependencies = &dep;
		rpci.dependencyCount = 1;

		m_render_pass = m_device->createRenderPassUnique(rpci);
	}

	void create_dear_imgui() {
		static constexpr std::uint32_t max_textures_v{16};
		auto const pool_sizes = std::array{
			vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, max_textures_v},
		};
		auto dpci = vk::DescriptorPoolCreateInfo{};
		dpci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		dpci.maxSets = max_textures_v;
		dpci.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
		dpci.pPoolSizes = pool_sizes.data();
		auto pool = m_device->createDescriptorPoolUnique(dpci);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
		// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

		ImGui::StyleColorsDark();

		static auto const load_vk_func = +[](char const* name, void* user_data) {
			return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(*static_cast<vk::Instance*>(user_data), name);
		};
		ImGui_ImplVulkan_LoadFunctions(vk_api_v, load_vk_func, &*m_instance);

		ImGui_ImplGlfw_InitForVulkan(m_window.get(), true);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = *m_instance;
		init_info.PhysicalDevice = m_gpu.device;
		init_info.Device = *m_device;
		init_info.QueueFamily = m_gpu.queue_family;
		init_info.Queue = m_queue;
		init_info.DescriptorPool = *pool;
		init_info.Subpass = 0;
		init_info.MinImageCount = 2;
		init_info.ImageCount = 2;
		init_info.MSAASamples = static_cast<VkSampleCountFlagBits>(1);
		init_info.RenderPass = *m_render_pass;

		ImGui_ImplVulkan_Init(&init_info);
		ImGui_ImplVulkan_CreateFontsTexture();

		m_imgui = std::unique_ptr<DearImGui, Deleter>{new DearImGui{.descriptor_pool = std::move(pool)}};
	}

	auto acquire_next_image() -> std::optional<vk::ImageView> {
		auto const framebuffer = to_vk_extent(get_framebuffer_size());
		if (framebuffer.width == 0 || framebuffer.height == 0) { return {}; }

		static constexpr auto wait_timeout_v = std::chrono::nanoseconds{std::chrono::seconds{3}};

		if (m_device->waitForFences(*m_frame.drawn, vk::True, wait_timeout_v.count()) != vk::Result::eSuccess) {
			throw Error{"Failed to wait for render fence"};
		}

		auto image_index = std::uint32_t{};
		auto const result =
			m_device->acquireNextImageKHR(*m_swapchain.swapchain, max_timeout_v, *m_frame.draw, {}, &image_index);
		if (result == vk::Result::eErrorOutOfDateKHR || framebuffer != m_swapchain.create_info.imageExtent) {
			recreate_swapchain(framebuffer);
			return {};
		}

		if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
			throw Error{"failed to acquire next image"};
		}

		m_device->resetFences(*m_frame.drawn);
		m_swapchain.image_index = image_index;
		return *m_swapchain.image_views.at(image_index);
	}

	void submit_and_present() {
		auto const framebuffer = to_vk_extent(get_framebuffer_size());
		if (framebuffer.width == 0 || framebuffer.height == 0) { return; }
		if (!m_swapchain.image_index) { return; }

		auto si = vk::SubmitInfo{};
		static constexpr vk::PipelineStageFlags wdsm = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		si.pCommandBuffers = &m_frame.command.buffer;
		si.commandBufferCount = 1;
		si.pWaitSemaphores = &*m_frame.draw;
		si.waitSemaphoreCount = 1;
		si.pWaitDstStageMask = &wdsm;
		si.pSignalSemaphores = &*m_present_sems.at(*m_swapchain.image_index);
		si.signalSemaphoreCount = 1;
		auto result = m_queue.submit(1, &si, *m_frame.drawn);

		auto pi = vk::PresentInfoKHR{};
		pi.pImageIndices = &*m_swapchain.image_index;
		pi.pSwapchains = &*m_swapchain.swapchain;
		pi.pWaitSemaphores = si.pSignalSemaphores;
		pi.waitSemaphoreCount = 1;
		pi.pSwapchains = &*m_swapchain.swapchain;
		pi.swapchainCount = 1;
		result = m_queue.presentKHR(&pi);

		m_swapchain.image_index.reset();

		switch (result) {
		case vk::Result::eSuccess:
		case vk::Result::eSuboptimalKHR: return;
		case vk::Result::eErrorOutOfDateKHR: recreate_swapchain(framebuffer); return;
		default: break;
		}

		throw Error{"failed to present swapchain image"};
	}

	auto recreate_swapchain(vk::Extent2D const framebuffer) -> bool {
		if (framebuffer.width <= 0 || framebuffer.height <= 0) { return false; }

		auto info = m_swapchain.create_info;
		info.surface = *m_surface;
		auto const caps = m_gpu.device.getSurfaceCapabilitiesKHR(*m_surface);
		info.imageExtent = image_extent(caps, framebuffer);
		info.minImageCount = image_count(caps);
		info.oldSwapchain = m_swapchain.swapchain.get();

		m_device->waitIdle();
		auto new_swapchain = m_device->createSwapchainKHRUnique(info);
		if (!new_swapchain) { return false; }

		auto count = std::uint32_t{};
		if (m_device->getSwapchainImagesKHR(*new_swapchain, &count, nullptr) != vk::Result::eSuccess) { return false; }

		m_swapchain.swapchain = std::move(new_swapchain);
		m_swapchain.create_info = info;
		m_swapchain.images = m_device->getSwapchainImagesKHR(*m_swapchain.swapchain);
		m_swapchain.image_views.clear();
		m_swapchain.image_views.reserve(m_swapchain.images.size());
		for (auto const image : m_swapchain.images) {
			m_swapchain.image_views.push_back(
				MakeImageView{.image = image, .format = m_swapchain.create_info.imageFormat}(*m_device));
		}

		m_frame.draw = m_device->createSemaphoreUnique({}); // reset signaled semaphore.
		// recreate present semaphores since image count might have changed.
		m_present_sems.clear();
		m_present_sems.resize(m_swapchain.images.size());
		for (auto& semaphore : m_present_sems) { semaphore = m_device->createSemaphoreUnique({}); }
		m_swapchain.image_index.reset();

		return true;
	}

	auto create_framebuffer(vk::ImageView const render_target) const -> vk::UniqueFramebuffer {
		auto fci = vk::FramebufferCreateInfo{};
		fci.renderPass = *m_render_pass;
		fci.pAttachments = &render_target;
		fci.attachmentCount = 1;
		fci.width = m_swapchain.create_info.imageExtent.width;
		fci.height = m_swapchain.create_info.imageExtent.height;
		fci.layers = 1;
		return m_device->createFramebufferUnique(fci);
	}

	UniqueWindow m_window{};
	vk::UniqueInstance m_instance{};
	vk::UniqueSurfaceKHR m_surface{};
	Gpu m_gpu{};
	std::uint32_t m_queue_family{};
	vk::UniqueDevice m_device{};
	vk::Queue m_queue{};

	Swapchain m_swapchain{};
	std::vector<vk::UniqueSemaphore> m_present_sems{};
	Frame m_frame{};
	vk::UniqueRenderPass m_render_pass{};
	vk::UniqueFramebuffer m_framebuffer{};
	std::unique_ptr<DearImGui, Deleter> m_imgui{};

	std::unique_ptr<DeviceBlocker, Deleter> m_blocker{};
};

void Context::Deleter::operator()(Impl* ptr) const noexcept { std::default_delete<Impl>{}(ptr); }

auto Context::create_window(ImVec2 const size, char const* title) -> UniqueWindow {
	if (glfwInit() != GLFW_TRUE) { throw Error{"Failed to initialize GLFW"}; }
	if (glfwVulkanSupported() != GLFW_TRUE) {
		glfwTerminate();
		throw Error{"Vulkan unsupported"};
	}

	auto const width = static_cast<int>(size.x);
	auto const height = static_cast<int>(size.y);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	auto ret = std::unique_ptr<GLFWwindow, WindowDeleter>{glfwCreateWindow(width, height, title, nullptr, nullptr)};
	if (!ret) { throw Error{"Failed to create Window"}; }

	return ret;
}

Context::Context(UniqueWindow window) : m_impl(new Impl{std::move(window)}) {}

auto Context::next_frame() -> bool {
	assert(m_impl);
	return m_impl->next_frame();
}

void Context::render(ImVec4 const& clear) {
	assert(m_impl);
	m_impl->render(clear);
}

void Context::close() {
	assert(m_impl);
	m_impl->close();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Context::rebuild_imgui_fonts() {
	if (auto* atlas = ImGui::GetIO().Fonts; !atlas->TexReady) { atlas->Build(); }
	ImGui_ImplVulkan_CreateFontsTexture();
}

auto Context::get_window() const -> GLFWwindow* {
	assert(m_impl);
	return m_impl->get_window();
}

auto Context::get_framebuffer_size() const -> ImVec2 {
	assert(m_impl);
	return m_impl->get_framebuffer_size();
}
} // namespace gvdi
