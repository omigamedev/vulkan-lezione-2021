#pragma once
#include <map>
#include <vulkan/vulkan.hpp>

struct MemoryRef : public std::enable_shared_from_this<MemoryRef>
{
    struct MemoryAllocator* allocator;
    std::shared_ptr<struct MemoryChunk> chunk;
    MemoryRef(std::weak_ptr<struct MemoryChunk> chunk, struct MemoryAllocator* allocator)
        : chunk(chunk), allocator(allocator) {}
    MemoryRef(const MemoryRef&) = delete;
    MemoryRef& operator=(const MemoryRef&) = delete;
    ~MemoryRef();
    template<typename T>
    struct Map
    {
        std::weak_ptr<MemoryRef> memref;
        T* ptr;
        Map(T* ptr, std::weak_ptr<MemoryRef> memref) : ptr(ptr), memref(memref) {}
        ~Map()
        {
            if (auto ref = memref.lock())
                ref->allocator->device.unmapMemory(ref->chunk->device_memory);
        }
        operator bool() const
        {
            return ptr != nullptr;
        }
    };
    template<typename T>
    Map<T> map(vk::DeviceAddress offset, vk::DeviceSize size /*= VK_WHOLE_SIZE*/)
    {
        return { reinterpret_cast<T*>(map_internal(offset, size)), shared_from_this() };
    }
    void* map_internal(vk::DeviceAddress offset, vk::DeviceSize size);
};

struct MemoryChunk
{
    bool used = false;
    uint32_t family_index;
    vk::DeviceMemory device_memory;
    vk::DeviceSize size;
    vk::DeviceSize offset;
    MemoryChunk(uint32_t family_index, const vk::DeviceMemory& device_memory, vk::DeviceSize size, vk::DeviceSize offset)
        : family_index(family_index), device_memory(device_memory), size(size), offset(offset) {}
};

struct MemoryAllocation
{
    uint32_t family_index;
    vk::UniqueDeviceMemory device_memory;
    vk::DeviceSize allocation_size;
    std::list<std::shared_ptr<MemoryChunk>> chunks;
    MemoryAllocation(const vk::Device& device, uint32_t family_index, vk::DeviceSize allocation_size)
        : allocation_size(allocation_size),
        family_index(family_index),
        chunks({ std::make_shared<MemoryChunk>(family_index, *device_memory, allocation_size, 0) }),
        device_memory(device.allocateMemoryUnique({ allocation_size, family_index })) {}
    MemoryAllocation(const MemoryAllocation&) = delete;
    MemoryAllocation& operator=(const MemoryAllocation&) = delete;
    std::shared_ptr<MemoryChunk> allocate(vk::DeviceSize required_size);
    bool free(const std::shared_ptr<MemoryChunk>& chunk);
};

struct MemoryFamily
{
    uint32_t family_index;
    vk::DeviceSize allocation_size;
    std::list<MemoryAllocation> allocations;
    MemoryFamily(vk::DeviceSize allocation_size)
        : allocation_size(allocation_size), family_index(0) {}
    MemoryFamily(const MemoryFamily&) = delete;
    MemoryFamily& operator=(const MemoryFamily&) = delete;
    std::shared_ptr<MemoryChunk> allocate(const vk::Device& device, uint32_t family_index, vk::DeviceSize size);
    void free(const std::shared_ptr<MemoryChunk>& chunk);
};

struct MemoryAllocator
{
    vk::DeviceSize allocation_size;
    vk::PhysicalDevice physical_device;
    vk::Device device;
    std::map<uint32_t, MemoryFamily> families;
    MemoryAllocator(const vk::PhysicalDevice& physical_device, const vk::Device& device, uint32_t allocation_size)
        : physical_device(physical_device), device(device), allocation_size(allocation_size) {}
    std::shared_ptr<MemoryRef> allocate(const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags);
    std::shared_ptr<MemoryRef> allocate(const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags, vk::DeviceSize size);
    void free(const std::shared_ptr<MemoryChunk>& chunk);
    std::shared_ptr<MemoryRef> allocate_bind(const vk::Image& img, vk::MemoryPropertyFlags flags);
    std::shared_ptr<MemoryRef> allocate_bind(const vk::Buffer& buffer, vk::MemoryPropertyFlags flags);
    uint32_t find_memory(const vk::PhysicalDevice& physical_device,
        const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags);
};
