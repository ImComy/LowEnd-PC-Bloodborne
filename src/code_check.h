/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_CODE_CHECK_H
#define BB_CODE_CHECK_H

#include <stddef.h>
#include <stdint.h>

/* A site in the game's code, recognised without carrying the game's bytes: its length and a
 * 64-bit FNV-1a hash of the original instructions there. The port checks a site before it hooks
 * or patches it; the bytes themselves are only ever read from the player's own executable. */
typedef struct {
    size_t n;
    uint64_t hash;
} BbCode;

static inline uint64_t bb_code_hash(const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

/* Whether `at` holds the code `code` describes. */
static inline int bb_code_is(const void *at, BbCode code) {
    return at && code.n && bb_code_hash(at, code.n) == code.hash;
}

#endif
