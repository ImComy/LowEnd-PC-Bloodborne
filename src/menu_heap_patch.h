#ifndef BB_MENU_HEAP_PATCH_H
#define BB_MENU_HEAP_PATCH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "code_check.h"

enum { BB_MENU_HEAP_UNSUPPORTED, BB_MENU_HEAP_APPLIED };

/* CUSA03173 1.09: the menu system's constructor (guest 0x1f59d50) carves two fixed heaps out of
 * the game's default heap, 18 MiB and 34 MiB, for Scaleform. The 34 MiB one holds
 * menu/common.tpf (31.3 MB unpacked), which left ~2.7 MB for everything else; button prompt
 * mods with a larger common.tpf (Xbox 360 Prompts (Elden Ring): 32.8 MB) left ~1.2 MB, and opening
 * a PC section of the in-game menu then made a Scaleform allocation return null and the next
 * constructor write through it (a write to 0x18 at guest 0xdcf79; Jgamrot/bloodborne_pc#3).
 * Adds `extra_mib` to that heap. The default heap it comes from has little room: doubling both
 * (+52 MiB) and +32 MiB failed at start-up; +16 started. Checks the instruction first; offsets are guest offsets. */
static inline int bb_patch_menu_heap(unsigned char *image, size_t size, unsigned extra_mib) {
    /* mov rsi, [r13]; mov edx, 0x2200000; xor ecx, ecx */
    static const BbCode code = {11, 0xa136fe1cce34196eull};
    enum { CODE_AT = 0x1f5a475, ORIGINAL = 0x2200000 };
    if (!image || extra_mib == 0 || extra_mib > 64 || size < CODE_AT + code.n ||
        !bb_code_is(image + CODE_AT, code))
        return BB_MENU_HEAP_UNSUPPORTED;
    const uint32_t bytes = ORIGINAL + (uint32_t)extra_mib * 0x100000u;
    memcpy(image + CODE_AT + 5, &bytes, 4);
    return BB_MENU_HEAP_APPLIED;
}

#endif
