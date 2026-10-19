// SPDX-License-Identifier: GPL-2.0-or-later
// Draws the in-game guide (gpu/shim/bbport_overlay.cpp) offscreen into a BMP, through ImGui's SDL
// software renderer: for working on its look without the game.
//   overlay-preview OUT.bmp [TAB 0-3] [WIDTH HEIGHT] [BACKGROUND.png]
// The guide's own code is compiled here under another namespace; settings come from BB_CONFIG.
#define BB_OVERLAY_PREVIEW 1
#define BbOverlay BbOverlayPreview
#include <string>
#include <vector>
#include "../shim/bbport_overlay.cpp"
#undef BbOverlay

#include "imgui_impl_sdlrenderer3.h"

using namespace BbOverlayPreview;

// What bb-probe provides to the library; the preview never restarts or runs game code.
extern "C" void runtime_restart(void) {}
extern "C" std::uint64_t runtime_disabled_optimizations = 0;
// Game state the map, the atlas and the menu sounds read: nothing is open, nothing is known.
extern "C" {
#include "../../src/runtime_atlas_state.h"
#include "../../src/runtime_map.h"
#include "../../src/runtime_menu_sound.h"
int runtime_atlas_flag(uint32_t) { return 0; }
int runtime_map_location(BbMapLocation*) { return 0; }
uintptr_t runtime_atlas_player_token(void) { return 0; }
int runtime_map_menu_open(void) { return 0; }
int runtime_map_menu_state(BbMapMenu*) { return 0; }
uint32_t runtime_map_confirm_button(void) { return 0; }
void runtime_audio_menu_event(int) {}
void runtime_audio_menu_stats(BbMenuSoundStats*) {}
int runtime_atlas_travel_request(const BbAtlasTravelDestination*) { return 0; }
int runtime_atlas_travel_status(void) { return 0; }
int runtime_atlas_travel_supported(void) { return 0; }
}

// The game frame behind the guide: a PNG of any size (avcodec, as DecodePng without its limit).
static SDL_Texture* LoadBackground(SDL_Renderer* renderer, const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> input((std::istreambuf_iterator<char>(file)), {});
    if (input.empty()) return nullptr;
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_PNG);
    AVCodecContext* context = avcodec_alloc_context3(codec);
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    SDL_Texture* texture = nullptr;
    if (avcodec_open2(context, codec, nullptr) >= 0 && av_new_packet(packet, int(input.size())) >= 0) {
        std::memcpy(packet->data, input.data(), input.size());
        if (avcodec_send_packet(context, packet) >= 0 && avcodec_receive_frame(context, frame) >= 0) {
            const int w = frame->width, h = frame->height;
            std::vector<unsigned char> rgba(std::size_t(w) * h * 4);
            SwsContext* sws = sws_getContext(w, h, AVPixelFormat(frame->format), w, h, AV_PIX_FMT_RGBA,
                                             SWS_BILINEAR, nullptr, nullptr, nullptr);
            std::uint8_t* out[] = {rgba.data()};
            const int stride[] = {w * 4};
            sws_scale(sws, frame->data, frame->linesize, 0, h, out, stride);
            sws_freeContext(sws);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
            SDL_UpdateTexture(texture, nullptr, rgba.data(), w * 4);
        }
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    return texture;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "overlay-preview OUT.bmp [TAB] [WIDTH HEIGHT] [BACKGROUND.png]\n");
        return 2;
    }
    const int tab = argc > 2 ? std::atoi(argv[2]) : 0;
    const int width = argc > 4 ? std::atoi(argv[3]) : 1920, height = argc > 4 ? std::atoi(argv[4]) : 1080;
    BbSettings::Load();
    // The trophy list from the game folder (read only), unlocks from a scratch folder; three earned.
    if (const char* game = std::getenv("BB_GAME_DIR")) {
        const char* user = std::getenv("BB_USER_DIR");
        BbTrophies::Configure(game, user ? user : ".", 1);
        const std::int64_t unlocked[] = {1760000000, 0, 1760100000, 0, 0, 0, 1760200000};
        BbTrophies::LoadUnlocks(unlocked, 7);
    }
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    SDL_Texture* background = argc > 5 ? LoadBackground(renderer, argv[5]) : nullptr;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ConfigureContext();
    // The software renderer draws ImGui's textured thin lines with gaps; plain geometry instead.
    ImGui::GetStyle().AntiAliasedLinesUseTex = false;
    ImGui_ImplSDLRenderer3_Init(renderer);

    menu_open = true;
    active_tab = tab;
    frame_ms_avg = 1000.0f / 60.0f;
    menu_opened_ms = ClockMs() - 2000; // past the slide-in
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    // BB_PREVIEW_KEYS=down,right,a,...: D-pad and face buttons pressed one after another, to
    // check controller navigation (each press is held for a frame, then released).
    std::vector<ImGuiKey> keys;
    if (const char* list = std::getenv("BB_PREVIEW_KEYS")) {
        std::string all(list);
        for (size_t at = 0; at <= all.size();) {
            const size_t end = std::min(all.find(',', at), all.size());
            const std::string k = all.substr(at, end - at);
            at = end + 1;
            if (k == "up") keys.push_back(ImGuiKey_GamepadDpadUp);
            else if (k == "down") keys.push_back(ImGuiKey_GamepadDpadDown);
            else if (k == "left") keys.push_back(ImGuiKey_GamepadDpadLeft);
            else if (k == "right") keys.push_back(ImGuiKey_GamepadDpadRight);
            else if (k == "a") keys.push_back(ImGuiKey_GamepadFaceDown);
        }
    }
    const int frames = 3 + int(keys.size()) * 4;
    for (int frame = 0; frame < frames; ++frame) { // layouts settle over a frame or two
        io.DisplaySize = ImVec2(float(width), float(height));
        io.DeltaTime = 1.0f / 60.0f;
        if (frame >= 2 && (frame - 2) / 4 < int(keys.size()) && (frame - 2) % 4 < 2)
            io.AddKeyEvent(keys[(frame - 2) / 4], (frame - 2) % 4 == 0);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
        DrawWindows(float(height));
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 22, 26, 255);
        SDL_RenderClear(renderer);
        if (background) {
            const float bw = float(background->w), bh = float(background->h);
            const float fit = std::max(width / bw, height / bh);
            const SDL_FRect dst{(width - bw * fit) * 0.5f, (height - bh * fit) * 0.5f, bw * fit, bh * fit};
            SDL_RenderTexture(renderer, background, nullptr, &dst);
        }
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    const bool saved = SDL_SaveBMP(surface, argv[1]);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    std::printf("Overlay preview: %s %s\n", argv[1], saved ? "written" : SDL_GetError());
    return saved ? 0 : 1;
}
