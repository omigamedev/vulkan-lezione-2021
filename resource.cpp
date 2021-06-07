#include "resource.h"

std::shared_ptr<ImageResource> ResourceManager::create_texture2D(glm::ivec2 size, uint8_t* data)
{
    auto tex = std::make_shared<ImageResource>();

    // Create texture and view
    tex->info.imageType = vk::ImageType::e2D;
    tex->info.format = vk::Format::eR8G8B8A8Unorm;
    tex->info.extent = vk::Extent3D(size.x, size.y, 1);
    tex->info.mipLevels = 1;
    tex->info.arrayLayers = 1;
    tex->info.samples = vk::SampleCountFlagBits::e1;
    tex->info.tiling = vk::ImageTiling::eLinear;
    tex->info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    tex->info.initialLayout = vk::ImageLayout::eUndefined;
    tex->texture = device.createImageUnique(tex->info);
    memory.allocate_bind(*tex->texture, vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::ImageViewCreateInfo tex_view_info;
    tex_view_info.image = *tex->texture;
    tex_view_info.viewType = vk::ImageViewType::e2D;
    tex_view_info.format = vk::Format::eR8G8B8A8Unorm;
    tex_view_info.components = vk::ComponentMapping();
    tex_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    tex_view_info.subresourceRange.baseMipLevel = 0;
    tex_view_info.subresourceRange.levelCount = 1;
    tex_view_info.subresourceRange.baseArrayLayer = 0;
    tex_view_info.subresourceRange.layerCount = 1;
    tex->view = device.createImageViewUnique(tex_view_info);

    if (data)
    {
        // Create staging texture
        vk::ImageCreateInfo staging_info;
        staging_info.imageType = vk::ImageType::e2D;
        staging_info.format = tex->info.format;
        staging_info.extent = vk::Extent3D(size.x, size.y, 1);
        staging_info.mipLevels = 1;
        staging_info.arrayLayers = 1;
        staging_info.samples = vk::SampleCountFlagBits::e1;
        staging_info.tiling = vk::ImageTiling::eLinear;
        staging_info.usage = vk::ImageUsageFlagBits::eTransferSrc;
        staging_info.initialLayout = vk::ImageLayout::ePreinitialized;
        vk::UniqueImage staging = device.createImageUnique(staging_info);
        auto staging_mem_chunk = memory.allocate_bind(*staging,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        vk::SubresourceLayout staging_layout = device.getImageSubresourceLayout(*staging,
            vk::ImageSubresource(vk::ImageAspectFlagBits::eColor, 0, 0));
        if (uint8_t* ptr = reinterpret_cast<uint8_t*>(device.mapMemory(staging_mem_chunk->chunk->device_memory, 0, VK_WHOLE_SIZE)))
        {
            for (int row = 0; row < size.y; row++)
                std::copy_n(
                    data + row * size.x * 4,
                    size.x * 4,
                    ptr + row * staging_layout.rowPitch);
            device.unmapMemory(staging_mem_chunk->chunk->device_memory);
        }

        vk::CommandBufferAllocateInfo cmd_tex_info;
        cmd_tex_info.commandPool = cmd_pool;
        cmd_tex_info.level = vk::CommandBufferLevel::ePrimary;
        cmd_tex_info.commandBufferCount = 1;
        vk::UniqueCommandBuffer cmd_tex = std::move(
            device.allocateCommandBuffersUnique(cmd_tex_info).front());
        cmd_tex->begin(vk::CommandBufferBeginInfo({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }));
        {
            vk::ImageMemoryBarrier barrier;
            barrier.subresourceRange = tex_view_info.subresourceRange;

            barrier.image = *staging;
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.oldLayout = vk::ImageLayout::ePreinitialized;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            cmd_tex->pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlagBits::eByRegion,
                nullptr, nullptr, barrier);

            barrier.image = *tex->texture;
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            cmd_tex->pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlagBits::eByRegion,
                nullptr, nullptr, barrier);

            vk::ImageCopy copy;
            copy.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copy.srcSubresource.mipLevel = 0;
            copy.srcSubresource.baseArrayLayer = 0;
            copy.srcSubresource.layerCount = 1;
            copy.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copy.dstSubresource.mipLevel = 0;
            copy.dstSubresource.baseArrayLayer = 0;
            copy.dstSubresource.layerCount = 1;
            copy.extent = tex->info.extent;
            cmd_tex->copyImage(*staging, vk::ImageLayout::eTransferSrcOptimal,
                *tex->texture, vk::ImageLayout::eTransferDstOptimal, copy);

            barrier.image = *tex->texture;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            cmd_tex->pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::DependencyFlagBits::eByRegion,
                nullptr, nullptr, barrier);
        }
        cmd_tex->end();

        vk::SubmitInfo cmd_tex_submit_info;
        cmd_tex_submit_info.setCommandBuffers(*cmd_tex);
        queue.submit(cmd_tex_submit_info);
        queue.waitIdle();
    }
    return tex;
}
