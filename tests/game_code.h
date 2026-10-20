/* SPDX-License-Identifier: GPL-2.0-or-later
 * Test fixtures from the player's own game. The port keeps no game bytes (src/code_check.h), so
 * a test that matches or runs original code reads it from a prepared eboot.elf (the output of
 * scripts/prepare.py) named by BB_TEST_EBOOT_ELF, at an image offset (the ELF's virtual
 * address). Without one such a test reports SKIP and passes. */
#ifndef BB_TEST_GAME_CODE_H
#define BB_TEST_GAME_CODE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copies `n` bytes of the game at image offset `at` into `out`; 1 when found. */
static inline int bb_test_game_code(uint64_t at, void *out, size_t n) {
    const char *path = getenv("BB_TEST_EBOOT_ELF");
    FILE *f = path && *path ? fopen(path, "rb") : NULL;
    if (!f) return 0;
    unsigned char header[64];
    int found = 0;
    if (fread(header, 1, sizeof(header), f) == sizeof(header) && !memcmp(header, "\x7f" "ELF", 4)) {
        uint64_t phoff;
        uint16_t phentsize, phnum;
        memcpy(&phoff, header + 0x20, 8);
        memcpy(&phentsize, header + 0x36, 2);
        memcpy(&phnum, header + 0x38, 2);
        for (uint16_t i = 0; i < phnum && !found; ++i) {
            unsigned char ph[56];
            uint32_t type;
            uint64_t offset, vaddr, filesz;
            if (fseek(f, (long)(phoff + (uint64_t)i * phentsize), SEEK_SET) || fread(ph, 1, sizeof(ph), f) != sizeof(ph))
                break;
            memcpy(&type, ph, 4);
            memcpy(&offset, ph + 8, 8);
            memcpy(&vaddr, ph + 16, 8);
            memcpy(&filesz, ph + 32, 8);
            if (type == 1 && vaddr <= at && at + n <= vaddr + filesz)
                found = !fseek(f, (long)(offset + at - vaddr), SEEK_SET) && fread(out, 1, n, f) == n;
        }
    }
    fclose(f);
    return found;
}

/* Exits with SKIP (success) when no game is given. */
static inline void bb_test_require_game(const char *test) {
    const char *path = getenv("BB_TEST_EBOOT_ELF");
    if (path && *path) return;
    printf("SKIP %s: set BB_TEST_EBOOT_ELF to a prepared eboot.elf (scripts/prepare.py)\n", test);
    exit(0);
}

/* Copies the game's code at `at` into a fixture image at the same offset; the test fails if
 * the game does not have it. */
static inline void bb_test_place_game_code(unsigned char *image, uint64_t at, size_t n) {
    if (!bb_test_game_code(at, image + at, n)) {
        fprintf(stderr, "BB_TEST_EBOOT_ELF has no code at %#llx\n", (unsigned long long)at);
        exit(1);
    }
}

#endif
