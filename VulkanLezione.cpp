#include <vulkan/vulkan.hpp>
#include <iostream>
#include <vector>
#include <fstream>
#include <deque>
#include <map>
#include <list>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

struct vertex_t 
{
    glm::vec3 pos;
    glm::vec3 col;
};

struct uniform_t
{
    glm::mat4 model;
};

size_t aligned_size(size_t sz, size_t alignment)
{
    return ((sz - 1) & alignment) + alignment;
}
template <typename T>
size_t aligned_size(const std::vector<T>& v, size_t alignment)
{
    return aligned_size(sizeof(T) * T.size(), alignment);
}
template <typename T, size_t N>
size_t aligned_size(const std::array<T, N>& v, size_t alignment)
{
    return aligned_size(sizeof(T) * N, alignment);
}

LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        return 0; // Everything is fine, continue with creation.
    case WM_CLOSE:
        PostQuitMessage(EXIT_SUCCESS);
        return 0;
    case WM_MOUSEMOVE:
        break;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

HWND create_window(int width, int height)
{
    WNDCLASS wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = TEXT("Vulkan Window");
    if (!RegisterClass(&wc))
        return NULL;
    RECT r = { 0, 0, width, height };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, false);
    return CreateWindow(wc.lpszClassName, TEXT("Vulkan"),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, nullptr);
}

std::pair<glm::ivec2, std::unique_ptr<uint8_t>> load_image(const char* path)
{
    int w, h, c;
    std::unique_ptr<uint8_t> data(stbi_load(path, &w, &h, &c, 4));
    return { glm::ivec2(w, h), std::move(data) };
}

vk::UniqueShaderModule load_shader(const vk::UniqueDevice& device, const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    size_t size = file.tellg();
    file.seekg(std::ios::beg);
    auto buffer = std::make_unique<char[]>(size);
    file.read(buffer.get(), size);

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = size;
    module_info.pCode = reinterpret_cast<uint32_t*>(buffer.get());
    return device->createShaderModuleUnique(module_info);
}

uint32_t find_memory(const vk::PhysicalDevice& physical_device, 
    const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags)
{
    static vk::PhysicalDeviceMemoryProperties mp = physical_device.getMemoryProperties();
    for (uint32_t mem_i = 0; mem_i < mp.memoryTypeCount; mem_i++)
        if ((1 << mem_i) & req.memoryTypeBits && (mp.memoryTypes[mem_i].propertyFlags & flags) == flags)
            return mem_i;
    throw std::runtime_error("find_memory failed");
}

int main()
{
    std::vector<const char*> inst_layers;
    inst_layers.emplace_back("VK_LAYER_KHRONOS_validation");
    std::vector<const char*> inst_extensions;
    inst_extensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
    inst_extensions.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

    vk::ApplicationInfo app_info;
    app_info.apiVersion = VK_VERSION_1_2;
    app_info.pApplicationName = "VulcanLezione";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 1);
    app_info.pEngineName = "Custom";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 1);
    vk::InstanceCreateInfo inst_info;
    inst_info.pApplicationInfo = &app_info;
    inst_info.setPEnabledLayerNames(inst_layers);
    inst_info.setPEnabledExtensionNames(inst_extensions);

    vk::UniqueInstance inst = vk::createInstanceUnique(inst_info);

    HWND hWnd = create_window(800, 600);
    vk::Win32SurfaceCreateInfoKHR surface_info;
    surface_info.hinstance = GetModuleHandle(NULL);
    surface_info.hwnd = hWnd;
    vk::UniqueSurfaceKHR surface = inst->createWin32SurfaceKHRUnique(surface_info);

    uint32_t device_family_index = 0;
    vk::PhysicalDevice physical_device;
    vk::UniqueDevice device;
    for (auto pd : inst->enumeratePhysicalDevices())
    {
        std::vector<vk::QueueFamilyProperties> families = pd.getQueueFamilyProperties();
        vk::PhysicalDeviceProperties props = pd.getProperties();
        for (int family_index = 0; family_index < families.size(); family_index++)
        {
            if (families[family_index].queueFlags & vk::QueueFlagBits::eGraphics
                && pd.getSurfaceSupportKHR(family_index, *surface)
                && props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            {
                std::cout << "Device trovato: " << props.deviceName << "\n";
        
                device_family_index = family_index;
                physical_device = pd;

                std::vector<const char*> device_layers;
                std::vector<const char*> device_extensions;
                device_extensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

                float queue_priority[1] = { 1.f };
                vk::DeviceQueueCreateInfo queue_info;
                queue_info.queueFamilyIndex = family_index;
                queue_info.queueCount = 1;
                queue_info.pQueuePriorities = queue_priority;

                vk::DeviceCreateInfo device_info;
                device_info.queueCreateInfoCount = 1;
                device_info.pQueueCreateInfos = &queue_info;
                device_info.setPEnabledExtensionNames(device_extensions);
                device_info.setPEnabledLayerNames(device_layers);
                device = pd.createDeviceUnique(device_info);

                break;
            }
        }
        if (device)
            break;
    }

    vk::SurfaceCapabilitiesKHR surface_caps = physical_device.getSurfaceCapabilitiesKHR(*surface);

    vk::SwapchainCreateInfoKHR swapchain_info;
    swapchain_info.surface = *surface;
    swapchain_info.minImageCount = 2;
    swapchain_info.imageFormat = vk::Format::eB8G8R8A8Unorm;
    swapchain_info.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    swapchain_info.imageExtent = surface_caps.currentExtent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
    swapchain_info.presentMode = vk::PresentModeKHR::eFifo;
    swapchain_info.clipped = true;
    vk::UniqueSwapchainKHR swapchain = device->createSwapchainKHRUnique(swapchain_info);

    vk::Queue q = device->getQueue(device_family_index, 0);
    vk::CommandPoolCreateInfo pool_info;
    pool_info.queueFamilyIndex = device_family_index;
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    vk::UniqueCommandPool cmd_pool = device->createCommandPoolUnique(pool_info);

    // Create Vertex and Index buffer
    uniform_t uniform_block;
    uniform_block.model = glm::scale(glm::vec3(0.5));

    std::vector<uint32_t> quad_indices{ 0, 1, 2, 0, 2, 3 };
    std::vector<vertex_t> quad_vertices{
        vertex_t{glm::vec3(-1, 1, 0), glm::vec3(1, 0, 0)},
        vertex_t{glm::vec3(-1,-1, 0), glm::vec3(1, 1, 0)},
        vertex_t{glm::vec3( 1,-1, 0), glm::vec3(0, 1, 1)},
        vertex_t{glm::vec3( 1, 1, 0), glm::vec3(1, 0, 1)},
    };
    size_t quad_indices_off = 0;
    size_t quad_indices_size = aligned_size(quad_indices.size() * sizeof(uint32_t), 0x100);
    size_t quad_vertices_off = quad_indices_off + quad_indices_size;
    size_t quad_vertices_size = aligned_size(quad_vertices.size() * sizeof(vertex_t), 0x100);
    size_t quad_uniform_off = quad_vertices_off + quad_vertices_size;
    size_t quad_uniform_size = aligned_size(sizeof(uniform_block), 0x100);
    vk::BufferCreateInfo quad_buffer_info;
    quad_buffer_info.size = quad_indices_size
        + quad_vertices_size
        + quad_uniform_size;
    quad_buffer_info.usage = vk::BufferUsageFlagBits::eIndexBuffer
        | vk::BufferUsageFlagBits::eVertexBuffer
        | vk::BufferUsageFlagBits::eUniformBuffer;
    vk::UniqueBuffer quad_buffer = device->createBufferUnique(quad_buffer_info);
    vk::MemoryRequirements quad_buffer_req = device->getBufferMemoryRequirements(*quad_buffer);
    uint32_t quad_memory_index = find_memory(physical_device, quad_buffer_req,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::MemoryAllocateInfo quad_buffer_mem_info;
    quad_buffer_mem_info.allocationSize = quad_buffer_req.size;
    quad_buffer_mem_info.memoryTypeIndex = quad_memory_index;
    vk::UniqueDeviceMemory quad_buffer_mem = device->allocateMemoryUnique(quad_buffer_mem_info);
    device->bindBufferMemory(*quad_buffer, *quad_buffer_mem, 0);

    if (uint8_t* ptr = reinterpret_cast<uint8_t*>(device->mapMemory(*quad_buffer_mem, 0, VK_WHOLE_SIZE)))
    {
        std::copy(quad_indices.begin(), quad_indices.end(), 
            reinterpret_cast<uint32_t*>(ptr + quad_indices_off));
        std::copy(quad_vertices.begin(), quad_vertices.end(), 
            reinterpret_cast<vertex_t*>(ptr + quad_vertices_off));
        *reinterpret_cast<uniform_t*>(ptr + quad_uniform_off) = uniform_block;
        device->unmapMemory(*quad_buffer_mem);
    }

    // Pipeline Layout
    std::vector<vk::DescriptorSetLayoutBinding> descrset_layout_bindings{
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex),
    };
    vk::DescriptorSetLayoutCreateInfo descrset_layout_info;
    descrset_layout_info.setBindings(descrset_layout_bindings);
    vk::UniqueDescriptorSetLayout descrset_layout = device->createDescriptorSetLayoutUnique(descrset_layout_info);
    vk::PipelineLayoutCreateInfo pipeline_layout_info;
    pipeline_layout_info.setSetLayouts(*descrset_layout);
    vk::UniquePipelineLayout pipeline_layout = device->createPipelineLayoutUnique(pipeline_layout_info);

    // Create RenderPass
    std::vector<vk::AttachmentDescription> renderpass_attachments(1);
    renderpass_attachments[0].format = swapchain_info.imageFormat;
    renderpass_attachments[0].samples = vk::SampleCountFlagBits::e1;
    renderpass_attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
    renderpass_attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
    renderpass_attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    renderpass_attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    renderpass_attachments[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
    renderpass_attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;
    std::vector<vk::SubpassDescription> renderpass_subpasses(1);
    vk::AttachmentReference renderpass_ref_coolor;
    renderpass_ref_coolor.attachment = 0;
    renderpass_ref_coolor.layout = vk::ImageLayout::eColorAttachmentOptimal;
    renderpass_subpasses[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    renderpass_subpasses[0].setColorAttachments(renderpass_ref_coolor);
    vk::RenderPassCreateInfo renderpass_info;
    renderpass_info.setAttachments(renderpass_attachments);
    renderpass_info.setSubpasses(renderpass_subpasses);
    vk::UniqueRenderPass renderpass = device->createRenderPassUnique(renderpass_info);

    // Load Shader modules
    auto VertexModule = load_shader(device, "shaders/color-vert.glsl.spv");
    auto FragmentModule = load_shader(device, "shaders/color-frag.glsl.spv");
    
    // Create Pipeline
    std::vector<vk::PipelineShaderStageCreateInfo> pipeline_stages{
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex,* VertexModule, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *FragmentModule, "main"),
    };
    std::vector<vk::VertexInputBindingDescription> pipeline_input_bindings{
        vk::VertexInputBindingDescription(0, sizeof(vertex_t), vk::VertexInputRate::eVertex),
    };
    std::vector<vk::VertexInputAttributeDescription> pipeline_input_attributes{
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, pos)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, col)),
    };
    vk::PipelineVertexInputStateCreateInfo pipeline_input;
    pipeline_input.setVertexBindingDescriptions(pipeline_input_bindings);
    pipeline_input.setVertexAttributeDescriptions(pipeline_input_attributes);

    vk::PipelineInputAssemblyStateCreateInfo pipeline_assembly;
    pipeline_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    pipeline_assembly.primitiveRestartEnable = false;

    vk::Viewport viewport{ 0, 0,
        (float)surface_caps.currentExtent.width, 
        (float)surface_caps.currentExtent.height };
    vk::Rect2D scissor{ {}, surface_caps.currentExtent };
    vk::PipelineViewportStateCreateInfo pipeline_viewport;
    pipeline_viewport.setViewports(viewport);
    pipeline_viewport.setScissors(scissor);

    vk::PipelineRasterizationStateCreateInfo pipeline_raster;
    pipeline_raster.depthClampEnable = false;
    pipeline_raster.rasterizerDiscardEnable = false;
    pipeline_raster.polygonMode = vk::PolygonMode::eFill;
    pipeline_raster.cullMode = vk::CullModeFlagBits::eNone;
    pipeline_raster.frontFace = vk::FrontFace::eClockwise;
    pipeline_raster.depthBiasEnable = false;
    pipeline_raster.lineWidth = 1.f;

    vk::PipelineMultisampleStateCreateInfo pipeline_multisample;
    pipeline_multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;
    pipeline_multisample.sampleShadingEnable = false;

    vk::PipelineDepthStencilStateCreateInfo pipeline_depth;
    pipeline_depth.depthTestEnable = false;
    pipeline_depth.stencilTestEnable = false;

    vk::ColorComponentFlags color_mask =
        vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB |
        vk::ColorComponentFlagBits::eA;
    std::vector<vk::PipelineColorBlendAttachmentState> pipeline_blend_attachments(1);
    pipeline_blend_attachments[0].blendEnable = false;
    pipeline_blend_attachments[0].colorWriteMask = color_mask;
    vk::PipelineColorBlendStateCreateInfo pipeline_blend;
    pipeline_blend.logicOpEnable = false;
    pipeline_blend.setAttachments(pipeline_blend_attachments);

    vk::PipelineDynamicStateCreateInfo pipeline_dynamic;
    pipeline_dynamic.dynamicStateCount = 0;

    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setStages(pipeline_stages);
    pipeline_info.pVertexInputState = &pipeline_input;
    pipeline_info.pInputAssemblyState = &pipeline_assembly;
    pipeline_info.pTessellationState = nullptr;
    pipeline_info.pViewportState = &pipeline_viewport;
    pipeline_info.pRasterizationState = &pipeline_raster;
    pipeline_info.pMultisampleState = &pipeline_multisample;
    pipeline_info.pDepthStencilState = &pipeline_depth;
    pipeline_info.pColorBlendState = &pipeline_blend;
    pipeline_info.pDynamicState = &pipeline_dynamic;
    pipeline_info.layout = *pipeline_layout;
    pipeline_info.renderPass = *renderpass;
    pipeline_info.subpass = 0;
    vk::UniquePipeline pipeline = device->createGraphicsPipelineUnique(nullptr, pipeline_info).value;

    std::vector<vk::DescriptorPoolSize> descrpool_sizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1},
    };
    vk::DescriptorPoolCreateInfo descrpool_info;
    descrpool_info.maxSets = 1;
    descrpool_info.setPoolSizes(descrpool_sizes);
    vk::UniqueDescriptorPool descrpool = device->createDescriptorPoolUnique(descrpool_info);

    vk::DescriptorSetAllocateInfo descrset_info;
    descrset_info.descriptorPool = *descrpool;
    descrset_info.descriptorSetCount = 1;
    descrset_info.setSetLayouts(*descrset_layout);
    vk::UniqueDescriptorSet descrset = std::move(device->allocateDescriptorSetsUnique(descrset_info).front());

    vk::DescriptorBufferInfo descr_sets_write_uniform;
    descr_sets_write_uniform.buffer = *quad_buffer;
    descr_sets_write_uniform.offset = quad_uniform_off;
    descr_sets_write_uniform.range = quad_uniform_size;
    std::vector<vk::WriteDescriptorSet> descr_sets_write{
        vk::WriteDescriptorSet(*descrset, 0, 0, 1, vk::DescriptorType::eUniformBuffer, 
            nullptr, &descr_sets_write_uniform, nullptr),
    };
    device->updateDescriptorSets(descr_sets_write, nullptr);

    vk::UniqueFence fence = device->createFenceUnique(vk::FenceCreateInfo());
    std::vector<vk::Image> swapchain_images = device->getSwapchainImagesKHR(*swapchain);
    vk::UniqueCommandBuffer cmd = std::move(
        device->allocateCommandBuffersUnique({ *cmd_pool, vk::CommandBufferLevel::ePrimary, 1 }).front());
    std::vector<vk::UniqueImageView> swapchain_views(2);
    std::vector<vk::UniqueFramebuffer> framebuffers(2);
    for (int i = 0; i < 2; i++)
    {
        vk::ImageViewCreateInfo fb_view_info;
        fb_view_info.image = swapchain_images[i];
        fb_view_info.viewType = vk::ImageViewType::e2D;
        fb_view_info.format = swapchain_info.imageFormat;
        fb_view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        swapchain_views[i] = device->createImageViewUnique(fb_view_info);
        
        vk::FramebufferCreateInfo fb_info;
        fb_info.renderPass = *renderpass;
        fb_info.setAttachments(*swapchain_views[i]);
        fb_info.width = surface_caps.currentExtent.width;
        fb_info.height = surface_caps.currentExtent.height;
        fb_info.layers = 1;
        framebuffers[i] = device->createFramebufferUnique(fb_info);
    }

    MSG msg;
    float alpha = 0;
    while (true)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;
            DispatchMessage(&msg);
        }
        alpha += 0.1f;
        auto next_image = device->acquireNextImageKHR(*swapchain, UINT64_MAX, nullptr, *fence);
        if (next_image.result == vk::Result::eSuccess)
        {
            cmd->reset();
            std::array color{ fabs(sinf(alpha)), 0.f, 0.f, 1.f };
            cmd->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
            vk::ImageMemoryBarrier barrier;
            barrier.image = swapchain_images[next_image.value];
            barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            cmd->pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::DependencyFlagBits::eByRegion, nullptr, nullptr, barrier);
            std::vector<vk::ClearValue> clear_values{
                vk::ClearColorValue(color),
            };
            vk::RenderPassBeginInfo renderpass_begin_info;
            renderpass_begin_info.renderPass = *renderpass;
            renderpass_begin_info.framebuffer = *framebuffers[next_image.value];
            renderpass_begin_info.renderArea = scissor;
            renderpass_begin_info.setClearValues(clear_values);
            cmd->beginRenderPass(renderpass_begin_info, vk::SubpassContents::eInline);
            {
                cmd->bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
                cmd->bindVertexBuffers(0, *quad_buffer, { quad_vertices_off });
                cmd->bindIndexBuffer(*quad_buffer, 0, vk::IndexType::eUint32);
                cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, *descrset, nullptr);
                cmd->drawIndexed(quad_indices.size(), 1, 0, 0, 0);
            }
            cmd->endRenderPass();
            cmd->end();

            vk::SubmitInfo submit_info;
            vk::CommandBuffer vkcmd = *cmd;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &vkcmd;
            q.submit(submit_info);
            q.waitIdle();

            vk::Result present_result;
            vk::PresentInfoKHR present_info;
            present_info.setSwapchains(*swapchain);
            present_info.pImageIndices = &next_image.value;
            present_info.pResults = &present_result;
            q.presentKHR(present_info);
            q.waitIdle();
        }
        device->waitForFences(*fence, true, UINT64_MAX);
        device->resetFences(*fence);

    }

    return EXIT_SUCCESS;
}
