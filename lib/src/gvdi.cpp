#include "gvdi/app.hpp"
#include "gvdi/build_version.hpp"
#include "gvdi/exception.hpp"
#include "gvdi/gpu.hpp"
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
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

[[nodiscard]] constexpr auto to_gpu_type(vk::PhysicalDeviceType const in) -> gpu::Type {
	switch (in) {
	case vk::PhysicalDeviceType::eDiscreteGpu: return gpu::Type::Discrete;
	case vk::PhysicalDeviceType::eIntegratedGpu: return gpu::Type::Integrated;
	case vk::PhysicalDeviceType::eCpu: return gpu::Type::Cpu;
	case vk::PhysicalDeviceType::eVirtualGpu: return gpu::Type::Virtual;
	default: return gpu::Type::Other;
	}
}

[[nodiscard]] auto get_viable_queue_family(vk::PhysicalDevice const& device, vk::SurfaceKHR const surface) -> std::optional<std::uint32_t> {
	static constexpr auto queue_flags_v = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
	auto const family_properties = device.getQueueFamilyProperties();
	for (std::uint32_t family = 0; family < std::uint32_t(family_properties.size()); ++family) {
		if (device.getSurfaceSupportKHR(family, surface) == 0) { continue; }
		if (!(family_properties[family].queueFlags & queue_flags_v)) { continue; }
		return family;
	}
	return {};
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
		if (glfwInit() != GLFW_TRUE) { throw Exception{"Failed to initialize GLFW"}; }
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

struct Surface {
	explicit Surface(GLFWwindow* window) : window(window) {
		create_instance();
		create_surface();
	}

	void create_instance() {
		VULKAN_HPP_DEFAULT_DISPATCHER.init();
		auto const api_version = vk::enumerateInstanceVersion();
		if (api_version < vk_api_v) { throw Exception{"Vulkan 1.2 not supported by loader"}; }

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
			instance = vk::createInstanceUnique(ici);
		} catch (vk::LayerNotPresentError const& e) {
			ici.enabledLayerCount = 0;
			instance = vk::createInstanceUnique(ici);
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
	}

	void create_surface() {
		VkSurfaceKHR raw_surface{};
		auto const result = glfwCreateWindowSurface(*instance, window, nullptr, &raw_surface);
		if (result != VK_SUCCESS || !raw_surface) { throw Exception{"Failed to create Window Surface"}; }
		surface = vk::UniqueSurfaceKHR{raw_surface, *instance};
	}

	[[nodiscard]] auto get_capabilities(vk::PhysicalDevice const device) const -> vk::SurfaceCapabilitiesKHR {
		return device.getSurfaceCapabilitiesKHR(*surface);
	}

	GLFWwindow* window{};
	vk::UniqueInstance instance{};
	vk::UniqueSurfaceKHR surface{};
};

struct ViableDevice {
	[[nodiscard]] static auto build(vk::Instance const instance, vk::SurfaceKHR const surface) -> std::vector<ViableDevice> {
		auto ret = std::vector<ViableDevice>{};
		for (auto const& device : instance.enumeratePhysicalDevices()) {
			auto const properties = device.getProperties();
			if (properties.apiVersion < vk_api_v) { continue; }

			auto const queue_family = get_viable_queue_family(device, surface);
			if (!queue_family) { continue; }

			auto viable = ViableDevice{.device = device, .properties = device.getProperties(), .family = *queue_family};
			viable.type = to_gpu_type(viable.properties.deviceType);
			ret.push_back(viable);
		}
		return ret;
	}

	vk::PhysicalDevice device{};
	vk::PhysicalDeviceProperties properties{};
	std::uint32_t family{};
	gpu::Type type{};
};

struct PhysicalDevice {
	[[nodiscard]] static auto select(std::span<gpu::Type const> desired, Surface const& surface) -> PhysicalDevice {
		if (desired.empty()) { desired = App::gpu_priority_v; }

		auto const viables = ViableDevice::build(*surface.instance, *surface.surface);
		if (viables.empty()) { throw Exception{"Failed to find viable Vulkan Physical Device (GPU)"}; }

		ViableDevice const* selected{};

		for (auto const type : desired) {
			auto const pred = [type](ViableDevice const& v) { return v.type == type; };
			if (auto const it = std::ranges::find_if(viables, pred); it != viables.end()) {
				selected = &*it;
				break;
			}
		}

		if (!selected) { selected = &viables.front(); }

		return PhysicalDevice{
			.device = selected->device,
			.queue_family = selected->family,
			.type = selected->type,
			.name = selected->properties.deviceName.data(),
		};
	}

	vk::PhysicalDevice device{};
	std::uint32_t queue_family{};
	gpu::Type type{};
	std::string name{};
};

struct Vulkan {
	Vulkan(Vulkan const&) = delete;
	Vulkan(Vulkan&&) = delete;
	auto operator=(Vulkan const&) = delete;
	auto operator=(Vulkan&&) = delete;

	explicit Vulkan(Surface surface, PhysicalDevice gpu) : m_surface(std::move(surface)), m_gpu(std::move(gpu)) {
		create_device();
		create_swapchain(Glfw::framebuffer_extent(m_surface.window));
		create_render_pass();
	}

	~Vulkan() { wait_idle(); }

	void create_dear_imgui(std::optional<DearImGui>& out, GLFWwindow* window) {
		out.emplace(window, *m_surface.instance, m_gpu.device, *m_device, m_gpu.queue_family, m_queue, *m_render_pass);
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

	[[nodiscard]] auto get_gpu_info() const -> gpu::Info { return gpu::Info{.type = m_gpu.type, .name = m_gpu.name}; }

	void wait_idle() const {
		if (!m_device) { return; }
		m_device->waitIdle();
	}

  private:
	static constexpr auto is_linear(vk::Format const format) {
		using enum vk::Format;
		constexpr auto linear_formats_v =
			std::array{eR8G8B8A8Unorm, eR8G8B8A8Snorm, eB8G8R8A8Unorm, eB8G8R8A8Snorm, eA8B8G8R8UnormPack32, eA8B8G8R8SnormPack32};
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
		static constexpr auto image_extent(vk::SurfaceCapabilitiesKHR const& caps, vk::Extent2D const extent) noexcept -> vk::Extent2D {
			constexpr auto limitless_v = std::numeric_limits<std::uint32_t>::max();
			if (caps.currentExtent.width < limitless_v && caps.currentExtent.height < limitless_v) { return caps.currentExtent; }
			auto const x = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
			auto const y = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
			return vk::Extent2D{x, y};
		}

		static constexpr auto image_count(vk::SurfaceCapabilitiesKHR const& caps) noexcept -> std::uint32_t {
			if (caps.maxImageCount < caps.minImageCount) { return std::max(3u, caps.minImageCount); }
			return std::clamp(3u, caps.minImageCount, caps.maxImageCount);
		}

		void setup_create_info(vk::SurfaceKHR const surface, std::uint32_t const queue_family, vk::SurfaceFormatKHR const& format) {
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

	void create_device() {
		static constexpr float priority_v = 1.0f;
		static constexpr std::array required_extensions_v = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if defined(__APPLE__)
															 VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#endif
		};

		auto const available_extensions = m_gpu.device.enumerateDeviceExtensionProperties();
		for (auto const* ext : required_extensions_v) {
			auto const found = [ext](vk::ExtensionProperties const& props) { return std::string_view{props.extensionName} == ext; };
			if (std::ranges::find_if(available_extensions, found) == available_extensions.end()) {
				throw Exception{std::format("Required extension '", ext, "' not supported by selected GPU '", m_gpu.name, "'")};
			}
		}

		auto qci = vk::DeviceQueueCreateInfo{};
		qci.setQueueFamilyIndex(m_gpu.queue_family).setQueueCount(1).setQueuePriorities(priority_v);
		auto dci = vk::DeviceCreateInfo{};
		dci.setQueueCreateInfos(qci).setPEnabledExtensionNames(required_extensions_v);
		m_device = m_gpu.device.createDeviceUnique(dci);
		m_queue = m_device->getQueue(m_gpu.queue_family, 0);

		VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_device);
	}

	void create_swapchain(vk::Extent2D const extent) {
		auto const format = select_format(m_gpu.device.getSurfaceFormatsKHR(*m_surface.surface));
		m_swapchain.setup_create_info(*m_surface.surface, m_gpu.queue_family, format);
		auto const caps = m_surface.get_capabilities(m_gpu.device);
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
		cpci.setQueueFamilyIndex(m_gpu.queue_family)
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
		if (result != vk::Result::eSuccess) { throw Exception{"Failed to wait for Vulkan render Fence"}; }
		m_device->resetFences(*m_render_fence);

		auto const caps = m_surface.get_capabilities(m_gpu.device);
		auto const image_extent = Swapchain::image_extent(caps, framebuffer);
		if (image_extent != m_swapchain.create_info.imageExtent) { recreate_swapchain(caps, image_extent); }

		auto image_index = std::uint32_t{};
		result = m_device->acquireNextImageKHR(*m_swapchain.swapchain, max_timeout_v, *m_draw_semaphore, {}, &image_index);
		if (result == vk::Result::eErrorOutOfDateKHR) {
			recreate_swapchain(caps, image_extent);
			return false;
		}
		if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
			throw Exception{"Failed to acquire Vulkan Swapchain Image"};
		}

		m_image_index = image_index;

		m_framebuffer = create_framebuffer(*m_swapchain.image_views.at(image_index));
		auto render_area = vk::Rect2D{};
		render_area.setExtent(m_swapchain.create_info.imageExtent);

		auto const vk_clear_colour = std::array<vk::ClearValue, 1>{vk::ClearColorValue{clear.x, clear.y, clear.z, clear.w}};
		auto rpbi = vk::RenderPassBeginInfo{};
		rpbi.setRenderPass(*m_render_pass).setFramebuffer(*m_framebuffer).setRenderArea(render_area).setClearValues(vk_clear_colour);

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
		if (result != vk::Result::eSuccess) { throw Exception{"Failed to submit Vulkan render Command Buffer"}; }

		auto pi = vk::PresentInfoKHR{};
		pi.setSwapchains(*m_swapchain.swapchain).setImageIndices(image_index).setWaitSemaphores(present_semaphore);
		result = m_queue.presentKHR(&pi);
		if (result == vk::Result::eErrorOutOfDateKHR) {
			auto const caps = m_surface.get_capabilities(m_gpu.device);
			auto const image_extent = Swapchain::image_extent(caps, framebuffer);
			recreate_swapchain(caps, image_extent);
		}
	}

	Surface m_surface;
	PhysicalDevice m_gpu{};
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

	void run_event_loop() {
		m_app.stage_initialize();

		m_app.stage_create();
		m_app.pre_event_loop();

		m_app.pre_first_frame();
		while (glfwWindowShouldClose(get_window()) == GLFW_FALSE) {
			glfwPollEvents();
			m_dear_imgui->begin_frame();
			m_app.update();
			m_dear_imgui->end_frame();
			auto const render = [](vk::CommandBuffer const command_buffer) {
				if (auto* draw_data = ImGui::GetDrawData()) { ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer); }
			};
			m_vulkan->execute_pass(Glfw::framebuffer_extent(get_window()), {}, render);

			if (m_reboot) {
				m_app.stage_reboot();
				m_reboot = false;
			}
		}

		m_vulkan->wait_idle();
		m_app.stage_destroy();
		m_app.post_event_loop();
	}

	[[nodiscard]] auto is_running() const -> bool { return m_window != nullptr; }

	[[nodiscard]] auto get_window() const -> GLFWwindow* { return m_window.get(); }

	[[nodiscard]] auto get_gpu_info() const -> gpu::Info {
		if (!m_vulkan) { return {}; }
		return m_vulkan->get_gpu_info();
	}

	[[nodiscard]] auto will_reboot() const -> bool { return m_reboot; }

	void schedule_reboot() {
		if (m_reboot || !is_running() || m_app.should_close_window()) { return; }
		m_reboot = true;
	}

	void stage_initialize() {
		m_glfw.emplace();
		if (glfwVulkanSupported() != GLFW_TRUE) { throw Exception{"GLFW: Vukan not supported"}; }
	}

	void stage_create() {
		create_window();
		create_vulkan();
		m_vulkan->create_dear_imgui(m_dear_imgui, get_window());
	}

	void stage_destroy() {
		if (!m_vulkan) { return; }
		m_vulkan->wait_idle();
		m_dear_imgui.reset();
		m_vulkan.reset();
		m_window.reset();
	}

	void create_window() {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_window.reset(m_app.create_glfw_window());
		if (!m_window) { throw Exception{"Failed to create GLFW Window"}; }
		glfwSetWindowUserPointer(get_window(), this);
		install_glfw_callbacks();
	}

  private:
	struct Deleter {
		void operator()(GLFWwindow* ptr) const noexcept { glfwDestroyWindow(ptr); }
	};

	void install_glfw_callbacks() const {
		static auto const self = [](GLFWwindow* window) -> Impl& { return *static_cast<Impl*>(glfwGetWindowUserPointer(window)); };
		auto* window = get_window();

		glfwSetWindowPosCallback(window, [](GLFWwindow* w, int x, int y) { self(w).m_app.on_window_reposition(x, y); });
		glfwSetWindowSizeCallback(window, [](GLFWwindow* w, int x, int y) { self(w).m_app.on_window_resize(x, y); });
		glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int x, int y) { self(w).m_app.on_framebuffer_resize(x, y); });
		glfwSetWindowCloseCallback(window, [](GLFWwindow* w) { self(w).m_app.on_window_close(); });
		glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int b) { self(w).m_app.on_window_focus(b == GLFW_TRUE); });
		glfwSetWindowIconifyCallback(window, [](GLFWwindow* w, int b) { self(w).m_app.on_window_iconify(b == GLFW_TRUE); });
		glfwSetWindowMaximizeCallback(window, [](GLFWwindow* w, int b) { self(w).m_app.on_window_maximize(b == GLFW_TRUE); });

		glfwSetKeyCallback(window, [](GLFWwindow* w, int k, int s, int a, int m) { self(w).on_key(k, s, a, m); });
		glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int codepoint) { self(w).m_app.on_character(codepoint); });

		glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) { self(w).m_app.on_cursor_reposition(x, y); });
		glfwSetCursorEnterCallback(window, [](GLFWwindow* w, int b) { self(w).m_app.on_cursor_enter(b == GLFW_TRUE); });
		glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int b, int a, int m) { self(w).on_mouse_button(b, a, m); });
		glfwSetScrollCallback(window, [](GLFWwindow* w, double x, double y) { self(w).m_app.on_mouse_scroll(x, y); });

		glfwSetDropCallback(window, [](GLFWwindow* w, int c, char const** p) { self(w).m_app.on_path_drop({p, std::size_t(c)}); });
	}

	void create_vulkan() {
		auto surface = Surface{get_window()};
		auto gpu = PhysicalDevice::select(m_app.get_gpu_type_priority(), surface);
		m_vulkan.emplace(std::move(surface), std::move(gpu));
	}

	void on_key(int const key, int const scancode, int const action, int const mods) {
		switch (action) {
		case GLFW_PRESS: m_app.on_key_press(key, scancode, mods); break;
		case GLFW_RELEASE: m_app.on_key_release(key, scancode, mods); break;
		case GLFW_REPEAT: m_app.on_key_repeat(key, scancode, mods); break;
		default: break;
		}
	}

	void on_mouse_button(int const button, int const action, int const mods) {
		switch (action) {
		case GLFW_PRESS: m_app.on_mouse_button_press(button, mods); break;
		case GLFW_RELEASE: m_app.on_mouse_button_release(button, mods); break;
		default: break;
		}
	}

	App& m_app;

	std::optional<Glfw> m_glfw{};
	std::unique_ptr<GLFWwindow, Deleter> m_window{};
	std::optional<Vulkan> m_vulkan{};
	std::optional<DearImGui> m_dear_imgui{};

	bool m_reboot{};
};

void App::Deleter::operator()(Impl* ptr) const noexcept { std::default_delete<Impl>{}(ptr); }

App::App() : m_impl(new Impl{*this}) {}

auto App::create_windowed_window(char const* title, int const width, int const height) -> GLFWwindow* {
	return glfwCreateWindow(width, height, title, nullptr, nullptr);
}

auto App::create_fullscreen_window(char const* title) -> GLFWwindow* {
	auto* monitor = glfwGetPrimaryMonitor();
	auto const* video_mode = glfwGetVideoMode(monitor);
	glfwWindowHint(GLFW_RED_BITS, video_mode->redBits);
	glfwWindowHint(GLFW_GREEN_BITS, video_mode->greenBits);
	glfwWindowHint(GLFW_BLUE_BITS, video_mode->blueBits);
	glfwWindowHint(GLFW_REFRESH_RATE, video_mode->refreshRate);
	return glfwCreateWindow(video_mode->width, video_mode->height, title, monitor, nullptr);
}

void App::run_event_loop() noexcept(false) { m_impl->run_event_loop(); }

void App::stage_initialize() { m_impl->stage_initialize(); }

void App::stage_create() { m_impl->stage_create(); }

void App::stage_reboot() {
	stage_destroy();
	stage_create();
	pre_first_frame();
}

void App::stage_destroy() { m_impl->stage_destroy(); }

auto App::is_running() const -> bool { return m_impl->is_running(); }

auto App::get_window() const -> GLFWwindow* { return m_impl->get_window(); }

auto App::get_gpu_info() const -> gpu::Info { return m_impl->get_gpu_info(); }

auto App::will_reboot() const -> bool { return m_impl->will_reboot(); }

void App::schedule_reboot() { m_impl->schedule_reboot(); }
} // namespace gvdi
