# Project History & Engineering Audit Log
**Document**: `docs/vulkan12_k4200/HISTORY.md`  
**Workspace**: `i:/Games/Bloodborne/development/bloodborne_pc`  

---

## 2026-10-26: Initial Assessment & Hardware Audit

### 1. Initial State Analysis
A review of the repository working tree revealed existing uncommitted modifications initiated by the user:
- `build.sh`: Added MSYS2 CLANG64 pathing to ensure tools (`clang`, `ninja`, `cmake`) are found.
- `gpu/shadps4/video_core/renderer_vulkan/vk_platform.h`: Changed `TargetVulkanApiVersion` from `VK_API_VERSION_1_3` to `VK_API_VERSION_1_2`.
- `gpu/shadps4/video_core/renderer_vulkan/vk_instance.h`: Added fallback logic in `GetMaxBufferSize()` when `vk13_props.maxBufferSize == 0`.
- `gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp`:
  - Added checks for `properties.apiVersion < VK_API_VERSION_1_3` to request `VK_KHR_synchronization2`, `VK_EXT_shader_demote_to_helper_invocation`, and `VK_EXT_subgroup_size_control`.
  - Added unlinking of `vk::PhysicalDeviceVulkan13Features`.
  - Added function pointer remapping for `vkCmdPipelineBarrier2` to `vkCmdPipelineBarrier2KHR`.
- `tools/gpu_capabilities.c`, `tools/bridge_helper/bridge_helper.c`, `tests/test_arena_overlap.c`: Adjusted `apiVersion` checks to `VK_API_VERSION_1_2`.

### 2. Live Hardware Audit Results (via `vulkaninfo`)
Auditing the user's local system revealed:
- **Device**: NVIDIA Quadro K4200 (DeviceID `0x11b4`, VendorID `0x10de`)
- **Microarchitecture**: NVIDIA Kepler (GK104GL)
- **Vulkan Driver**: NVIDIA Proprietary 475.14 (Legacy EOL branch)
- **Vulkan API Version**: `1.2.175`
- **Supported Key Features**:
  - `VK_KHR_synchronization2` (rev 1)
  - `VK_KHR_buffer_device_address` (rev 1)
  - `VK_EXT_robustness2` (`robustBufferAccess2=true`, `robustImageAccess2=true`, `nullDescriptor=true`)
  - `VK_EXT_shader_demote_to_helper_invocation` (rev 1)
  - `VK_EXT_subgroup_size_control` (rev 2, fixed size 32)
  - `VK_EXT_custom_border_color`
  - `VK_KHR_timeline_semaphore`
  - `VK_KHR_workgroup_memory_explicit_layout`
- **Missing / Unsupported Features**:
  - `VK_KHR_dynamic_rendering` (completely absent)
  - `VkPhysicalDeviceVulkan13Features` / `VkPhysicalDeviceVulkan13Properties`
  - `shaderFloat16` (Kepler does not have FP16 execution units)
  - `shaderBufferInt64Atomics` / `shaderSharedInt64Atomics`
  - `shaderOutputLayer`
  - Subgroup size 64 (Kepler warp size is 32)

### 3. Key Findings & Strategic Decision
The user's initial edits were headed in the right direction for feature struct unlinking and extension loading, but hit an architectural barrier:
**The entire render pipeline in shadPS4 relies on Vulkan 1.3 Dynamic Rendering (`vkCmdBeginRendering`)**, which cannot be enabled by driver extensions on Kepler.

**Conclusion**: Created the `docs/vulkan12_k4200/` project documentation suite establishing:
1. `AGENTS.md`: Complete operating instructions and context for any future agent.
2. `FEASIBILITY_ANALYSIS.md`: Detailed feasibility study, hardware comparison, complexity rating, and timeline.
3. `TECHNICAL_PLAN.md`: A 6-phase engineering plan centered on an internal RenderPass/Framebuffer Virtualization Layer.
4. `GOAL.md`: Clear performance, stability, and acceptance criteria.

---

## 2026-10-26: Phase 1 Execution & Verification

### Implementation Summary
- **Updated `vk_instance.h`**:
  - Added `IsDynamicRenderingSupported()` query method.
  - Updated `IsSubgroupSizeControlEnabled()` and `IsSubgroupSize64Supported()` to handle both Vulkan 1.3 core and Vulkan 1.2 `VK_EXT_subgroup_size_control` (with K4200's fixed 32-thread limit).
  - Added `sync2_features`, `demote_features`, `subgroup_size_control_features`, `dynamic_rendering_features`, and `subgroup_size_control_props`.
- **Updated `vk_instance.cpp`**:
  - Expanded `feature_chain` and `properties_chain` to query Vulkan 1.2 extension features.
  - Chained `VkPhysicalDeviceSynchronization2FeaturesKHR`, `VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT`, `VkPhysicalDeviceSubgroupSizeControlFeaturesEXT`, and `VkPhysicalDeviceDynamicRenderingFeaturesKHR` into `device_chain`.
  - Implemented dynamic unlinking:
    - If `apiVersion >= VK_API_VERSION_1_3`, unlinks extension structs in favor of Vulkan 1.3 core.
    - If `apiVersion < VK_API_VERSION_1_3`, unlinks `VkPhysicalDeviceVulkan13Features` and any unsupported extension structs.
  - Dispatched `vkCmdPipelineBarrier2 = vkCmdPipelineBarrier2KHR` and `vkCmdBeginRendering / vkCmdEndRendering` if available.

### Verification Results
1. **Full Build Clean**: Executed `./build.sh` via MSYS2 Clang 64-bit; compiled `bbgpu`, `bb-probe.exe`, `bb-gpu-capabilities.exe`, and `bb-play.exe` with exit code 0.
2. **Backend Smoke Test**: Ran `out/bb-probe.exe --vulkan-only` on the Quadro K4200: passed with exit code 0.
3. **GPU Capabilities Check**: Ran `out/bb-gpu-capabilities.exe --live-resolution`: correctly detected Quadro K4200, 4052 MiB VRAM, and set live resolution copying to off.
4. **Vulkan Memory Test**: Built and executed `tests/test_arena_overlap.c` against `-lvulkan-1`: successfully initialized Vulkan 1.2 device and memory queries with exit code 0.

---

## 2026-10-26: Phase 2 Execution & Verification (RenderPass Virtualization Layer)

### Implementation Summary
- **Created `vk_renderpass_cache.h` & `vk_renderpass_cache.cpp`**:
  - Implemented `CompatibleRenderPassKey`, `ExecutionRenderPassKey`, and `FramebufferKey` with zero-padding representation maintaining `static_assert(std::has_unique_object_representations_v<...>)`.
  - Implemented `RenderPassCache` with specialized creation for both compatible passes (`VkPipeline` compilation) and execution passes (`vkCmdBeginRenderPass`).
  - Implemented thread-safe `FramebufferCache` with automatic deduplication by attachment views and dimensions.
- **Updated `vk_instance.h` & `vk_instance.cpp`**:
  - Initialized `render_pass_cache` and `framebuffer_cache` upon device creation.
  - Added accessors `GetRenderPassCache()` and `GetFramebufferCache()`.
  - Guaranteed orderly shutdown destroying caches prior to `vkDestroyDevice`.
- **Integrated RPFVL into `Scheduler` (`vk_scheduler.h` & `vk_scheduler.cpp`)**:
  - Added `vk::Format format{}` and `u32 pad{0}` to `RenderAttachment`.
  - Rewrote `Scheduler::BeginRendering`:
    - On Vulkan 1.3: preserves zero-overhead `vkCmdBeginRendering`.
    - On Vulkan 1.2: queries `RenderPassCache` and `FramebufferCache` and records `vkCmdBeginRenderPass`.
  - Updated `Scheduler::EndRendering` to conditionally record `vkCmdEndRendering` or `vkCmdEndRenderPass`.
- **Populated Format Metadata across Engine**:
  - `vk_rasterizer.cpp`: Assigned `attachment.format = image_view.info.format` for color and depth/stencil attachments in `BeginRenderingFull`.
  - `blit_helper.cpp`: Assigned `attachment.format` for both depth blits and color blits.
  - `vk_object_motion.cpp`: Assigned format `vk::Format::eR32G32B32A32Sfloat` for motion vector attachments.
  - `vk_graphics_pipeline.cpp`: Conditionally linked compatible render pass from cache to `GraphicsPipelineCreateInfo` when dynamic rendering is absent.
  - `pp_pass.h` / `pp_pass.cpp`: Implemented fallback render pass and framebuffer caching for post-processing display pass.
  - `vk_scene_resolution.cpp`: Integrated RPFVL fallback for scene proxy depth/stencil resample passes.

### Verification Results
1. **Full Build Clean**: Built with `./build.sh` (MSYS2 Clang 64-bit); `libbbgpu.a`, `bb-probe.exe`, `bb-gpu-capabilities.exe`, `bb-play.exe` compiled and linked cleanly with exit code 0.
2. **Backend Smoke Test**: `out/bb-probe.exe --vulkan-only` executed with exit code 0.
3. **GPU Capabilities Check**: `out/bb-gpu-capabilities.exe` executed with exit code 0.
4. **RPFVL Unit Test Suite**: Created and ran `tests/test_vulkan12_renderpass.cpp`, validating:
   - Key hashing consistency across all permutations.
   - Unique object representations.
   - Deduplication and cache lookup semantics.
   - All assertions passed with exit code 0.

---

## 2026-10-26: Phase 3 Completed (Pipeline Creation & Command Buffer Integration)

### Objectives
1. Eliminate SPIR-V 1.6 incompatibilities on Kepler GK104 / Vulkan 1.2.175.
2. Adapt pipeline creation in `blit_helper.cpp` (`CreateColorToMSDepthPipeline` and `CreateMsCopyPipeline`) to use compatible render passes instead of requiring dynamic rendering.
3. Configure host shader GLSL compilation to target Vulkan 1.2 / SPIR-V 1.5 across CMake configurations.
4. Replace direct swapchain blank frame initialization in `vk_presenter.cpp` with RPFVL execution render pass and framebuffer caching.
5. Ensure compliance of `vk_graphics_pipeline.cpp` and `vk_compute_pipeline.cpp` for fixed 32-thread subgroup Kepler warps.

### Implementation Details
- **SPIR-V 1.5 Dynamic Target in `vk_pipeline_cache.cpp`**:
  - Introduced `SpirvVersion1_5 = 0x00010500U`.
  - Conditioned `Shader::Profile::supported_spirv` on `instance.ApiVersion() >= VK_API_VERSION_1_3 ? SpirvVersion1_6 : SpirvVersion1_5`.
- **SPIR-V Demote Extension Conditioning in `emit_spirv.cpp`**:
  - In `emit_spirv.cpp`, added `if (ctx.profile.supported_spirv < 0x00010600) ctx.AddExtension("SPV_EXT_demote_to_helper_invocation");` when `info.has_discard` is active, maintaining strict SPIR-V 1.5 spec compliance.
- **BlitHelper Pipeline Virtualization in `blit_helper.cpp`**:
  - In `CreateColorToMSDepthPipeline`, generated `CompatibleRenderPassKey` (depth-only) and conditionally linked `compatible_render_pass` to `GraphicsPipelineCreateInfo::renderPass` with `.pNext = nullptr` on Vulkan 1.2.
  - In `CreateMsCopyPipeline`, generated `CompatibleRenderPassKey` (1 color attachment) and conditionally linked `compatible_render_pass` on Vulkan 1.2.
- **Host Shader Compilation Target in CMake**:
  - In `gpu/CMakeLists.txt` and `gpu/shadps4/video_core/host_shaders/CMakeLists.txt`, adjusted `${GLSLANG_COMPILER}` flag `--target-env vulkan1.3` to `--target-env vulkan1.2`.
- **Swapchain Blank Frame Clearance in `vk_presenter.cpp`**:
  - Gated direct `cmdbuf.beginRendering` / `endRendering` in `Presenter::PrepareBlankFrame` behind `instance.IsDynamicRenderingSupported()`.
  - Added RPFVL execution render pass and framebuffer caching path for Vulkan 1.2, executing `vkCmdBeginRenderPass` with clear attachment and `vkCmdEndRenderPass`.

### Verification Results
1. **Full Build Clean**: Built with `./build.sh` (MSYS2 Clang 64-bit); `libbbgpu.a`, `bb-probe.exe`, `bb-gpu-capabilities.exe`, `bb-play.exe` compiled and linked cleanly with exit code 0.
2. **Backend Smoke Test**: `out/bb-probe.exe --vulkan-only` executed with exit code 0.
3. **GPU Capabilities Check**: `out/bb-gpu-capabilities.exe` executed with exit code 0 (`GPU scene scaling: Quadro K4200 supports live presets`).
4. **Phase 3 Pipeline Unit Test**: Created and ran `tests/test_vulkan12_pipeline.cpp`:
   - Validated SPIR-V 1.5 module header version stamping (`0x00010500`).
   - Validated `BlitHelper` depth and color compatible render pass key creation.
   - Validated `Presenter` execution render pass and framebuffer layout parameters.
   - All assertions passed with exit code 0.

---

## 2026-10-27: Phase 4 Completed (Overlay & Auxiliary Passes Migration)

### Objectives
1. Eliminate the last ungated `beginRendering` / `endRendering` in `gpu/shim/bbport_overlay.cpp` (ImGui debug, settings HUD, trophies, and map rendering).
2. Configure `ImGui_ImplVulkan_InitInfo` for Vulkan 1.2 by setting `UseDynamicRendering = false`, binding a cached `VkRenderPass` to `PipelineInfoMain.RenderPass`, and subpass index 0.
3. Virtualize the per-frame overlay render pass inside `BbOverlay::Render`:
   - On Vulkan 1.3: continue fast-path execution with `cmdbuf.beginRendering` / `cmdbuf.endRendering`.
   - On Vulkan 1.2: query `FramebufferCache` for the target swapchain image view and extent, issue `cmdbuf.beginRenderPass(rp_begin, vk::SubpassContents::eInline)`, execute `ImGui_ImplVulkan_RenderDrawData`, and call `cmdbuf.endRenderPass()`.
4. Verify all 5 rendering paths across the entire engine (`bbport_overlay.cpp`, `vk_presenter.cpp`, `pp_pass.cpp`, `vk_scene_resolution.cpp`, `vk_scheduler.cpp`) cleanly support Vulkan 1.2.

### Implementation Details
- **`gpu/shim/bbport_overlay.cpp`**:
  - Included `video_core/renderer_vulkan/vk_renderpass_cache.h`.
  - Added tracking variables `s_instance`, `s_format`, and `s_render_pass`.
  - In `BbOverlay::Init`:
    - Evaluated `instance.IsDynamicRenderingSupported()`.
    - If false, initialized `Vulkan::ExecutionRenderPassKey` with 1 color attachment matching the swapchain format (`eLoad` loadOp, `eStore` storeOp, `eColorAttachmentOptimal` layout), retrieved cached `VkRenderPass`, and populated `info.UseDynamicRendering = false` and `info.PipelineInfoMain.RenderPass = static_cast<VkRenderPass>(s_render_pass)`.
  - In `BbOverlay::Render`:
    - Branching on `s_instance->IsDynamicRenderingSupported()`:
      - Vulkan 1.3: Dynamic rendering path with `vk::RenderingInfo`.
      - Vulkan 1.2: Constructs `FramebufferKey` with current presentation view and extent, obtains cached `vk::Framebuffer`, and encapsulates `ImGui_ImplVulkan_RenderDrawData` inside `cmdbuf.beginRenderPass` / `cmdbuf.endRenderPass`.

### Verification Results
1. **Full Build Clean**: Built with `./build.sh` (MSYS2 Clang 64-bit); `libbbgpu.a`, `bb-probe.exe`, `bb-gpu-capabilities.exe`, `bb-play.exe` compiled and linked cleanly with exit code 0.
2. **Backend Smoke Test**: `out/bb-probe.exe --vulkan-only` returned 0: `Vulkan: Quadro K4200; command submission + 4096-byte readback PASS`.
3. **GPU Capabilities Check**: `out/bb-gpu-capabilities.exe` returned 0: `GPU scene scaling: Quadro K4200 supports live presets`.
4. **Phase 4 Overlay Unit Test**: Created and executed `tests/test_vulkan12_overlay.cpp`:
   - Validated ImGui `ExecutionRenderPassKey` parameters (1 color attachment, LoadOp=Load, StoreOp=Store, ImageLayout=ColorAttachmentOptimal).
   - Validated `FramebufferKey` caching, hashing, and resolution distinction (720p vs 1080p).
   - All assertions passed with exit code 0.
5. **Full Suite Regression Test**: Executed `test_vulkan12_renderpass.exe`, `test_vulkan12_pipeline.exe`, and `test_vulkan12_overlay.exe` in sequence with 100% pass rate.

---

## 2026-10-27: Phase 5 Completed (VRAM Budgeting & Kepler Performance Profiling)

### Objectives
1. Eliminate PCIe memory paging stutter on Kepler GK104 / Quadro K4200 (4052 MiB VRAM).
2. Restrict texture cache memory allocation pool to `2560 MiB` on discrete GPUs with <= 4.5 GB VRAM (`<= 4608_MB`), reserving at least `1500 MiB` for framebuffers, depth/stencil buffers, scene resolution scaling buffers, staging arenas, and Windows DWM composition.
3. Configure `run.py` low-VRAM profile reserve calculation to scale appropriately for 4 GB GPUs, preventing oversubscription of the device-local memory heap.
4. Establish Kepler compute upscaler fallback:
   - When 16-bit floating point (`shaderFloat16`) is unsupported, fall back cleanly from FSR 3 / FSR 4 to `UpscalerOff` (or native FP32 TAA), preventing invalid SPIR-V execution or driver crashes.
   - Retain full high-performance FSR 3 / FSR 4 / DLSS paths on modern GPUs with FP16 and INT8 dot product support.

### Implementation Details
- **`gpu/shadps4/video_core/texture_cache/texture_cache.cpp`**:
  - In `TextureCache::TextureCache` constructor: Added threshold clamping for discrete cards with `<= 4608_MB` VRAM:
    - `critical_gc_memory` capped to `2560_MB`.
    - `pressure_gc_memory` capped to `2048_MB`.
    - `trigger_gc_memory` capped to `1792_MB`.
  - In `TextureCache::GarbageCollectImages`: When `forced_budget` is unset and the device is a discrete GPU with `<= 4608_MB` VRAM, clamped `budget` to `2560_MB` to guarantee continuous VRAM headroom even when launched without `run.py`.
- **`run.py`**:
  - Adjusted `low_vram_mb` profiling in `run.py`: on GPUs with `vram <= LOW_VRAM_MB`, set reserve to `1500 MiB` (instead of 600 MiB) and clamped `BB_GC_BUDGET_MB` to `min(2560, max(2048, vram - reserve))`.
- **`gpu/shim/bbport_settings.h` & `gpu/shim/bbport_settings.cpp`**:
  - Updated `ConfigureUpscalerSupport(bool fsr4, bool fsr411, bool fp16 = true)`.
  - Added FP16 availability check: if `!fp16`, requested FSR 3/4 settings immediately and cleanly fall back to `UpscalerOff` with diagnostic warning logged.
- **`gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp`**:
  - Passed `instance.IsShaderFloat16Supported()` into `BbSettings::ConfigureUpscalerSupport`.

### Verification Results
1. **Full Build Clean**: Built with `./build.sh` (MSYS2 Clang 64-bit); `libbbgpu.a`, `bb-probe.exe`, `bb-gpu-capabilities.exe`, `bb-play.exe` compiled and linked cleanly with exit code 0.
2. **Backend Smoke Test**: `out/bb-probe.exe --vulkan-only` returned 0: `Vulkan: Quadro K4200; command submission + 4096-byte readback PASS`.
3. **GPU Capabilities Check**: `out/bb-gpu-capabilities.exe --vram` returned 0: `4052 1`.
4. **Phase 5 VRAM & Kepler Profiling Test**: Created and executed `tests/test_vulkan12_vram.cpp` (`out/test_vulkan12_vram.exe`):
   - Validated Quadro K4200 (4052 MiB) critical GC memory clamped to 2560 MiB.
   - Validated modern 8 GB GPU retains unconstrained critical GC memory (6.4 GiB).
   - Validated clean fallback to `UpscalerOff` on GPUs lacking FP16.
   - All assertions passed with exit code 0.
5. **Regression Testing**:
   - `python tests/test_low_vram.py` passed with exit code 0.
   - `test_vulkan12_renderpass.exe`, `test_vulkan12_pipeline.exe`, `test_vulkan12_overlay.exe`, and `test_vulkan12_vram.exe` all passed with exit code 0.

---

## 2026-10-27: Phase 6 Execution & In-Game Verification (Vulkan 1.2 End-to-End Success)

### Objectives
1. Resolve all Python module import errors in `run.py` and across the project for static analysis tools, linters, and IDEs.
2. Enable automatic game directory discovery for `I:\Games\Bloodborne\game`.
3. Resolve video core runtime initialization crashes on Vulkan 1.2 Kepler:
   - Tooling info inspection null pointer dereference.
   - Page fault signal handler null dereferences during early startup.
   - Buffer cache Vulkan 1.3 memory requirements probe call.
   - Dynamic viewport/scissor (`vkCmdSetViewportWithCount` / `vkCmdSetScissorWithCount`) null execution faults.
4. Execute full in-game execution on the NVIDIA Quadro K4200 and verify clean rendering and frame presentation.

### Implementation Summary
1. **Python Module Resolution & Tooling Integrity**:
   - Added `sys.path.insert(0, str(PORT / 'scripts'))` and `sys.path.insert(0, str(PORT / 'launcher'))` unconditionally at module scope in [`run.py`](file:///I:/Games/Bloodborne/development/bloodborne_pc/run.py).
   - Created [`pyrightconfig.json`](file:///I:/Games/Bloodborne/development/bloodborne_pc/pyrightconfig.json) configuring `"extraPaths": ["scripts", "launcher"]`, eliminating static analysis / IDE "module not found" warnings.
   - Created package anchors [`scripts/__init__.py`](file:///I:/Games/Bloodborne/development/bloodborne_pc/scripts/__init__.py) and [`launcher/__init__.py`](file:///I:/Games/Bloodborne/development/bloodborne_pc/launcher/__init__.py).
   - Added `Path('I:/Games/Bloodborne/game')` to `game_candidates` in [`run.py`](file:///I:/Games/Bloodborne/development/bloodborne_pc/run.py) for automatic zero-config game detection.
   - Fixed Windows file conflict detection in [`scripts/mods.py`](file:///I:/Games/Bloodborne/development/bloodborne_pc/scripts/mods.py) line 332 so case-insensitive hard link comparisons do not trigger false `File/directory conflict` errors.

2. **Vulkan 1.2 Video Core Runtime Compatibility**:
   - **Tooling Info Guard** ([`vk_instance.cpp`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp)): Guarded `physical_device.getToolProperties()` when `vkGetPhysicalDeviceToolProperties` is absent on Vulkan 1.2 drivers.
   - **Early Fault Signal Handler Guards** ([`page_manager.cpp`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/page_manager.cpp) & [`vk_rasterizer.cpp`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp)): Added null pointer checks on `rasterizer` and `liverpool` during guest page faults occurring before GPU engine initialization.
   - **Buffer Cache Vulkan 1.2 Fallback** ([`buffer_cache.cpp`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp)): Replaced `device.getBufferMemoryRequirements(req_info)` (Vulkan 1.3 core) with a fallback to `device.createBufferUnique(probe_ci)` + `device.getBufferMemoryRequirements(*buffer.value)` on Vulkan 1.2.
   - **Dynamic State Compatibility & Viewport/Scissor Fallback**:
     - Added `VK_EXT_extended_dynamic_state` query, chaining, unlinking, and feature accessors in [`vk_instance.h`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/renderer_vulkan/vk_instance.h) and [`vk_instance.cpp`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp).
     - Aliased `VULKAN_HPP_DEFAULT_DISPATCHER` core 1.3 dynamic state function pointers to their `...EXT` equivalents when loaded via extension.
     - Guarded all dynamic state commands in [`vk_scheduler.h`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.h) (`CommitWith`) with function pointer checks.
     - Implemented Vulkan 1.0 fallbacks: `c.setViewport(0, v)` and `c.setScissor(0, v)` when `vkCmdSetViewportWithCount` is null.
     - Updated [`vk_graphics_pipeline.cpp`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/renderer_vulkan/vk_graphics_pipeline.cpp), [`blit_helper.cpp`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/texture_cache/blit_helper.cpp), and [`vk_scene_resolution.cpp`](file:///I:/Games/Bloodborne/development/bloodborne_pc/gpu/shadps4/video_core/renderer_vulkan/vk_scene_resolution.cpp) to populate `.viewportCount = 1, .scissorCount = 1` and fallback dynamic states for Vulkan 1.2.

### In-Game Verification Results
- Executed `BB_TIMEOUT=15 python run.py` directly against the Quadro K4200:
  - Game detected: `I:\Games\Bloodborne\game\eboot.bin` (version 01.09).
  - CPU compatibility translations: 6,388 instruction sites patched.
  - Native libc & modules linked: `libc.prx@0x56e0000`, `libSceFios2.prx@0x57c0000`.
  - ATRAC9 & WASAPI audio backend initialized.
  - FMOD mixer and audio threads running.
  - Render threads spawned (`GXRenderThread`, `GXWorker:1-5`).
  - Quadro K4200 initialized (Vulkan 1.2.175, NVIDIA 475.14).
  - Low-VRAM texture budget applied: 2552 MiB.
  - Splash screen hidden; window displayed.
  - Multiple graphics pipelines compiled and executed without faults.
  - Frames presented to display: `Display: host buffer 1280x720 (live)`.
  - Save data directory mounted and loaded: `SPRJ0005`.
  - Zero crashes, zero access violations, zero device loss.





