/* SPDX-License-Identifier: GPL-2.0-or-later
 * cheat_no_stamina / cheat_stamina_out_of_combat: the game's "no stamina use" debug flag, live. */
#include "runtime_stamina.h"
#include "code_check.h"
#include "gpu/bbgpu.h"
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define STAMINA_FLAG_RVA 0x553e88a
#define PLAYER_DATA_RVA 0x553b130
#define COMBAT_MS 8000u
/* R1, R2, L1, L2; one of them or lost health keeps the player in combat for COMBAT_MS. */
#define ATTACK_BUTTONS (0x800u | 0x200u | 0x400u | 0x100u)

static volatile unsigned char *flag;
static uintptr_t base;
static int written, last_hp;
static uint64_t combat_until;

static uint64_t now_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    return 0;
#endif
}
static int read_mem(uintptr_t at, void *out, size_t size) {
#ifdef _WIN32
    SIZE_T done = 0;
    return at >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), (const void *)at, out, size, &done) &&
           done == size;
#else
    (void)at; (void)out; (void)size; return 0;
#endif
}
/* The player's current health, or -1 without a character. */
static int player_hp(void) {
    uintptr_t data = 0, record = 0;
    int32_t hp = 0;
    if (!read_mem(base + PLAYER_DATA_RVA, &data, sizeof(data)) || !data ||
        !read_mem(data + 8, &record, sizeof(record)) || !record || !read_mem(record + 0x14, &hp, 4))
        return -1;
    return hp;
}

void runtime_stamina_bind(void *image, uint64_t size) {
    flag = NULL; base = 0; written = 0; last_hp = -1; combat_until = 0;
    if (!image || size < 0x553e880) return;
#include "map_build_guard.inc"
    if (!bb_code_is((unsigned char *)image + BB_MAP_GUARD_OFFSET, bb_map_guard)) return;
    base = (uintptr_t)image;
    flag = (volatile unsigned char *)image + STAMINA_FLAG_RVA;
}

void runtime_stamina_tick(uint32_t buttons) {
    if (!flag) return;
    int always = 0, out_of_combat = 0;
    bbgpu_setting_get("cheat_no_stamina", &always);
    bbgpu_setting_get("cheat_stamina_out_of_combat", &out_of_combat);
    if (!always && !out_of_combat) {
        /* Switched off: give the flag back once; a game patch may still own it. */
        if (written) { *flag = 0; written = 0; }
        return;
    }
    const uint64_t now = now_ms();
    if (buttons & ATTACK_BUTTONS) combat_until = now + COMBAT_MS;
    const int hp = player_hp();
    if (hp >= 0 && last_hp > 0 && hp < last_hp) combat_until = now + COMBAT_MS;
    last_hp = hp;
    *flag = (unsigned char)(always || now >= combat_until);
    written = 1;
}
