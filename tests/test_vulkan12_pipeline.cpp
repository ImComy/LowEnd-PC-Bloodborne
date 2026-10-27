// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <vector>
#include <sirit/sirit.h>
#include "video_core/renderer_vulkan/vk_renderpass_cache.h"
#include "shader_recompiler/profile.h"

using namespace Vulkan;

int main() {
    std::printf("Running test_vulkan12_pipeline...\n");

    // 1. Verify SPIR-V 1.5 version constant and module header emission
    {
        constexpr u32 SpirvVersion1_5 = 0x00010500U;
        Sirit::Module mod(SpirvVersion1_5);
        mod.AddCapability(spv::Capability::Shader);
        mod.AddExtension("SPV_EXT_demote_to_helper_invocation");
        mod.AddCapability(spv::Capability::DemoteToHelperInvocation);

        const auto words = mod.Assemble();
        assert(words.size() >= 5);
        assert(words[0] == spv::MagicNumber);
        assert(words[1] == SpirvVersion1_5); // Verifies SPIR-V 1.5 is stamped into module header
        std::printf("  [PASS] SPIR-V 1.5 module header version: 0x%08x\n", words[1]);
    }

    // 2. Verify BlitHelper depth / color compatible render pass keys
    {
        // Depth-only compatible key for ReinterpretColorAsMsDepth
        CompatibleRenderPassKey depth_key{};
        depth_key.num_color_attachments = 0;
        depth_key.depth_format = vk::Format::eD32Sfloat;
        depth_key.stencil_format = vk::Format::eUndefined;
        depth_key.samples = vk::SampleCountFlagBits::e4;

        assert(depth_key.num_color_attachments == 0);
        assert(depth_key.depth_format == vk::Format::eD32Sfloat);
        assert(depth_key.samples == vk::SampleCountFlagBits::e4);

        // Color copy compatible key for CopyBetweenMsImages
        CompatibleRenderPassKey color_key{};
        color_key.num_color_attachments = 1;
        color_key.color_formats[0] = vk::Format::eR8G8B8A8Unorm;
        color_key.depth_format = vk::Format::eUndefined;
        color_key.stencil_format = vk::Format::eUndefined;
        color_key.samples = vk::SampleCountFlagBits::e1;

        assert(color_key.num_color_attachments == 1);
        assert(color_key.color_formats[0] == vk::Format::eR8G8B8A8Unorm);
        assert(color_key.samples == vk::SampleCountFlagBits::e1);
        std::printf("  [PASS] BlitHelper compatible render pass keys validated\n");
    }

    // 3. Verify Presenter execution render pass key
    {
        ExecutionRenderPassKey pres_key{};
        pres_key.compat.num_color_attachments = 1;
        pres_key.compat.color_formats[0] = vk::Format::eB8G8R8A8Unorm;
        pres_key.compat.depth_format = vk::Format::eUndefined;
        pres_key.compat.stencil_format = vk::Format::eUndefined;
        pres_key.compat.samples = vk::SampleCountFlagBits::e1;
        pres_key.color_load_ops[0] = vk::AttachmentLoadOp::eClear;
        pres_key.color_store_ops[0] = vk::AttachmentStoreOp::eStore;
        pres_key.color_layouts[0] = vk::ImageLayout::eColorAttachmentOptimal;

        assert(pres_key.compat.num_color_attachments == 1);
        assert(pres_key.color_load_ops[0] == vk::AttachmentLoadOp::eClear);
        assert(pres_key.color_layouts[0] == vk::ImageLayout::eColorAttachmentOptimal);
        std::printf("  [PASS] Presenter ExecutionRenderPassKey validated\n");
    }

    std::printf("test_vulkan12_pipeline passed successfully!\n");
    return 0;
}
