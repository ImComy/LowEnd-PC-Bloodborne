/* SPDX-License-Identifier: GPL-2.0-or-later
 * "Rebirth in the Nightmare" at the Altar of Despair: the runtime half of the altar's rewritten
 * talk script (scripts/enhancements.py, pc_rebirth). Ported from droogie's bbhost
 * (https://github.com/droogie/bbhost, GPL-3.0-or-later, engine/rebirth.cpp, player_data.cpp,
 * params.cpp, menu_steps.cpp).
 *
 * The script and the runtime talk through four event flags the game's own scripts never use:
 * the script sets REQ_RESET and waits; the runtime returns the hunter to their origin's level
 * and attributes, gives back the blood echoes those levels cost (FAIL when it cannot) and clears
 * the flag. The script then sets MENU and opens the doll's level-up menu; the runtime clears MENU
 * once that menu has closed (the doll's own "menu is open" check never reads true at the altar).
 * Leaving it, "Undo Rebirth" sets REQ_UNDO and the runtime restores the hunter as they were.
 *
 * Everything runs once a frame on the game's main thread, and only in the altar's map; any world
 * load (or leaving the map) clears the flags, so a request a save carried over is not one. */
#include "rebirth.h"
#include "engine_hooks.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GAME_DATA_MAN = 0x593b130,    /* GameDataMan*; the local player's record at +0x08 */
    WORLD_CHR_MAN = 0x593e878,    /* WorldChrMan*; the main player ChrIns at +0x60 */
    EVENT_FLAG_MAN = 0x593b100,   /* SprjEventFlagMan* */
    IS_EVENT_FLAG = 0x17cfc00,    /* (man, id, int* exists) -> on */
    SET_EVENT_FLAG = 0x17cfcc0,   /* (man, id, on) */
    PARAM_REPOSITORY = 0x5940340, /* SoloParamRepository* */
    APPLY_STATS = 0x2013d00,      /* the level-up's apply: (captures*) */
    STEP_CTOR = 0x201cce0,        /* a menu step's constructor (step, ctx, type, name, ...) */
};
enum { ALTAR_BLOCK = 0x18020000 }; /* m24_02_00_00, the Altar of Despair's map */
enum { REQ_RESET = 72500390, REQ_UNDO = 72500391, FAIL = 72500392, MENU = 72500393 };
enum { LEVEL_UP_STEP = 7, MENU_WAIT_FRAMES = 120 };
static const int64_t ECHO_CAP = 999999999; /* the game's own limit */
static const BbCode step_prologue = {16, 0x7ded7eceb396e10aull};

typedef int64_t (BB_GUEST_ABI *GuestFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static GuestFn fn(uint64_t bn) { return (GuestFn)bb_guest(BB_BN(bn)); }
static uint64_t slot_value(uint64_t bn) {
    uint64_t v = 0;
    return bb_read((uint64_t)(uintptr_t)bb_guest(BB_BN(bn)), &v, 8) ? v : 0;
}

/* --- The player ---------------------------------------------------------------------------- */

/* GameDataManPlayerRecord fields, 32-bit signed. */
enum { REC_HP = 0x14, REC_LEVEL = 0x90, REC_ECHOES = 0x94, REC_ORIGIN = 0xce };
static const uint32_t STAT_AT[6] = {0x40, 0x48, 0x58, 0x60, 0x68, 0x70}; /* vit end str skl bld arc */

typedef struct {
    int64_t level, stat[6], echoes;
} Build;

static uint64_t player_record(void) {
    const uint64_t man = slot_value(GAME_DATA_MAN);
    uint64_t rec = 0;
    return man && bb_read(man + 8, &rec, 8) ? rec : 0;
}

static int read32(uint64_t at, int64_t *out) {
    int32_t v;
    if (!bb_read(at, &v, 4)) return 0;
    *out = v;
    return 1;
}

static void write32(uint64_t at, int64_t value) {
    const int32_t v = (int32_t)value;
    memcpy((void *)(uintptr_t)at, &v, 4);
}

static int read_build(Build *b) {
    const uint64_t rec = player_record();
    if (!rec || !read32(rec + REC_LEVEL, &b->level) || !read32(rec + REC_ECHOES, &b->echoes)) return 0;
    for (int i = 0; i < 6; ++i)
        if (!read32(rec + STAT_AT[i], &b->stat[i])) return 0;
    return 1;
}

/* The player's map block, or 0 when no world is loaded. */
static uint32_t player_block(void) {
    const uint64_t man = slot_value(WORLD_CHR_MAN);
    uint64_t chr = 0;
    uint32_t block = 0;
    if (!man || !bb_read(man + 0x60, &chr, 8) || !chr || !bb_read(chr + 0x3f8, &block, 4) || block == 0xffffffffu)
        return 0;
    return block;
}

/* --- Event flags, through the game's own functions ------------------------------------------ */

static int flag(uint32_t id) {
    const uint64_t man = slot_value(EVENT_FLAG_MAN);
    int32_t exists = 0;
    return man && (fn(IS_EVENT_FLAG)(man, id, (uint64_t)(uintptr_t)&exists, 0, 0, 0) & 0xff) != 0;
}

static void set_flag(uint32_t id, int on) {
    const uint64_t man = slot_value(EVENT_FLAG_MAN);
    if (man) fn(SET_EVENT_FLAG)(man, id, (uint64_t)(on != 0), 0, 0, 0);
}

/* --- Param rows ------------------------------------------------------------------------------ */
/* SoloParamRepository: 63 slots at +0x70, 0x48 apart, each a count and up to eight caps; a cap's
 * name is the wstring at +0x18 (inline below 8 characters, capacity at +0x30) and its file at
 * *(*(cap + 0x70) + 0x70): rows u16 at +0xa, format bytes at +0x2d/+0x2e, records from +0x40. */

static int cap_named(uint64_t cap, const char *want) {
    uint64_t capacity = 0, text = cap + 0x18;
    if (!bb_read(cap + 0x30, &capacity, 8) || (capacity >= 8 && !bb_read(cap + 0x18, &text, 8))) return 0;
    for (size_t i = 0;; ++i) {
        uint16_t c = 0;
        if (!bb_read(text + 2 * i, &c, 2)) return 0;
        if (c != (unsigned char)want[i]) return 0;
        if (!c) return 1;
    }
}

static uint64_t param_row(const char *table, uint32_t id) {
    const uint64_t repo = slot_value(PARAM_REPOSITORY);
    if (!repo) return 0;
    for (unsigned s = 0; s < 63; ++s) {
        int32_t count = 0;
        if (!bb_read(repo + 0x70 + s * 0x48, &count, 4) || count <= 0) continue;
        for (int k = 0; k < count && k < 8; ++k) {
            uint64_t cap = 0, holder = 0, file = 0;
            if (!bb_read(repo + 0x70 + s * 0x48 + 8 + 8 * k, &cap, 8) || !cap || !cap_named(cap, table)) continue;
            if (!bb_read(cap + 0x70, &holder, 8) || !holder || !bb_read(holder + 0x70, &file, 8) || !file) return 0;
            uint16_t rows = 0;
            uint8_t format = 0, flags = 0;
            if (!bb_read(file + 0xa, &rows, 2) || !bb_read(file + 0x2d, &format, 1) || !bb_read(file + 0x2e, &flags, 1))
                return 0;
            /* Records by id, ascending: a binary search as bbhost's params_row. */
            uint32_t lo = 0, hi = rows;
            while (lo < hi) {
                const uint32_t mid = lo + (hi - lo) / 2;
                uint32_t rid = 0;
                uint64_t data = 0;
                if (format == 2) {
                    uint32_t off = 0;
                    if (!bb_read(file + 0x34 + 0xc * mid, &rid, 4) || !bb_read(file + 0x38 + 0xc * mid, &off, 4)) return 0;
                    data = file + off;
                } else if (format <= 3 || !(flags & 2)) {
                    uint32_t off = 0;
                    if (!bb_read(file + 0x40 + 0xc * mid, &rid, 4) || !bb_read(file + 0x44 + 0xc * mid, &off, 4)) return 0;
                    data = file + off;
                } else {
                    uint64_t off = 0;
                    if (!bb_read(file + 0x40 + 0x18 * mid, &rid, 4) || !bb_read(file + 0x48 + 0x18 * mid, &off, 8)) return 0;
                    data = file + off;
                }
                if (rid == id) return data;
                if (rid < id) lo = mid + 1;
                else hi = mid;
            }
            return 0;
        }
    }
    return 0;
}

/* --- The level-up menu's step --------------------------------------------------------------- */
/* Each menu the game opens is a refcounted step (count at +8) made by STEP_CTOR with a type and
 * a name; the runtime holds a reference on the level-up menu's, and when its own is the last the
 * menu has closed. */

static volatile uint64_t g_step_made;

static BB_GUEST_ABI void step_hook(uint64_t step, uint64_t ctx, uint64_t type, const uint16_t *name) {
    (void)ctx;
    static const char want[] = "LevelUp";
    if (type != LEVEL_UP_STEP || !name) return;
    for (size_t i = 0; i < sizeof(want); ++i)
        if (name[i] != (unsigned char)want[i]) return;
    g_step_made = step;
}

static int32_t *step_count(uint64_t step) { return (int32_t *)(uintptr_t)(step + 8); }

static void step_release(uint64_t step) {
    if (--*step_count(step) == 0) {
        uint64_t vtable = 0, dtor = 0;
        if (bb_read(step, &vtable, 8) && bb_read(vtable, &dtor, 8)) ((GuestFn)(uintptr_t)dtor)(step, 0, 0, 0, 0, 0);
    }
}

/* --- Rebirth --------------------------------------------------------------------------------- */

static int g_loading = 1, g_at_altar, g_have_snapshot, g_menu_wait, g_said;
static uint64_t g_menu;
static Build g_snapshot;

/* The game's own apply, which the level-up's YES runs: level and attributes into the record,
 * then hit points, stamina and the character refreshed. It reads them from a functor's captures:
 * +8 the level, +0xc.. the six attributes. */
static void apply(const Build *b) {
    static _Alignas(16) uint32_t captures[10];
    memset(captures, 0, sizeof(captures));
    captures[2] = (uint32_t)b->level;
    for (int i = 0; i < 6; ++i) captures[3 + i] = (uint32_t)b->stat[i];
    fn(APPLY_STATS)((uint64_t)(uintptr_t)captures, 0, 0, 0, 0, 0);
}

/* Lowering vitality takes the drop in maximum hit points off the current ones, which could leave
 * the hunter dead at the altar: the character's hit points and the record's are made whole. */
static void heal(void) {
    const uint64_t man = slot_value(WORLD_CHR_MAN);
    uint64_t chr = 0, mods = 0, hp = 0;
    int32_t max = 0;
    if (!man || !bb_read(man + 0x60, &chr, 8) || !chr || !bb_read(chr + 0x3b0, &mods, 8) || !mods ||
        !bb_read(mods + 0x20, &hp, 8) || !hp || !bb_read(hp + 0xfc, &max, 4) || max <= 0)
        return;
    memcpy((void *)(uintptr_t)(hp + 0xf8), &max, 4);
    const uint64_t rec = player_record();
    if (rec) write32(rec + REC_HP, max);
}

/* The echoes for one level, as the level-up screen prices it: CalcCorrectGraph row 200's
 * init_inclination_soul, adjustment_value, boundry_inclination_soul and boundry_value. */
static int64_t level_cost(int64_t level, const float g[4]) {
    const float x = (float)(level + 81);
    float k = x - g[3];
    if (k < 0.f) k = 0.f;
    k = k * g[2] + g[0];
    return (int64_t)(x * x * k + g[1] + 0.5f);
}

static int reset(void) {
    Build cur, base;
    int64_t origin = 0;
    const uint64_t rec = player_record();
    uint8_t origin_byte = 0;
    if (!read_build(&cur) || !rec || !bb_read(rec + REC_ORIGIN, &origin_byte, 1)) {
        puts("Rebirth: no character to read");
        return 0;
    }
    origin = origin_byte;
    const uint64_t row = param_row("CharaInitParam", 3000u + (uint32_t)origin);
    const uint64_t graph = param_row("CalcCorrectGraph", 200);
    int16_t level = 0;
    uint8_t at[0xc9 - 0xc2];
    float g[4];
    if (!row || !graph || !bb_read(row + 0xc0, &level, 2) || !bb_read(row + 0xc2, at, sizeof(at)) ||
        !bb_read(graph + 0x3c, g, sizeof(g))) {
        printf("Rebirth: no origin row (origin %d) or level-cost row\n", (int)origin);
        return 0;
    }
    base.level = level;
    /* baseVit, baseWil, baseStr, baseDex, baseMag, baseFai at +0xc2 +0xc3 +0xc5 +0xc6 +0xc7 +0xc8 */
    const int pick[6] = {0, 1, 3, 4, 5, 6};
    for (int i = 0; i < 6; ++i) base.stat[i] = at[pick[i]];
    /* Every attribute point above the origin's is a level bought. */
    int64_t gained = 0, refund = 0;
    for (int i = 0; i < 6; ++i) gained += cur.stat[i] > base.stat[i] ? cur.stat[i] - base.stat[i] : 0;
    if (gained != cur.level - base.level)
        printf("Rebirth: level %lld but %lld points above the origin's: refunding the points\n",
               (long long)cur.level, (long long)gained);
    for (int64_t l = base.level; l < base.level + gained; ++l) refund += level_cost(l, g);
    if (cur.echoes + refund > ECHO_CAP) {
        printf("Rebirth: %lld echoes held and %lld to give back pass the limit\n", (long long)cur.echoes, (long long)refund);
        return 0;
    }
    g_snapshot = cur;
    g_have_snapshot = 1;
    base.echoes = cur.echoes + refund;
    apply(&base);
    heal();
    write32(rec + REC_ECHOES, base.echoes);
    printf("Rebirth: level %lld to %lld (origin %d), %lld blood echoes given back\n", (long long)cur.level,
           (long long)base.level, (int)origin, (long long)refund);
    return 1;
}

static void undo(void) {
    const uint64_t rec = player_record();
    if (!g_have_snapshot || !rec) {
        puts("Rebirth: nothing to undo");
        return;
    }
    apply(&g_snapshot);
    heal();
    write32(rec + REC_ECHOES, g_snapshot.echoes);
    g_have_snapshot = 0;
    printf("Rebirth: undone - level %lld\n", (long long)g_snapshot.level);
}

/* The handshake starts over: flags cleared, the undo forgotten, the level-up menu let go. */
static void start_over(void) {
    set_flag(REQ_RESET, 0), set_flag(REQ_UNDO, 0), set_flag(FAIL, 0), set_flag(MENU, 0);
    g_have_snapshot = 0;
    if (g_menu) step_release(g_menu), g_menu = 0;
}

static void tick(void) {
    const uint64_t made = g_step_made;  /* every frame: a doll's level-up elsewhere is not ours */
    g_step_made = 0;
    const uint32_t block = player_block();
    if (!block) {
        g_loading = 1;
        return;
    }
    const int here = block == ALTAR_BLOCK;
    if (g_loading || here != g_at_altar) {  /* a world came (back), or the hunter walked in or out */
        g_loading = 0;
        g_at_altar = here;
        start_over();
        return;
    }
    if (!here) return;
    if (flag(REQ_RESET)) {
        if (!reset()) set_flag(FAIL, 1);
        set_flag(REQ_RESET, 0);
    }
    if (flag(REQ_UNDO)) {
        undo();
        set_flag(REQ_UNDO, 0);
    }
    if (flag(MENU)) {
        if (!g_menu && made && *step_count(made) >= 1) {
            ++*step_count(made);  /* ours: when it is the last, the menu has closed */
            g_menu = made;
            g_menu_wait = 0;
        }
        if (g_menu && *step_count(g_menu) == 1) {
            step_release(g_menu);
            g_menu = 0;
            set_flag(MENU, 0);
        } else if (!g_menu && ++g_menu_wait > MENU_WAIT_FRAMES) {
            g_menu_wait = 0;
            set_flag(MENU, 0);
            if (g_said++ < 4) puts("Rebirth: the level-up menu did not open");
        }
    } else if (g_menu) {
        step_release(g_menu);
        g_menu = 0;
    }
}

int bb_rebirth_install(void) {
    const char *on = getenv("BB_PC_REBIRTH_ACTIVE");
    if (!on || strcmp(on, "1")) {
        puts("Rebirth: off (pc_rebirth, or the altar's script was not made)");
        return 0;
    }
    if (bb_hook_entry(BB_BN(STEP_CTOR), step_prologue, (void *)step_hook)) {
        printf("Rebirth: off - the menu step constructor: %s\n", bb_hooks_error());
        return 0;
    }
    bb_on_main_frame(tick);
    puts("Rebirth: the Altar of Despair offers Rebirth in the Nightmare (with the Yharnam Stone)");
    return 1;
}
