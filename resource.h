#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>

struct Device;
struct MemoryAllocator;
struct MemoryRef;

struct Resource
{
    Resource() = default;
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
};

struct ImageResource : public Resource
{
    std::shared_ptr<MemoryRef> mem;
    vk::UniqueImage texture;
    vk::UniqueImageView view;
    vk::ImageCreateInfo info;
};

struct ResourceManager
{
    Device& device;
    MemoryAllocator& memory;

    ResourceManager(Device& device, MemoryAllocator& memory)
        : device(device), memory(memory) {}

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    std::shared_ptr<ImageResource> create_texture2D(int width, int height, uint8_t* data);
    std::shared_ptr<ImageResource> load_texture2D(const std::string& path);
};


