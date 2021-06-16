#include "allocator.h"
#include "window.h"
#include "device.h"
#include "resource.h"

#include <vulkan/vulkan.hpp>
#include <iostream>
#include <fstream>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/matrix4x4.h>

struct vertex_t 
{
    glm::vec3 pos;
    glm::vec3 col;
    glm::vec2 uvs;
};

struct uniform_vertex_t
{
    glm::mat4 model;
};

struct uniform_fragment_t
{
    glm::vec4 tint;
};

size_t aligned_size(size_t sz, size_t alignment)
{
    return ((sz - 1) & alignment) + alignment;
}
template <typename T>
size_t aligned_size(const std::vector<T>& v, size_t alignment)
{
    return aligned_size(sizeof(T) * T.size(), alignment);
}
template <typename T, size_t N>
size_t aligned_size(const std::array<T, N>& v, size_t alignment)
{
    return aligned_size(sizeof(T) * N, alignment);
}

vk::UniqueShaderModule load_shader(const vk::UniqueDevice& device, const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    size_t size = file.tellg();
    file.seekg(std::ios::beg);
    auto buffer = std::make_unique<char[]>(size);
    file.read(buffer.get(), size);

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = size;
    module_info.pCode = reinterpret_cast<uint32_t*>(buffer.get());
    return device->createShaderModuleUnique(module_info);
}

int main()
{
    Device device;
    device.init_instance();
    HWND hWnd = create_window(800, 600);
    device.create_device(hWnd);
    device.create_swapchain();

    MemoryAllocator ma(device, 64 << 20);
    ResourceManager rm(device, ma);

    // Load texture
    auto tex = rm.load_texture2D("vulkan-logo.png");

    vk::SamplerCreateInfo sampler_info;
    sampler_info.minFilter = vk::Filter::eLinear;
    sampler_info.magFilter = vk::Filter::eLinear;
    vk::UniqueSampler sampler = device.device->createSamplerUnique(sampler_info);

    // Create Vertex and Index buffer
    std::vector<uint32_t> quad_indices{ 0, 1, 2, 0, 2, 3 };
    std::vector<vertex_t> quad_vertices{
        vertex_t{glm::vec3(-1,-1, 0), glm::vec3(1, 0, 0), glm::vec2(0, 0)},
        vertex_t{glm::vec3(-1, 1, 0), glm::vec3(1, 1, 0), glm::vec2(0, 1)},
        vertex_t{glm::vec3( 1, 1, 0), glm::vec3(0, 1, 1), glm::vec2(1, 1)},
        vertex_t{glm::vec3( 1,-1, 0), glm::vec3(1, 0, 1), glm::vec2(1, 0)},
    };
    size_t quad_indices_off = 0;
    size_t quad_indices_size = aligned_size(quad_indices.size() * sizeof(uint32_t), 0x100);
    size_t quad_vertices_off = quad_indices_off + quad_indices_size;
    size_t quad_vertices_size = aligned_size(quad_vertices.size() * sizeof(vertex_t), 0x100);
    size_t quad_uniform_vertex_off = quad_vertices_off + quad_vertices_size;
    size_t quad_uniform_vertex_size = aligned_size(sizeof(uniform_vertex_t), 0x100);
    size_t quad_uniform_fragment_off = quad_uniform_vertex_off + quad_uniform_vertex_size;
    size_t quad_uniform_fragment_size = aligned_size(sizeof(uniform_fragment_t), 0x100);
    vk::BufferCreateInfo quad_buffer_info;
    quad_buffer_info.size = quad_indices_size
        + quad_vertices_size
        + quad_uniform_vertex_size
        + quad_uniform_fragment_size;
    quad_buffer_info.usage = vk::BufferUsageFlagBits::eIndexBuffer
        | vk::BufferUsageFlagBits::eVertexBuffer
        | vk::BufferUsageFlagBits::eUniformBuffer;
    vk::UniqueBuffer quad_buffer = device.device->createBufferUnique(quad_buffer_info);
    vk::MemoryRequirements quad_buffer_req = device.device->getBufferMemoryRequirements(*quad_buffer);
    auto quad_buffer_mem = ma.allocate(quad_buffer_req,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    device.device->bindBufferMemory(*quad_buffer, quad_buffer_mem->chunk->device_memory, quad_buffer_mem->chunk->offset);

    if (auto map = quad_buffer_mem->map(0, VK_WHOLE_SIZE))
    {
        std::copy(quad_indices.begin(), quad_indices.end(), 
            reinterpret_cast<uint32_t*>(map.ptr + quad_indices_off));
        std::copy(quad_vertices.begin(), quad_vertices.end(), 
            reinterpret_cast<vertex_t*>(map.ptr + quad_vertices_off));
    }

    // Pipeline Layout
    std::vector<vk::DescriptorSetLayoutBinding> descrset_layout_bindings{
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
    };
    vk::DescriptorSetLayoutCreateInfo descrset_layout_info;
    descrset_layout_info.setBindings(descrset_layout_bindings);
    vk::UniqueDescriptorSetLayout descrset_layout = device.device->createDescriptorSetLayoutUnique(descrset_layout_info);
    vk::PipelineLayoutCreateInfo pipeline_layout_info;
    pipeline_layout_info.setSetLayouts(*descrset_layout);
    vk::UniquePipelineLayout pipeline_layout = device.device->createPipelineLayoutUnique(pipeline_layout_info);

    // Create RenderPass
    std::vector<vk::AttachmentDescription> renderpass_attachments(1);
    renderpass_attachments[0].format = device.swapchain_info.imageFormat;
    renderpass_attachments[0].samples = vk::SampleCountFlagBits::e1;
    renderpass_attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
    renderpass_attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
    renderpass_attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    renderpass_attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    renderpass_attachments[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
    renderpass_attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;
    std::vector<vk::SubpassDescription> renderpass_subpasses(1);
    vk::AttachmentReference renderpass_ref_coolor;
    renderpass_ref_coolor.attachment = 0;
    renderpass_ref_coolor.layout = vk::ImageLayout::eColorAttachmentOptimal;
    renderpass_subpasses[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    renderpass_subpasses[0].setColorAttachments(renderpass_ref_coolor);
    vk::RenderPassCreateInfo renderpass_info;
    renderpass_info.setAttachments(renderpass_attachments);
    renderpass_info.setSubpasses(renderpass_subpasses);
    vk::UniqueRenderPass renderpass = device.device->createRenderPassUnique(renderpass_info);

    // Load Shader modules
    auto VertexModule = load_shader(device.device, "shaders/color-vert.glsl.spv");
    auto FragmentModule = load_shader(device.device, "shaders/color-frag.glsl.spv");
    
    // Create Pipeline
    std::vector<vk::PipelineShaderStageCreateInfo> pipeline_stages{
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex,* VertexModule, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *FragmentModule, "main"),
    };
    std::vector<vk::VertexInputBindingDescription> pipeline_input_bindings{
        vk::VertexInputBindingDescription(0, sizeof(vertex_t), vk::VertexInputRate::eVertex),
    };
    std::vector<vk::VertexInputAttributeDescription> pipeline_input_attributes{
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, pos)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, col)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(vertex_t, uvs)),
    };
    vk::PipelineVertexInputStateCreateInfo pipeline_input;
    pipeline_input.setVertexBindingDescriptions(pipeline_input_bindings);
    pipeline_input.setVertexAttributeDescriptions(pipeline_input_attributes);

    vk::PipelineInputAssemblyStateCreateInfo pipeline_assembly;
    pipeline_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    pipeline_assembly.primitiveRestartEnable = false;

    vk::Viewport viewport{ 0, 0,
        (float)device.surface_caps.currentExtent.width,
        (float)device.surface_caps.currentExtent.height };
    vk::Rect2D scissor{ {}, device.surface_caps.currentExtent };
    vk::PipelineViewportStateCreateInfo pipeline_viewport;
    pipeline_viewport.setViewports(viewport);
    pipeline_viewport.setScissors(scissor);

    vk::PipelineRasterizationStateCreateInfo pipeline_raster;
    pipeline_raster.depthClampEnable = false;
    pipeline_raster.rasterizerDiscardEnable = false;
    pipeline_raster.polygonMode = vk::PolygonMode::eFill;
    pipeline_raster.cullMode = vk::CullModeFlagBits::eNone;
    pipeline_raster.frontFace = vk::FrontFace::eClockwise;
    pipeline_raster.depthBiasEnable = false;
    pipeline_raster.lineWidth = 1.f;

    vk::PipelineMultisampleStateCreateInfo pipeline_multisample;
    pipeline_multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;
    pipeline_multisample.sampleShadingEnable = false;

    vk::PipelineDepthStencilStateCreateInfo pipeline_depth;
    pipeline_depth.depthTestEnable = false;
    pipeline_depth.stencilTestEnable = false;

    vk::ColorComponentFlags color_mask =
        vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB |
        vk::ColorComponentFlagBits::eA;
    std::vector<vk::PipelineColorBlendAttachmentState> pipeline_blend_attachments(1);
    pipeline_blend_attachments[0].blendEnable = false;
    pipeline_blend_attachments[0].colorWriteMask = color_mask;
    vk::PipelineColorBlendStateCreateInfo pipeline_blend;
    pipeline_blend.logicOpEnable = false;
    pipeline_blend.setAttachments(pipeline_blend_attachments);

    vk::PipelineDynamicStateCreateInfo pipeline_dynamic;
    pipeline_dynamic.dynamicStateCount = 0;

    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setStages(pipeline_stages);
    pipeline_info.pVertexInputState = &pipeline_input;
    pipeline_info.pInputAssemblyState = &pipeline_assembly;
    pipeline_info.pTessellationState = nullptr;
    pipeline_info.pViewportState = &pipeline_viewport;
    pipeline_info.pRasterizationState = &pipeline_raster;
    pipeline_info.pMultisampleState = &pipeline_multisample;
    pipeline_info.pDepthStencilState = &pipeline_depth;
    pipeline_info.pColorBlendState = &pipeline_blend;
    pipeline_info.pDynamicState = &pipeline_dynamic;
    pipeline_info.layout = *pipeline_layout;
    pipeline_info.renderPass = *renderpass;
    pipeline_info.subpass = 0;
    vk::UniquePipeline pipeline = device.device->createGraphicsPipelineUnique(nullptr, pipeline_info).value;

    std::vector<vk::DescriptorPoolSize> descrpool_sizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 2},
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1},
    };
    vk::DescriptorPoolCreateInfo descrpool_info;
    descrpool_info.maxSets = 1;
    descrpool_info.setPoolSizes(descrpool_sizes);
    vk::UniqueDescriptorPool descrpool = device.device->createDescriptorPoolUnique(descrpool_info);

    vk::DescriptorSetAllocateInfo descrset_info;
    descrset_info.descriptorPool = *descrpool;
    descrset_info.descriptorSetCount = 1;
    descrset_info.setSetLayouts(*descrset_layout);
    vk::UniqueDescriptorSet descrset = std::move(device.device->allocateDescriptorSetsUnique(descrset_info).front());

    vk::DescriptorBufferInfo descr_sets_write_uniform_vertex;
    descr_sets_write_uniform_vertex.buffer = *quad_buffer;
    descr_sets_write_uniform_vertex.offset = quad_uniform_vertex_off;
    descr_sets_write_uniform_vertex.range = quad_uniform_vertex_size;
    vk::DescriptorBufferInfo descr_sets_write_uniform_fragment;
    descr_sets_write_uniform_fragment.buffer = *quad_buffer;
    descr_sets_write_uniform_fragment.offset = quad_uniform_fragment_off;
    descr_sets_write_uniform_fragment.range = quad_uniform_fragment_size;
    vk::DescriptorImageInfo descr_sets_write_tex;
    descr_sets_write_tex.sampler = *sampler;
    descr_sets_write_tex.imageView = *tex->view;
    descr_sets_write_tex.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    std::vector<vk::WriteDescriptorSet> descr_sets_write{
        vk::WriteDescriptorSet(*descrset, 0, 0, 1, vk::DescriptorType::eUniformBuffer, 
            nullptr, &descr_sets_write_uniform_vertex, nullptr),
        vk::WriteDescriptorSet(*descrset, 1, 0, 1, vk::DescriptorType::eUniformBuffer,
            nullptr, &descr_sets_write_uniform_fragment, nullptr),
        vk::WriteDescriptorSet(*descrset, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler,
            &descr_sets_write_tex, nullptr, nullptr),
    };
    device.device->updateDescriptorSets(descr_sets_write, nullptr);

    vk::UniqueFence fence = device.device->createFenceUnique(vk::FenceCreateInfo());
    std::vector<vk::Image> swapchain_images = device.device->getSwapchainImagesKHR(*device.swapchain);
    vk::UniqueCommandBuffer cmd = std::move(
        device.device->allocateCommandBuffersUnique({ *device.cmd_pool, vk::CommandBufferLevel::ePrimary, 1 }).front());
    std::vector<vk::UniqueImageView> swapchain_views(2);
    std::vector<vk::UniqueFramebuffer> framebuffers(2);
    for (int i = 0; i < 2; i++)
    {
        vk::ImageViewCreateInfo fb_view_info;
        fb_view_info.image = swapchain_images[i];
        fb_view_info.viewType = vk::ImageViewType::e2D;
        fb_view_info.format = device.swapchain_info.imageFormat;
        fb_view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        swapchain_views[i] = device.device->createImageViewUnique(fb_view_info);
        
        vk::FramebufferCreateInfo fb_info;
        fb_info.renderPass = *renderpass;
        fb_info.setAttachments(*swapchain_views[i]);
        fb_info.width = device.surface_caps.currentExtent.width;
        fb_info.height = device.surface_caps.currentExtent.height;
        fb_info.layers = 1;
        framebuffers[i] = device.device->createFramebufferUnique(fb_info);
    }

    MSG msg;
    float alpha = 0;
    while (true)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;
            DispatchMessage(&msg);
        }
        alpha += 0.1f;

        // update uniform
        if (auto map = quad_buffer_mem->map(quad_uniform_vertex_off, 
            quad_uniform_vertex_size + quad_uniform_fragment_size))
        {
            float aspect_ratio = (float)tex->info.extent.height / (float)tex->info.extent.width;
            reinterpret_cast<uniform_vertex_t*>(map.ptr)->model = 
                glm::eulerAngleZ(alpha * 0.1f)
                * glm::scale(glm::vec3(0.5f, aspect_ratio * 0.5f, 1.f));
            reinterpret_cast<uniform_fragment_t*>(map.ptr + quad_uniform_vertex_size)->tint = 
                glm::vec4(1, glm::abs(glm::sin(alpha)), 1, 1);
        }

        auto next_image = device.device->acquireNextImageKHR(*device.swapchain, UINT64_MAX, nullptr, *fence);
        if (next_image.result == vk::Result::eSuccess)
        {
            cmd->reset();
            std::array color{ 1.f, 0.f, 0.f, 1.f };
            cmd->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
            vk::ImageMemoryBarrier barrier;
            barrier.image = swapchain_images[next_image.value];
            barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            cmd->pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::DependencyFlagBits::eByRegion, nullptr, nullptr, barrier);
            std::vector<vk::ClearValue> clear_values{
                vk::ClearColorValue(color),
            };
            vk::RenderPassBeginInfo renderpass_begin_info;
            renderpass_begin_info.renderPass = *renderpass;
            renderpass_begin_info.framebuffer = *framebuffers[next_image.value];
            renderpass_begin_info.renderArea = scissor;
            renderpass_begin_info.setClearValues(clear_values);
            cmd->beginRenderPass(renderpass_begin_info, vk::SubpassContents::eInline);
            {
                cmd->bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
                cmd->bindVertexBuffers(0, *quad_buffer, { quad_vertices_off });
                cmd->bindIndexBuffer(*quad_buffer, 0, vk::IndexType::eUint32);
                cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, *descrset, nullptr);
                cmd->drawIndexed(quad_indices.size(), 1, 0, 0, 0);
            }
            cmd->endRenderPass();
            cmd->end();

            vk::SubmitInfo submit_info;
            vk::CommandBuffer vkcmd = *cmd;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &vkcmd;
            device.q.submit(submit_info);
            device.q.waitIdle();

            vk::Result present_result;
            vk::PresentInfoKHR present_info;
            present_info.setSwapchains(*device.swapchain);
            present_info.pImageIndices = &next_image.value;
            present_info.pResults = &present_result;
            device.q.presentKHR(present_info);
            device.q.waitIdle();
        }
        device.device->waitForFences(*fence, true, UINT64_MAX);
        device.device->resetFences(*fence);

    }
    descrset.reset();
    return EXIT_SUCCESS;
}
