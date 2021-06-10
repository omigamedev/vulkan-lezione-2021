#include "resource.h"
#include "device.h"
#include "allocator.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

std::shared_ptr<ImageResource> ResourceManager::create_texture2D(int width, int height, uint8_t* data)
{
    auto res = std::make_shared<ImageResource>();

    res->info.imageType = vk::ImageType::e2D;
    res->info.format = vk::Format::eR8G8B8A8Unorm;
    res->info.extent = vk::Extent3D(width, height, 1);
    res->info.mipLevels = 1;
    res->info.arrayLayers = 1;
    res->info.samples = vk::SampleCountFlagBits::e1;
    res->info.tiling = vk::ImageTiling::eLinear;
    res->info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    res->info.initialLayout = vk::ImageLayout::eUndefined;
    res->texture = device.device->createImageUnique(res->info);
    vk::MemoryRequirements tex_mem_req = device.device->getImageMemoryRequirements(*res->texture);
    res->mem = memory.allocate(tex_mem_req, vk::MemoryPropertyFlagBits::eDeviceLocal);
    device.device->bindImageMemory(*res->texture, res->mem->chunk->device_memory, res->mem->chunk->offset);

    vk::ImageViewCreateInfo tex_view_info;
    tex_view_info.image = *res->texture;
    tex_view_info.viewType = vk::ImageViewType::e2D;
    tex_view_info.format = res->info.format;
    tex_view_info.components = vk::ComponentMapping();
    tex_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    tex_view_info.subresourceRange.baseMipLevel = 0;
    tex_view_info.subresourceRange.levelCount = 1;
    tex_view_info.subresourceRange.baseArrayLayer = 0;
    tex_view_info.subresourceRange.layerCount = 1;
    res->view = device.device->createImageViewUnique(tex_view_info);

    if (data)
    {
        vk::ImageCreateInfo staging_info;
        staging_info.imageType = vk::ImageType::e2D;
        staging_info.format = res->info.format;
        staging_info.extent = vk::Extent3D(width, height, 1);
        staging_info.mipLevels = 1;
        staging_info.arrayLayers = 1;
        staging_info.samples = vk::SampleCountFlagBits::e1;
        staging_info.tiling = vk::ImageTiling::eLinear;
        staging_info.usage = vk::ImageUsageFlagBits::eTransferSrc;
        staging_info.initialLayout = vk::ImageLayout::ePreinitialized;
        vk::UniqueImage staging = device.device->createImageUnique(staging_info);
        vk::MemoryRequirements staging_mem_req = device.device->getImageMemoryRequirements(*staging);
        auto staging_mem = memory.allocate(staging_mem_req,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        device.device->bindImageMemory(*staging, staging_mem->chunk->device_memory, staging_mem->chunk->offset);
        vk::SubresourceLayout staging_layout = device.device->getImageSubresourceLayout(*staging,
            vk::ImageSubresource(vk::ImageAspectFlagBits::eColor, 0, 0));
        if (auto map = staging_mem->map(0, VK_WHOLE_SIZE))
        {
            for (int row = 0; row < height; row++)
                std::copy_n(
                    data + row * width * 4,
                    width * 4,
                    map.ptr + row * staging_layout.rowPitch);
        }

        vk::CommandBufferAllocateInfo cmd_tex_info;
        cmd_tex_info.commandPool = *device.cmd_pool;
        cmd_tex_info.level = vk::CommandBufferLevel::ePrimary;
        cmd_tex_info.commandBufferCount = 1;
        vk::UniqueCommandBuffer cmd_tex = std::move(
            device.device->allocateCommandBuffersUnique(cmd_tex_info).front());
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

            barrier.image = *res->texture;
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
            copy.extent = res->info.extent;
            cmd_tex->copyImage(*staging, vk::ImageLayout::eTransferSrcOptimal,
                *res->texture, vk::ImageLayout::eTransferDstOptimal, copy);

            barrier.image = *res->texture;
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
        device.q.submit(cmd_tex_submit_info);
        device.q.waitIdle();
    }
    return res;
}

std::shared_ptr<ImageResource> ResourceManager::load_texture2D(const std::string& path)
{
    int w, h, c;
    std::unique_ptr<uint8_t> data(stbi_load(path.c_str(), &w, &h, &c, 4));
    return create_texture2D(w, h, data.get());
}
