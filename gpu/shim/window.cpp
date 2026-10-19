// bbport: SDL3 window for the Vulkan swapchain (X11 or Wayland).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <SDL3/SDL.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "sdl_window.h"
#include "bbport_overlay.h"
#include "stage_point.h"
#include "bbport_settings.h"
#include "../bbgpu.h"

void NoteMouseWheel(float y); // below: wheel notches for the pad read
void NoteMouseMotion(float x, float y); // below: motion for the mouse camera
bool TakeWindowStatus(std::string& status); // below: a status for the title (shader pre-build)
void SetMouseCaptured(bool captured);
static std::atomic<bool> g_menu_pointer{false}; // below: a menu shows the pointer
static std::atomic<bool> g_window_focused{false};
static std::atomic<int> g_window_points_w{0}, g_window_points_h{0};
static bool g_pad_in_use = false; // window thread: the last input came from a controller

namespace Frontend {

WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title) : width{width_}, height{height_} {
    // Gamepads are sampled by runtime_pad.c; their events are pumped here with the window's.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        UNREACHABLE_MSG("Failed to initialize SDL video: {}", SDL_GetError());
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width_);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height_);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    fullscreen_applied = BbSettings::Get().fullscreen.load(); // BB_FULLSCREEN, from the launcher
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, fullscreen_applied);
    base_title = title;
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    ASSERT_MSG(window, "Failed to create window: {}", SDL_GetError());

    const char* driver = SDL_GetCurrentVideoDriver();
    const SDL_PropertiesID wp = SDL_GetWindowProperties(window);
#ifdef _WIN32
    if (driver && !std::strcmp(driver, "windows")) {
        window_info.type = WindowSystemType::Windows;
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        on_hdr_display = SDL_GetBooleanProperty(wp, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
    } else
#endif
    if (driver && !std::strcmp(driver, "x11")) {
        window_info.type = WindowSystemType::X11;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        window_info.render_surface = reinterpret_cast<void*>(SDL_GetNumberProperty(wp, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    } else if (driver && !std::strcmp(driver, "wayland")) {
        window_info.type = WindowSystemType::Wayland;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        UNREACHABLE_MSG("Unsupported SDL video driver {}", driver ? driver : "(none)");
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    width = w;
    height = h;
    LOG_INFO(Frontend, "Window {}x{} on {}", w, h, driver);
    if (on_hdr_display) {
        std::printf("Display: HDR on in Windows\n");
    }
}

WindowSDL::~WindowSDL() {
    SDL_DestroyWindow(window);
}

void WindowSDL::BeginTextInput(const std::string& initial, const std::string& prompt) {
    std::scoped_lock lock{text_mutex};
    text = initial;
    text_prompt = prompt;
    text_state = 0;
    text_requested = true;
}

int WindowSDL::PollTextInput(std::string& out) {
    std::scoped_lock lock{text_mutex};
    out = text;
    return text_state;
}

void WindowSDL::UpdateTextTitle() {
    const std::string title = text_active ? base_title + " \u2014 " + text_prompt + ": " + text + "_  (Enter = OK, Esc = cancel)"
                                          : base_title;
    SDL_SetWindowTitle(window, title.c_str());
    BbOverlay::SetTextEntry(text_active, text_prompt, text);
}

bool WindowSDL::PollEvents() {
    {
        std::scoped_lock lock{text_mutex};
        if (text_requested) { // SDL text input must be toggled from the window thread
            text_requested = false;
            text_active = true;
            SDL_StartTextInput(window);
            UpdateTextTitle();
        }
    }
    if (!text_active) {
        BbOverlay::UpdateTextInput(window);
    }
    // A status the renderer shows while the game cannot draw yet (shader pre-build progress).
    if (std::string status; !text_active && TakeWindowStatus(status)) {
        const std::string title = status.empty() ? base_title : base_title + " — " + status;
        SDL_SetWindowTitle(window, title.c_str());
    }
    // Fullscreen follows the setting (the in-game PC menu changes it while playing). The size
    // change arrives as a window event below and resizes the swapchain as a drag would.
    const bool want_fullscreen = BbSettings::Get().fullscreen.load();
    if (want_fullscreen != fullscreen_applied) {
        fullscreen_applied = want_fullscreen;
        SDL_SetWindowFullscreen(window, want_fullscreen);
    }
    // The mouse camera (src/mouse_camera.c) holds the mouse after a click in the window, while
    // the setting is on and nothing else needs the pointer: focus elsewhere, the overlay or a
    // text entry gives it back (a click takes it again).
    const bool focused = SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS;
    g_window_focused.store(focused, std::memory_order_relaxed);
    {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        g_window_points_w.store(w, std::memory_order_relaxed);
        g_window_points_h.store(h, std::memory_order_relaxed);
    }
    // A menu with the pointer (src/menu_pointer.c) shows the cursor; the camera takes the mouse
    // back without a click when the menu closes, if it held it when the menu opened.
    const bool menu_pointer = g_menu_pointer.load(std::memory_order_relaxed);
    if (menu_pointer && !menu_pointer_seen) {
        menu_pointer_seen = true;
        rearm_after_menu = mouse_captured;
    } else if (!menu_pointer && menu_pointer_seen) {
        menu_pointer_seen = false;
        mouse_armed = mouse_armed || rearm_after_menu;
        rearm_after_menu = false;
    }
    const bool can_capture = BbSettings::Get().mouse_camera.load() && !text_active &&
                             !BbOverlay::ReleasesMouse() && focused && !menu_pointer;
    if (!can_capture && !menu_pointer) {
        mouse_armed = false;
    }
    if ((can_capture && mouse_armed) != mouse_captured) {
        mouse_captured = can_capture && mouse_armed;
        SDL_SetWindowRelativeMouseMode(window, mouse_captured);
        SetMouseCaptured(mouse_captured);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
             (event.gaxis.value > 8000 || event.gaxis.value < -8000))) {
            g_pad_in_use = true;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                   event.type == SDL_EVENT_KEY_DOWN) {
            g_pad_in_use = false;
        }
        if (text_active && (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN)) {
            std::scoped_lock lock{text_mutex};
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                text += event.text.text;
            } else if (event.key.key == SDLK_BACKSPACE && !text.empty()) {
                size_t cut = text.size() - 1; // drop one UTF-8 code point
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
                text.erase(cut);
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) {
                text_state = event.key.key == SDLK_ESCAPE ? 2 : 1;
                text_active = false;
                SDL_StopTextInput(window);
            }
            UpdateTextTitle();
            continue;
        }
        // The controller finishes the text dialog too: Cross (A) accepts, Circle (B) cancels.
        if (text_active && event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
             event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)) {
            std::scoped_lock lock{text_mutex};
            text_state = event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ? 1 : 2;
            text_active = false;
            SDL_StopTextInput(window);
            UpdateTextTitle();
            continue;
        }
        if (BbOverlay::HandleEvent(event)) {
            continue;
        }
        switch (event.type) {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            width = w;
            height = h;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            mouse_armed = can_capture; // a click in a menu is the pointer's, not the camera's
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (mouse_captured) {
                NoteMouseMotion(event.motion.xrel, event.motion.yrel);
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            mouse_armed = false;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            NoteMouseWheel(event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y);
            break;
#ifdef _WIN32
        // HDR switched in Windows (Win+Alt+B) or the window moved to another display.
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
            on_hdr_display = event.window.data1 != 0;
            break;
#endif
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            is_open = false;
            break;
        default:
            break;
        }
    }
    // Hidden while playing on a controller; the mouse camera's relative mode hides it on its own.
    if (const bool show = !g_pad_in_use || !focused || text_active;
        !mouse_captured && SDL_CursorVisible() != show) {
        show ? SDL_ShowCursor() : SDL_HideCursor();
    }
    return is_open;
}

} // namespace Frontend

// Mouse wheel notches since the pad last took one: the pad read makes each a short press of the
// action bound to WheelUp / WheelDown (input_bindings.h).
static std::atomic<int> g_wheel_up{0}, g_wheel_down{0};
void NoteMouseWheel(float y) {
    if (y > 0) g_wheel_up.fetch_add(1, std::memory_order_relaxed);
    if (y < 0) g_wheel_down.fetch_add(1, std::memory_order_relaxed);
}

// One notch a read, each direction: bit 0 up, bit 1 down. A notch is held for one read and
// released the next, so two in a row read as two presses.
extern "C" uint32_t bbgpu_mouse_wheel_take(void) {
    static bool up_held, down_held;
    uint32_t bits = 0;
    if (up_held) up_held = false;
    else if (g_wheel_up.load(std::memory_order_relaxed) > 0) {
        g_wheel_up.fetch_sub(1, std::memory_order_relaxed);
        up_held = true;
        bits |= 1;
    }
    if (down_held) down_held = false;
    else if (g_wheel_down.load(std::memory_order_relaxed) > 0) {
        g_wheel_down.fetch_sub(1, std::memory_order_relaxed);
        down_held = true;
        bits |= 2;
    }
    return bits;
}

// Mouse motion for the mouse camera, in 1/16 counts, taken once a camera update on the game's
// thread. Motion that waited longer than 250 ms (a loading screen, a menu without the camera)
// is dropped rather than turning the camera all at once afterwards.
static std::atomic<long long> g_motion_x{0}, g_motion_y{0};
static std::atomic<unsigned long long> g_motion_taken_ms{0};
static std::atomic<bool> g_mouse_captured{false};
void NoteMouseMotion(float x, float y) {
    g_motion_x.fetch_add(std::llround(x * 16.0f), std::memory_order_relaxed);
    g_motion_y.fetch_add(std::llround(y * 16.0f), std::memory_order_relaxed);
}
void SetMouseCaptured(bool captured) {
    g_mouse_captured.store(captured, std::memory_order_relaxed);
    g_motion_x.store(0, std::memory_order_relaxed);
    g_motion_y.store(0, std::memory_order_relaxed);
}
extern "C" void bbgpu_set_menu_pointer(int active) {
    g_menu_pointer.store(active != 0, std::memory_order_relaxed);
}

// The presented frame's size (vk_presenter.cpp letterboxes it into the window).
static std::atomic<int> g_frame_w{0}, g_frame_h{0};
void BbNotePresentedFrame(int width, int height) {
    g_frame_w.store(width, std::memory_order_relaxed);
    g_frame_h.store(height, std::memory_order_relaxed);
}

extern "C" int bbgpu_mouse_stage_position(float* x, float* y) {
    const float ww = float(g_window_points_w.load(std::memory_order_relaxed));
    const float wh = float(g_window_points_h.load(std::memory_order_relaxed));
    const float fw = float(g_frame_w.load(std::memory_order_relaxed));
    const float fh = float(g_frame_h.load(std::memory_order_relaxed));
    if (!g_window_focused.load(std::memory_order_relaxed) || ww <= 0 || wh <= 0 || fw <= 0 || fh <= 0 ||
        g_mouse_captured.load(std::memory_order_relaxed)) {
        return 0;
    }
    float mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my); // window points, like SDL_GetWindowSize
    return BbStage::Point(mx, my, ww, wh, fw, fh, x, y);
}

extern "C" int bbgpu_mouse_captured(void) {
    return g_mouse_captured.load(std::memory_order_relaxed);
}
extern "C" int bbgpu_mouse_motion_take(float* dx, float* dy) {
    const unsigned long long now = SDL_GetTicks();
    const unsigned long long last = g_motion_taken_ms.exchange(now, std::memory_order_relaxed);
    const long long x = g_motion_x.exchange(0, std::memory_order_relaxed);
    const long long y = g_motion_y.exchange(0, std::memory_order_relaxed);
    const bool stale = now - last > 250;
    *dx = stale ? 0.0f : float(x) / 16.0f;
    *dy = stale ? 0.0f : float(y) / 16.0f;
    return (*dx != 0.0f || *dy != 0.0f) ? 1 : 0;
}

// The window title's status (SetWindowStatus, any thread; empty: the plain title).
static std::mutex g_status_mutex;
static std::string g_status;
static bool g_status_changed = false;
void SetWindowStatus(const std::string& status) {
    std::scoped_lock lock{g_status_mutex};
    if (status != g_status) {
        g_status = status;
        g_status_changed = true;
    }
}
bool TakeWindowStatus(std::string& status) {
    std::scoped_lock lock{g_status_mutex};
    if (!g_status_changed) {
        return false;
    }
    g_status_changed = false;
    status = g_status;
    return true;
}

// The in-game PC menu's Quit Game: the same as closing the window (SDL_PushEvent is safe from
// any thread, here the game's menu thread).
extern "C" void bbgpu_request_quit(void) {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}
