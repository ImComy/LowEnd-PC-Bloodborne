# Bloodborne for Windows (Vulkan 1.2 & Low-End PC Edition)

Run your own decrypted copy of Bloodborne (CUSA03173, version 1.09) natively on Windows 10 or 11 — **now running on Vulkan 1.2+ GPUs without requiring Vulkan 1.3!**

<div align="center">

[![Download ZIP](https://img.shields.io/badge/Download_ZIP-Ready_to_Play_(Direct_Download)-2ea44f?style=for-the-badge&logo=github&logoColor=white)](https://github.com/ImComy/LowEnd-PC-Bloodborne/archive/refs/heads/windows.zip)

</div>

The game's code runs directly on your CPU and its graphics are translated to Vulkan. This project is a fork of [Jgamrot's fork](https://github.com/Jgamrot/bloodborne_pc), which is based on [Supermedo's Windows port](https://github.com/Supermedo/bloodborne_pc) by Mohammed Albarghouthi, building upon [deadinside28's bbport](https://github.com/deadinside28/bloodborne_pc) and the [shadPS4](https://github.com/shadps4-emu/shadPS4) renderer.

> [!IMPORTANT]
> **No game files are included.** You need your own decrypted dump of Bloodborne (CUSA03173 v1.09). This project is not affiliated with Sony Interactive Entertainment or FromSoftware.

---

### Why this fork?

Have you ever tried running Bloodborne PC on a low-end or older GPU and gotten slapped with this error?

```text
GPU [Debug] <Critical> vk_platform.cpp:287 CreateInstance: Assertion Failed!
Vulkan 1.3 is required, but only 1.2 is supported by instance!
STOP: GPU library assertion failed (see GPU log above)
— The game exited (code 23) —
```

Upstream ports were coded strictly against **Vulkan 1.3**, relying heavily on modern features like `VK_KHR_dynamic_rendering` (`vkCmdBeginRendering`) and locking out millions of capable older GPUs (such as Kepler / Maxwell / older Quadro & Radeon series).

**This fork eliminates the Vulkan 1.3 barrier:**
* **Vulkan 1.2+ Universal Compatibility Layer**: Emulates dynamic rendering through a high-performance `VkRenderPass` & `VkFramebuffer` virtualization layer, custom SPIR-V 1.5 shader compilation profiles, and legacy Vulkan driver guards.
* **Universal Push Descriptor Fallback**: Automatically emulates push descriptors for GPUs lacking `VK_KHR_push_descriptor` (such as AMD GCN 1.0 / FirePro M6100 / Radeon HD 7000/8000 / R9 270/280) through thread-safe descriptor set binding and updates.
* **Tested on Legacy Hardware**: Verified on NVIDIA Quadro K4200 (Kepler 4 GB, Driver 475.14) and AMD FirePro M6100 (GCN 1.0 2 GB, Driver 2.0.179 / Adrenalin 21.5).
* **Low-End VRAM Anti-Thrashing (Working Set Protection)**: Completely prevents the 30+ texture evictions/sec stutter on 2 GB / 4 GB VRAM cards when turning the camera, without degrading texture resolution.
* **Clustered Light Grid Resolution Matching**: Dynamically scales the 3D lighting grid compute passes to the active render resolution, saving 10%–18% GPU time in Yharnam with zero visual degradation.
* **Zero Bloat & Simple 1-Click Launch**: Includes clean Windows native executables (`Bloodborne.exe`, `Bloodborne Launcher.exe`, `Play Bloodborne.exe`, `Setup Bloodborne.exe`) with hardened Python detection (bypassing dummy WindowsApps stubs) and zero silent exits.

---

## 📥 Download & Installation

### Option 1: Direct ZIP Download (Recommended)
1. **[👉 Click here to Download LowEnd-PC-Bloodborne (.zip) 👈](https://github.com/ImComy/LowEnd-PC-Bloodborne/archive/refs/heads/windows.zip)**
   *(Or click the green **<> Code** button at the top of the GitHub page and select **Download ZIP**)*.
2. Extract the downloaded ZIP archive into a permanent folder (e.g. `C:\Games\Bloodborne\BloodBorne-Port`).
   > [!WARNING]
   > **Do not run the game from inside the zip file!** Always right-click the zip, choose **Extract All...**, and run the game from the extracted folder.
3. Make sure you have your decrypted game dump of **Bloodborne (CUSA03173) updated to version 1.09**.

### Option 2: Clone via Git
```bash
git clone -b windows https://github.com/ImComy/LowEnd-PC-Bloodborne.git
```

```text
C:\Games\Bloodborne\
├── BloodBorne-Port\
│   ├── Bloodborne.exe           ← Launcher (settings, mods, controls)
│   ├── Play Bloodborne.exe      ← Instant game launcher (uses saved settings)
│   ├── Setup Bloodborne.exe     ← Quick engine dependency checker
│   └── bin\bb-probe.exe         ← Optimized game execution engine
└── CUSA03173\                   ← Your dumped game folder
    ├── eboot.bin
    ├── dvdroot_ps4\
    ├── sce_module\
    └── sce_sys\
```

---

## How to Run

### Option A: The Full GUI Launcher (`Bloodborne.exe`)
Double-click **`Bloodborne.exe`**.
* On first start, the setup wizard will open automatically.
* Select your game folder (the folder containing `eboot.bin`).
* Adjust your graphics, display resolution, controls, and cheats.
* Click **Play now** to start!

### Option B: Quick Play (`Play Bloodborne.exe`)
Once configured, double-click **`Play Bloodborne.exe`** to launch directly into the game without opening the launcher window.

### Option C: Python Command Line
If you prefer running via terminal:
```powershell
python run.py
```

---

## System Requirements

| Component | Minimum Requirement | Recommended |
|---|---|---|
| **OS** | Windows 10 (1903+) or Windows 11 (64-bit) | Windows 10/11 64-bit |
| **GPU** | **Vulkan 1.2 capable GPU** (e.g. NVIDIA Kepler / Quadro K4200 / GTX 700 / AMD GCN) | NVIDIA GTX 1060 / RTX 2060 or AMD RX 580+ |
| **VRAM** | **2 GB VRAM** (Low-VRAM active working-set optimization enabled) | 6 GB - 8 GB VRAM |
| **CPU** | Intel Core 4th Gen (Haswell) or AMD Ryzen (with AVX). Older CPUs (Sandy Bridge / Ivy Bridge) run via built-in instruction shims. | 6+ Cores Modern CPU |
| **RAM** | 4 GB System RAM (with page file enabled) | 16 GB System RAM |
| **Game** | Bloodborne CUSA03173 updated to **version 1.09** | Game of the Year Edition (includes The Old Hunters) |

---

## Key Features

### 🎮 In-Game PC Menu & Overlays
* Press **Insert** on your keyboard or **L3 + R3** on your controller to open the live PC settings overlay:
  * Adjust upscaling, sharpness, motion blur, depth of field, and chromatic aberration live.
  * Real-time FPS graph and performance stats.
  * In-game achievements and trophy notifications with sound.

### 🕹️ Complete Keyboard & Mouse Support
* **True 1:1 Mouse Look**: Smooth camera movement tuned specifically for PC mouse input.
* **Target Flicking**: Flick the mouse sideways while locked on to cycle targets (like *Dark Souls III*).
* **Movement Direction Smoothing**: Eliminates the abrupt stutter when transitioning between WASD keys.
* **Button Prompts**: Choose between PlayStation, Xbox, or Keyboard prompt overlays.

### ⚡ Tailored Low-End Performance
* **Active Working-Set Protection**: Pins in-scene textures to prevent eviction thrashing on 4 GB graphics cards.
* **Dynamic Clustered Lighting**: Matches light grid resolution to active display resolution to save 10–18% render time.
* **Frame-Rate Uncap & Physics Corrections**: Run at 60 FPS, 90 FPS, or unlocked without physics or character movement speed breaking.
* **Heap Expansion**: Pre-allocates expanded graphics heaps to prevent stutter during spell effects and blood particles.

---

## Troubleshooting & Feedback

* **The game crashed with exit code 3 or 1**:
  * Check `user\last_run.log` or the console output.
  * If on Vulkan 1.2 or Kepler/Maxwell hardware, ensure your NVIDIA driver is updated to the latest supported release (e.g., 475.14 for Kepler).
* **"No eboot.bin found"**:
  * Point the launcher directly to the root game folder that contains `eboot.bin`, `dvdroot_ps4`, and `sce_sys`.
* **Black screen or shader compilation delay**:
  * The first launch compiles game shaders (*Preparing shaders* in window title). Later boots are instant.
* **Report an issue / feedback**:
  * This project is actively developed. If you test on low-end hardware, open an issue on the [GitHub Repository](https://github.com/ImComy/LowEnd-PC-Bloodborne/issues) and include:
    1. Your GPU model and driver version.
    2. Your CPU and RAM.
    3. What happened and the contents of `user\last_run.log`.

---

## Credits & Acknowledgments

* **[Jgamrot](https://github.com/Jgamrot/bloodborne_pc)** for the feature-rich launcher, setup wizard, PC menu enhancements, and packaging foundation.
* **[Supermedo](https://github.com/Supermedo/bloodborne_pc)** (Mohammed Albarghouthi) for the standalone Windows port and frame generation integration.
* **[deadinside28](https://github.com/deadinside28/bloodborne_pc)** for the foundational `bbport` runtime.
* **[shadPS4 Team](https://github.com/shadps4-emu/shadPS4)** for the open-source PS4 Liverpool GPU emulation and shader recompiler.
* **[droogie's bbhost](https://github.com/droogie/bbhost)** for in-engine PC menus, mirror, and rebirth hooks.
* **[Awaken](https://github.com/Awakenchan/BloodBorneButPc)** and **[The Hunter's Dream](https://thehuntersdream.com)** team for multiplayer and network backend services.
* **Community Patch Creators**: Kyo, auser1337, Aljoscha-Ide, DUDUYG, emurphy04, and all contributors across the Bloodborne PC reverse-engineering community.

The port is distributed as free software under the [GPL-2.0 License](LICENSE). Bloodborne is a trademark of Sony Interactive Entertainment and FromSoftware.
