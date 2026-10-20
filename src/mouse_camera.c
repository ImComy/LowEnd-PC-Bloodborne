/* SPDX-License-Identifier: GPL-2.0-or-later
 * Mouse camera (mouse_camera.h). The camera update and its fields are imedved's (Bloodborne
 * mouse camera, Nexus Mods), as mcrib884 (Supermedo/bloodborne_pc#3) and Jhony-Jhansen
 * (Supermedo/bloodborne_pc#23) ported it. They patched the code from the window's thread while
 * the game ran; here the hooks go in before the game starts and everything runs on the game's
 * own thread, at the start of its camera update:
 *
 * - The camera object is in r13 there (the update reads its pitch next). The mouse motion since
 *   the last update turns it: pitch (target, smoothed and converging) and yaw (target and
 *   converging), the pitch kept within the camera's own limits.
 * - Four stores of the game's own turn (yaw twice, 0x130 and 0x294) are skipped while the mouse
 *   drives the camera, as the mod removes them, and run again as soon as the right stick turns
 *   it (a gamepad or bound keys) or the mouse has been still for half a second: controller play
 *   is the game's own code.
 * - Locked on, the game turns the camera; a mouse flick is then a right-stick flick, which
 *   switches targets.
 * - Through the monocular, the motion turns its view instead, at half the rate. */
#include "mouse_camera.h"

#include "engine_hooks.h"
#include "gpu/bbgpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CAMERA_UPDATE = 0x143ceaa, /* vmovss xmm0, [r13+0x140]: image offsets of game 1.09 */
    MONOCULAR = 0x553e8d0,     /* pointer chain to the monocular's view */
    PITCH = 0x140, YAW = 0x144, PITCH_SMOOTH = 0x150, LOCKED = 0x154, TURN_RATE = 0x130,
    PITCH_MAX = 0x1ec, PITCH_MIN = 0x1f0, PITCH_CONVERGE = 0x26c, YAW_CONVERGE = 0x270,
    STILL_UPDATES = 30,  /* the mouse lets go after this many updates without motion */
    STICK_UPDATES = 20,  /* ... and at once for this many after the stick turned the camera */
    FLICK_SAMPLES = 6, FLICK_COOLDOWN = 15,
};
static const float COUNTS_PER_RADIAN = 900.0f, FLICK_COUNTS = 60.0f;
static const float PI = 3.14159265f, TAU = 6.28318531f;

static const BbCode update_code = {9, 0x025464a80bd36ee0ull};
static const struct {
    uint64_t at;
    BbCode code;
} stores[] = {
    {0x143c6e8, {9, 0x599f4439954122e5ull}}, /* vmovss [r13+0x144], xmm0 */
    {0x143c984, {9, 0x71be1eacbb1f4181ull}}, /* vmovss [r13+0x130], xmm1 */
    {0x143dde6, {9, 0x414d109e95be3994ull}}, /* vmovss [r13+0x294], xmm0 */
    {0x143c870, {9, 0x5e4bc59600d8836dull}}, /* vmovss [r13+0x144], xmm5 */
};
#define STORE_COUNT ((int)(sizeof(stores) / sizeof(*stores)))

static BbSkip *g_skips[STORE_COUNT];
static int g_skipping, g_active, g_cooldown;
static float g_flick_counts;
static volatile int g_stick_updates, g_flick; /* written by the pad read and the camera update */

static int setting(const char *key, int fallback) {
    int value = fallback;
    return bbgpu_setting_get(key, &value) ? value : fallback;
}

static void skip_stores(int skip) {
    if (skip == g_skipping) return;
    g_skipping = skip;
    for (int i = 0; i < STORE_COUNT; ++i) bb_skip(g_skips[i], skip);
}

static float field(unsigned char *cam, int at) {
    float value;
    memcpy(&value, cam + at, 4);
    return value;
}
static void set_field(unsigned char *cam, int at, float value) { memcpy(cam + at, &value, 4); }

static float wrap(float yaw) {
    while (yaw > PI) yaw -= TAU;
    while (yaw < -PI) yaw += TAU;
    return yaw;
}

/* Through the monocular: its view turns instead. 1 when it took the motion. */
static int turn_monocular(float pitch, float yaw) {
    uint64_t p1, p2, p3;
    int32_t on;
    if (!bb_read((uint64_t)(uintptr_t)bb_guest(MONOCULAR), &p1, 8) || !p1 || !bb_read(p1 + 0x68, &p2, 8) || !p2 ||
        !bb_read(p2 + 0x84, &on, 4) || !on || !bb_read(p2 + 0x68, &p3, 8) || !p3)
        return 0;
    float view[2];
    if (!bb_read(p3 + 0x148, view, 8)) return 0;
    view[0] = fminf(fmaxf(view[0] + pitch * 0.5f, -1.94f), 1.71f);
    view[1] = wrap(view[1] + yaw * 0.5f);
    memcpy((void *)(uintptr_t)(p3 + 0x148), view, 8);
    return 1;
}

static BB_GUEST_ABI void on_camera_update(BbRegs *regs) {
    unsigned char *cam = (unsigned char *)(uintptr_t)regs->r13;
    float dx = 0, dy = 0;
    const int captured = bbgpu_mouse_captured();
    if (captured) bbgpu_mouse_motion_take(&dx, &dy);
    if (g_cooldown) --g_cooldown;
    if (!captured || !setting("mouse_camera", 1)) {
        g_active = 0;
        skip_stores(0);
        return;
    }
    const float scale = (float)setting("mouse_sensitivity", 10) / 10.0f;
    if (setting("mouse_invert_y", 0)) dy = -dy;
    const float pitch = dy / COUNTS_PER_RADIAN * scale, yaw = dx / COUNTS_PER_RADIAN * scale;

    if (field(cam, LOCKED) == 1.0f) { /* the game turns the camera: flicks switch targets */
        g_active = 0;
        skip_stores(0);
        g_flick_counts = dx ? g_flick_counts + dx : g_flick_counts * 0.9f;
        if (!g_cooldown && fabsf(g_flick_counts) >= FLICK_COUNTS) {
            __atomic_store_n(&g_flick, g_flick_counts > 0 ? FLICK_SAMPLES : -FLICK_SAMPLES, __ATOMIC_RELAXED);
            g_flick_counts = 0;
            g_cooldown = FLICK_COOLDOWN;
        }
        return;
    }
    g_flick_counts = 0;
    const int stick = __atomic_load_n(&g_stick_updates, __ATOMIC_RELAXED);
    if (stick) {
        __atomic_store_n(&g_stick_updates, stick - 1, __ATOMIC_RELAXED);
        g_active = 0;
        skip_stores(0);
        return;
    }
    if (dx || dy) g_active = STILL_UPDATES;
    else if (g_active) --g_active;
    if (!g_active) {
        skip_stores(0);
        return;
    }
    skip_stores(1);
    set_field(cam, TURN_RATE, 0.7f); /* the mod's value while its store is skipped */
    if ((!dx && !dy) || turn_monocular(pitch, yaw)) return;

    float min = field(cam, PITCH_MIN), max = field(cam, PITCH_MAX);
    if (!(min < max) || min < -3.0f || max > 3.0f) min = -1.94f, max = 1.71f;
    const float new_pitch = fminf(fmaxf(field(cam, PITCH) + pitch, min), max);
    const float new_yaw = wrap(field(cam, YAW) + yaw);
    set_field(cam, PITCH, new_pitch);
    set_field(cam, PITCH_SMOOTH, new_pitch);
    set_field(cam, PITCH_CONVERGE, new_pitch);
    set_field(cam, YAW, new_yaw);
    set_field(cam, YAW_CONVERGE, new_yaw);
}

int bb_mouse_camera_install(void) {
    const char *env = getenv("BB_MOUSE_CAMERA");
    if (env && !strcmp(env, "0")) return 0;
    /* Every site first, so a mismatch leaves the game unchanged. */
    const unsigned char *update = bb_guest(CAMERA_UPDATE);
    int ok = bb_code_is(update, update_code);
    for (int i = 0; ok && i < STORE_COUNT; ++i) ok = bb_code_is(bb_guest(stores[i].at), stores[i].code);
    if (!ok) {
        printf("Mouse camera: the camera code is not game 1.09's; not installed\n");
        return 0;
    }
    for (int i = 0; i < STORE_COUNT; ++i) {
        g_skips[i] = bb_hook_skip(stores[i].at, stores[i].code);
        if (!g_skips[i]) {
            printf("Mouse camera: %s\n", bb_hooks_error());
            return i;
        }
    }
    if (bb_hook_mid(CAMERA_UPDATE, update_code, on_camera_update)) {
        printf("Mouse camera: %s\n", bb_hooks_error());
        return STORE_COUNT;
    }
    printf("Mouse camera: installed (click the game window to use it)\n");
    return STORE_COUNT + 1;
}

int bb_mouse_camera_stick_x(void) {
    const int flick = __atomic_load_n(&g_flick, __ATOMIC_RELAXED);
    if (!flick) return -1;
    __atomic_store_n(&g_flick, flick > 0 ? flick - 1 : flick + 1, __ATOMIC_RELAXED);
    return flick > 0 ? 255 : 0;
}

void bb_mouse_camera_note_stick(uint8_t right_x, uint8_t right_y) {
    if (abs(right_x - 128) > 48 || abs(right_y - 128) > 48)
        __atomic_store_n(&g_stick_updates, STICK_UPDATES, __ATOMIC_RELAXED);
}
