// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <unordered_map>
#include "video_core/renderer_vulkan/vk_renderpass_cache.h"

using namespace Vulkan;

int main() {
    std::printf("Running test_vulkan12_renderpass...\n");

    // 1. Static assertion checks
    static_assert(std::has_unique_object_representations_v<CompatibleRenderPassKey>);
    static_assert(std::has_unique_object_representations_v<ExecutionRenderPassKey>);
    static_assert(std::has_unique_object_representations_v<FramebufferKey>);

    // 2. CompatibleRenderPassKey hashing and lookup test
    {
        CompatibleRenderPassKey key1{};
        key1.num_color_attachments = 2;
        key1.color_formats[0] = vk::Format::eR8G8B8A8Unorm;
        key1.color_formats[1] = vk::Format::eR16G16B16A16Sfloat;
        key1.depth_format = vk::Format::eD32Sfloat;
        key1.stencil_format = vk::Format::eUndefined;
        key1.samples = vk::SampleCountFlagBits::e1;

        CompatibleRenderPassKey key2 = key1;

        std::hash<CompatibleRenderPassKey> hasher;
        assert(hasher(key1) == hasher(key2));
        assert(key1 == key2);

        CompatibleRenderPassKey key3 = key1;
        key3.depth_format = vk::Format::eD24UnormS8Uint;
        assert(!(key1 == key3));

        std::unordered_map<CompatibleRenderPassKey, int> test_map;
        test_map[key1] = 42;
        assert(test_map.find(key2) != test_map.end());
        assert(test_map[key2] == 42);
        assert(test_map.find(key3) == test_map.end());
    }

    // 3. ExecutionRenderPassKey hashing and lookup test
    {
        ExecutionRenderPassKey key1{};
        key1.compat.num_color_attachments = 1;
        key1.compat.color_formats[0] = vk::Format::eR8G8B8A8Unorm;
        key1.color_load_ops[0] = vk::AttachmentLoadOp::eClear;
        key1.color_store_ops[0] = vk::AttachmentStoreOp::eStore;
        key1.color_layouts[0] = vk::ImageLayout::eColorAttachmentOptimal;
        key1.depth_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        key1.depth_load_op = vk::AttachmentLoadOp::eClear;
        key1.depth_store_op = vk::AttachmentStoreOp::eStore;

        ExecutionRenderPassKey key2 = key1;
        std::hash<ExecutionRenderPassKey> hasher;
        assert(hasher(key1) == hasher(key2));
        assert(key1 == key2);

        ExecutionRenderPassKey key3 = key1;
        key3.color_load_ops[0] = vk::AttachmentLoadOp::eLoad;
        assert(!(key1 == key3));

        std::unordered_map<ExecutionRenderPassKey, int> test_map;
        test_map[key1] = 100;
        assert(test_map.find(key2) != test_map.end());
        assert(test_map[key2] == 100);
        assert(test_map.find(key3) == test_map.end());
    }

    // 4. FramebufferKey hashing and lookup test
    {
        FramebufferKey fb1{};
        fb1.width = 1920;
        fb1.height = 1080;
        fb1.layers = 1;
        fb1.num_attachments = 2;
        fb1.attachments[0] = vk::ImageView(reinterpret_cast<VkImageView>(0x1000));
        fb1.attachments[1] = vk::ImageView(reinterpret_cast<VkImageView>(0x2000));

        FramebufferKey fb2 = fb1;
        std::hash<FramebufferKey> hasher;
        assert(hasher(fb1) == hasher(fb2));
        assert(fb1 == fb2);

        FramebufferKey fb3 = fb1;
        fb3.height = 720;
        assert(!(fb1 == fb3));

        std::unordered_map<FramebufferKey, int> test_map;
        test_map[fb1] = 777;
        assert(test_map.find(fb2) != test_map.end());
        assert(test_map[fb2] == 777);
        assert(test_map.find(fb3) == test_map.end());
    }

    std::printf("test_vulkan12_renderpass passed successfully!\n");
    return 0;
}
