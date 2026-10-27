// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_renderpass_cache.h"
#include "common/assert.h"

namespace Vulkan {

RenderPassCache::RenderPassCache(vk::Device device_) : device{device_} {}

RenderPassCache::~RenderPassCache() = default;

vk::RenderPass RenderPassCache::GetOrCreateCompatibleRenderPass(const CompatibleRenderPassKey& key) {
    std::lock_guard lock{mutex};
    auto it = compat_cache.find(key);
    if (it != compat_cache.end()) {
        return *it->second;
    }
    auto pass = CreateCompatibleRenderPass(key);
    vk::RenderPass raw = *pass;
    compat_cache.emplace(key, std::move(pass));
    return raw;
}

vk::RenderPass RenderPassCache::GetOrCreateExecutionRenderPass(const ExecutionRenderPassKey& key) {
    std::lock_guard lock{mutex};
    auto it = exec_cache.find(key);
    if (it != exec_cache.end()) {
        return *it->second;
    }
    auto pass = CreateExecutionRenderPass(key);
    vk::RenderPass raw = *pass;
    exec_cache.emplace(key, std::move(pass));
    return raw;
}

vk::UniqueRenderPass RenderPassCache::CreateCompatibleRenderPass(const CompatibleRenderPassKey& key) {
    boost::container::static_vector<vk::AttachmentDescription, 9> attachments;
    boost::container::static_vector<vk::AttachmentReference, 8> color_refs;
    vk::AttachmentReference depth_ref{};
    bool has_depth_ref = false;

    for (u32 i = 0; i < key.num_color_attachments; ++i) {
        if (key.color_formats[i] == vk::Format::eUndefined) {
            color_refs.push_back(vk::AttachmentReference{
                .attachment = VK_ATTACHMENT_UNUSED,
                .layout = vk::ImageLayout::eUndefined,
            });
            continue;
        }
        attachments.push_back(vk::AttachmentDescription{
            .format = key.color_formats[i],
            .samples = key.samples,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
        });
        color_refs.push_back(vk::AttachmentReference{
            .attachment = static_cast<u32>(attachments.size() - 1),
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        });
    }

    const bool has_depth = key.depth_format != vk::Format::eUndefined;
    const bool has_stencil = key.stencil_format != vk::Format::eUndefined;
    if (has_depth || has_stencil) {
        const auto ds_format = has_depth ? key.depth_format : key.stencil_format;
        attachments.push_back(vk::AttachmentDescription{
            .format = ds_format,
            .samples = key.samples,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        });
        depth_ref = vk::AttachmentReference{
            .attachment = static_cast<u32>(attachments.size() - 1),
            .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        };
        has_depth_ref = true;
    }

    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = static_cast<u32>(color_refs.size()),
        .pColorAttachments = color_refs.data(),
        .pDepthStencilAttachment = has_depth_ref ? &depth_ref : nullptr,
    };

    const std::array<vk::SubpassDependency, 2> dependencies = {
        vk::SubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                             vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                             vk::AccessFlagBits::eColorAttachmentWrite |
                             vk::AccessFlagBits::eDepthStencilAttachmentRead |
                             vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        },
        vk::SubpassDependency{
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eLateFragmentTests,
            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                             vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                             vk::AccessFlagBits::eShaderRead,
        },
    };

    const vk::RenderPassCreateInfo rp_ci = {
        .attachmentCount = static_cast<u32>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<u32>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };

    auto [result, pass] = device.createRenderPassUnique(rp_ci);
    ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create compatible render pass: {}",
               vk::to_string(result));
    return std::move(pass);
}

vk::UniqueRenderPass RenderPassCache::CreateExecutionRenderPass(const ExecutionRenderPassKey& key) {
    boost::container::static_vector<vk::AttachmentDescription, 9> attachments;
    boost::container::static_vector<vk::AttachmentReference, 8> color_refs;
    vk::AttachmentReference depth_ref{};
    bool has_depth_ref = false;

    for (u32 i = 0; i < key.compat.num_color_attachments; ++i) {
        if (key.compat.color_formats[i] == vk::Format::eUndefined) {
            color_refs.push_back(vk::AttachmentReference{
                .attachment = VK_ATTACHMENT_UNUSED,
                .layout = vk::ImageLayout::eUndefined,
            });
            continue;
        }
        attachments.push_back(vk::AttachmentDescription{
            .format = key.compat.color_formats[i],
            .samples = key.compat.samples,
            .loadOp = key.color_load_ops[i],
            .storeOp = key.color_store_ops[i],
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = key.color_layouts[i],
            .finalLayout = key.color_layouts[i],
        });
        color_refs.push_back(vk::AttachmentReference{
            .attachment = static_cast<u32>(attachments.size() - 1),
            .layout = key.color_layouts[i] == vk::ImageLayout::eGeneral
                          ? vk::ImageLayout::eGeneral
                          : vk::ImageLayout::eColorAttachmentOptimal,
        });
    }

    const bool has_depth = key.compat.depth_format != vk::Format::eUndefined;
    const bool has_stencil = key.compat.stencil_format != vk::Format::eUndefined;
    if (has_depth || has_stencil) {
        const auto ds_format = has_depth ? key.compat.depth_format : key.compat.stencil_format;
        attachments.push_back(vk::AttachmentDescription{
            .format = ds_format,
            .samples = key.compat.samples,
            .loadOp = key.depth_load_op,
            .storeOp = key.depth_store_op,
            .stencilLoadOp = key.stencil_load_op,
            .stencilStoreOp = key.stencil_store_op,
            .initialLayout = key.depth_layout,
            .finalLayout = key.depth_layout,
        });
        depth_ref = vk::AttachmentReference{
            .attachment = static_cast<u32>(attachments.size() - 1),
            .layout = key.depth_layout == vk::ImageLayout::eGeneral
                          ? vk::ImageLayout::eGeneral
                          : vk::ImageLayout::eDepthStencilAttachmentOptimal,
        };
        has_depth_ref = true;
    }

    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = static_cast<u32>(color_refs.size()),
        .pColorAttachments = color_refs.data(),
        .pDepthStencilAttachment = has_depth_ref ? &depth_ref : nullptr,
    };

    const std::array<vk::SubpassDependency, 2> dependencies = {
        vk::SubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                             vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                             vk::AccessFlagBits::eColorAttachmentWrite |
                             vk::AccessFlagBits::eDepthStencilAttachmentRead |
                             vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        },
        vk::SubpassDependency{
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eLateFragmentTests,
            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                             vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                             vk::AccessFlagBits::eShaderRead,
        },
    };

    const vk::RenderPassCreateInfo rp_ci = {
        .attachmentCount = static_cast<u32>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<u32>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };

    auto [result, pass] = device.createRenderPassUnique(rp_ci);
    ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create execution render pass: {}",
               vk::to_string(result));
    return std::move(pass);
}

FramebufferCache::FramebufferCache(vk::Device device_) : device{device_} {}

FramebufferCache::~FramebufferCache() = default;

vk::Framebuffer FramebufferCache::GetOrCreateFramebuffer(const FramebufferKey& key) {
    std::lock_guard lock{mutex};
    auto it = cache.find(key);
    if (it != cache.end()) {
        return *it->second;
    }
    const vk::FramebufferCreateInfo fb_ci = {
        .renderPass = key.render_pass,
        .attachmentCount = key.num_attachments,
        .pAttachments = key.attachments.data(),
        .width = key.width,
        .height = key.height,
        .layers = key.layers,
    };
    auto [result, fb] = device.createFramebufferUnique(fb_ci);
    ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create framebuffer: {}",
               vk::to_string(result));
    vk::Framebuffer raw = *fb;
    cache.emplace(key, std::move(fb));
    return raw;
}

void FramebufferCache::Clear() {
    std::lock_guard lock{mutex};
    cache.clear();
}

} // namespace Vulkan
