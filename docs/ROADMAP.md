# Roadmap

What this fork is working on, in order. Requirements are kept as they were asked for:
investigate before building, every feature can be switched off, credit the original authors,
and never offer a toggle for something that does not work.

Sources checked for ideas (last on 12 October 2026): shadPS4 (our renderer matches its
`d1cb90db`, 1 October), [Supermedo's pull requests](https://github.com/Supermedo/bloodborne_pc/pulls)
(through #49), [deadinside28's pull requests](https://github.com/deadinside28/bloodborne_pc/pulls)
(through #157), [droogie's bbhost](https://github.com/droogie/bbhost) and
[Mrsuss60's Windows port](https://github.com/Mrsuss60/bloodborne_pc_windows_port).

## Main work, in order

1. **Seamless co-op on our own servers** (the main focus since 14 October 2026). The public
   shadPS4 server (srv.shadps4.net) signs the port out since its 14 October update, whatever
   version the port reports, and catching up with shadPS4 would not change that: a fork's build
   still reports itself as a fork. So playing together goes through servers the players run.
   1.9.1 runs [BBSC](https://github.com/Classmg22/BBSC) 0.0.1 (CLS_AMG; Wozzardman's
   shadnet-p2p) from Settings > Co-op, hosted on one player's PC over Tailscale. Next: a two-PC
   test (joining, deaths, lamps, area changes, a boss, leaving), fix what breaks, then make
   hosting simpler (same-network play without Tailscale, clearer invite and error messages).
   Keep an eye on deadinside28 [#130](https://github.com/deadinside28/bloodborne_pc/pull/130)
   for ideas. Ask the srv.shadps4.net admins to allow `shadPS4 Jgamrot/` for ordinary online.
2. **Boss Rematches** (Bloodborne Enhanced features, independently implemented). A Cleric
   Beast rematch was played through on a copied save (14 October 2026); 1.9.1 extends it to all
   19 bosses with a lamp of their own, as an experimental launcher toggle (off;
   `BB_PC_REMATCHES=1`). Next: play each of the other 18 (the entry, the arena, Insight, the
   reward, death and leaving), then the Broken Lamp, chalice bosses and Distorted Memories.
   Requirements:
   - Rematch bosses already defeated on the current character, without NG+, from their lamps;
     also through the Broken Lamp in the Hunter's Dream (raised garden near the tree, back and
     to the right of the spawn point). Chalice dungeon bosses only from their own lamps.
   - A rematch consumes one Insight when the player has any, and is still allowed with none.
   - Original progression and completed-boss flags are preserved, including after a death or quit.
   - Distorted Memories: Father Gascoigne, Ludwig and Orphan of Kos with both phases at once;
     Gehrman and the Moon Presence together (Broken Lamp only); Mergo's Wet Nurse with its clone
     phase always on.

   [Bloodborne Enhanced](https://www.nexusmods.com/bloodborne/mods/19)'s files may not be
   redistributed or modified, so everything is made from the player's own game files. Notes:
   [ENHANCED_LAMPS_RESEARCH.md](ENHANCED_LAMPS_RESEARCH.md).
3. **Mouse pointer in the game's menus.** Hover a row to move the game's cursor there, click
   to confirm, right-click to go back, wheel to scroll; only while a menu is open, out of the
   mouse camera's way, with its own off switch. After bbhost's `menu_pointer.cpp`, whose list
   hooks the PC menu already uses. Start with System and the PC sections, then the other menus.
4. **Shader pre-build.** In the 1.8 preview with a catalog up to the Blood-starved Beast.
   Next: extend the catalog through the rest of the capture playthrough (the Old Hunters and
   chalice dungeons too), make NVIDIA-recorded fragment shaders match on AMD and Intel (the
   clip-distance emulation input), and the two compute shaders whose thread-ID addressing the
   records do not carry.
5. **Frame generation and AMD Anti-Lag:** FSR 3.1 frame generation on any GPU, DLSS frame
   generation on RTX 40 and later, from AwesomeObserver via Supermedo
   [#48](https://github.com/Supermedo/bloodborne_pc/pull/48) and
   [#49](https://github.com/Supermedo/bloodborne_pc/pull/49). They build on the bbport 0.4 merge
   (#37), which this fork does not take, so they are ported, not merged.
6. **Catch up with shadPS4**, after seamless co-op (it does not bring back
   the public server) and only after the shader capture is finished. Port, in order, the
   renderer and shader commits since `d1cb90db`: shader correctness (`945dbc3c`, `c4952908`,
   `1c2b8f97`), texture layout (`7e778987`, `e7929178`, `c679ce60`, `69b6041d`, `14ada48a`,
   `995e37e4`, and small-mip detiling, shadPS4 #5196 / Supermedo
   [#42](https://github.com/Supermedo/bloodborne_pc/pull/42)), draw edge cases (`643c7997`,
   `7721a1ef`, `00276961`, `c5dd8b8b`, `3a9f5726`, `8bafaa5a`) and speed-ups (`8e23388a`,
   `99e33b7e`, `145932bf`, `dade3afd`, `0f6d1b87`). Where a commit changes the shader records
   (`.meta` v8, `.key` v5), keep them compatible or convert the capture's catalog. Then merge
   background shader compiling (`feature/async-pipelines`, `BB_ASYNC_PIPELINES=0` off) after
   measuring new areas. Test in the Research Hall and shadowed areas.

## Small fixes, alongside the main work

From Supermedo's own 1.7 commits and a few of deadinside28's (checked 12 October 2026; most of
the other upstream commits are pull requests we already took or chose to skip). None touches
the shader recompiler or the pipeline cache format unless noted.

- **Physics step of the FPS presets** (Supermedo `be1cde3`): since 1.6.10 (`fd35aa8`) we write
  the presets' physics step to its real address, but with the patch file's values, which
  Supermedo says are wrong: 60 FPS++ writes 1/15 (should be 1/30, the game's own), 90 FPS++
  1/30 (1/90), Uncap FPS++ 1/240 (1/120). Test: ragdolls, cloth and falling at a 60 FPS preset.
- Crash fixes that apply cleanly: `188c1f6` + `f33e761` (a broken pipeline-cache entry stops
  every start with code 23, mainly AMD; touches the pipeline cache, so after the shader work),
  `3a7cde8` (texture cache crash in FindDepthTarget when it grows), `acdfdf0` (another route to
  the crash at 0x20000, besides PR #5), `136a72d` (no hashing of every texture upload, after
  PR #36).
- Need porting: `479f2f5` (code 23 during area loads; builds on PR #29, which we took
  differently), `d9e505b` + `9b0d326` (1.09 patches only on the real 1.09 code; other builds
  labelled 1.09 crash after start), `1dd8471` (an unsupported CPU named in a message, not a
  crash), `a3baca3` (an eboot with the 60 FPS patch baked in crashes the gestures menu),
  `d646b1d` (frame limiter, after `fix/frame-cap-28`), `331f6ae` (closing the launcher could cut
  the game's log output; check ours).
- Later: `e750af8` / `38e7588`, the game on Ivy Bridge and older CPUs (PR #10, about 2,300
  lines; few players need it).
- Swapchain recreated through `oldSwapchain` (optional;
  [deadinside28 #93](https://github.com/deadinside28/bloodborne_pc/pull/93)).
- Dark spots on Intel CPUs (Old Hunters areas): shadPS4 `235dcabf` (RCPPS / RSQRTPS answer
  slightly differently on Intel). It changes the game's CPU code, not shaders.
- The overlay menu kept within the screen ([Supermedo #46](https://github.com/Supermedo/bloodborne_pc/pull/46)).

## In 1.7, to confirm in play

Released as 1.7 (merged into `windows`; each also on its branch):

- The redesigned launcher and in-game guide (`feature/ui-redesign`): controller movement on the
  guide's rail and pages, L3 + R3 to close, the mouse pointer.
- Keyboard prompts (PCBButtons by JinkiJinn): choosing Keyboard in the launcher and in the
  guide. Since 1.9.8 neither prompt mod ships; the launcher adds the player's own Nexus download.
- `feature/menu-pointer`: the mouse in the game's menus (after droogie's bbhost).
- `fix/safe-saves-43`, `fix/indirect-guard-41`, `fix/xbox-touchpad-39`, `fix/retail-serials-47`,
  `perf/skip-empty-draws-34`, `fix/dlss-device-fallback`, `fix/pad-calibration-71`,
  `fix/vram-pressure`, `fix/keep-run-logs`.
- From the 1.6.13 preview: frame cap per frame (sprinting at a 120 cap), sprint speed above
  90-120 FPS, the map-load crash on an unbound descriptor table, mods from Armoury Crate (ROG
  Ally), the quiet update check on direct launches, graphics presets, ultrawide outputs.
- Older: the 1.6.12 preview fixes (character creation preview, area loads, video memory over a
  long session), Rebirth at the Altar of Despair, two invaders, keyboard prompt text when keys
  are rebound away from the PCBButtons layout.

Held back for 1.8, after the shader capture: `feature/shader-prebuild`, `feature/shadps4-catchup`,
`feature/async-pipelines` and `fix/snormnz-format` (shader recompiler or pipeline cache), plus
`feature/boss-rematches` and `feature/enhanced-lamps-foundation` (still in progress).

## Parked

Wanted at some point, not scheduled: mouse settings rows in the game's menus (extra rows crash
the menu movie), a Key Bindings screen, Switch-style prompts (Supermedo #25), FOV, more bbhost
image settings (AO strength, shadow distance, fog, bloom, saturation, vignette), achievement
icons in the game's menus, a debug menu without extra files, Brazilian Portuguese and Circle to
confirm (Supermedo #20, #17), a performance overlay and idle cursor hiding, data-field mods and
gameplay plugins, the mirror's character preview.

## Not planned

- **deadinside28's GPU memory model (0.5)** and the Windows sync built on it (deadinside28
  #138): Linux-first, about 7,000 lines of core GPU memory code on 0.3's model, which this
  Windows line never took. Revisit if AMD players report the bus-bound slowdowns it fixes.
- **bbport 0.3/0.4 merges** (Supermedo #37, #38): this fork has its own texture collector,
  controls, mouse camera and English menu. Only DS4-style touchpad reporting might be worth
  checking on its own.
- Supermedo #45 (the same as #26, included), #40 and #30 (we build with clang 23 already), #44
  (Quit Game is in the title menu), #35 (the same as #4, included), #27 and #13 (English menu
  included). deadinside28 #127 (our achievements cover it), #8 (FSR 4.1.1 FP8 on RX 9000, niche),
  #123 (only with the 0.5 memory model), translations of its launcher.
- shadPS4's kernel, network, file system and frontend changes; bbhost's libc and shader
  translator: they belong to their own architectures.
