#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <gvdi/app.hpp>
#include <gvdi/error.hpp>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <format>
#include <optional>
#include <sstream>

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace gvdi {
namespace {
using namespace std::chrono_literals;

constexpr auto vk_api_v = VK_API_VERSION_1_2;

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

struct Glfw {
	[[nodiscard]] static auto instance_extensions() -> std::vector<char const*> {
		auto count = std::uint32_t{};
		auto const* extensions_ptr = glfwGetRequiredInstanceExtensions(&count);
		auto const extensions = std::span{extensions_ptr, count};
		return {extensions.begin(), extensions.end()};
	}

	[[nodiscard]] static auto framebuffer_extent(GLFWwindow* window) -> vk::Extent2D {
		auto width = int{};
		auto height = int{};
		glfwGetFramebufferSize(window, &width, &height);
		return vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
	}

	Glfw(Glfw const&) = delete;
	Glfw(Glfw&&) = delete;
	auto operator=(Glfw const&) = delete;
	auto operator=(Glfw&&) = delete;

	explicit Glfw() {
		if (glfwInit() != GLFW_TRUE) { throw Error{"Failed to initialize GLFW"}; }
	}

	~Glfw() { glfwTerminate(); }
};

struct DearImGui {
	DearImGui(DearImGui const&) = delete;
	DearImGui(DearImGui&&) = delete;
	auto operator=(DearImGui const&) = delete;
	auto operator=(DearImGui&&) = delete;

	explicit DearImGui(GLFWwindow* window, vk::Instance instance, vk::PhysicalDevice physical_device, vk::Device device,
					   std::uint32_t queue_family, vk::Queue queue, vk::RenderPass render_pass)
		: m_device(device) {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGui::StyleColorsDark();

		static auto const load_vk_func = +[](char const* name, void* user_data) {
			return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(*static_cast<vk::Instance*>(user_data), name);
		};
		ImGui_ImplVulkan_LoadFunctions(vk_api_v, load_vk_func, &instance);

		ImGui_ImplGlfw_InitForVulkan(window, true);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = instance;
		init_info.PhysicalDevice = physical_device;
		init_info.Device = device;
		init_info.QueueFamily = queue_family;
		init_info.Queue = queue;
		init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
		init_info.Subpass = 0;
		init_info.MinImageCount = 2;
		init_info.ImageCount = 2;
		init_info.MSAASamples = static_cast<VkSampleCountFlagBits>(1);
		init_info.RenderPass = render_pass;

		ImGui_ImplVulkan_Init(&init_info);
	}

	~DearImGui() {
		m_device.waitIdle();
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void begin_frame() {
		if (m_state == State::Begun) { end_frame(); }
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		m_state = State::Begun;
	}

	void end_frame() {
		if (m_state == State::Ended) { return; }
		// ImGui::Render calls ImGui::EndFrame
		ImGui::Render();
		m_state = State::Ended;
	}

  private:
	enum class State : std::int8_t { Ended, Begun };

	vk::Device m_device{};
	State m_state{State::Ended};
};

struct Vulkan {
	Vulkan(Vulkan const&) = delete;
	Vulkan(Vulkan&&) = delete;
	auto operator=(Vulkan const&) = delete;
	auto operator=(Vulkan&&) = delete;

	explicit Vulkan(GLFWwindow* window) {
		create_instance();
		create_surface(window);
		select_physical_device();
		create_device();
		create_swapchain(Glfw::framebuffer_extent(window));
		create_render_pass();
	}

	~Vulkan() {
		if (!m_device) { return; }
		m_device->waitIdle();
	}

	void create_dear_imgui(std::optional<DearImGui>& out, GLFWwindow* window) {
		out.emplace(window, *m_instance, m_physical_device, *m_device, m_queue_family, m_queue, *m_render_pass);
	}

	template <typename Func>
	void execute_pass(vk::Extent2D const framebuffer, ImVec4 const& clear, Func render) {
		if (!begin_pass(framebuffer, clear)) { return; }
		render(m_command_buffer);
		end_pass(framebuffer);
	}

	void recreate_swapchain(vk::SurfaceCapabilitiesKHR const& caps, vk::Extent2D const image_extent) {
		assert(image_extent.width > 0 && image_extent.height > 0);
		m_swapchain.create_info.imageExtent = image_extent;
		m_swapchain.create_info.minImageCount = Swapchain::image_count(caps);
		m_swapchain.recreate(*m_device);
	}

  private:
	static constexpr auto is_linear(vk::Format const format) {
		using enum vk::Format;
		constexpr auto linear_formats_v = std::array{eR8G8B8A8Unorm, eR8G8B8A8Snorm,	   eB8G8R8A8Unorm,
													 eB8G8R8A8Snorm, eA8B8G8R8UnormPack32, eA8B8G8R8SnormPack32};
		return std::ranges::find(linear_formats_v, format) != linear_formats_v.end();
	}

	static constexpr auto select_format(std::span<vk::SurfaceFormatKHR const> available) -> vk::SurfaceFormatKHR {
		for (auto const format : available) {
			if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
				if (is_linear(format.format)) { return format; }
			}
		}
		return available.front();
	}

	struct Swapchain {
		static constexpr auto image_extent(vk::SurfaceCapabilitiesKHR const& caps, vk::Extent2D const extent) noexcept
			-> vk::Extent2D {
			constexpr auto limitless_v = std::numeric_limits<std::uint32_t>::max();
			if (caps.currentExtent.width < limitless_v && caps.currentExtent.height < limitless_v) {
				return caps.currentExtent;
			}
			auto const x = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
			auto const y = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
			return vk::Extent2D{x, y};
		}

		static constexpr auto image_count(vk::SurfaceCapabilitiesKHR const& caps) noexcept -> std::uint32_t {
			if (caps.maxImageCount < caps.minImageCount) { return std::max(3u, caps.minImageCount); }
			return std::clamp(3u, caps.minImageCount, caps.maxImageCount);
		}

		void setup_create_info(vk::SurfaceKHR const surface, std::uint32_t const queue_family,
							   vk::SurfaceFormatKHR const& format) {
			create_info.setImageArrayLayers(1)
				.setPresentMode(vk::PresentModeKHR::eFifo)
				.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
				.setSurface(surface)
				.setQueueFamilyIndices(queue_family)
				.setImageColorSpace(format.colorSpace)
				.setImageFormat(format.format);
		}

		void recreate(vk::Device const device) {
			create_info.oldSwapchain = *swapchain;
			device.waitIdle();
			swapchain = device.createSwapchainKHRUnique(create_info);
			images = device.getSwapchainImagesKHR(*swapchain);
			image_views.clear();
			image_views.reserve(images.size());
			auto ivci = vk::ImageViewCreateInfo{};
			ivci.setViewType(vk::ImageViewType::e2D)
				.setFormat(create_info.imageFormat)
				.setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
			for (auto const image : images) {
				ivci.setImage(image);
				image_views.push_back(device.createImageViewUnique(ivci));
			}
			present_semaphores.clear();
			present_semaphores.resize(images.size());
			for (auto& semaphore : present_semaphores) { semaphore = device.createSemaphoreUnique({}); }
		}

		vk::SwapchainCreateInfoKHR create_info{};
		vk::UniqueSwapchainKHR swapchain{};
		std::vector<vk::Image> images{};
		std::vector<vk::UniqueImageView> image_views{};
		std::vector<vk::UniqueSemaphore> present_semaphores{};
	};

	void create_instance() {
		VULKAN_HPP_DEFAULT_DISPATCHER.init();
		auto const api_version = vk::enumerateInstanceVersion();
		if (api_version < vk_api_v) { throw Error{"Vulkan 1.2 not supported by loader"}; }

		auto ici = vk::InstanceCreateInfo{};
		auto const version = to_vk_version(build_version_v);
		auto vai = vk::ApplicationInfo{};
		vai.setApiVersion(vk_api_v).setApplicationVersion(version);
		ici.setPApplicationInfo(&vai);
		auto extensions = Glfw::instance_extensions();
#if defined(__APPLE__)
		ici.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
		ici.setPEnabledExtensionNames(extensions);

		try {
			m_instance = vk::createInstanceUnique(ici);
		} catch (vk::LayerNotPresentError const& e) {
			ici.enabledLayerCount = 0;
			m_instance = vk::createInstanceUnique(ici);
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_instance);
	}

	void create_surface(GLFWwindow* window) {
		VkSurfaceKHR surface{};
		auto const result = glfwCreateWindowSurface(*m_instance, window, nullptr, &surface);
		if (result != VK_SUCCESS || !surface) { throw Error{"Failed to create Window Surface"}; }
		m_surface = vk::UniqueSurfaceKHR{surface, *m_instance};
	}

	void select_physical_device() {
		auto const get_queue_family = [surface = *m_surface](vk::PhysicalDevice const& device,
															 std::uint32_t& out_family) {
			static constexpr auto queue_flags_v = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
			auto const properties = device.getQueueFamilyProperties();
			for (std::uint32_t family = 0; family < static_cast<std::uint32_t>(properties.size()); ++family) {
				if (device.getSurfaceSupportKHR(family, surface) == 0) { continue; }
				if (!(properties[family].queueFlags & queue_flags_v)) { continue; }
				out_family = family;
				return true;
			}
			return false;
		};

		for (auto const& device : m_instance->enumeratePhysicalDevices()) {
			auto const properties = device.getProperties();
			if (properties.apiVersion < vk_api_v) { continue; }
			if (!get_queue_family(device, m_queue_family)) { continue; }
			m_physical_device = device;
			m_gpu_name = properties.deviceName.data();
			return;
		}

		throw Error{"Failed to select suitable Vulkan Physical Device (GPU)"};
	}

	void create_device() {
		static constexpr float priority_v = 1.0f;
		static constexpr std::array required_extensions_v = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if defined(__APPLE__)
															 VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#endif
		};

		auto const available_extensions = m_physical_device.enumerateDeviceExtensionProperties();
		for (auto const* ext : required_extensions_v) {
			auto const found = [ext](vk::ExtensionProperties const& props) {
				return std::string_view{props.extensionName} == ext;
			};
			if (std::ranges::find_if(available_extensions, found) == available_extensions.end()) {
				auto const err =
					std::format("Required extension '", ext, "' not supported by selected GPU '", m_gpu_name, "'");
				throw Error{err};
			}
		}

		auto qci = vk::DeviceQueueCreateInfo{};
		qci.setQueueFamilyIndex(m_queue_family).setQueueCount(1).setQueuePriorities(priority_v);
		auto dci = vk::DeviceCreateInfo{};
		dci.setQueueCreateInfos(qci).setPEnabledExtensionNames(required_extensions_v);
		m_device = m_physical_device.createDeviceUnique(dci);
		m_queue = m_device->getQueue(m_queue_family, 0);

		VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_device);
	}

	void create_swapchain(vk::Extent2D const extent) {
		auto const format = select_format(m_physical_device.getSurfaceFormatsKHR(*m_surface));
		m_swapchain.setup_create_info(*m_surface, m_queue_family, format);
		auto const caps = m_physical_device.getSurfaceCapabilitiesKHR(*m_surface);
		auto const image_extent = Swapchain::image_extent(caps, extent);
		recreate_swapchain(caps, image_extent);
	}

	void create_render_pass() {
		m_render_fence = m_device->createFenceUnique(vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
		auto rpci = vk::RenderPassCreateInfo{};
		auto sd = vk::SubpassDescription{};
		auto ar = vk::AttachmentReference{};
		ar.setAttachment(0).setLayout(vk::ImageLayout::eColorAttachmentOptimal);
		sd.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics).setColorAttachments(ar);
		auto ad = vk::AttachmentDescription{};
		ad.setSamples(vk::SampleCountFlagBits::e1)
			.setLoadOp(vk::AttachmentLoadOp::eClear)
			.setStoreOp(vk::AttachmentStoreOp::eStore)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setFinalLayout(vk::ImageLayout::ePresentSrcKHR)
			.setFormat(m_swapchain.create_info.imageFormat);
		rpci.setSubpasses(sd).setAttachments(ad);
		m_render_pass = m_device->createRenderPassUnique(rpci);

		m_draw_semaphore = m_device->createSemaphoreUnique({});

		auto cpci = vk::CommandPoolCreateInfo{};
		cpci.setQueueFamilyIndex(m_queue_family)
			.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient);
		m_command_pool = m_device->createCommandPoolUnique(cpci);
		auto cbai = vk::CommandBufferAllocateInfo{};
		cbai.setLevel(vk::CommandBufferLevel::ePrimary).setCommandBufferCount(1).setCommandPool(*m_command_pool);
		m_command_buffer = m_device->allocateCommandBuffers(cbai).front();
	}

	[[nodiscard]] auto create_framebuffer(vk::ImageView const render_target) const -> vk::UniqueFramebuffer {
		auto fci = vk::FramebufferCreateInfo{};
		fci.setLayers(1)
			.setRenderPass(*m_render_pass)
			.setAttachments(render_target)
			.setWidth(m_swapchain.create_info.imageExtent.width)
			.setHeight(m_swapchain.create_info.imageExtent.height);
		return m_device->createFramebufferUnique(fci);
	}

	auto begin_pass(vk::Extent2D const framebuffer, ImVec4 const& clear) -> bool {
		if (framebuffer.width == 0 || framebuffer.height == 0) { return false; }

		static constexpr auto max_timeout_v = static_cast<std::uint64_t>(std::chrono::nanoseconds(2s).count());

		auto result = m_device->waitForFences(*m_render_fence, vk::True, max_timeout_v);
		if (result != vk::Result::eSuccess) { throw Error{"Failed to wait for Vulkan render Fence"}; }
		m_device->resetFences(*m_render_fence);

		auto const caps = m_physical_device.getSurfaceCapabilitiesKHR(*m_surface);
		auto const image_extent = Swapchain::image_extent(caps, framebuffer);
		if (image_extent != m_swapchain.create_info.imageExtent) { recreate_swapchain(caps, image_extent); }

		auto image_index = std::uint32_t{};
		result =
			m_device->acquireNextImageKHR(*m_swapchain.swapchain, max_timeout_v, *m_draw_semaphore, {}, &image_index);
		if (result == vk::Result::eErrorOutOfDateKHR) {
			recreate_swapchain(caps, image_extent);
			return false;
		}
		if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
			throw Error{"Failed to acquire Vulkan Swapchain Image"};
		}

		m_image_index = image_index;

		m_framebuffer = create_framebuffer(*m_swapchain.image_views.at(image_index));
		auto render_area = vk::Rect2D{};
		render_area.setExtent(m_swapchain.create_info.imageExtent);

		auto const vk_clear_colour =
			std::array<vk::ClearValue, 1>{vk::ClearColorValue{clear.x, clear.y, clear.z, clear.w}};
		auto rpbi = vk::RenderPassBeginInfo{};
		rpbi.setRenderPass(*m_render_pass)
			.setFramebuffer(*m_framebuffer)
			.setRenderArea(render_area)
			.setClearValues(vk_clear_colour);

		m_command_buffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
		m_command_buffer.beginRenderPass(rpbi, vk::SubpassContents::eInline);
		return true;
	}

	void end_pass(vk::Extent2D const framebuffer) {
		assert(m_image_index);
		auto const image_index = *std::exchange(m_image_index, {});

		m_command_buffer.endRenderPass();
		m_command_buffer.end();

		auto si = vk::SubmitInfo{};
		static constexpr vk::PipelineStageFlags wdsm = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		auto const present_semaphore = *m_swapchain.present_semaphores.at(image_index);
		si.setCommandBuffers(m_command_buffer)
			.setWaitSemaphores(*m_draw_semaphore)
			.setWaitDstStageMask(wdsm)
			.setSignalSemaphores(present_semaphore);
		auto result = m_queue.submit(1, &si, *m_render_fence);
		if (result != vk::Result::eSuccess) { throw Error{"Failed to submit Vulkan render Command Buffer"}; }

		auto pi = vk::PresentInfoKHR{};
		pi.setSwapchains(*m_swapchain.swapchain).setImageIndices(image_index).setWaitSemaphores(present_semaphore);
		result = m_queue.presentKHR(&pi);
		if (result == vk::Result::eErrorOutOfDateKHR) {
			auto const caps = m_physical_device.getSurfaceCapabilitiesKHR(*m_surface);
			auto const image_extent = Swapchain::image_extent(caps, framebuffer);
			recreate_swapchain(caps, image_extent);
		}
	}

	vk::UniqueInstance m_instance{};
	vk::UniqueSurfaceKHR m_surface{};
	vk::PhysicalDevice m_physical_device{};
	std::string m_gpu_name{};
	std::uint32_t m_queue_family{};
	vk::UniqueDevice m_device{};
	vk::Queue m_queue{};

	Swapchain m_swapchain{};
	vk::UniqueRenderPass m_render_pass{};
	vk::UniqueSemaphore m_draw_semaphore{};
	vk::UniqueFence m_render_fence{};
	vk::UniqueCommandPool m_command_pool{};
	vk::CommandBuffer m_command_buffer{};

	std::optional<std::uint32_t> m_image_index{};
	vk::UniqueFramebuffer m_framebuffer{};
};
} // namespace

class App::Impl {
  public:
	explicit Impl(App& app) : m_app(app) {}

	void run() {
		init();
		while (glfwWindowShouldClose(m_window.get()) == GLFW_FALSE) {
			glfwPollEvents();
			m_dear_imgui->begin_frame();
			m_app.update();
			m_dear_imgui->end_frame();
			auto const render = [](vk::CommandBuffer const command_buffer) {
				if (auto* draw_data = ImGui::GetDrawData()) {
					ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
				}
			};
			m_vulkan->execute_pass(Glfw::framebuffer_extent(m_window.get()), {}, render);
		}
		m_app.post_run();
		deinit();
	}

	[[nodiscard]] auto get_window() const -> GLFWwindow* { return m_window.get(); }

  private:
	struct Deleter {
		void operator()(GLFWwindow* ptr) const noexcept { glfwDestroyWindow(ptr); }
	};

	void init() {
		m_app.pre_init();
		create_window();
		m_vulkan.emplace(m_window.get());
		m_vulkan->create_dear_imgui(m_dear_imgui, m_window.get());
		m_app.post_init();
	}

	void deinit() {
		m_dear_imgui.reset();
		m_vulkan.reset();
		m_window.reset();
		m_glfw.reset();
	}

	void create_window() {
		m_glfw.emplace();
		if (glfwVulkanSupported() != GLFW_TRUE) { throw Error{"GLFW: Vukan not supported"}; }
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_window.reset(m_app.create_window());
		if (!m_window) { throw Error{"Failed to create Window"}; }
	}

	App& m_app;

	std::optional<Glfw> m_glfw{};
	std::unique_ptr<GLFWwindow, Deleter> m_window{};
	std::optional<Vulkan> m_vulkan{};
	std::optional<DearImGui> m_dear_imgui{};
};

void App::Deleter::operator()(Impl* ptr) const noexcept { std::default_delete<Impl>{}(ptr); }

App::App() : m_impl(new Impl{*this}) {}

void App::run() noexcept(false) {
	if (!m_impl) { return; }
	m_impl->run();
}

auto App::get_window() const -> GLFWwindow* {
	if (!m_impl) { return nullptr; }
	return m_impl->get_window();
}

auto App::create_window() -> GLFWwindow* { return glfwCreateWindow(800, 600, "gvdi App", nullptr, nullptr); }
} // namespace gvdi
