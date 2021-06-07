#pragma once
#include "allocator.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

struct Device;

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
    Device& dev;
    MemoryAllocator memory;
    
    ResourceManager(Device& dev)
        : dev(dev), memory{ dev, 64 << 20 } {}
    
    std::shared_ptr<ImageResource> create_texture2D(glm::ivec2 size, uint8_t* data);
    std::shared_ptr<ImageResource> load_texture2D(const std::string& path);
};
