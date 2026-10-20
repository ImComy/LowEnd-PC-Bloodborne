#ifndef BB_RESPAWN_PATCH_H
#define BB_RESPAWN_PATCH_H

#include <stddef.h>
#include <string.h>

#include "code_check.h"

enum { BB_RESPAWN_UNSUPPORTED, BB_RESPAWN_APPLIED, BB_RESPAWN_ALREADY_APPLIED };

/* CUSA03173 1.09: the player-death flag initializes a minimum loading time to
 * 12 seconds. The world-loading step tests this countdown independently of
 * character readiness. Change only its initialization; keep the character
 * readiness branch, resource-loading steps and multiplayer timeout intact.
 * Offsets refer to the loaded image, not SELF/ELF file offsets.
 */
static inline int bb_patch_respawn_delay(unsigned char *image, size_t size) {
    /* cmp [r12+e4],0; je; vmovss xmm0,[12.0]; jmp; vxorps; store the countdown; 15 s.
     * Checked by length and hash (code_check.h): the game's bytes are not kept here. */
    static const BbCode init = {47, 0x03a30c3249b23aa5ull};
    /* The same with `zero` over the vmovss: this patch already applied. */
    static const BbCode init_applied = {47, 0xad4a1b101cc3c172ull};
    static const BbCode ready = {19, 0x753d0105a489b794ull};
    static const unsigned char twelve[] = {0x00,0x00,0x40,0x41};
    static const unsigned char zero[] = {0xc5,0xf8,0x57,0xc0,0x0f,0x1f,0x40,0x00};
    /* The linked image can place the same code/data at either of these positions,
     * depending on the port's linker layout. Verify the full instructions before
     * changing either layout. */
    static const struct { size_t init_at, ready_at, twelve_at; } layouts[] = {
        {0x1938b59, 0x193a581, 0x49280c8},
        {0x208fe59, 0x2091881, 0x507f3c8}
    };
    if (!image) return BB_RESPAWN_UNSUPPORTED;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
        const size_t init_at = layouts[i].init_at;
        const size_t instruction_at = init_at + 11;
        if (size < init_at + init.n || size < layouts[i].ready_at + ready.n ||
            size < layouts[i].twelve_at + sizeof(twelve)) continue;
        if (memcmp(image + layouts[i].twelve_at, twelve, sizeof(twelve)) ||
            !bb_code_is(image + layouts[i].ready_at, ready)) continue;
        if (bb_code_is(image + init_at, init)) {
            memcpy(image + instruction_at, zero, sizeof(zero));
            return BB_RESPAWN_APPLIED;
        }
        if (bb_code_is(image + init_at, init_applied))
            return BB_RESPAWN_ALREADY_APPLIED;
    }
    return BB_RESPAWN_UNSUPPORTED;
}

#endif
