// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "video_core/renderer_vulkan/vk_renderpass_cache.h"

using namespace Vulkan;

int main() {
    std::printf("Running test_vulkan12_overlay...\n");

    // 1. Validate ImGui overlay ExecutionRenderPassKey parameters
    {
        ExecutionRenderPassKey overlay_key{};
        overlay_key.compat.num_color_attachments = 1;
        overlay_key.compat.color_formats[0] = vk::Format::eB8G8R8A8Unorm;
        overlay_key.compat.depth_format = vk::Format::eUndefined;
        overlay_key.compat.stencil_format = vk::Format::eUndefined;
        overlay_key.compat.samples = vk::SampleCountFlagBits::e1;
        overlay_key.color_load_ops[0] = vk::AttachmentLoadOp::eLoad;
        overlay_key.color_store_ops[0] = vk::AttachmentStoreOp::eStore;
        overlay_key.color_layouts[0] = vk::ImageLayout::eColorAttachmentOptimal;

        assert(overlay_key.compat.num_color_attachments == 1);
        assert(overlay_key.color_load_ops[0] == vk::AttachmentLoadOp::eLoad);
        assert(overlay_key.color_store_ops[0] == vk::AttachmentStoreOp::eStore);
        assert(overlay_key.color_layouts[0] == vk::ImageLayout::eColorAttachmentOptimal);
        std::printf("  [PASS] ImGui ExecutionRenderPassKey parameters validated\n");
    }

    // 2. Validate FramebufferKey creation for overlay at 720p and 1080p
    {
        const vk::RenderPass mock_rp = reinterpret_cast<VkRenderPass>(0x12345678);
        const vk::ImageView mock_view1 = reinterpret_cast<VkImageView>(0xAAAAAAAA);
        const vk::ImageView mock_view2 = reinterpret_cast<VkImageView>(0xBBBBBBBB);

        FramebufferKey fb_720p{};
        fb_720p.render_pass = mock_rp;
        fb_720p.num_attachments = 1;
        fb_720p.attachments[0] = mock_view1;
        fb_720p.width = 1280;
        fb_720p.height = 720;
        fb_720p.layers = 1;

        FramebufferKey fb_720p_same = fb_720p;
        auto h1 = std::hash<FramebufferKey>{}(fb_720p);
        auto h2 = std::hash<FramebufferKey>{}(fb_720p_same);
        assert(fb_720p == fb_720p_same);
        assert(h1 == h2);

        FramebufferKey fb_720p_diff_view = fb_720p;
        fb_720p_diff_view.attachments[0] = mock_view2;
        assert(!(fb_720p == fb_720p_diff_view));

        FramebufferKey fb_1080p{};
        fb_1080p.render_pass = mock_rp;
        fb_1080p.num_attachments = 1;
        fb_1080p.attachments[0] = mock_view1;
        fb_1080p.width = 1920;
        fb_1080p.height = 1080;
        fb_1080p.layers = 1;

        assert(!(fb_720p == fb_1080p));
        std::printf("  [PASS] Overlay FramebufferKey hashing and distinct resolution keys validated\n");
    }

    std::printf("test_vulkan12_overlay passed successfully!\n");
    return 0;
}
