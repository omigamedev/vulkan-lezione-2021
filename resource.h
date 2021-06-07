#pragma once
#include "allocator.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

struct Resource
{
    
};

struct ImageResource : public Resource
{
    vk::UniqueImage texture;
    vk::UniqueImageView view;
    vk::ImageCreateInfo info;
};

struct ResourceManager
{
    vk::Queue queue;
    vk::Device device;
    vk::PhysicalDevice physical_device;
    vk::CommandPool cmd_pool;
    MemoryAllocator memory;
    
    ResourceManager(const vk::PhysicalDevice& physical_device, const vk::Device& device, const vk::Queue& queue, const vk::CommandPool& cmd_pool)
        : physical_device(physical_device), device(device), queue(queue), cmd_pool(cmd_pool), memory{ physical_device, device, 64 << 20 } {}
    
    std::shared_ptr<ImageResource> create_texture2D(glm::ivec2 size, uint8_t* data);
};
