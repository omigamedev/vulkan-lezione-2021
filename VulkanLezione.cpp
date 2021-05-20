#include <vulkan/vulkan.hpp>
#include <iostream>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

int main()
{
    auto image = load_image("vulkan-logo.png");
    std::cout << "image size: " << image.first.x << " x " << image.first.y << "\n";
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
        for (int family_index = 0; family_index < families.size(); family_index++)
        {
            if (families[family_index].queueFlags & vk::QueueFlagBits::eGraphics
                && pd.getSurfaceSupportKHR(family_index, *surface))
            {
                vk::PhysicalDeviceProperties props = pd.getProperties();
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

    vk::UniqueFence fence = device->createFenceUnique(vk::FenceCreateInfo());
    std::vector<vk::Image> swapchain_images = device->getSwapchainImagesKHR(*swapchain);
    vk::UniqueCommandBuffer cmd = std::move(
        device->allocateCommandBuffersUnique({ *cmd_pool, vk::CommandBufferLevel::ePrimary, 1 }).front());

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
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            cmd->pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlagBits::eByRegion, nullptr, nullptr, barrier);

            cmd->clearColorImage(swapchain_images[next_image.value],
                vk::ImageLayout::eTransferDstOptimal, color, barrier.subresourceRange);

            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
            cmd->pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
                vk::DependencyFlagBits::eByRegion, nullptr, nullptr, barrier);

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
