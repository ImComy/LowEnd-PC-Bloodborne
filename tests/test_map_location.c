/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/runtime_map.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../src/code_check.h"
#include "../src/code_check.h"
#include "../src/map_build_guard.inc"
#include "game_code.h"
#include "game_code.h"
static void ptr(unsigned char* memory,size_t off,void* value){memcpy(memory+off,&value,sizeof(value));}
int main(void) {
    BbMapLocation l;
    bb_test_require_game("map location");
    bb_test_require_game("map location");
    unsigned char *image=calloc(1,0x5570000),*world=calloc(1,0x100),*chr=calloc(1,0x600),*modules=calloc(1,0x100),*transform=calloc(1,0x300),*menu=calloc(1,0x100);
    assert(image && world && chr && modules && transform);
    runtime_map_bind(image,0x5570000);assert(!runtime_map_location(&l) && !l.valid && !runtime_map_menu_open());
    /* The anchors, the menu predicate and the focus read, from the player's game. */
    bb_test_place_game_code(image,BB_MAP_GUARD_OFFSET,bb_map_guard.n);
    bb_test_place_game_code(image,0x1bdf835,16);
    bb_test_place_game_code(image,0x176b89b,11);ptr(image,0x5562878,menu);
    runtime_map_bind(image,0x5570000);assert(!runtime_map_menu_open());
    assert(runtime_map_confirm_button()==0); /* Missing native predicate: fail closed. */
    const unsigned char cross[]={0x31,0xc0,0xc3,0x90,0x90,0x90};
    const unsigned char circle[]={0xb8,0x01,0x00,0x00,0x00,0xc3};
    memcpy(image+0x1fb8730,cross,sizeof(cross));
    _putenv_s("BB_ENTER_BUTTON","0");
    assert(runtime_map_confirm_button()==0x4000u); /* Conflicting environment cannot override game. */
    memcpy(image+0x1fb8730,circle,sizeof(circle));
    _putenv_s("BB_ENTER_BUTTON","1");
    assert(runtime_map_confirm_button()==0x2000u);
    image[0x1fb8730+1]=2;assert(runtime_map_confirm_button()==0);
    BbMapMenu native_menu;assert(!runtime_map_menu_state(&native_menu)&&!native_menu.supported);
    memcpy(image+0x1fb8730,cross,sizeof(cross));
    _putenv_s("BB_ENTER_BUTTON","");
    bb_test_place_game_code(image,0x1fb8730,32); /* the game's own region predicate */
    assert(runtime_map_confirm_button()==0x4000u);
    image[0x1fb8730+24]^=1;assert(runtime_map_confirm_button()==0);
    memcpy(image+0x1fb8730,cross,sizeof(cross));
    int state=2;memcpy(menu+0x54,&state,4);assert(runtime_map_menu_open());
    state=1;memcpy(menu+0x54,&state,4);assert(!runtime_map_menu_open());
    state=0;memcpy(menu+0x54,&state,4);assert(!runtime_map_menu_open());
    ptr(image,0x5562878,(void*)0x12345678);assert(!runtime_map_menu_open());
    ptr(image,0x5562878,menu);
    ptr(image,0x553e878,world);ptr(world,0x60,chr);ptr(chr,0x3b0,modules);ptr(modules,0x68,transform);
    ptr(chr,0x288,world);uint32_t block=0x18010000;memcpy(chr+0x3f8,&block,4);
    float pos[3]={-196.75f,-28.65f,65.39f},yaw=1.0f;memcpy(transform+0x1e0,pos,12);memcpy(transform+0x1d4,&yaw,4);
    assert(runtime_map_location(&l) && l.valid && l.x==pos[0] && l.block==block);
    pos[0]=NAN;memcpy(transform+0x1e0,pos,12);assert(!runtime_map_location(&l) && !l.valid);
    ptr(modules,0x68,(void*)0x12345);assert(!runtime_map_location(&l));
    ptr(world,0x60,NULL);assert(!runtime_map_location(&l));
    runtime_map_bind(NULL,0);assert(runtime_map_confirm_button()==0);
    free(menu);free(transform);free(modules);free(chr);free(world);free(image);
    puts("MAP_LOCATION_READ_ONLY_PASS");return 0;
}
