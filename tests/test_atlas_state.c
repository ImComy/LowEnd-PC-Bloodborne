/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/runtime_atlas_state.h"
#include "../src/runtime_map.h"
#include "../src/code_check.h"
#include "../src/code_check.h"
#include "../src/map_build_guard.inc"
#include "game_code.h"
#include "game_code.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
/* This fixture exercises guarded flag memory and disabled travel. Live map/menu
 * discovery and successful travel are covered by copied-save gameplay checks. */
int runtime_map_location(BbMapLocation *out){memset(out,0,sizeof(*out));return 0;}
int runtime_map_menu_state(BbMapMenu *out){memset(out,0,sizeof(*out));return 0;}
static void put_ptr(unsigned char *p,size_t off,void *value){memcpy(p+off,&value,sizeof(value));}
static void put_u32(unsigned char *p,size_t off,uint32_t value){memcpy(p+off,&value,4);}
int main(void) {
    bb_test_require_game("atlas state");
    bb_test_require_game("atlas state");
    unsigned char *image=calloc(1,0x5570000),*manager=calloc(1,0x100),*head=calloc(1,0x38),*node=calloc(1,0x38),*data=calloc(1,125);
    assert(image&&manager&&head&&node&&data);
    _putenv_s("BB_MAP_TRAVEL_ENABLED","0");
    runtime_atlas_state_bind(image,0x5570000);assert(runtime_atlas_flag(12417830)==-1);
    bb_test_place_game_code(image,0x13cfc00,24);bb_test_place_game_code(image,BB_MAP_GUARD_OFFSET,bb_map_guard.n);
    put_ptr(image,0x553b100,manager);put_u32(manager,0x1c,1000);put_ptr(manager,0x38,head);
    head[0x19]=1;put_ptr(head,8,node);put_ptr(node,0,head);put_ptr(node,0x10,head);put_u32(node,0x20,12417);put_u32(node,0x28,2);put_ptr(node,0x30,data);
    runtime_atlas_state_bind(image,0x5570000);
    assert(runtime_atlas_flag(12417830)==0);
    data[830/8]|=(unsigned char)(0x80u>>(830&7));assert(runtime_atlas_flag(12417830)==1);
    assert(runtime_atlas_flag(12417831)==0);assert(runtime_atlas_flag(12418830)==-1);
    put_u32(node,0x28,1);put_u32(node,0x30,0);put_ptr(manager,0x28,data);put_u32(manager,0x20,125);
    assert(runtime_atlas_flag(12417830)==1);put_u32(manager,0x20,0);assert(runtime_atlas_flag(12417830)==-1);
    put_u32(manager,0x20,125);put_ptr(node,0,node);assert(runtime_atlas_flag(12417830)==-1);
    assert(!runtime_atlas_travel_supported());BbAtlasTravelDestination d={0};d.return_point=2412951;d.unlock_flag=12417830;assert(!runtime_atlas_travel_request(&d));
    runtime_atlas_state_bind(NULL,0);assert(runtime_atlas_flag(12417830)==-1);
    free(data);free(node);free(head);free(manager);free(image);puts("ATLAS_STATE_PASS");return 0;
}
