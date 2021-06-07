#include "allocator.h"

MemoryRef::~MemoryRef()
{
    allocator->free(chunk);
}

void* MemoryRef::map_internal(vk::DeviceAddress offset, vk::DeviceSize size)
{
    return allocator->device.mapMemory(chunk->device_memory,
        chunk->offset + offset, size == VK_WHOLE_SIZE ? chunk->size : size);
}

bool MemoryAllocation::free(const std::shared_ptr<MemoryChunk>& chunk)
{
    if (auto it = std::find(chunks.begin(), chunks.end(), chunk); it != chunks.end())
    {
        (*it)->used = false;
        if (chunks.size() == 1)
            return true;

        auto next = std::next(it);
        //auto prev = std::prev(it);

        if (next != chunks.end() && !(*next)->used
            && it != chunks.begin() && !(*std::prev(it))->used)
        {
            auto prev = std::prev(it);
            (*prev)->size += (*it)->size + (*next)->size;
            chunks.erase(next);
            chunks.erase(prev);
            return true;
        }

        if (it == chunks.begin() && !(*next)->used)
        {
            (*it)->size += (*next)->size;
            chunks.erase(next);
            return true;
        }
        if (next == chunks.end() && it != chunks.begin() && !(*std::prev(it))->used)
        {
            auto prev = std::prev(it);
            (*it)->size += (*prev)->size;
            chunks.erase(prev);
            return true;
        }
    }
    return false;
}

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
                auto new_chunk = std::make_shared<MemoryChunk>(family_index, *device_memory,
                    cur_chunk->size - required_size, cur_chunk->offset + required_size);
                cur_chunk->size = required_size;
                cur_chunk->used = true;
                chunks.insert(++chunk, std::move(new_chunk));
                return cur_chunk;
            }
        }
    }
    return nullptr;
}

void MemoryFamily::free(const std::shared_ptr<MemoryChunk>& chunk)
{
    for (auto& allocation : allocations)
        if (allocation.free(chunk))
            // delete extra allocations
            return;
    throw std::runtime_error("MemoryAllocator cannot free chunk");
}

std::shared_ptr<MemoryChunk> MemoryFamily::allocate(const vk::Device& device, uint32_t family_index, vk::DeviceSize size)
{
    for (auto& allocation : allocations)
        if (auto ptr = allocation.allocate(size))
            return ptr;
    // Free allocation not found, create new
    return allocations.emplace_back(device, family_index, allocation_size).allocate(size);
}

void MemoryAllocator::free(const std::shared_ptr<MemoryChunk>& chunk)
{
    if (auto it = families.find(chunk->family_index); it != families.end())
        return it->second.free(chunk);
}

std::shared_ptr<MemoryRef> MemoryAllocator::allocate(const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags, vk::DeviceSize size)
{
    uint32_t family_index = find_memory(physical_device, req, flags);
    if (auto it = families.find(family_index); it != families.end())
        return std::make_shared<MemoryRef>(it->second.allocate(device, family_index, size), this);
    if (auto alloc = families.emplace(family_index, allocation_size); alloc.second)
        return std::make_shared<MemoryRef>(alloc.first->second.allocate(device, family_index, size), this);
    throw std::runtime_error("MemoryAllocator cannot allocate");
}

std::shared_ptr<MemoryRef> MemoryAllocator::allocate(const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags)
{
    return allocate(req, flags, req.size);
}

std::shared_ptr<MemoryRef> MemoryAllocator::allocate_bind(const vk::Image& img, vk::MemoryPropertyFlags flags)
{
    vk::MemoryRequirements req = device.getImageMemoryRequirements(img);
    auto memref = allocate(req, flags);
    device.bindImageMemory(img, memref->chunk->device_memory, 0);
    return memref;
}

std::shared_ptr<MemoryRef> MemoryAllocator::allocate_bind(const vk::Buffer& buffer, vk::MemoryPropertyFlags flags)
{
    vk::MemoryRequirements req = device.getBufferMemoryRequirements(buffer);
    auto memref = allocate(req, flags);
    device.bindBufferMemory(buffer, memref->chunk->device_memory, 0);
    return memref;
}

uint32_t MemoryAllocator::find_memory(const vk::PhysicalDevice& physical_device, const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags)
{
    static vk::PhysicalDeviceMemoryProperties mp = physical_device.getMemoryProperties();
    for (uint32_t mem_i = 0; mem_i < mp.memoryTypeCount; mem_i++)
        if ((1 << mem_i) & req.memoryTypeBits && (mp.memoryTypes[mem_i].propertyFlags & flags) == flags)
            return mem_i;
    throw std::runtime_error("find_memory failed");
}
