#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include <list>
#include <map>

struct MemoryAllocator;
struct Device;

struct MemoryChunk
{
    bool used = false;
    uint32_t family_index;
    vk::DeviceMemory device_memory;
    vk::DeviceSize size;
    vk::DeviceSize offset;
    MemoryChunk(uint32_t family_index, const vk::DeviceMemory& device_memory, vk::DeviceSize size, vk::DeviceSize offset)
        : family_index(family_index)
        , device_memory(device_memory)
        , size(size), offset(offset) {}

    MemoryChunk(const MemoryChunk&) = delete;
    MemoryChunk& operator=(const MemoryChunk&) = delete;
};

struct MemoryRef : public std::enable_shared_from_this<MemoryRef>
{
    MemoryAllocator* allocator;
    std::shared_ptr<MemoryChunk> chunk;
    MemoryRef(std::shared_ptr<MemoryChunk> chunk, MemoryAllocator* allocator)
        : chunk(chunk), allocator(allocator) {}
    
    MemoryRef(const MemoryRef&) = delete;
    MemoryRef& operator=(const MemoryRef&) = delete;

    template<typename T>
    struct Map
    {
        T* ptr;
        std::weak_ptr<MemoryRef> memref;
        Map(T* ptr, std::weak_ptr<MemoryRef> memref)
            : ptr(ptr), memref(memref) {}
        ~Map()
        {
            if (auto ref = memref.lock())
                ref->allocator->device.device->unmapMemory(ref->chunk->device_memory);
        }
        operator bool() const
        {
            return ptr != nullptr;
        }
    };

    template <typename T = uint8_t>
    Map<T> map(vk::DeviceAddress offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE)
    {
        return { reinterpret_cast<T*>(map_internal(offset, size)), shared_from_this() };
    }
    void* map_internal(vk::DeviceAddress offset, vk::DeviceSize size);
};

struct MemoryAllocation
{
    uint32_t family_index;
    vk::UniqueDeviceMemory device_memory;
    vk::DeviceSize allocation_size;
    std::list<std::shared_ptr<MemoryChunk>> chunks;
    MemoryAllocation(vk::Device& device, uint32_t family_index, vk::DeviceSize allocation_size)
        : family_index(family_index)
        , allocation_size(allocation_size)
        , device_memory(device.allocateMemoryUnique({ allocation_size, family_index }))
        , chunks({ std::make_shared<MemoryChunk>(family_index, *device_memory, allocation_size, 0) }) {}
    
    MemoryAllocation(const MemoryAllocation&) = delete;
    MemoryAllocation& operator=(const MemoryAllocation&) = delete;
    
    std::shared_ptr<MemoryChunk> allocate(vk::DeviceSize required_size);
};

struct MemoryFamily
{
    uint32_t family_index;
    vk::DeviceSize allocation_size;
    std::list<MemoryAllocation> allocations;
    MemoryFamily(vk::DeviceSize allocation_size)
        : family_index(family_index)
        , allocation_size(allocation_size) {}
    
    MemoryFamily(const MemoryFamily&) = delete;
    MemoryFamily& operator=(const MemoryFamily&) = delete;

    std::shared_ptr<MemoryChunk> allocate(vk::Device& device, uint32_t family_index, vk::DeviceSize required_size);
};

struct MemoryAllocator
{
    Device& device;
    vk::DeviceSize allocation_size;
    std::map<uint32_t/*family_index*/, MemoryFamily> families;
    MemoryAllocator(Device& device, vk::DeviceSize allocation_size)
        : allocation_size(allocation_size)
        , device(device) {}
    
    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;
    
    std::shared_ptr<MemoryRef> allocate(const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags);
    uint32_t find_memory(const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags);
};


