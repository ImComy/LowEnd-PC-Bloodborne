// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_atlas_menu.h"
#include "bbport_atlas_menu_state.h"
#include "bbport_map.h"
#include "bbport_map_art.h"
#include "../../src/runtime_map.h"
#include "../../src/runtime_menu_sound.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace BbAtlasMenu {
namespace {
// The tile's state: the game's pad thread (RoutePad), the window thread (HandleMouse) and the
// present thread (Tick, Draw). Taken before BbMap's own lock, never while holding it.
std::recursive_mutex menu_mutex;
BbAtlasMenuState state;
// visible: drawn (fading in from the first frame the menu is up); ready: also takes the
// mouse, once the menu has been up for the settle time, as the pad already waits.
std::atomic<bool> visible{false},ready{false},focused{false},controller{false},suppressed{false};
std::atomic<SDL_JoystickID> last_controller{0};
bool mouse_held=false,mouse_up_owned=false,hovered=false;
uint32_t map_previous=0;
// The game takes its menu away (a submenu over it, or closing, however it was asked): the
// tile fades out with it from the frame the game starts (BbMapMenu::leaving). Back from a
// submenu the game starts its icons sooner after the list reports it is up than after Start.
std::atomic<bool> exiting{false};std::atomic<uint64_t> exit_since{0};
bool returning=false;float entry_delay_ms=35;
ImVec2 tile_min{},tile_max{};
BbMapMenu menu{};
bool Query() {
    BbMapLocation loc{};
    return !suppressed && BbMap::Enabled() && runtime_map_menu_state(&menu) && menu.top_level && !menu.leaving && runtime_map_location(&loc);
}
void Refresh() {
    const uint64_t now=SDL_GetTicks();
    const bool was=state.was_visible;
    const bool settled=state.Settled(Query(),now);
    if(was&&!state.was_visible&&!BbMap::IsOpen()&&!suppressed&&menu.supported){exiting=true;exit_since=now;returning=true;}
    if(!was&&state.was_visible){entry_delay_ms=returning?17:35;returning=false;exiting=false;}
    if(!state.was_visible&&!menu.supported)returning=false;
    visible=state.was_visible&&!BbMap::IsOpen();ready=settled&&visible;focused=state.focused;
}
bool Hit(float x,float y){return x>=tile_min.x&&y>=tile_min.y&&x<tile_max.x&&y<tile_max.y;}
}
bool Visible(){return visible;}
bool Animating(){return exiting&&SDL_GetTicks()-exit_since<220;}
bool Interacting(){return visible || BbMap::IsOpen() || runtime_map_menu_open();}
bool UsesControllerGlyphs(){const char* qa=std::getenv("BB_ATLAS_QA_DEVICE");return std::getenv("BB_ATLAS_QA")&&qa?std::strcmp(qa,"controller")==0:bool(controller);}
bool UsesPlayStationGlyphs(){const auto type=SDL_GetGamepadTypeForID(last_controller.load());return type==SDL_GAMEPAD_TYPE_PS3||type==SDL_GAMEPAD_TYPE_PS4||type==SDL_GAMEPAD_TYPE_PS5;}
bool Focused(){return focused;}
void Suppress(bool value){std::lock_guard<std::recursive_mutex> guard(menu_mutex);suppressed=value;if(value){visible=false;ready=false;exiting=false;returning=false;state.Reset();}}
void Reset(){std::lock_guard<std::recursive_mutex> guard(menu_mutex);state.Reset();focused=false;visible=false;ready=false;exiting=false;returning=false;hovered=false;mouse_held=false;map_previous=0;}
void Tick(){std::lock_guard<std::recursive_mutex> guard(menu_mutex);Refresh();if(BbMap::IsOpen()&&!runtime_map_menu_open()&&state.focused){BbMap::Close();Reset();}}
uint32_t RoutePad(uint32_t buttons,uint8_t lx,uint8_t ly,uint8_t rx,uint8_t ry) {
    std::lock_guard<std::recursive_mutex> guard(menu_mutex);
    const bool in_menu=Query();
    const bool was_open=BbMap::IsOpen();
    const uint32_t rising=buttons&~map_previous;
    auto result=state.Route(in_menu,menu.selected,was_open,buttons,runtime_map_confirm_button(),SDL_GetTicks());
    if(result.move)runtime_audio_menu_event(0);
    if(result.open){runtime_audio_menu_event(1);BbMap::OpenFromMenu();}
    if(!was_open&&in_menu&&BbMap::ShortcutMode()==1&&(rising&0x4u)){runtime_audio_menu_event(1);BbMap::OpenFromMenu();state.blocked|=buttons;result.buttons=0;}
    if(was_open) {
        if(result.close)BbMap::Back();
        else BbMap::MappedPad(rising,lx,ly,rx,ry,buttons);
        result.buttons=0;
    }
    map_previous=buttons;
    (void)rx;(void)ry;
    Refresh();return result.buttons;
}
bool HandleMouse(const SDL_Event& e) {
    std::lock_guard<std::recursive_mutex> guard(menu_mutex);
    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST || e.type==SDL_EVENT_GAMEPAD_REMOVED){if(e.type==SDL_EVENT_GAMEPAD_REMOVED)BbMap::Close();Reset();return false;}
    if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || (e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&std::abs(int(e.gaxis.value))>6000)){controller=true;last_controller=e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN?e.gbutton.which:e.gaxis.which;}
    if(e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_MOUSE_MOTION || e.type==SDL_EVENT_MOUSE_BUTTON_DOWN)controller=false;
    if(e.type==SDL_EVENT_MOUSE_BUTTON_UP&&e.button.button==SDL_BUTTON_LEFT&&mouse_up_owned){
        mouse_up_owned=false;const bool pressed=mouse_held;mouse_held=false;
        SDL_Window* window=SDL_GetWindowFromID(e.button.windowID);const float d=window?SDL_GetWindowPixelDensity(window):1;
        if(pressed&&ready&&Hit(e.button.x*d,e.button.y*d)){runtime_audio_menu_event(1);BbMap::OpenFromMenu();}return true;
    }
    if(!ready)return false;
    if(e.type==SDL_EVENT_MOUSE_MOTION) {
        SDL_Window* window=SDL_GetWindowFromID(e.motion.windowID);const float d=window?SDL_GetWindowPixelDensity(window):1;
        const bool hit=Hit(e.motion.x*d,e.motion.y*d);
        if(hit&&!hovered&&!state.focused)runtime_audio_menu_event(0);
        hovered=hit;
        ImGui::GetIO().AddMousePosEvent(e.motion.x*d,e.motion.y*d);
        return true;
    }
    if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT){
        SDL_Window* window=SDL_GetWindowFromID(e.button.windowID);const float d=window?SDL_GetWindowPixelDensity(window):1;
        if(Hit(e.button.x*d,e.button.y*d)){mouse_held=true;mouse_up_owned=true;if(!state.focused&&!hovered)runtime_audio_menu_event(0);state.focused=true;state.anchor=menu.selected;focused=true;return true;}
    }
    return false;
}
void Draw() {
    std::lock_guard<std::recursive_mutex> guard(menu_mutex);
    const bool leaving=!Visible()&&Animating();
    if(!Visible()&&!leaving)return;
    const auto* vp=ImGui::GetMainViewport();const float s=std::min(vp->Size.x/1280.0f,vp->Size.y/720.0f);
    const ImVec2 offset(vp->Pos.x+(vp->Size.x-1280*s)*.5f,vp->Pos.y+(vp->Size.y-720*s)*.5f);
    // Moves with the game's menu, measured on 60 FPS captures. The game animates its icons in
    // 6 steps at 30 per second: in, the first step about 35 ms after the list reports it is up,
    // 1/6 bright and 11 units (of 1280) left of their place, then 1/6 brighter and 1/5 of the
    // way right each step; out, the same in reverse from the frame the game starts. Back from the
    // open map the game's menu never left, so nothing moves.
    constexpr float step_ms=1000.f/30,slide=11;constexpr int steps=6;
    const uint64_t now=SDL_GetTicks();
    float alpha,dx;
    if(leaving){
        const int k=int(float(now-exit_since.load())/step_ms);
        alpha=std::clamp(1.f-float(k)/steps,0.f,1.f);dx=-slide*float(std::min(k,steps-1))/(steps-1);
    } else {
        const float t=float(now-state.visible_since)-entry_delay_ms;const int k=t<0?-1:int(t/step_ms);
        alpha=k<0?0.f:std::min(1.f,float(k+1)/steps);dx=-slide*(1.f-float(std::clamp(k,0,steps-1))/(steps-1));
    }
    auto p=[&](float x,float y){return ImVec2(offset.x+(x+dx)*s,offset.y+y*s);};
    tile_min=p(474,280);tile_max=p(530,336);
    auto* draw=ImGui::GetForegroundDrawList();
    auto fade=[&](ImU32 c){return (c&~IM_COL32_A_MASK)|(ImU32(((c>>IM_COL32_A_SHIFT)&255)*alpha)<<IM_COL32_A_SHIFT);};
    const bool active=!leaving&&(state.focused||hovered);
    if(active){
        // Cover only the native contextual text and focus rim. The native
        // selection remains intact, so Left/Back needs no memory patch.
        const ImU32 plate=fade(IM_COL32(25,27,26,255)),clear=IM_COL32(25,27,26,0);
        draw->AddRectFilled(p(160,321),p(276,343),plate);
        draw->AddRectFilledMultiColor(p(155,321),p(160,343),clear,plate,plate,clear);
        draw->AddRectFilledMultiColor(p(276,321),p(281,343),plate,clear,clear,plate);
        const char* language=std::getenv("BB_LANGUAGE");const char* label=language&&std::atoi(language)==8?"Карта":"Map";
        ImFont* font=language&&std::atoi(language)==8?ImGui::GetFont():BbMap::TitleFont();const float size=20*s;const auto extent=font->CalcTextSizeA(size,1000,0,label);
        draw->AddText(font,size,ImVec2(offset.x+269*s-extent.x,offset.y+323*s),fade(IM_COL32(196,177,125,255)),label);
        if(state.anchor==2||state.anchor==0){
            const float x=state.anchor==2?414.f:294.f;
            draw->AddRect(p(x,280),p(x+56,336),fade(IM_COL32(38,34,28,255)),0,0,5.5f*s);
            draw->AddRectFilled(p(x+2,282),p(x+54,334),fade(IM_COL32(20,18,15,64)));
            draw->AddRect(p(x,280),p(x+56,336),fade(IM_COL32(131,123,103,255)),0,0,.8f*s);
        }
    }
    BbMapArt::DrawMenuIcon(draw,ImVec2((tile_min.x+tile_max.x)*.5f,(tile_min.y+tile_max.y)*.5f),56*s,fade(mouse_held?IM_COL32(180,170,145,255):IM_COL32_WHITE));
    draw->AddRect(tile_min,tile_max,fade(active?IM_COL32(235,233,220,255):IM_COL32(131,123,103,255)),0,0,(active?1.7f:.8f)*s);
    if(hovered)ImGui::SetTooltip("Map");
}
}
