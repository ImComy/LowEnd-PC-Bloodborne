// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: trophy unlock notifications (the toast drawn over the game), their themes, timeline,
// sounds and the player's own pictures and animations. Real unlocks and the guide's previews go
// through the same queue and renderer. Everything the player can change is in
// <user>/custom_trophy (README.md, Custom trophy notifications); without any file the port's
// original banner shows and no sound plays.

#pragma once

#include <string>
#include "imgui.h"
#include "bbport_trophy_toast_model.h"

namespace BbToast {

Options GetOptions();
/// Applies at once (volume) or from the next toast; saved on the worker thread.
void SetOptions(const Options& options);

/// The trophy's artwork as an ImGui texture (the guide's cache); empty when there is none.
using IconSource = ImTextureRef (*)(int id);
/// Decodes the artwork ahead of the toast (worker thread), so IconSource only uploads it.
using IconPrefetch = void (*)(int id);
/// Theme Original: the guide draws its banner. alpha: opacity; appear: 0 -> 1 as it comes in.
using BannerDraw = void (*)(int id, const char* first_line, float alpha, float appear);

/// Once, with the ImGui context: reads the options, loads the fonts, pictures and animations,
/// then decodes the sounds and opens the audio stream on the worker thread (never during a toast).
void Start(ImFontAtlas* atlas, IconSource icons, IconPrefetch prefetch, BannerDraw banner);
/// Decodes the sound files again (after the player replaced them). Pictures and animations are
/// read when the game starts.
void ReloadSounds();
/// The files in use, for the guide: "Bronze: bronze.mp3  ·  Silver: none ..." and the pictures.
std::string SoundSummary();
std::string PictureSummary();

/// Any thread. preview: shown and heard like an unlock, nothing is saved. grade: 1-4 shows the
/// toast as that grade (bronze..platinum) whatever the trophy's own (unknown without the key).
void Enqueue(int id, bool preview, int grade = 0);
void Clear();
bool Busy();
/// Present thread, inside the ImGui frame. scale: display height / 1080.
void Draw(float scale);

/// Tests and the offscreen preview: a fixed clock in ms (< 0: steady_clock again).
void SetClockOverride(double ms);
/// Theme forced regardless of the options (offscreen preview), or Count for none.
void SetThemeOverride(Theme theme);
struct DrawStats {
    double last_us, avg_us, max_us;
    unsigned frames;
    unsigned vertices; ///< added to the draw list by the last frame
};
DrawStats Stats();
void ResetStats();

} // namespace BbToast
