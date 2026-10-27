# Agent Operating Guide & Handover Context (AGENTS.md)
**Project**: Bloodborne PC Port (Jgamrot fork) - Vulkan 1.2 / NVIDIA Quadro K4200 (Kepler) Compatibility Subsystem  
**Workspace**: `i:/Games/Bloodborne/development/bloodborne_pc`  
**Last Updated**: October 2026  

---

## 1. Mission Objective & Context
This project ports and runs native decrypted Bloodborne (CUSA03173 v1.09) on Windows x64.
The engine translates PS4 Liverpool (GCN) GPU commands into Vulkan via the `shadPS4` video core library (`libbbgpu`).

### The Target Hardware & Problem
- **GPU**: NVIDIA Quadro K4200 (GK104GL Kepler architecture, 4 GB GDDR5, 1344 CUDA cores, compute capability 3.0).
- **Driver**: NVIDIA Proprietary 475.14 (Kepler legacy EOL branch).
- **Supported Vulkan Version**: **Vulkan 1.2.175**.
- **The Core Problem**: The upstream port was written against **Vulkan 1.3** and depends heavily on Vulkan 1.3 core features—most critically **Dynamic Rendering (`VK_KHR_dynamic_rendering` / `vkCmdBeginRendering`)**, which Kepler's driver **does not support**.
- **The Goal**: Refactor and extend the renderer to run natively, reliably, and smoothly on Vulkan 1.2.175 and Kepler hardware within the 4 GB VRAM budget.

---

## 2. Environment & Toolchain
- **OS**: Windows 10/11 x64
- **MSYS2 Root**: `C:\msys64`
- **Compiler Toolchain**: Clang 64-bit (`/clang64/bin/clang`, `/clang64/bin/clang++`, `/clang64/bin/cmake`, `/clang64/bin/ninja`)
- **Execution Rule**: Always use `C:\msys64\usr\bin\env.exe MSYSTEM=CLANG64 /usr/bin/bash -lc "<command>"` or PowerShell wrappers that set `$env:MSYSTEM = 'CLANG64'`.
- **Workspace Location**: `i:\Games\Bloodborne\development\bloodborne_pc` (in MSYS2 path syntax: `/i/Games/Bloodborne/development/bloodborne_pc`).

### Safety Policy
- **NO GLOBAL SYSTEM MUTATIONS**: Do not modify system-wide registry, drivers, or files outside `i:\Games\Bloodborne\development\bloodborne_pc` and MSYS2 build outputs.
- Keep all temporary data, logs, and artifacts in `out/` or `docs/`.

---

## 3. Verified Hardware Capability Baseline (Quadro K4200 on 475.14)
*Audited directly via `vulkaninfo` on the target machine:*

| Feature / Extension | Status on K4200 | Note |
|---|---|---|
| **Vulkan API Version** | **1.2.175** | Maximum possible on Kepler hardware |
| `VK_KHR_dynamic_rendering` | **MISSING** | Driver does not expose it; requires RenderPass/Framebuffer fallback |
| `VK_KHR_synchronization2` | **SUPPORTED** (rev 1) | Can use `vkCmdPipelineBarrier2KHR` |
| `VK_KHR_buffer_device_address` | **SUPPORTED** (rev 1) | `bufferDeviceAddress = true` |
| `VK_EXT_robustness2` | **SUPPORTED** | `robustBufferAccess2 = true`, `robustImageAccess2 = true`, `nullDescriptor = true` |
| `VK_EXT_shader_demote_to_helper_invocation` | **SUPPORTED** (rev 1) | Supported via extension |
| `VK_EXT_subgroup_size_control` | **SUPPORTED** (rev 2) | Supported; min=32, max=32 (Kepler warp size is 32) |
| `VK_KHR_timeline_semaphore` | **SUPPORTED** | Vulkan 1.2 core |
| `VK_EXT_custom_border_color` | **SUPPORTED** | Enabled in renderer |
| `shaderFloat16` (FP16 Math) | **UNSUPPORTED** | Must fallback to FP32 in compute shaders |
| `shaderBufferInt64Atomics` | **UNSUPPORTED** | Must not be requested |
| `shaderOutputLayer` | **UNSUPPORTED** | Must not be requested |
| VRAM Capacity | **4096 MiB** | Low-VRAM profile required |

---

## 4. Key Architectural Files to Touch

1. **Vulkan Instance & Device Initialization**:
   - `gpu/shadps4/video_core/renderer_vulkan/vk_instance.h` & `vk_instance.cpp`: Device extension queries, feature struct chaining, unlinking unsupported Vulkan 1.3 structs when `apiVersion < VK_API_VERSION_1_3`.
   - `gpu/shadps4/video_core/renderer_vulkan/vk_platform.h`: `TargetVulkanApiVersion` constant (set to `VK_API_VERSION_1_2`).

2. **Render Pass & Dynamic Rendering Abstraction**:
   - `gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.h` & `vk_scheduler.cpp`: `BeginRendering` and `EndRendering`. Needs dual-path support: native `vkCmdBeginRendering` if extension present, or `VkRenderPass` / `VkFramebuffer` fallback.
   - `gpu/shadps4/video_core/renderer_vulkan/vk_graphics_pipeline.cpp`: `GraphicsPipeline::GraphicsPipeline`. Needs dual-path: `VkPipelineRenderingCreateInfo` vs `VkRenderPass` compatibility.
   - `gpu/shadps4/video_core/texture_cache/blit_helper.cpp`: Blit helper pipelines using dynamic rendering.
   - `gpu/shadps4/video_core/renderer_vulkan/host_passes/pp_pass.cpp`: Post-processing pass rendering.
   - `gpu/shadps4/video_core/renderer_vulkan/vk_scene_resolution.cpp`: Scene scaling render pass.
   - `gpu/shim/bbport_overlay.cpp`: In-game ImGui overlay (switch to `VkRenderPass` path if dynamic rendering unavailable).

3. **Shader & Upscaler Fallbacks**:
   - `gpu/third_party/fsr-vulkan/src/ffx_vk_portable.c` & passes: Ensure FP16 math does not hard-fail on Kepler.
   - Low-VRAM budgeting in `tools/gpu_capabilities.c` and `run.py`.

---

## 5. Build Commands (Copy-Paste Ready)
```bash
# Clean build of GPU library and tools via MSYS2 CLANG64:
C:\msys64\usr\bin\env.exe MSYSTEM=CLANG64 /usr/bin/bash -lc "cd /i/Games/Bloodborne/development/bloodborne_pc && ./build.sh"

# Direct ninja build of GPU components:
C:\msys64\usr\bin\env.exe MSYSTEM=CLANG64 /usr/bin/bash -lc "cd /i/Games/Bloodborne/development/bloodborne_pc/out/gpu && ninja bb-probe bb-gpu-capabilities bb-play"
```

---

## 6. Implementation Status (Roadmap Progress)

- [x] **Phase 1: Device Initialization & Feature Unlinking**: Dynamically unlinks Vulkan 1.3 core structs, chains Vulkan 1.2 extensions (`synchronization2`, `demote_to_helper_invocation`, `subgroup_size_control`), and handles Kepler 32-thread fixed subgroups. *(Complete & Verified)*
- [x] **Phase 2: RenderPass & Framebuffer Virtualization Layer (RPFVL)**: Created `RenderPassCache` and `FramebufferCache` with compatible & execution keys; dynamic rendering vs renderpass branching in `Scheduler`. *(Complete & Verified)*
- [x] **Phase 3: Pipeline Creation & Command Buffer Integration**: Added SPIR-V 1.5 target profile, conditional `SPV_EXT_demote_to_helper_invocation`, GLSL compilation targeting Vulkan 1.2, BlitHelper compatible render passes, Presenter blank frame execution renderpass. *(Complete & Verified)*
- [x] **Phase 4: Overlay & Auxiliary Passes Migration**: ImGui overlay (`bbport_overlay.cpp`) converted to `UseDynamicRendering = false`, execution render pass caching, framebuffer query, and wrapped `ImGui_ImplVulkan_RenderDrawData` in `vkCmdBeginRenderPass` / `vkCmdEndRenderPass`. *(Complete & Verified)*
- [x] **Phase 5: VRAM Budgeting & Kepler Performance Profiling**: Clamped texture cache critical threshold to 2560 MiB for 4GB discrete GPUs, updated `run.py` 1500 MiB reserve, configured automatic fallback from FP16 upscalers (FSR 3/4) to `UpscalerOff` on Kepler GK104. *(Complete & Verified)*
- [x] **Phase 6: In-Game Verification & End-to-End Testing**: Resolved Python module environment issues and static analyzer paths, guarded Vulkan 1.2 video core entrypoints (tooling, signal handler, buffer memory requirements), implemented dynamic state viewport/scissor fallbacks, and successfully verified in-game execution and frame presentation on the Quadro K4200. *(Complete & Verified)*

