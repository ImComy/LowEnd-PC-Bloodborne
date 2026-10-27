# Feasibility Analysis: Porting Bloodborne PC to Vulkan 1.2 on NVIDIA Quadro K4200
**Document**: `docs/vulkan12_k4200/FEASIBILITY_ANALYSIS.md`  
**Target Hardware**: NVIDIA Quadro K4200 (Kepler GK104GL, 4 GB GDDR5, Driver 475.14)  
**Target API**: Vulkan 1.2.175  

---

## 1. Executive Summary & Feasibility Verdict

### Verdict: **FEASIBLE WITH ARCHITECTURAL COMPATIBILITY LAYER**
Running Bloodborne on the Quadro K4200 is **technically feasible**, but cannot be achieved by simply bumping `TargetVulkanApiVersion` down from `1.3` to `1.2`. 

The upstream renderer (`shadPS4` video core) was architected from the ground up assuming **Vulkan 1.3 Dynamic Rendering (`VK_KHR_dynamic_rendering` / `vkCmdBeginRendering`)**, which decouples render passes from framebuffers. Kepler hardware (NVIDIA driver 475.14) has **no hardware/driver support for Dynamic Rendering**.

Therefore, feasibility hinges upon creating a **zero-overhead RenderPass & Framebuffer Translation Cache** inside `vk_scheduler` and `vk_graphics_pipeline` to translate `VkRenderingInfo` into traditional `VkRenderPass` and `VkFramebuffer` pairs transparently.

---

## 2. Hardware Architecture Comparison: PS4 Liverpool vs. Quadro K4200

| Metric / Feature | PlayStation 4 (GCN 1.1 "Liverpool") | NVIDIA Quadro K4200 (Kepler GK104GL) | Architectural Impact |
|---|---|---|---|
| **Release Year** | 2013 | 2014 | Same hardware generation |
| **Compute Units / SMs** | 18 CUs (1152 Stream Processors) | 7 SMX (1344 CUDA Cores) | Raw FP32 compute is comparable (~1.84 TFLOPs vs ~2.1 TFLOPs) |
| **GPU Clock** | 800 MHz | 771 - 784 MHz | Nearly identical clocks |
| **Subgroup / Wavefront Size** | **64 (Wave64)** | **32 (Warp32)** | PS4 code relies on 64-thread waves. Kepler executes 32-thread warps. Subgroup size control must handle 32. |
| **Memory Capacity** | 8 GB GDDR5 Unified (~5.5 GB for game) | **4 GB GDDR5 Dedicated** | **VRAM bottleneck**. Bloodborne easily uses 5+ GB VRAM. Requires aggressive VRAM management and 720p/900p target. |
| **Memory Bus & Bandwidth** | 256-bit @ 176.0 GB/s | 256-bit @ 173.0 GB/s | Memory bandwidth is virtually identical (~173 vs 176 GB/s) |
| **Vulkan Feature Level** | N/A (GNM low-level console API) | **Vulkan 1.2.175** (Driver 475.14 EOL) | Lacks Vulkan 1.3 core features |

---

## 3. Deep Technical Gap Analysis

### 3.1. The Critical Blocker: Dynamic Rendering
- **The Issue**: In Vulkan 1.3, `vkCmdBeginRendering` replaces `vkCmdBeginRenderPass`. All graphics pipelines are created with `VkPipelineRenderingCreateInfo` instead of a `VkRenderPass`.
- **K4200 Reality**: The 475.14 driver does not support `VK_KHR_dynamic_rendering`. Attempting to call `vkCmdBeginRendering` causes an immediate null function pointer dereference or driver validation crash.
- **The Solution**: 
  - Build a `RenderPassCache` that generates lightweight compatible `VkRenderPass` handles based on `(color_formats, depth_format, stencil_format, load_ops, store_ops, samples)`.
  - Build a `FramebufferCache` that binds `(VkImageView attachments, width, height, layers)` to `VkFramebuffer`.
  - Implement a dual-path `Scheduler::BeginRendering` / `Scheduler::EndRendering`:
    - If `features.dynamicRendering` is supported $\to$ call `cmdbuf.beginRendering(...)`.
    - If fallback $\to$ retrieve/create cached `VkRenderPass` and `VkFramebuffer`, bind via `cmdbuf.beginRenderPass(...)`.

### 3.2. Vulkan 1.3 Feature Struct Chaining
- **The Issue**: `Instance::CreateDevice()` chains `VkPhysicalDeviceVulkan13Features` and `VkPhysicalDeviceVulkan13Properties`.
- **K4200 Reality**: On Vulkan 1.2 drivers, requesting Vulkan 1.3 feature structs causes `VK_ERROR_INITIALIZATION_FAILED` or returns garbage/zeroed values.
- **The Solution**:
  - Dynamically unlink `VkPhysicalDeviceVulkan13Features` and bypass `VkPhysicalDeviceVulkan13Properties` if `apiVersion < VK_API_VERSION_1_3`.
  - For extensions that exist in Vulkan 1.2 via extensions (e.g., `VK_KHR_synchronization2`, `VK_EXT_shader_demote_to_helper_invocation`, `VK_EXT_subgroup_size_control`, `VK_KHR_buffer_device_address`), query and link their specific extension structs (`VkPhysicalDeviceSynchronization2FeaturesKHR`, etc.).

### 3.3. Subgroup Size (Wave64 vs Warp32)
- **The Issue**: PS4 compute and pixel shaders use 64-thread wavefronts. The upstream renderer requests `requiredSubgroupSize = 64`.
- **K4200 Reality**: Kepler only supports 32-thread warps (`minSubgroupSize = 32`, `maxSubgroupSize = 32`). Requesting 64 will fail pipeline creation.
- **The Solution**:
  - Check `instance.IsSubgroupSize64Supported()` before attaching `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo`.
  - Since Kepler reports `maxSubgroupSize = 32`, do not force 64. Shaders compiled through SPIR-V will execute across 2 warps of 32 automatically.

### 3.4. Missing Shader Features (`shaderFloat16`, `shaderBufferInt64Atomics`)
- **The Issue**: FSR 4 and certain post-processing compute shaders in bbport use 16-bit floating point arithmetic (`shaderFloat16`).
- **K4200 Reality**: Kepler hardware does not have native FP16 execution units (it was introduced with Maxwell/Pascal). It reports `shaderFloat16 = false`.
- **The Solution**:
  - Disable FSR 4 and DLSS on Kepler.
  - Rely on native resolution, FSR 1, or bilinear/Bicubic/CAS temporal scaling.
  - Compile shaders with 32-bit float fallback if 16-bit math is requested.

### 3.5. 4 GB VRAM Budget Constraint
- **The Issue**: Bloodborne's scene textures, shadow maps, geometry arenas, and render targets can exceed 5 GB at 1080p, triggering Windows D3D/Vulkan memory eviction over PCIe 3.0 x16. Eviction stalls drop framerates from 30 FPS down to single digits.
- **The Solution**:
  - Enable the low-VRAM profile already present in `run.py` (`tools/gpu_capabilities.c --vram` reports `4096 1`).
  - Cap target resolution to **720p (1280x720)** or **900p (1600x900)**.
  - Clamp staging buffer arena sizes to 256 MiB instead of 1 GiB.

---

## 4. Complexity & Risk Assessment

| Subsystem | Complexity | Risk Level | Rationale |
|---|---|---|---|
| **1. Vulkan 1.2 Device Init & Extension Mapping** | **Low-Medium** | Low | Mapping Vulkan 1.3 core features to 1.2 KHR/EXT equivalents is straightforward. |
| **2. Dynamic Rendering Fallback (Pass & Framebuffer Cache)** | **High** | Medium | Requires robust hash caching of `VkRenderPass` and `VkFramebuffer` to avoid runtime compilation hitching. |
| **3. Graphics & Compute Pipeline Creation** | **Medium-High** | Medium | Pipelines must associate with a compatible `VkRenderPass` rather than `VkPipelineRenderingCreateInfo`. |
| **4. Subgroup & Shader Math Adaptations** | **Medium** | Low | Fallback paths already exist in SPIR-V translation layer; need to ensure they activate on Kepler. |
| **5. VRAM & Kepler Performance Tuning** | **Medium** | Low | Target 720p/30fps ensures Kepler's 1344 CUDA cores and 4GB memory remain within thermal and bandwidth budget. |

---

## 5. Realistic Effort & Timeline Estimation

Assuming focused senior engineering execution:

1. **Phase 1: Vulkan 1.2 Device Lifecycle & Clean Probe** $\to$ **1 - 2 Days**
   - Clean up instance/device creation. Ensure `bb-probe.exe` runs and initializes Vulkan without errors on the K4200.
2. **Phase 2: RenderPass & Framebuffer Compatibility Engine** $\to$ **3 - 5 Days**
   - Implement `RenderPassCache` and `FramebufferCache`.
   - Update `Scheduler::BeginRendering` and `EndRendering` to select the fallback path.
3. **Phase 3: Pipeline Compatibility Matrix** $\to$ **2 - 3 Days**
   - Update `GraphicsPipeline` to pass cached `VkRenderPass` instead of `VkPipelineRenderingCreateInfo` when running on Vulkan 1.2.
   - Update `blit_helper.cpp`, `pp_pass.cpp`, and `bbport_overlay.cpp` (ImGui).
4. **Phase 4: Shader & Extension Guards** $\to$ **1 - 2 Days**
   - Guard FP16, int64 atomics, Subgroup64, and ensure SPIR-V compilation targets Kepler-compatible features.
5. **Phase 5: VRAM & Performance Optimization (Smooth 30 FPS Target)** $\to$ **2 - 3 Days**
   - Tune 720p rendering pipeline, low-VRAM staging, verify no GPU-CPU sync stalls.
6. **Phase 6: In-Game Verification & End-to-End Polish** $\to$ **2 Days**
   - Full gameplay verification, title screen, Hunter's Dream, Central Yharnam.

**Total Estimated Engineering Time**: **~11 - 17 Engineering Days** (executed incrementally with verifiable test milestones).
