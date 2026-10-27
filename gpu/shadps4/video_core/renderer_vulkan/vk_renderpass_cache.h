// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <boost/container/static_vector.hpp>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

struct CompatibleRenderPassKey {
    u32 num_color_attachments{0};
    std::array<vk::Format, 8> color_formats{};
    vk::Format depth_format{vk::Format::eUndefined};
    vk::Format stencil_format{vk::Format::eUndefined};
    vk::SampleCountFlagBits samples{vk::SampleCountFlagBits::e1};

    bool operator==(const CompatibleRenderPassKey& o) const noexcept {
        return std::memcmp(this, &o, sizeof(CompatibleRenderPassKey)) == 0;
    }
};
static_assert(std::has_unique_object_representations_v<CompatibleRenderPassKey>);

struct ExecutionRenderPassKey {
    CompatibleRenderPassKey compat{};
    std::array<vk::AttachmentLoadOp, 8> color_load_ops{};
    std::array<vk::AttachmentStoreOp, 8> color_store_ops{};
    std::array<vk::ImageLayout, 8> color_layouts{};
    vk::AttachmentLoadOp depth_load_op{vk::AttachmentLoadOp::eDontCare};
    vk::AttachmentStoreOp depth_store_op{vk::AttachmentStoreOp::eDontCare};
    vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
    vk::AttachmentLoadOp stencil_load_op{vk::AttachmentLoadOp::eDontCare};
    vk::AttachmentStoreOp stencil_store_op{vk::AttachmentStoreOp::eDontCare};
    vk::ImageLayout stencil_layout{vk::ImageLayout::eUndefined};

    bool operator==(const ExecutionRenderPassKey& o) const noexcept {
        return std::memcmp(this, &o, sizeof(ExecutionRenderPassKey)) == 0;
    }
};
static_assert(std::has_unique_object_representations_v<ExecutionRenderPassKey>);

struct FramebufferKey {
    vk::RenderPass render_pass{};
    u32 num_attachments{0};
    u32 width{0};
    u32 height{0};
    u32 layers{0};
    std::array<vk::ImageView, 9> attachments{}; // 8 color + 1 depth/stencil

    bool operator==(const FramebufferKey& o) const noexcept {
        return std::memcmp(this, &o, sizeof(FramebufferKey)) == 0;
    }
};
static_assert(std::has_unique_object_representations_v<FramebufferKey>);

} // namespace Vulkan

namespace std {
template <>
struct hash<Vulkan::CompatibleRenderPassKey> {
    size_t operator()(const Vulkan::CompatibleRenderPassKey& k) const noexcept {
        size_t h = 0;
        const auto* ptr = reinterpret_cast<const size_t*>(&k);
        constexpr size_t count = sizeof(k) / sizeof(size_t);
        for (size_t i = 0; i < count; ++i) {
            h ^= ptr[i] + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

template <>
struct hash<Vulkan::ExecutionRenderPassKey> {
    size_t operator()(const Vulkan::ExecutionRenderPassKey& k) const noexcept {
        size_t h = 0;
        const auto* ptr = reinterpret_cast<const size_t*>(&k);
        constexpr size_t count = sizeof(k) / sizeof(size_t);
        for (size_t i = 0; i < count; ++i) {
            h ^= ptr[i] + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

template <>
struct hash<Vulkan::FramebufferKey> {
    size_t operator()(const Vulkan::FramebufferKey& k) const noexcept {
        size_t h = 0;
        const auto* ptr = reinterpret_cast<const size_t*>(&k);
        constexpr size_t count = sizeof(k) / sizeof(size_t);
        for (size_t i = 0; i < count; ++i) {
            h ^= ptr[i] + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};
} // namespace std

namespace Vulkan {

class RenderPassCache {
public:
    explicit RenderPassCache(vk::Device device);
    ~RenderPassCache();

    vk::RenderPass GetOrCreateCompatibleRenderPass(const CompatibleRenderPassKey& key);
    vk::RenderPass GetOrCreateExecutionRenderPass(const ExecutionRenderPassKey& key);

private:
    vk::UniqueRenderPass CreateCompatibleRenderPass(const CompatibleRenderPassKey& key);
    vk::UniqueRenderPass CreateExecutionRenderPass(const ExecutionRenderPassKey& key);

    vk::Device device;
    std::mutex mutex;
    std::unordered_map<CompatibleRenderPassKey, vk::UniqueRenderPass> compat_cache;
    std::unordered_map<ExecutionRenderPassKey, vk::UniqueRenderPass> exec_cache;
};

class FramebufferCache {
public:
    explicit FramebufferCache(vk::Device device);
    ~FramebufferCache();

    vk::Framebuffer GetOrCreateFramebuffer(const FramebufferKey& key);
    void Clear();

private:
    vk::Device device;
    std::mutex mutex;
    std::unordered_map<FramebufferKey, vk::UniqueFramebuffer> cache;
};

} // namespace Vulkan
