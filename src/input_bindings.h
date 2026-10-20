/* SPDX-License-Identifier: GPL-2.0-or-later
 * Keyboard and mouse bindings, the way Dark Souls III PC keeps them (after droogie's bbhost,
 * host/bindings.cpp): every action has a key and a mouse button, either may be unbound, and each
 * sets the pad bit (or stick direction) the game reads for it. They apply on top of a connected
 * gamepad, so both work at once.
 *
 * bbport.ini: key_<action>=<binding>, a key's SDL name and/or Mouse1..Mouse5 (left, right,
 * middle, the two side buttons), WheelUp / WheelDown, separated by a comma, or "none":
 *   key_interact=E    key_firearm=X, Mouse2    key_gestures=none */
#ifndef BB_INPUT_BINDINGS_H
#define BB_INPUT_BINDINGS_H

#include <stdbool.h>
#include <stdint.h>

enum {
    BIND_MOVE_F, BIND_MOVE_B, BIND_MOVE_L, BIND_MOVE_R, BIND_WALK, BIND_ROLL, BIND_LOCK_ON,
    BIND_ATTACK, BIND_STRONG, BIND_STRONG_MOD, BIND_TRANSFORM, BIND_FIREARM, BIND_BLOOD_VIAL, BIND_USE_ITEM,
    BIND_INTERACT, BIND_SWITCH_ITEM, BIND_SWITCH_RIGHT, BIND_SWITCH_LEFT, BIND_DPAD_UP, BIND_GESTURES, BIND_EFFECTS,
    BIND_LOOK_U, BIND_LOOK_D, BIND_LOOK_L, BIND_LOOK_R, BIND_MENU, BIND_CONFIRM, BIND_BACK, BIND_L3,
    BIND_COUNT
};
/* Mouse inputs: 1..5 the buttons, 6 and 7 the wheel turned up and down (a short press a notch). */
enum { MOUSE_WHEEL_UP = 6, MOUSE_WHEEL_DOWN = 7 };

typedef struct {
    const char *key;     /* the bbport.ini name after "key_" */
    const char *label;
    uint32_t button;     /* the pad bit; 0 for stick directions, walk and the modifier */
    const char *def_key; /* SDL scancode name, or NULL */
    int def_mouse;       /* 1..7, or 0 */
} BindInfo;
const BindInfo *bb_bind_info(int action);

/* The defaults, then each key_<action> line of the ini file (missing file: the defaults). */
void bb_bindings_load(const char *ini_path);
/* One key_<action>=<value>; false for an unknown action. */
bool bb_bindings_parse(const char *action, const char *value);
int bb_binding_key(int action);   /* SDL scancode, 0 when unbound */
int bb_binding_mouse(int action); /* 1..7, 0 when unbound */

/* What the held keys and mouse inputs do to the pad: buttons to set, a touchpad side to press
 * (-1 none, 0 left, 1 right), and stick axes (0..255, or -1 to leave the gamepad's). `keys` is
 * SDL's key state (NULL: none), `mouse` bit n-1 for mouse input n. */
typedef struct {
    uint32_t buttons;
    uint32_t mouse_buttons; /* the part of buttons only mouse inputs hold */
    int touch_side;
    int lx, ly, rx, ry;
    bool any;
} BindOutput;
void bb_bindings_apply(const bool *keys, int nkeys, uint32_t mouse, BindOutput *out);

#endif
