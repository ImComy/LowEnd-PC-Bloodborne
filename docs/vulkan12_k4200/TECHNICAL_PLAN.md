# Technical Architecture & Execution Plan: Vulkan 1.2 / Kepler K4200
**Document**: `docs/vulkan12_k4200/TECHNICAL_PLAN.md`  
**Target Hardware**: NVIDIA Quadro K4200 (Kepler GK104GL, 4 GB GDDR5, Driver 475.14)  
**Target API**: Vulkan 1.2.175  

---

## 1. Architecture Overview

To enable Bloodborne to run cleanly and smoothly on Vulkan 1.2 without hardware support for Dynamic Rendering, the renderer must be augmented with an internal **RenderPass & Framebuffer Virtualization Layer (RPFVL)**.

```
                      +-----------------------------+
                      |     PS4 Command Stream      |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |   Rasterizer / BlitHelper   |
                      +-----------------------------+
                                     |
                    Requests BeginRendering(State)
                                     |
                                     v
                      +-----------------------------+
                      |   Scheduler::BeginRendering |
                      +-----------------------------+
                                     |
                +--------------------+--------------------+
                |                                         |
   [Dynamic Rendering Supported]              [Vulkan 1.2 Fallback: K4200]
                |                                         |
                v                                         v
   vkCmdBeginRendering(...)                   +-----------------------+
                                              |    RenderPassCache    |
                                              |   FramebufferCache    |
                                              +-----------------------+
                                                          |
                                                          v
                                              vkCmdBeginRenderPass(...)
```

---

## 2. Component Design & Implementation Details

### Component A: Instance & Device Lifecycle (`vk_instance.cpp`, `vk_instance.h`)

#### Objectives:
1. Set `TargetVulkanApiVersion = VK_API_VERSION_1_2`.
2. Do not query or pass `VkPhysicalDeviceVulkan13Features` or `VkPhysicalDeviceVulkan13Properties` if `apiVersion < VK_API_VERSION_1_3`.
3. Query and link extension features via their Vulkan 1.2 KHR/EXT structs:
   - `VkPhysicalDeviceSynchronization2FeaturesKHR` for `VK_KHR_synchronization2`
   - `VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT` for `VK_EXT_shader_demote_to_helper_invocation`
   - `VkPhysicalDeviceSubgroupSizeControlFeaturesEXT` for `VK_EXT_subgroup_size_control`
   - `VkPhysicalDeviceBufferDeviceAddressFeatures` for `VK_KHR_buffer_device_address`
4. Set function pointers:
   - Wire `vkCmdPipelineBarrier2 = vkCmdPipelineBarrier2KHR`
   - Probe `has_dynamic_rendering = instance.IsExtensionSupported("VK_KHR_dynamic_rendering")` (will be false on K4200).
5. Guard unsupported features:
   - Disable `shaderFloat16` (Kepler does not support FP16 math).
   - Disable `shaderBufferInt64Atomics` / `shaderSharedInt64Atomics`.
   - Set `subgroupSize = 32`.

---

### Component B: RenderPass & Framebuffer Virtualization Layer (RPFVL)

To eliminate the need for `vkCmdBeginRendering`, we implement two dedicated, zero-allocation caches in `video_core/renderer_vulkan`:

#### 1. `RenderPassKey` and `RenderPassCache`
Per the Vulkan specification (Section 8.2), pipelines only require *compatible* render passes. A compatibility key consists of:
```cpp
struct CompatibleRenderPassKey {
    u32 num_color_attachments;
    std::array<vk::Format, 8> color_formats;
    vk::Format depth_format;
    vk::Format stencil_format;
    vk::SampleCountFlagBits samples;

    bool operator==(const CompatibleRenderPassKey& other) const = default;
};
```
For actual command recording (`vkCmdBeginRenderPass`), an exact render pass key adds `loadOp`, `storeOp`, and attachment layouts:
```cpp
struct ExecutionRenderPassKey {
    CompatibleRenderPassKey compat;
    std::array<vk::AttachmentLoadOp, 8> color_load_ops;
    std::array<vk::AttachmentStoreOp, 8> color_store_ops;
    vk::AttachmentLoadOp depth_load_op;
    vk::AttachmentStoreOp depth_store_ops;
    // ...
};
```
- Implemented using `std::unordered_map<RenderPassKey, vk::UniqueRenderPass, RenderPassKeyHash>` with mutex locking (or thread-local caches).
- Shaders and pipelines compile against the `CompatibleRenderPass`.

#### 2. `FramebufferKey` and `FramebufferCache`
A framebuffer binds concrete `VkImageView` handles with dimensions:
```cpp
struct FramebufferKey {
    vk::RenderPass render_pass;
    u32 num_attachments;
    std::array<vk::ImageView, 9> attachments; // color + depth/stencil
    u32 width;
    u32 height;
    u32 layers;

    bool operator==(const FramebufferKey& other) const = default;
};
```
- Reuses framebuffers across frames. Cleared on image destruction via garbage collector/destruction listeners.

---

### Component C: Graphics Pipeline Adaptation (`vk_graphics_pipeline.cpp`)

#### In Vulkan 1.3:
```cpp
const vk::GraphicsPipelineCreateInfo pipeline_info = {
    .pNext = &pipeline_rendering_ci, // VkPipelineRenderingCreateInfo
    // ...
};
```

#### In Vulkan 1.2 Fallback:
```cpp
vk::RenderPass compat_render_pass = VK_NULL_HANDLE;
if (!instance.IsDynamicRenderingSupported()) {
    compat_render_pass = render_pass_cache.GetOrCreateCompatibleRenderPass(key);
}

const vk::GraphicsPipelineCreateInfo pipeline_info = {
    .pNext = instance.IsDynamicRenderingSupported() ? &pipeline_rendering_ci : nullptr,
    .renderPass = compat_render_pass,
    .subpass = 0,
    // ...
};
```

---

### Component D: Auxiliary Passes Adaptation

The following auxiliary passes currently call `beginRendering` and must support the RPFVL fallback:
1. **`blit_helper.cpp`**: Depth/stencil and color blit operations.
2. **`host_passes/pp_pass.cpp`**: Post-processing fullscreen quad passes.
3. **`vk_scene_resolution.cpp`**: Dynamic scene scaling passes.
4. **`bbport_overlay.cpp`**: ImGui debug and settings HUD (`ImGui_ImplVulkan_InitInfo` has built-in support for `RenderPass` mode when `UseDynamicRendering = false`).

---

### Component E: Kepler GK104 & 4GB VRAM Optimization Strategy

1. **VRAM Footprint**:
   - Limit default resolution to `1280x720` or `1600x900` in `bbport.ini`.
   - Restrict texture cache memory pool to `2560 MiB` (leaving `1536 MiB` for framebuffers, depth buffers, staging arenas, and Windows DWM).
   - Enable low-VRAM staging mode to prevent host-memory paging stalls.
2. **Compute & Shaders**:
   - Enforce 32-thread subgroup operations.
   - Fall back to standard bilinear or bicubic upscaling; bypass FSR 4 (requires INT8 dot product and FP16 which Kepler lacks).

---

## 3. Phased Implementation Roadmap

```
+------------------------------------------------------------------------+
| Phase 1: Device Init & Feature Unlinking [COMPLETE]                    |
| -> Target: bb-probe.exe and bb-gpu-capabilities.exe pass on K4200      |
+------------------------------------------------------------------------+
                                   |
                                   v
+------------------------------------------------------------------------+
| Phase 2: RenderPass & Framebuffer Virtualization Layer (RPFVL) [COMPLETE]
| -> Target: Standalone unit tests for RenderPass/Framebuffer Cache      |
+------------------------------------------------------------------------+
                                   |
                                   v
+------------------------------------------------------------------------+
| Phase 3: Pipeline Creation & Command Buffer Integration [COMPLETE]     |
| -> Target: vk_graphics_pipeline, vk_scheduler, blit_helper migrated    |
+------------------------------------------------------------------------+
                                   |
                                   v
+------------------------------------------------------------------------+
| Phase 4: Overlay & Auxiliary Passes Migration [COMPLETE]               |
| -> Target: ImGui overlay (bbport_overlay) & post-processing working    |
+------------------------------------------------------------------------+
                                   |
                                   v
+------------------------------------------------------------------------+
| Phase 5: VRAM Budgeting & Kepler Performance Profiling [COMPLETE]      |
| -> Target: Stable 30 FPS at 720p without PCIe memory paging stutters   |
+------------------------------------------------------------------------+
                                   |
                                   v
+------------------------------------------------------------------------+
| Phase 6: In-Game Verification & End-to-End Testing [COMPLETE]          |
| -> Target: Boot to title, Hunter's Dream, Central Yharnam gameplay     |
+------------------------------------------------------------------------+
```

---

## 4. Verification & Validation Metrics

Each phase will be validated with non-destructive, deterministic tests:

1. **Device Creation Test**: `out/bb-probe.exe` returns 0 with no Vulkan loader or validation errors.
2. **Capability Check**: `out/bb-gpu-capabilities.exe` correctly identifies Quadro K4200 and prints VRAM profile (`4096 1`).
3. **Pipeline Creation Test**: Graphics pipeline cache builds known game shaders without `VK_ERROR_UNKNOWN` or missing extension errors.
4. **Rendering Test**: Frame presentation functions correctly to SDL3 window surface without device lost errors (`VK_ERROR_DEVICE_LOST`).
