// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <algorithm>
#include "gpu/shim/bbport_settings.h"

// Replicate texture cache sizing calculation for verification
constexpr unsigned long long operator""_MB(unsigned long long v) { return v * 1024ULL * 1024ULL; }
constexpr unsigned long long operator""_GB(unsigned long long v) { return v * 1024ULL * 1024ULL * 1024ULL; }

struct MemoryBudgetMock {
    unsigned long long device_local_memory;
    bool is_integrated;

    unsigned long long CalculateCriticalGcMemory() const {
        constexpr long long DEFAULT_PRESSURE_GC_MEMORY = 1_GB + 512_MB;
        constexpr long long DEFAULT_CRITICAL_GC_MEMORY = 3_GB;
        constexpr long long TARGET_GC_THRESHOLD = 8_GB;

        const long long dev_mem = static_cast<long long>(device_local_memory);
        const long long min_spacing_critical = dev_mem - 512_MB;
        const long long mem_threshold = std::min<long long>(dev_mem, TARGET_GC_THRESHOLD);
        const long long min_vacancy_critical = (2 * mem_threshold) / 10;
        unsigned long long critical = static_cast<unsigned long long>(
            std::max<unsigned long long>(std::min(dev_mem - min_vacancy_critical, min_spacing_critical),
                                         DEFAULT_CRITICAL_GC_MEMORY));

        if (!is_integrated && device_local_memory <= 4608_MB) {
            critical = std::min(critical, static_cast<unsigned long long>(2560_MB));
        }
        return critical;
    }
};

int main() {
    std::printf("Running test_vulkan12_vram...\n");

    // 1. Verify VRAM budgeting for Quadro K4200 (4052 MiB, discrete)
    {
        MemoryBudgetMock k4200{ .device_local_memory = 4052_MB, .is_integrated = false };
        const unsigned long long critical = k4200.CalculateCriticalGcMemory();
        assert(critical == 2560_MB);
        std::printf("  [PASS] Quadro K4200 (4052 MiB) critical GC memory clamped to 2560 MiB\n");
    }

    // 2. Verify VRAM budgeting for 8 GB card (8192 MiB, discrete)
    {
        MemoryBudgetMock rtx4060{ .device_local_memory = 8192_MB, .is_integrated = false };
        const unsigned long long critical = rtx4060.CalculateCriticalGcMemory();
        assert(critical > 2560_MB);
        assert(critical == 8192_MB - (2 * 8192_MB) / 10);
        std::printf("  [PASS] Modern 8 GB GPU maintains unconstrained critical GC memory (%.1f GiB)\n",
                    float(critical) / float(1_GB));
    }

    // 3. Verify Upscaler fallback when FP16 is unsupported (Kepler architecture)
    {
        auto& settings = BbSettings::Get();

        // Test FSR 3 fallback without FP16
        settings.upscaler = BbSettings::UpscalerFsr3;
        BbSettings::ConfigureUpscalerSupport(false, false, false /* no FP16 */);
        assert(settings.upscaler == BbSettings::UpscalerOff);
        assert(settings.fsr4_problem != nullptr);
        std::printf("  [PASS] FSR 3 cleanly fell back to UpscalerOff on GPU lacking FP16\n");

        // Test FSR 4 fallback without FP16
        settings.upscaler = BbSettings::UpscalerFsr4;
        BbSettings::ConfigureUpscalerSupport(false, false, false /* no FP16 */);
        assert(settings.upscaler == BbSettings::UpscalerOff);
        std::printf("  [PASS] FSR 4 cleanly fell back to UpscalerOff on GPU lacking FP16\n");

        // Test normal FSR 3 on modern GPU with FP16
        settings.upscaler = BbSettings::UpscalerFsr3;
        BbSettings::ConfigureUpscalerSupport(false, false, true /* has FP16 */);
        assert(settings.upscaler == BbSettings::UpscalerFsr3);
        std::printf("  [PASS] Modern GPU with FP16 retains FSR 3\n");
    }

    std::printf("test_vulkan12_vram passed successfully!\n");
    return 0;
}
