# Model 1 Recomps

<p align="center"><b>SEGA's classic arcade games on your computer, with sharper graphics,
widescreen support, and smooth motion at up to 120 FPS.</b></p>

This project forks Mr. Finster's superb [Virtua Fighter recomp](https://github.com/mr-finster/vf1-recomp), refactoring it to support the rest of Sega's Model 1 arcade hardware games library as native desktop runtimes. The Model 1 was used by
multiple games; this repository currently supports Virtua Racing.

<p align="center">
  <a href="https://github.com/svera/model1-recomps/releases"><img alt="Latest release" src="https://img.shields.io/github/v/release/svera/model1-recomps?include_prereleases&amp;label=release"></a>
  <a href="LICENSE"><img alt="Licence: MIT" src="https://img.shields.io/badge/licence-MIT-blue"></a>
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Steam%20Deck%20%2F%20Linux-informational">
</p>
<p align="center"><img src="assets/games/vr/icon/icon.png" alt="Virtua Racing Recomp" width="160"></p>

<h2 align="center">Virtua Racing Recomp</h2>

<p align="center">
  <img src="assets/games/vr/screenshots/bay-bridge.png" alt="Racing on Bay Bridge in widescreen" width="900">
</p>

> [!IMPORTANT]
> You need your own **`vr.zip`** and **`model1io.zip`** ROM archives. Neither is included.
> Both are needed for steering and pedals to work. This is an unofficial fan project,
> not affiliated with or supported by SEGA.

<p align="center">
  <a href="https://github.com/svera/model1-recomp/releases">Download</a> ·
  <a href="#getting-started">Getting started</a> ·
  <a href="#controls">Controls</a> ·
  <a href="https://github.com/svera/model1-recomp/issues">Report a bug</a>
</p>

### Features

- All three courses, four camera views, and the original arcade racing.
- Sharper graphics, widescreen and ultrawide views without stretching the HUD.
- Original arcade motion or smoother 60 / 120 FPS.
- Full-detail models by default, with an optional LOD toggle for simpler distant models.
- Keyboard and gamepad support, with customizable controls.
- Saved settings and high scores.
- Car color, language, difficulty, and course settings.

**Single-player only.** Split screen, linked cabinets, and online multiplayer are not supported.

### Getting started

1. [Download](https://github.com/svera/model1-recomp/releases) the package for your system and extract it.
2. Keep **`vr.zip`** and **`model1io.zip`** together in the same folder. Leave them zipped.
3. Start the game and select **`vr.zip`** when asked. It remembers your files for next time.
4. Press **1** on the keyboard or **Start** on your gamepad to play.

**macOS:** open **Virtua Racing Recomp.app**. If macOS blocks it, allow it through
**System Settings > Privacy & Security**.

**Windows:** run the included executable. Windows may show a SmartScreen warning for preview builds.

**Steam Deck / Linux:** put both ROM archives in the package's **roms** folder and run
**VirtuaRacing.sh** instead of using a file picker. On Steam Deck, add the launcher as a
Non-Steam Game and set **Start In** to the extracted **VirtuaRacing** folder.

### Controls

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Steer | Left / Right | Left stick |
| Accelerate | Up | Right trigger |
| Brake | Down | Left trigger |
| Shift up / down | X / Z | A / B (Cross / Circle) |
| Change camera | C | Y / Triangle |
| Start | 1 | Start |
| Insert coin (Arcade mode) | 5 | D-pad Up |
| Open menu | Esc | View / Select |

To see or change your actual controller bindings, open **Esc > Controls > Player 1 >
Pad buttons**. Coins are only needed in Arcade play style.

### Useful settings

Open the menu with **Esc** or **View / Select**:

- **Display:** change sharpness, screen shape, fullscreen, and frame rate.
- **Game > Play style:** PC mode starts without coins; Arcade mode uses the cabinet's coin settings.
- **Game:** choose your car color, language, course, and difficulty.
- **Sound:** adjust the audio levels.

Higher graphics settings need a faster computer. If play feels slow, lower the render scale
or frame rate.

### Screenshots

| Big Forest · Beginner | Acropolis · Expert |
| --- | --- |
| ![Big Forest gameplay](assets/games/vr/screenshots/big-forest.png) | ![Acropolis gameplay](assets/games/vr/screenshots/acropolis.png) |

## Shared Model 1 implementation

The shared runtime models the Model 1 board around its main NEC V60 CPU: the Fujitsu MB86233 geometry
coprocessor (TGP), polygon rasterizer, System 24 tile layers, sound board, and I/O board. The V60 interpreter,
TGP core, and much of the machine implementation are based on MAME. The SDL 3 host supplies the window, input,
audio, saves, and menus.

Menu > **Display > Frame rate** selects **30**, **60**, or **120 FPS**. The 30 FPS arcade option
disables interpolation; 60 and 120 use interpolated pictures. The choice applies immediately and
is saved as `fps` in each game's `settings.ini` (default: 120). Game logic and audio retain their
original timing. The screen's refresh rate and available performance limit the visible frame rate.

The CPU renderer reproduces the arcade's 1x picture. Optional higher render scales, antialiasing, and GPU rendering
are host-side features. On supported systems, the GPU renderer is checked against the CPU renderer and falls back
to the CPU if it detects a difference or error.

The recompiler translates the V60 program at startup using an address-only function map and SLJIT. It reads the
game program from the user's ROM set; ROM bytes are not shipped. The developer-only ahead-of-time path emits C++
from analyzed V60 functions and keeps ROM-derived output under `build/`.

## Build and test

Development requires a C++20 compiler, zlib, SDL 3, and FreeType. On macOS, install the latter dependencies with
`brew install sdl3 freetype`. Games have separate build instructions in their guides.
Every game has the same make targets, made from its `src/games/<game>/game.mk`: `<game>`, `<game>-native`,
`<game>-regions`, `<game>-coverage`, `<game>-static`, `<game>-lockstep`, and `<game>-native-lockstep`.

### Star Wars Arcade (experimental)

Star Wars Arcade can be played from start to finish, but it is not released as a download
yet: build it from source (see above), then start it and select your own US **`swa.zip`**
when asked. Keep **`model1io.zip`** in the same folder.

```sh
make swa
build/bin/swa
```

Two players share one cabinet: the **Pilot** flies and fires, the **Gunner** aims a second
laser.

Hold **Tab** to fast-forward, including during attract mode; release it to return to normal
speed. This is useful for quickly reaching scenes to debug. **P** pauses and resumes.

| Action | Pilot keys | Gunner keys | Gamepad |
| --- | --- | --- | --- |
| Fly | Arrows | I / J / K / L | Left stick, d-pad |
| Throttle | W faster, S slower | - | Right stick up / down |
| Laser | Z | U | Right trigger |
| Torpedo | X | O | Left trigger |
| Change view | C | - | Y / Triangle |
| Start | 1 | 2 | Start |
| Insert coin | 5 | 6 | Left bumper |

Useful settings (**Esc**):

- **Cheats > Gunner assist:** one player can fly while the Gunner's laser aims at the nearest
  enemy by itself, in PILOT & GUNNER mode.
- **Game > Invert flight Y:** push forward to climb instead of dive.
- **Display > Frame rate:** 30 shows the game as the arcade drew it; 60 and 120 smooth the
  motion between pictures, though some of the game is not smoothed yet.
- **Display > LOD:** Off (default) keeps the most detailed models at every distance; On uses
  the arcade's simpler distant models. Off also retains the complete Death Star globe through
  the attract-mode approach instead of switching to smaller surface patches.
- **Display > Draw distance:** Full (default) shows the Death Star's surface further out and
  pre-draws asteroid clusters and Star Destroyers at the asteroid-field attract scene's opening
  to avoid their pop-in, without adding them to the Death Star demo. Arcade disables these
  additions and keeps the cabinet's original pop-in.
- **Display > Widescreen cockpit:** On (default) continues the X-wing cockpit's canopy struts
  and side consoles out to the screen edges in 16:9 and wider, following their lines instead of
  stretching the panel. Off leaves the sides of the cockpit open. No other 2D art is stretched:
  attract-mode screens such as the notices and the Rankings stay at their 4:3 proportions. The
  opening starfield carries on across the full width from its own scrolling map.

In 16:9 and wider, the game's own view test is widened to the whole picture, so ships and
scenery no longer pop in at the old 4:3 edges. Enemies in the extra width therefore act as if on
screen (turrets and fighters can fire from there), so play differs slightly from 4:3, and an
input recording should be replayed at the aspect ratio it was recorded in.

Developers can also record and replay play sessions; see
[how SWA is built, run and tested](docs/HOW-IT-WAS-MADE.md#building-running-and-testing-swa).

Release packaging is enabled only for Virtua Racing on macOS, Windows, and Linux. Each platform's
`package.sh` builds VR by default.

Useful shared checks:

```sh
make test-model1io-adc
make test-host-gamepads
make test-vr-profile test-vr-gameplay test-vr-tgp-track
```

## Repository layout

| Path | Contents |
| --- | --- |
| `src/core` | NEC V60 interpreter and Fujitsu MB86233 (TGP) core |
| `src/machine` | Shared Model 1 memory map, interrupts, TGP, rasterizer, tile chip, sound and I/O boards |
| `src/rt` | Recompiler runtime and startup native recompilation |
| `src/host` | SDL 3 host, input, pacing, and RmlUi menu |
| `src/games/vr` | VR profiles and game-specific behavior |
| `src/games/swa` | Experimental Star Wars Arcade profile, ROM tables, scripted play and host |
| `assets/games/vr` | VR address map, icon and other artwork |
| `src/gpu` | Metal and SDL_GPU renderers and shaders |
| `tools/recomp` | Ahead-of-time V60-to-C++ recompiler |
| `tools/mac`, `tools/win`, `tools/linux` | Each platform's `package.sh [game ...]`: the downloads, the same for every game |
| `tools/release` | What the platforms share: game settings (`games.sh`), third-party sources (`deps.sh`), `LICENSES.txt` |
| `tools/games/<game>/release` | A game's download settings (`game.sh`), READMEs and launcher |

## Project notes

- [How it was made and validated](docs/HOW-IT-WAS-MADE.md)

## Credits and licence

The project incorporates work from MAME, Musashi, ymfm, SLJIT, the Z80 core, RmlUi, GLM, stb_image, SDL 3, and
FreeType. `tools/release/licenses.sh` gathers the third-party notices for distribution packages.

The project's own code is MIT-licensed: see [LICENSE](LICENSE). Third-party components retain their respective
licences. This project is not affiliated with or endorsed by Sega.
