#include "device.h"
#include <iostream>

void Device::init_instance()
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

    inst = vk::createInstanceUnique(inst_info);
}

bool Device::create_device(HWND hWnd)
{
    vk::Win32SurfaceCreateInfoKHR surface_info;
    surface_info.hinstance = GetModuleHandle(NULL);
    surface_info.hwnd = hWnd;
    surface = inst->createWin32SurfaceKHRUnique(surface_info);

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
                surface_caps = physical_device.getSurfaceCapabilitiesKHR(*surface);

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

                q = device->getQueue(device_family_index, 0);
                vk::CommandPoolCreateInfo pool_info;
                pool_info.queueFamilyIndex = device_family_index;
                pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
                cmd_pool = device->createCommandPoolUnique(pool_info);

                return true;
            }
        }
    }
    return false;
}

void Device::create_swapchain()
{
    swapchain_info.surface = *surface;
    swapchain_info.minImageCount = 2;
    swapchain_info.imageFormat = vk::Format::eB8G8R8A8Unorm;
    swapchain_info.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    swapchain_info.imageExtent = surface_caps.currentExtent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
    swapchain_info.presentMode = vk::PresentModeKHR::eFifo;
    swapchain_info.clipped = true;
    swapchain = device->createSwapchainKHRUnique(swapchain_info);
}
