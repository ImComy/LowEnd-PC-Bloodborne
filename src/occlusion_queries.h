/* SPDX-License-Identifier: GPL-2.0-or-later
 * The game's occlusion queries without GPU memory (occlusion_queries.c): skipped and reported
 * visible instead of crashing (#83, #93). Always on with the engine hooks. */
#ifndef BB_OCCLUSION_QUERIES_H
#define BB_OCCLUSION_QUERIES_H

/* Hooks the queries' create, begin and end calls; returns hooks added. */
int bb_occlusion_queries_install(void);

#endif
