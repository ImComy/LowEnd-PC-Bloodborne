# Goal & Acceptance Criteria: Vulkan 1.2 / Quadro K4200 Port
**Document**: `docs/vulkan12_k4200/GOAL.md`  
**Workspace**: `i:/Games/Bloodborne/development/bloodborne_pc`  

---

## 1. Prime Goal
Enable Bloodborne (CUSA03173 v1.09) on PC to run natively, stably, and with smooth, playable performance on the **NVIDIA Quadro K4200** (Kepler GK104, 4 GB GDDR5, Driver 475.14) under **Vulkan 1.2.175**.

---

## 2. Hard Requirements & Acceptance Criteria

### A. Technical Correctness (Functional)
1. **Device Initialization**: The application must create the `VkInstance` and `VkDevice` targeting `VK_API_VERSION_1_2` without requesting Vulkan 1.3 features or missing extensions.
2. **Rendering Compatibility**: All draw calls, blits, and post-processing passes must function properly via the RenderPass / Framebuffer fallback mechanism without relying on `VK_KHR_dynamic_rendering`.
3. **Pipeline Compilation**: Graphics and compute pipelines must compile without driver crashes or validation layer errors.
4. **Overlay Compatibility**: The in-game settings and HUD overlay (ImGui in `bbport_overlay.cpp`) must render cleanly using standard `VkRenderPass` support.

### B. Stability & Fault Tolerance
1. **Zero Device Lost Crashes**: No `VK_ERROR_DEVICE_LOST` caused by invalid descriptors, unsupported dynamic states, or out-of-bounds memory accesses.
2. **Safe Fallbacks**: If an optional feature (e.g. FSR 4, DLSS, Subgroup64) is requested by configuration, the engine must silently degrade to supported alternatives (bilinear/CAS upscaling, FP32 shaders, Subgroup32).

### C. Performance & Smoothness (User Experience)
1. **Target Framerate**: Rock-solid **30 FPS** at 720p (`1280x720`) or 900p (`1600x900`).
2. **VRAM Confinement**: Peak VRAM usage must stay below **3.5 GiB** (leaving headroom for Windows desktop composition), completely avoiding PCIe host-memory paging stalls.
3. **Clean Frame Pacing**: Maintain smooth 33.3ms frame presentation without erratic stuttering or swapchain starvation.

### D. Architectural Cleanliness & Safety
1. **Zero Global Mutations**: All build steps, configs, and temporary files must reside strictly within the project directory. No system-wide registry, driver, or OS environment disruptions.
2. **Modular Architecture**: The Vulkan 1.2 compatibility layer must be cleanly encapsulated, preserving the existing Vulkan 1.3 high-performance path when running on newer GPUs.
