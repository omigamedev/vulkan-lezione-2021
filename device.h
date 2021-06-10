#pragma once
#include <windows.h>
#include <vulkan/vulkan.hpp>
#include <vector>

struct Device
{
    vk::UniqueInstance inst;
    vk::UniqueSurfaceKHR surface;
    vk::SurfaceCapabilitiesKHR surface_caps;
    uint32_t device_family_index;
    vk::PhysicalDevice physical_device;
    vk::UniqueDevice device;
    vk::Queue q;
    vk::UniqueCommandPool cmd_pool;
    vk::UniqueSwapchainKHR swapchain;
    vk::SwapchainCreateInfoKHR swapchain_info;

    void init_instance();
    bool create_device(HWND hWnd);
    void create_swapchain();
};


