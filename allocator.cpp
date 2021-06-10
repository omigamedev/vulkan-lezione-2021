#include "allocator.h"
#include "device.h"
#include <iostream>

std::shared_ptr<MemoryChunk> MemoryAllocation::allocate(vk::DeviceSize required_size)
{
    for (auto chunk = chunks.begin(); chunk != chunks.end(); ++chunk)
    {
        if (!(*chunk)->used)
        {
            if ((*chunk)->size == required_size)
            {
                (*chunk)->used = true;
                return *chunk;
            }
            else
            {
                auto cur_chunk = (*chunk);
                auto new_chunk = std::make_shared<MemoryChunk>(
                    family_index, *device_memory, cur_chunk->size - required_size,
                    cur_chunk->offset + required_size);
                cur_chunk->size = required_size;
                cur_chunk->used = true;
                chunks.insert(std::next(chunk), std::move(new_chunk));
                return cur_chunk;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<MemoryChunk> MemoryFamily::allocate(vk::Device& device, uint32_t family_index, vk::DeviceSize required_size)
{
    for (auto& allocation : allocations)
        if (auto ptr = allocation.allocate(required_size))
            return ptr;
    return allocations.emplace_back(device, family_index, allocation_size).allocate(required_size);
}

std::shared_ptr<MemoryRef> MemoryAllocator::allocate(const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags)
{
    // @todo implement memory alignment
    uint32_t family_index = find_memory(req, flags);
    std::cout << "MemoryAllocator::allocate family_index = " << family_index << "\n";
    // check if family already exists
    if (auto it = families.find(family_index); it != families.end())
    {
        std::cout << "MemoryAllocator::allocate family already exists\n";
        return std::make_shared<MemoryRef>(it->second.allocate(*device.device, family_index, req.size), this);
    }
    // create family and allocate
    if (auto alloc = families.emplace(family_index, allocation_size); alloc.second)
    {
        std::cout << "MemoryAllocator::allocate new family allocated\n";
        return std::make_shared<MemoryRef>(alloc.first->second.allocate(*device.device, family_index, req.size), this);
    }
    throw std::runtime_error("MemoryAllocator::allocate failed");
}

uint32_t MemoryAllocator::find_memory(const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags)
{
    static vk::PhysicalDeviceMemoryProperties mp = device.physical_device.getMemoryProperties();
    for (uint32_t mem_i = 0; mem_i < mp.memoryTypeCount; mem_i++)
        if ((1 << mem_i) & req.memoryTypeBits && (mp.memoryTypes[mem_i].propertyFlags & flags) == flags)
            return mem_i;
    throw std::runtime_error("find_memory failed");
}

void* MemoryRef::map_internal(vk::DeviceAddress offset, vk::DeviceSize size)
{
    return allocator->device.device->mapMemory(chunk->device_memory,
        chunk->offset + offset, size == VK_WHOLE_SIZE ? chunk->size : size);
}
