/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_RUNTIME_STAMINA_H
#define BB_RUNTIME_STAMINA_H
#include <stdint.h>
void runtime_stamina_bind(void *image, uint64_t size);
/* The game thread, on each pad read: the buttons the game is given. */
void runtime_stamina_tick(uint32_t buttons);
#endif
