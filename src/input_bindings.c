/* SPDX-License-Identifier: GPL-2.0-or-later
 * Keyboard and mouse bindings (input_bindings.h). Actions, defaults and the way they reach the
 * pad after droogie's bbhost (https://github.com/droogie/bbhost, GPL-3.0-or-later,
 * host/bindings.cpp), which follows Dark Souls III PC. */
#include "input_bindings.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

/* PS4 pad bits (runtime_pad.c). */
enum {
    PAD_L3 = 0x2, PAD_R3 = 0x4, PAD_OPTIONS = 0x8, PAD_UP = 0x10, PAD_RIGHT = 0x20, PAD_DOWN = 0x40,
    PAD_LEFT = 0x80, PAD_L2 = 0x100, PAD_R2 = 0x200, PAD_L1 = 0x400, PAD_R1 = 0x800, PAD_TRIANGLE = 0x1000,
    PAD_CIRCLE = 0x2000, PAD_CROSS = 0x4000, PAD_SQUARE = 0x8000, PAD_TOUCHPAD = 0x100000,
};
/* Menus confirm with Cross and go back with Circle (the Western layout the game runs with). */
enum { CONFIRM = PAD_CROSS, BACK = PAD_CIRCLE };
enum { STICK_LOW = 0, STICK_REST = 128, STICK_HIGH = 255 };
static const float WALK_PUSH = 0.35f; /* a gentle push the game reads as a walk */

static const BindInfo INFO[BIND_COUNT] = {
    {"move_forward", "Move Forward", 0, "W", 0},
    {"move_back", "Move Back", 0, "S", 0},
    {"move_left", "Move Left", 0, "A", 0},
    {"move_right", "Move Right", 0, "D", 0},
    {"walk", "Walk", 0, "Left Alt", 0},
    {"roll", "Dodge / Sprint", PAD_CIRCLE, "Space", 0},
    {"lock_on", "Lock On", PAD_R3, "Q", 0},
    {"attack", "Attack", PAD_R1, NULL, 1},
    {"strong_attack", "Strong Attack", PAD_R2, "V", 0},
    {"strong_modifier", "Strong Attack Modifier", 0, "Left Shift", 0},
    {"transform", "Transform", PAD_L1, "C", 3},
    {"firearm", "Firearm / Parry", PAD_L2, "X", 2},
    {"blood_vial", "Blood Vial", PAD_TRIANGLE, "R", 0},
    {"use_item", "Use Item", PAD_SQUARE, "F", 0},
    {"interact", "Interact", PAD_CROSS, "E", 0},
    {"switch_item", "Switch Item", PAD_DOWN, "Down", 0},
    {"switch_right", "Switch Right Weapon", PAD_RIGHT, "Right", 0},
    {"switch_left", "Switch Left Weapon", PAD_LEFT, "Left", 0},
    {"dpad_up", "D-pad Up", PAD_UP, "Up", 0},
    {"gestures", "Gestures", PAD_TOUCHPAD, "G", 0},
    {"personal_effects", "Personal Effects", PAD_TOUCHPAD, "T", 0},
    {"look_up", "Look Up", 0, "I", 0},
    {"look_down", "Look Down", 0, "K", 0},
    {"look_left", "Look Left", 0, "J", 0},
    {"look_right", "Look Right", 0, "L", 0},
    {"menu", "Menu", PAD_OPTIONS, "Escape", 0},
    {"confirm", "Menu Confirm", CONFIRM, "Return", 0},
    {"back", "Menu Back", BACK, "Backspace", 0},
    {"l3", "Left Stick Press", PAD_L3, "Left Ctrl", 0},
};

static int g_key[BIND_COUNT], g_mouse[BIND_COUNT];
/* Movement smoothing: travel per 1/60 s as a share of a full push (1: off), and the stick. */
#define MOVE_SMOOTHING_DEFAULT 0.6f
static float g_move_step = MOVE_SMOOTHING_DEFAULT, g_move_x, g_move_y;
static double g_move_angle;
static bool g_move_active;
static uint64_t g_move_time;

const BindInfo *bb_bind_info(int action) { return action >= 0 && action < BIND_COUNT ? &INFO[action] : NULL; }
int bb_binding_key(int action) { return action >= 0 && action < BIND_COUNT ? g_key[action] : 0; }
int bb_binding_mouse(int action) { return action >= 0 && action < BIND_COUNT ? g_mouse[action] : 0; }

static const char *const MOUSE_NAMES[] = {NULL, "mouse1", "mouse2", "mouse3", "mouse4", "mouse5", "wheelup", "wheeldown"};

static void defaults(void) {
    for (int a = 0; a < BIND_COUNT; ++a) {
        g_key[a] = INFO[a].def_key ? (int)SDL_GetScancodeFromName(INFO[a].def_key) : 0;
        g_mouse[a] = INFO[a].def_mouse;
    }
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    size_t i = 0;
    while (isspace((unsigned char)s[i])) ++i;
    memmove(s, s + i, strlen(s + i) + 1);
}

bool bb_bindings_parse(const char *action, const char *value) {
    int a = 0;
    while (a < BIND_COUNT && strcmp(INFO[a].key, action)) ++a;
    if (a == BIND_COUNT) return false;
    int key = 0, mouse = 0;
    char copy[128];
    snprintf(copy, sizeof(copy), "%s", value);
    for (char *part = strtok(copy, ","); part; part = strtok(NULL, ",")) {
        trim(part);
        char lower[64];
        size_t i = 0;
        for (; part[i] && i + 1 < sizeof(lower); ++i) lower[i] = (char)tolower((unsigned char)part[i]);
        lower[i] = 0;
        if (!strcmp(lower, "none") || !*lower) continue;
        int m = 0;
        for (int k = 1; k <= MOUSE_WHEEL_DOWN; ++k)
            if (!strcmp(lower, MOUSE_NAMES[k])) m = k;
        if (m) mouse = m;
        else key = (int)SDL_GetScancodeFromName(part);
    }
    /* One key, one action: whatever else had it gives it up. */
    for (int b = 0; b < BIND_COUNT && key; ++b)
        if (b != a && g_key[b] == key) g_key[b] = 0;
    for (int b = 0; b < BIND_COUNT && mouse; ++b)
        if (b != a && g_mouse[b] == mouse) g_mouse[b] = 0;
    g_key[a] = key;
    g_mouse[a] = mouse;
    return true;
}

void bb_bindings_load(const char *ini_path) {
    defaults();
    g_move_step = MOVE_SMOOTHING_DEFAULT;
    FILE *file = ini_path ? fopen(ini_path, "r") : NULL;
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *eq = strchr(line, '=');
        if (eq && !strncmp(line, "move_smoothing", 14)) {
            const int percent = atoi(eq + 1);
            g_move_step = percent > 0 && percent < 100 ? (float)percent / 100.0f : 1.0f;
            continue;
        }
        if (line[0] == '#' || !eq || strncmp(line, "key_", 4)) continue;
        *eq = 0;
        char *name = line + 4, *value = eq + 1;
        trim(name);
        trim(value);
        if (!bb_bindings_parse(name, value)) fprintf(stderr, "Keys: unknown action %s in %s\n", name, ini_path);
    }
    fclose(file);
}

void bb_bindings_apply(const bool *keys, int nkeys, uint32_t mouse, BindOutput *out) {
    bool held[BIND_COUNT], by_key[BIND_COUNT];
    out->buttons = 0;
    out->mouse_buttons = 0;
    out->touch_side = -1;
    out->lx = out->ly = out->rx = out->ry = -1;
    out->any = false;
    for (int a = 0; a < BIND_COUNT; ++a) {
        const int k = g_key[a], m = g_mouse[a];
        by_key[a] = keys && k > 0 && k < nkeys && keys[k];
        held[a] = by_key[a] || (m && (mouse >> (m - 1)) & 1);
    }
    uint32_t key_buttons = 0;
    for (int a = 0; a < BIND_COUNT; ++a) {
        if (!held[a]) continue;
        out->any = true;
        uint32_t bit = INFO[a].button;
        /* The modifier turns Attack into the strong attack, as Dark Souls III's Shift does. */
        if (a == BIND_ATTACK && held[BIND_STRONG_MOD]) bit = PAD_R2;
        out->buttons |= bit;
        if (by_key[a]) key_buttons |= bit;
        switch (a) {
        case BIND_LOOK_U: out->ry = STICK_LOW; break;
        case BIND_LOOK_D: out->ry = STICK_HIGH; break;
        case BIND_LOOK_L: out->rx = STICK_LOW; break;
        case BIND_LOOK_R: out->rx = STICK_HIGH; break;
        case BIND_GESTURES: out->touch_side = 0; break;
        case BIND_EFFECTS: out->touch_side = 1; break;
        default: break;
        }
    }
    out->mouse_buttons = out->buttons & ~key_buttons;
    /* The movement keys: a full push each way, a diagonal the square's corner; with Walk held
     * a gentle push, a diagonal's two axes shortened so its reach is the same. Opposite keys
     * cancel. */
    const int x = held[BIND_MOVE_R] - held[BIND_MOVE_L], y = held[BIND_MOVE_B] - held[BIND_MOVE_F];
    if (!x && !y) {
        g_move_active = false;
        return;
    }
    float reach = (float)(STICK_HIGH - STICK_REST);
    const bool walk = held[BIND_WALK];
    if (walk) reach *= WALK_PUSH;
    /* Movement smoothing (Jgamrot/bloodborne_pc#4, after MKB2Controller's setting): a key
     * pressed or released while moving turns the stick to its new direction over a few frames
     * instead of at once - holding W, then A, otherwise snapped the stick 45 degrees in a frame
     * and the character stopped to turn. Starting to move and letting go stay immediate.
     * The stick turns along the gate rather than across it (measured by cassioKenji,
     * Supermedo/bloodborne_pc PR #74): the game picks walk or run by the larger axis and ends a
     * sprint when the direction moves 45 degrees or more between two reads, so a run stays on
     * the square (the larger axis full at every angle) and no read turns more than 30 degrees.
     * A half turn (A <-> D, W <-> S) goes through forward, or through the right when both ways
     * are sideways. A walk keeps the circle of its push (a diagonal's two axes shortened).
     * bbport.ini move_smoothing: how fast it turns (0.6 by default: 900 degrees a second). */
    const double pi = 3.14159265358979323846;
    const double target = atan2((double)y, (double)x);
    const uint64_t now = SDL_GetTicksNS();
    if (!g_move_active || g_move_step >= 1.0f) {
        g_move_angle = target;
    } else {
        double dt = (double)(now - g_move_time) / 1e9;
        dt = dt < 0.0 ? 0.0 : dt;
        double step = (double)g_move_step * 1500.0 * pi / 180.0 * dt;
        if (step > 30.0 * pi / 180.0) step = 30.0 * pi / 180.0;
        const double diff = fmod(target - g_move_angle + 3.0 * pi, 2.0 * pi) - pi; /* -pi..pi */
        double dir = diff > 0 ? 1.0 : -1.0;
        if (fabs(diff) > 170.0 * pi / 180.0) {
            /* Half way round counter-clockwise is angle + pi/2; forward is y < 0. */
            const double hy = sin(g_move_angle + pi / 2.0), hx = cos(g_move_angle + pi / 2.0);
            dir = hy < -1e-9 ? 1.0 : hy > 1e-9 ? -1.0 : hx > 0 ? 1.0 : -1.0;
        }
        if (fabs(diff) <= step) g_move_angle = target;
        else g_move_angle = atan2(sin(g_move_angle + dir * step), cos(g_move_angle + dir * step));
    }
    const double c = cos(g_move_angle), sn = sin(g_move_angle);
    const double k = walk ? reach : reach / fmax(fabs(c), fabs(sn));
    g_move_x = (float)(c * k);
    g_move_y = (float)(sn * k);
    g_move_active = true;
    g_move_time = now;
    out->any = true;
    out->lx = STICK_REST + (int)lroundf(g_move_x);
    out->ly = STICK_REST + (int)lroundf(g_move_y);
    if (out->lx > STICK_HIGH) out->lx = STICK_HIGH;
    if (out->ly > STICK_HIGH) out->ly = STICK_HIGH;
}
