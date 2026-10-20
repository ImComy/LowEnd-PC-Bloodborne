// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <boost/container/set.hpp>
#include <boost/container/small_vector.hpp>
#include "common/types.h"

namespace Serialization {
struct Archive;
}

namespace Shader {

using PFN_SrtWalker = void PS4_SYSV_ABI (*)(const u32* /*user_data*/, u32* /*flat_dst*/);
PFN_SrtWalker RegisterWalkerCode(const u8* ptr, size_t size);
/// bbport: runs `walker` on this thread. A fault inside it (a null or stale table pointer) makes
/// it return false and marks the thread (TakeSrtWalkFault) instead of rewriting the walker, which
/// sent zeroed addresses to every later draw with that shader and into the shader cache.
bool RunSrtWalker(PFN_SrtWalker walker, const u32* user_data, u32* flat_dst);
/// Whether a walk on this thread faulted since the last call; clears the mark.
bool TakeSrtWalkFault();

/// bbport: whether walker code holds a load the old fault handler rewrote to read 0
/// (`xor rdi,rdi` or `xor r10d,r10d`, padded with NOPs). The generator never emits a NOP, so the
/// pair only appears in code that was patched; caches written before 1.9.8 can hold such code.
inline bool WalkerCodeLooksPatched(std::span<const u8> code) {
    static constexpr std::array<u8, 4> patched_rdi{0x48, 0x31, 0xFF, 0x90};
    static constexpr std::array<u8, 4> patched_r10d{0x45, 0x31, 0xD2, 0x90};
    return !std::ranges::search(code, patched_rdi).empty() ||
           !std::ranges::search(code, patched_r10d).empty();
}

struct PersistentSrtInfo {
    // Special case when fetch shader uses step rates.
    struct SrtSharpReservation {
        u32 sgpr_base;
        u32 dword_offset;
        u32 num_dwords;
    };

    PFN_SrtWalker walker_func{};
    size_t walker_func_size{};
    u32 flattened_bufsize_dw = 16; // NumUserDataRegs

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

} // namespace Shader
