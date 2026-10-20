// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone CPU regression check: cached SRT walkers the old fault handler patched are found.
#include <array>
#include <cassert>
#include <cstdio>
#include "shader_recompiler/ir/passes/srt.h"

int main() {
    using Shader::WalkerCodeLooksPatched;
    // mov rdi, [rdi + 0x10]; mov r10d, [rdi + 0x14]; mov [rsi + 0x40], r10d; ret
    const std::array<u8, 13> clean{0x48, 0x8B, 0x7F, 0x10, 0x44, 0x8B, 0x57, 0x14,
                                   0x44, 0x89, 0x56, 0x40, 0xC3};
    assert(!WalkerCodeLooksPatched(clean));
    // The first load patched: xor rdi, rdi + NOP over its displacement.
    auto rdi = clean;
    rdi[0] = 0x48, rdi[1] = 0x31, rdi[2] = 0xFF, rdi[3] = 0x90;
    assert(WalkerCodeLooksPatched(rdi));
    // The second load patched: xor r10d, r10d + NOP.
    auto r10d = clean;
    r10d[4] = 0x45, r10d[5] = 0x31, r10d[6] = 0xD2, r10d[7] = 0x90;
    assert(WalkerCodeLooksPatched(r10d));
    // A plain xor without the NOP padding is code the generator may emit.
    const std::array<u8, 4> plain_xor{0x48, 0x31, 0xFF, 0xC3};
    assert(!WalkerCodeLooksPatched(plain_xor));
    assert(!WalkerCodeLooksPatched({}));
    std::puts("srt walker cache check: ok");
}
