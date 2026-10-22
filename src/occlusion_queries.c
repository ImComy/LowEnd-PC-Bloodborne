/* SPDX-License-Identifier: GPL-2.0-or-later
 * Occlusion queries the game could not create (#83, #93: "read access to 0000000000000000 at
 * guest offset 0x2169edc" after an hour or more of play).
 *
 * The game keeps its occlusion tests in a pool of 0x30-byte entries (sub_ec6820 makes 8192 at
 * start-up; sub_ec68d0 makes another whenever the free list is empty). Each entry holds at +0 a
 * query descriptor, created by sub_21674f0 with its result memory from the graphics allocator at
 * [manager+0xdc18]: the 152 MiB direct-memory heap (heap-size table entry image 0x4736dd0) that
 * "Increased Graphics Heap Sizes" leaves alone, with ~5 MiB free after start-up. When that
 * allocation fails the descriptor stays NULL - the entry's constructor (sub_2288d00) ignores
 * the result - and the first begin (sub_e2f130 -> sub_2169ec0) reads through it.
 *
 * Here an entry without a descriptor is skipped: begin and end leave its query state at 0 and
 * its result at a non-zero pixel count, which the result read (sub_e2f190, query state 0) hands
 * back as "visible". That is what every query already answers on this port: the GPU layer
 * reports a fixed non-zero count for each one (EventWrite ZpassDone in liverpool.cpp), so the
 * game draws exactly what it drew before. Failed creations are logged with the heap's free space.
 *
 * BB_QUERY_STATS=1 also logs the heap and the number of queries made every 10 seconds (main
 * thread), to watch the heap across area loads.
 *
 * Addresses are image offsets (eboot virtual address; Binary Ninja's minus 0x400000). */
#include "occlusion_queries.h"
#include "engine_hooks.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    QUERY_CREATE = 0x21674f0, /* (manager, entry, &kind, 0, 0) -> 0 on success */
    QUERY_BEGIN = 0xe2f130,   /* (entry, context) -> 1 */
    QUERY_END = 0xe2f160,     /* (entry, context) -> 1 */
    GRAPHICS_MANAGER = 0x55406c8, /* slot holding the manager's address */
};
/* The calls to them: the one create (in sub_e2f080), and begin/end from the frame packet's
 * occlusion pass (sub_2285e10) and the entry's own steps (sub_2288d70, sub_2288d80). */
static const uint64_t CREATE_CALL = 0xe2f102;
static const uint64_t BEGIN_CALLS[] = {0x228625b, 0x22864fb, 0x2288d74};
static const uint64_t END_CALLS[] = {0x2286428, 0x22866c8, 0x2288d89};

/* Entry fields: descriptor, query state (0 idle, 2 begun, 1 ended), result. */
enum { ENTRY_QUERY = 0x00, ENTRY_STATE = 0x08, ENTRY_RESULT = 0x10 };
/* Graphics allocator fields: free bytes, live allocations. */
enum { MANAGER_ALLOCATOR = 0xdc18, ALLOCATOR_FREE = 0x28, ALLOCATOR_LIVE = 0x30 };
/* The result a skipped query reports: the GPU layer's count per query (liverpool.cpp). */
static const uint64_t VISIBLE = 0x2ffffff;
enum { STATS_FRAMES = 600 };

typedef int (BB_GUEST_ABI *CreateFn)(uint64_t, uint64_t, void *, uint64_t, uint64_t);
typedef int (BB_GUEST_ABI *StepFn)(uint64_t, uint64_t);

static atomic_uint g_created, g_failed, g_skipped;

static uint64_t read_u64(uint64_t address) {
    uint64_t v = 0;
    return bb_read(address, &v, 8) ? v : 0;
}

/* The graphics allocator's free bytes and live allocations; 0 when not there yet. */
static int heap_state(uint64_t *free_bytes, uint64_t *live) {
    const uint64_t slot = read_u64((uint64_t)(uintptr_t)bb_guest(GRAPHICS_MANAGER));
    const uint64_t manager = read_u64(slot);
    const uint64_t allocator = manager ? read_u64(manager + MANAGER_ALLOCATOR) : 0;
    return allocator && bb_read(allocator + ALLOCATOR_FREE, free_bytes, 8) &&
           bb_read(allocator + ALLOCATOR_LIVE, live, 8);
}

static BB_GUEST_ABI int on_create(uint64_t manager, uint64_t entry, void *kind, uint64_t a, uint64_t b) {
    const int status = ((CreateFn)bb_guest(QUERY_CREATE))(manager, entry, kind, a, b);
    atomic_fetch_add(&g_created, 1);
    if (status == 0 && read_u64(entry + ENTRY_QUERY)) return status;
    /* The first failure, then every 1000th. */
    const unsigned failed = atomic_fetch_add(&g_failed, 1);
    if (failed % 1000 == 0) {
        uint64_t free_bytes = 0, live = 0;
        heap_state(&free_bytes, &live);
        printf("Occlusion query: could not be created (code %d, %u so far): graphics heap %llu KiB "
               "free, %llu allocations; its tests are skipped and drawn as visible\n",
               status, failed + 1, (unsigned long long)(free_bytes >> 10), (unsigned long long)live);
        fflush(stdout);
    }
    return status;
}

static void note_skip(void) {
    if (atomic_fetch_add(&g_skipped, 1) == 0) {
        puts("Occlusion query: an entry without a query was skipped (drawn as visible)");
        fflush(stdout);
    }
}

static BB_GUEST_ABI int on_begin(uint64_t entry, uint64_t context) {
    if (read_u64(entry + ENTRY_QUERY)) return ((StepFn)bb_guest(QUERY_BEGIN))(entry, context);
    uint32_t idle = 0;
    memcpy((void *)(uintptr_t)(entry + ENTRY_STATE), &idle, 4);
    memcpy((void *)(uintptr_t)(entry + ENTRY_RESULT), &VISIBLE, 8);
    note_skip();
    return 1;
}

static BB_GUEST_ABI int on_end(uint64_t entry, uint64_t context) {
    if (read_u64(entry + ENTRY_QUERY)) return ((StepFn)bb_guest(QUERY_END))(entry, context);
    return 1;
}

static void stats_tick(void) {
    static unsigned frames;
    if (++frames % STATS_FRAMES) return;
    uint64_t free_bytes = 0, live = 0;
    if (!heap_state(&free_bytes, &live)) return;
    printf("Occlusion query stats: graphics heap %llu KiB free, %llu allocations; queries made %u, "
           "failed %u, skipped begins %u\n",
           (unsigned long long)(free_bytes >> 10), (unsigned long long)live, atomic_load(&g_created),
           atomic_load(&g_failed), atomic_load(&g_skipped));
    fflush(stdout);
}

int bb_occlusion_queries_install(void) {
    int hooks = 0, refused = 0;
    /* Begin and end first: without them a logged failure would still crash. */
    for (size_t i = 0; i < sizeof(BEGIN_CALLS) / sizeof(*BEGIN_CALLS); ++i)
        if (bb_hook_call(BEGIN_CALLS[i], QUERY_BEGIN, (void *)on_begin)) ++refused; else ++hooks;
    for (size_t i = 0; i < sizeof(END_CALLS) / sizeof(*END_CALLS); ++i)
        if (bb_hook_call(END_CALLS[i], QUERY_END, (void *)on_end)) ++refused; else ++hooks;
    if (refused) {
        printf("Occlusion query guard: %d of %d calls not hooked (%s)\n", refused, hooks + refused,
               bb_hooks_error());
        return hooks;
    }
    hooks += !bb_hook_call(CREATE_CALL, QUERY_CREATE, (void *)on_create);
    const char *env = getenv("BB_QUERY_STATS");
    if (env && *env == '1') bb_on_main_frame(stats_tick);
    return hooks;
}
