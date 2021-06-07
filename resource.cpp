#include "resource.h"
#include "device.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
    tex->texture = dev.device->createImageUnique(tex->info);
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
    tex->view = dev.device->createImageViewUnique(tex_view_info);

    if (data)
    {
        // Create staging buffer
        vk::BufferCreateInfo buffer_info;
        buffer_info.size = size.x * size.y * 4;
        buffer_info.usage = vk::BufferUsageFlagBits::eTransferSrc;
        vk::UniqueBuffer buffer = dev.device->createBufferUnique(buffer_info);
        auto buffer_mem = memory.allocate_bind(*buffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        if (auto map = buffer_mem->map())
            std::copy(data, data + buffer_info.size, map.ptr);

        vk::CommandBufferAllocateInfo cmd_tex_info;
        cmd_tex_info.commandPool = *dev.cmd_pool;
        cmd_tex_info.level = vk::CommandBufferLevel::ePrimary;
        cmd_tex_info.commandBufferCount = 1;
        vk::UniqueCommandBuffer cmd_tex = std::move(
            dev.device->allocateCommandBuffersUnique(cmd_tex_info).front());
        cmd_tex->begin(vk::CommandBufferBeginInfo({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }));
        {
            vk::BufferMemoryBarrier buffer_barrier;
            buffer_barrier.srcAccessMask = {};
            buffer_barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            buffer_barrier.buffer = *buffer;
            buffer_barrier.size = buffer_info.size;
            buffer_barrier.offset = 0;
            cmd_tex->pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlagBits::eByRegion,
                nullptr, buffer_barrier, nullptr);

            vk::ImageMemoryBarrier img_barrier;
            img_barrier.subresourceRange = tex_view_info.subresourceRange;
            img_barrier.image = *tex->texture;
            
            img_barrier.srcAccessMask = {};
            img_barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            img_barrier.oldLayout = vk::ImageLayout::eUndefined;
            img_barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            cmd_tex->pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlagBits::eByRegion,
                nullptr, nullptr, img_barrier);

            vk::BufferImageCopy copy;
            copy.bufferOffset = 0;
            copy.bufferRowLength = size.x;
            copy.bufferImageHeight = size.y;
            copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = vk::Offset3D();
            copy.imageExtent = tex->info.extent;
            cmd_tex->copyBufferToImage(*buffer,
                *tex->texture, vk::ImageLayout::eTransferDstOptimal, copy);

            img_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            img_barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            img_barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            img_barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            cmd_tex->pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::DependencyFlagBits::eByRegion,
                nullptr, nullptr, img_barrier);
        }
        cmd_tex->end();

        vk::SubmitInfo cmd_tex_submit_info;
        cmd_tex_submit_info.setCommandBuffers(*cmd_tex);
        dev.q.submit(cmd_tex_submit_info);
        dev.q.waitIdle();
    }
    return tex;
}

std::shared_ptr<ImageResource> ResourceManager::load_texture2D(const std::string& path)
{
    int w, h, c;
    std::unique_ptr<uint8_t> data(stbi_load(path.c_str(), &w, &h, &c, 4));
    return create_texture2D({ w, h }, data.get());
}
