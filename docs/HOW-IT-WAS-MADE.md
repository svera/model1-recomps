# How Virtua Fighter Recomp was made

This is a write-up of how the port was built and checked, for anyone who wants to know what "recomp" means here, how
much of it comes from MAME, and how AI coding tools were used. The commit history has the detail; this is the overview.

## Pull request checks

The **Tests** GitHub Actions workflow runs all nine ROM-free test targets on macOS when a pull request
is opened, updated with new commits, or reopened. It can also be run manually from the Actions tab.
New updates cancel an older run for the same pull request. ROM-backed checks remain local: CI does
not download game ROMs or require secrets.

## The approach

Virtua Fighter (1993) runs on Sega's Model 1 board. Its game logic is a program for the main CPU, an NEC V60. Around
that CPU the board has a Fujitsu MB86233 geometry DSP (the TGP), a polygon rasterizer, the System 24 tile chip for the
2D layers, an I/O board, and a sound board (a 68000, two MultiPCMs and a YM3438).

**The V60 program is statically recompiled.** The port never ships any of the game's code. It ships an address map,
`assets/games/vf/vf_map.txt`: where the program's functions start and where its indirect jumps and calls lead, addresses only.
When the game starts, it reads the V60 program from the player's own `vf.zip`, walks the code from those addresses and
turns it into ARM64 or x86-64 code with SLJIT (`src/rt/analyse.cpp`, `src/rt/native.cpp`). That takes about 30 ms.
The map was made from coverage runs (scripted fights, attract mode, the ending), and if anything goes wrong the game
falls back to the interpreter.

There are two forms of the recompiled code:

- **At start-up (the release build, `make native`).** Control flow is native code. The instructions the game spends
  most of its time on (byte moves, bit tests, branches, and the run loop's cycle and interrupt check, together about
  80% of what runs) are emitted as machine instructions. Every other instruction calls MAME's handler for it.
- **Ahead of time, to C++ (`make rec` or `make <game>-lockstep`, for development).** `tools/recomp/v60recomp` and `emit.py` write one C++
  function per V60 function. Each instruction's operands are decoded at build time and MAME's handler body is pasted in
  with them, so about 99.5% of the instructions (25,823 of about 25,960) become straight-line C++; the rest (bit-string
  instructions and a few addressing modes) still go through MAME's handler. This build needs the ROM, so it stays on the
  developer's machine.

Both keep the interpreter's exact cycle counting and interrupt timing, so the rest of the board sees the same CPU.

**Virtua Racing's static-recompile pipeline is experimental.** It is the same pipeline as VF's (every game's
targets come from one Makefile template; its coverage runs are listed in `src/games/vr/game.mk`). `make vr-static` first runs a scripted VR
coverage session against the interpreter, analyzing attract mode and a driving sequence with wheel sweeps,
acceleration, braking, and gear changes. It builds `vrrun-static` (`model1run` with the generated C++ under `build/`
linked in); run it with `M1_REC=1` to use the generated table. `make vr-lockstep` compares the interpreter and generated-code runner and
fails on the first interpreter fallback, reporting its address and frame. Missing entries are written to
`build/cov/vr/runtime_entries.txt` and included in the next generated map. Lockstep also compares CPU registers,
battery-backed and work RAM, and rendered frames. `vr-lockstep` runs both the default Beginner script and an
alternate Expert-course script, checking the course label as well as the track-select, starting-grid, active-race,
and course-results transitions. Coverage also includes a Medium-course script, all four view buttons, test-menu
entry/exit, and the Beginner second-credit race followed by attract rankings. The checkpoints use the state
byte at `0x501138`, dispatched by the ROM at `FE1C7D`: results/game over is state `0x1c`, not attract rankings
(`0x03`). Neither course-dependent cameras, the unrelated changing mode byte, nor a render-dependent
polygon-count range identifies results reliably.
`assets/games/vr/vr_map.txt` contains the address-only map used by the desktop host. Both `make vr` and
`make vr-native` now build startup-native execution, and release packages use `vr-native`. `--interp` retains
the interpreter option. `make vr-native-lockstep` verifies native execution against the interpreter on the
scripted scenarios (with `model1lock --native`, which has no generated C++ in it). This is still scripted
coverage, not broad release validation. The `qualifying` scenario covers an EASY Beginner five-lap finish
and ranking-name entry: it selects the difficulty through the test menu, drives with wheel/pedal/shift
inputs, confirms `SDK`, and verifies the first-place ranking's name and finishing time in battery RAM
before returning to attract rankings. It never patches the ROM or injects a finishing race state.
The shared script API lets this driver read the reference machine; lockstep applies the resulting controls
identically to the interpreter and native machine. A focused ROM-backed test also checks the saved entry
after reset and reboot. Medium/Expert qualifying finishes and default-difficulty finishes are not covered.

**The rest of the board is MAME's code.** The TGP runs its own program on MAME's MB86233 core. The rasterizer, tile
chip, sound chips (MultiPCM, the 68000 via Musashi, the YM3438 via ymfm) and the I/O board's Z80 and EEPROM follow
MAME. They were brought into a standalone runtime (`src/machine`) without the rest of MAME: one machine, no device
framework, no drivers for other games. At 1x and 4:3 the picture is MAME's, pixel for pixel.

**The host is new.** `src/host` is an SDL 3 program: the window, keyboard and pad controls for both players, an in-game
menu (RmlUi), saves, the first-launch file picker, and the parts that go beyond the arcade:

- **Render scale.** The 3D is drawn at 1x to 6x the arcade's resolution. Above 1x the polygon fill is MAME's filler
  rewritten to sample at pixel centres, so neighbouring polygons share their edges without cracks.
- **Smooth motion.** VF's logic runs at 30 Hz. Above 1x, each picture is drawn for a moment between the last two game
  frames: every object's matrix is blended with its match in the frame before, paired like a diff of the two display
  lists. Camera cuts and objects that can't be paired aren't blended. The game itself runs untouched; the picture is a
  game frame behind. VR draws at 30 Hz too, but its wheels change model as they turn and its trackside reuses
  models along the track, so guessing pairs by model and place put tyres between cars. Instead the game's own memory
  says what each part is (`draw_records`): as the CPU writes a matrix into the display list, the register holding the
  record of the thing drawn (a car) and the camera in RAM are read. So a car's parts pair with its own parts, the
  track is where the older camera saw it, and a wheel that changed model moves with its car. The SEGA logo is drawn
  the way the track is but without the camera, so a "world" part that stays in its place on screen while the camera
  moves is blended on screen instead. Each car part then follows a
  curve that keeps its speed from one game frame to the next, since the game's own steps are uneven. The camera turns
  unevenly too: in a bend about 0.5 degrees a game frame, then 0.06, so the road's lines jerked sideways. Its turn is
  smoothed with an alpha-beta filter, held within a degree of the game's, and the whole 3D picture is turned about the
  eye to match, so cars and track stay together. A camera turn over 25 degrees or a jump over 8 units is a cut.
  Some views sway the camera about 3 degrees back and forth each game frame while keeping the car still on screen
  (the attract drive past the bridge, the in-car view); smoothing that sway moved the car instead. When the camera
  swings back by more than a degree, it follows the game's own turn for 8 game frames, then the filter goes on from
  the camera's speed at that point rather than the swing's (from the swing's, as the camera slowed the smoothed turn
  ran up to a degree ahead and every car slid across the screen). Within a game frame the picture turns evenly from
  one smoothed turn to the next: a curve through them at the filter's turn speeds sped up and slowed, now ahead, now
  back, 15 times a second. A car the camera follows moves as unevenly in the world as the camera turns (after the
  bridge every car does), so with the turn smoothed it went now ahead, now back on screen; each car whose direction
  from the eye goes at least as evenly as the game drew it as turned with the smoothed camera is left as drawn (eased
  in and out over a few game frames) while the rest of the picture is turned.
  VR's sky scroll alternates between the current position and the average of two game frames.
  The port keeps the half-pixels that average drops and calculates the next horizontal scroll from the game's RAM,
  so the sky moves between those positions at render resolution instead of extrapolating whole-pixel steps.
  At 1x and 4:3 the tile layer is unchanged. Running VR's logic at 60 Hz instead isn't possible: it waits for two
  vblanks a frame by design, and its timing (the attract mode, the race clock) counts game frames.
- **Widescreen.** In fights the 3D view opens out to the sides and the stage sky is drawn from its whole panorama. The
  title and player select get painted side columns, drawn for this port.
- **Antialiasing** (an edge filter, or 2x/3x supersampling), scanline and CRT filters, and hi-res stage skies built
  from the game's own panoramas.
- **Pacing and a load governor.** The game runs on its own clock, trimmed to keep the sound queued. If a machine can't
  keep up, the governor turns off antialiasing and then lowers the render scale.
  Returning to the foreground or a wall-clock gap over half a second resets the game clock, pacing averages,
  and queued audio, so waking from sleep does not fast-forward through missed time.
  `M1_HIDDEN=1 M1_QUIT_AFTER=240 M1_EVENTS_AT=80:sleep,160:sleep M1_TRACE=/tmp/resume.log build/bin/vr`
  exercises two simulated one-minute gaps without displaying a window.
  A real Mac sleep barely shows up in that clock: a SWA trace saw a 30-second sleep as a 2-second gap. After
  waking, the sound device stalled for about 7 seconds, and the game waited for it. Then, for about 2 seconds,
  it took sound faster than it played it. Each frame run to keep it fed also moved the game clock on, so the
  game ran at three times its speed. Such catch-up frames are now limited to the sound's slack (3 frames)
  ahead of the clock. That allowance recovers at the 1% trim's rate, so the device gets a moment of silence
  instead. `M1_EVENTS_AT=400:gulp` simulates that device for 2 seconds; the trace's last column, `queued_ms`,
  shows the sound queue.
- **Scene sync.** VF builds a new scene over several frames. The host holds the last complete picture until the new
  3D has settled, so scene changes don't show half-built frames.

**Title-specific code is separated from the shared board.** `src/games/game.h` defines what a game gives the shared
code (its profile, I/O profile, video traits, TGP HLE, gameplay hooks and scripted play), and each game's folder
registers its own at start-up (`src/games/<name>/game.cpp`); the shared code looks a game up by its short name and
names none, so a new game is a new folder. The build works the same way: each game's `src/games/<name>/game.mk`
gives its name and settings (whether its desktop build links the generated C++, its coverage and lockstep scenarios),
and one Makefile template makes the same targets for every game: `<name>` and `<name>-native` (the desktop and
release builds), `<name>-regions`, `<name>-coverage` (the recompile's hints in `build/cov/<name>/`), `<name>-static`,
`<name>-lockstep` and `<name>-native-lockstep`. VF's ROM-set loader, TGP HLE driver, and recompiler map live under `src/games/vf/` and `assets/games/vf/`.
The TGP command table, VF collision bounds, and list-handling state are owned by that driver; common TGP operations
remain in the shared machine. VF's gameplay patches, character/random-fighter state, secret-icon rendering, and
per-frame hooks now live in `src/games/vf/gameplay.cpp`; the shared frame loop and backdrop hashing use generic
callbacks. Host and tool code now reaches VF-only operations through the `game::vf::gameplay` interface rather than
through the shared machine API. That module is no longer part of the machine class (no friends, no VF members): it
works through the machine's public interface for games (the program ROM it patches, RAM, tiles, characters, colours
and its own `game_state()`), as does VF's TGP HLE table through the public TGP operations, FIFOs and matrix. The shared machine retains the real/stand-in I/O board, raw input registers and EEPROM
storage; the VF-specific EEPROM layout, coin settings and GAME ASSIGNMENTS schema live in the VF gameplay module.
The fight-camera test and title/select backdrop fingerprints for widescreen handling are also owned by the VF module;
the shared renderer consumes its screen-name and scene-width callbacks while retaining the generic drawing and scaling.
What the shared video code needs to know about a game's own data comes from `game::video_traits`
(`src/games/*/video.cpp`): whether to blend the 3D between game frames, which polygons are scenery for the shadow fix,
the stage skies' ROM records and the camera RAM their smooth scroll reads, and texture blocks the game leaves unset.
VF fills them in; VR leaves them empty, so the machine holds no game's addresses.
Virtua Racing has its own I/O profile: digital labels and bit positions, analog channel names, neutral values, and
default host-axis mappings. The shared I/O board models the ADC channels and secondary analog bank; analog values can
be supplied to the headless VR runner with `VR_WHEEL`, `VR_ACCEL`, and `VR_BRAKE` when the optional Model 1 I/O-board
ROM is available. The SDL host now builds as separate `vf` and `vr` executables; the VR binary loads the VR region
directory, has its own settings/save directory, and exposes VR-profiled shift, steering, and pedal controls without
showing VF's arcade settings or cheats. VR's TGP track, collision, and vehicle-operation handlers are in
`src/games/vr/tgp.cpp` (`game::vr::tgp_track_hle`, which owns their state), while the shared machine keeps common TGP
operations and dispatch. VR runs its real TGP program, so they wait for a VR HLE table; VF's table, which never sends
them, leaves them out. Both hosts read the player's
MAME set at launch through one Model 1 loader (`src/machine/model1_romset.cpp`: the zip reader, the CRC checks, the
CPU and I/O boards' ROMs), each game giving only its own ROM table (`src/games/*/romset.cpp`). The development
regions are built the same way: `tools/romset.py` has the board's ROMs and each game's table is
`tools/games/*/romset.py` (picked by `--game` or the zip's name), and `make test-romset` checks the two loaders agree.
The desktop host (`src/host/main.cpp`) is shared by both games and has no game checks: each executable links one
game's `game_host` (`src/host/game_host.h`, implemented in `src/games/*/host.cpp`), which gives the ROM set, the
control profile (`src/games/*/controls.h`: action labels, key and pad defaults, start bits, analog keys; the shared
`src/host/controls.h` has only the directions, Start, Coin, Menu and nine button slots, which each game names, VF's
Guard to All three and VR's pedals, with their own `settings.ini` names), the game's
own settings, its menu pages, and boot, menu-close and per-frame hooks. VF's arcade settings, cheats, title/select
screens (`src/games/vf/art.h`) and secret icon live in VF's host; VR's host gives only its ROM set and controls.
The few work RAM addresses the shared code reads (the camera pointer that marks the end of the boot, and the game's
mode byte and its test-menu value) are in each game's `game::profile`, and the host's trace asks the game for its own
columns. The headless tools (`model1run`, `model1cov`, `model1lock`, `model1script`) are one binary each for every
game: `--game vf|vr` picks it (or the regions folder's name, `build/vr`; with neither, they ask), the game's scripted play, lockstep checks
and script bots (`mash`, `brawl`, `fight`) come from `src/games/*/script.cpp` (`src/games/script.h`), and the tools'
own extras for a game (VF's widescreen pictures, cheats, panoramas, select icon) from `tools/games/*/tool.cpp`
(`tools/tool_game.h`, registered the same way). The test and tuning variables are `M1_*` for every game. Each desktop executable still links one
game's host and, for a static build, one game's recompiled table; the generic tools link none, and each game has
`<name>run-static`, `<name>lock-static` and `<name>script-static` with its own. The entries the recompiled code falls
back to the interpreter for are noted in `build/cov/<name>/runtime_entries.txt` and go into that game's next recompile.
The downloads are made the same way for every game and every platform too: `tools/mac/package.sh`,
`tools/win/package.sh` and `tools/linux/package.sh` (which runs `tools/linux/container.sh` in Valve's Steam Runtime)
take the games to package (all by default), build SDL3 and FreeType from the checked sources `tools/release/deps.sh`
fetches, and write `build/<os>/<game>/<File>Recomp-<os>`; each reads a game's settings from
`tools/games/<name>/release/game.sh` (its title, file names, which build ships, icons, READMEs, and the Linux
launcher kept next to it); each package gets the shared `assets/ui` and `assets/fonts` and only that game's
`assets/games/<name>`. The Linux package (`<File>Recomp-linux.tar.gz`, `-deck` until v1.3.2) is one x86-64 build
made in Valve's Steam Runtime, so it runs on SteamOS on the Deck as well as on other current distributions. Whatever is only one game's sits under its own folders, even outside the program: VF's
select-icon renders (`tools/games/vf/select_icons`, into `assets/games/vf/select_icons`), its hi-res skies and
widescreen sides (`assets/games/vf/backgrounds`; the host only looks for skies, and only shows the Background filter,
when the game's `video_traits` lists some). Each game's `romset.cpp`
registers its set with the shared loader too, so `tests/romset_test` finds a game's set by name. The shared video code says "scenery" and "a moving model's shadow" for what VF calls stages
and fighters (`video_traits::scenery_polys_end` sets where a game's scenery polygons end), and the build flags are
`M1_NATIVE` and `M1_GPU3D_METAL` / `M1_GPU3D_SDL`.

**The GPU renderer.** Since v1.1.11 the 3D can also be drawn on the GPU (Display > 3D renderer): Metal on a Mac,
SDL_GPU compute on Windows (Direct3D 12 or Vulkan) and Linux / Steam Deck (Vulkan). The goal was the CPU renderer's
picture exactly, not an approximation of it:

- **Draw order as depth.** Model 1 draws its polygons in a sorted order, with no depth buffer to hand to a GPU. So the
  GPU isn't used as a normal triangle rasteriser: a compute shader fills the polygons' rows itself, and for each pixel
  the last polygon in the game's order wins (an atomic maximum of its place in the list), as on the CPU.
- **The same arithmetic.** Each edge's crossing is worked out as the CPU does it, each step rounded on its own.
  Matching that on every platform took care: GPU division isn't correctly rounded, so divisions are done in integer
  arithmetic; shader compilers fuse multiply-adds, so nothing is fused; and float-to-integer conversion copies x86's.
- **Checked, then trusted.** Over the attract and fights, at several scales and shapes, on Metal, Vulkan and
  Direct3D 12, the GPU's pictures are the CPU's: 100%, no pixel different. The game repeats a small version of this at
  every start: a test picture drawn both ways, then its first pictures of a fight. A difference, an error or a
  missing driver, and that run uses the CPU. Auto (the default) then uses the GPU unless it's clearly slower than the
  CPU on that machine.

## How it was checked

Nothing was taken as working until it was measured. These checks run on every change that touches the game:

- **Lockstep against the interpreter.** `tools/model1lock.cpp` runs two machines side by side, one on MAME's V60
  interpreter and one on the recompiled code. It compares every CPU register after every scanline and the RAM after
  every frame. Standard runs are 18,000 frames (five minutes of scripted fights), for both the start-up and the C++
  forms. The first difference stops the run and says where.
- **Frame and RAM comparison with MAME.** Reference pictures are taken from MAME offscreen. The port's headless runner
  (`tools/model1run.cpp`) renders the same frames: attract mode matches MAME pixel for pixel, with both the interpreter and
  the recompiled code. These checks need the ROMs, so the script that makes the MAME references isn't in this repo.
- **Scripted, headless runs.** `tools/model1script.cpp` plays input scripts with no window: the operator menus, scripted
  fighters (random presses, or real moves and combos), a full ladder to the ending and name entry. It records clips
  with their sound, logs each frame's inputs, and dumps or pokes RAM to find the game's variables. Bugs were reproduced
  as scripts before they were fixed.
- **GPU against CPU.** From 2x the picture is put together on the GPU. A check draws the same pictures both ways and
  requires them to match pixel for pixel at every scale and shape. At start-up the game runs a small version of the
  same check on the player's own renderer (Metal, Direct3D, OpenGL or Vulkan).
- **Where MAME is wrong.** MAME is the reference, but not the arcade. When they disagree, cabinet footage and photos
  decide. In name entry, for example, MAME draws half the wheel's letters black, while cabinet captures show them blue
  and red, so the port departs from MAME there on purpose. The commit says why, and the MAME check is expected to differ
  on those frames.

A typical commit message ends with the check results, such as "model1lock 18000 identical both modes; MAME 120/120 both;
GPU check 0 differ at every scale/shape".

## Working with AI

The recompiler, runtime, host and tools were written with Claude, through Claude Code. In practice the work went like
this:

- **A person directs, tests and decides.** The human side was QA and play-testing on real machines (Mac, Windows, a
  Steam Deck), deciding what the port should do (where to stay arcade-exact, where to add a setting), checking footage
  of the real cabinet, and reviewing what went out. Many fixes started as "this looks wrong at 4x" or "the bushes
  jump", and the AI's job was to find out why and prove it.
- **Agents in parallel, all headless.** Several agents worked at once on separate problems. Each had its own build
  and data folder and ran the game hidden (`M1_HIDDEN=1`, no window, no Dock icon) through the headless tools. They
  never opened a visible window on the machine the person was working on.
- **Measure, don't assume.** Claims had to come with a measurement: a lockstep run, a pixel diff, a frame trace
  (`M1_TRACE`), a timing. Several plausible fixes were reverted when the numbers didn't support them.
- **Memory in the repo.** AI sessions don't remember each other, so the knowledge lives in writing. Commit messages say
  what changed, why, and how it was checked. A private notes file holds findings, dead ends and open questions. A new
  session reads those first instead of rediscovering them.

The AI wrote nearly all the code. A person decided what was right.

## Problems worth knowing about

- **Fighters' hair on the floor** → MAME's old high-level simulation of the TGP gets some geometry wrong → the TGP now
  runs its real program, and the port requires the ROM that contains it.
- **Dotted lines along the ring's edge at 4x** → MAME's filler rounds vertices to whole pixels, leaving gaps between
  polygons → above 1x, edges are sampled at pixel centres from unrounded positions. 1x keeps MAME's filler.
- **Every button did the wrong move** → the game's bits are punch, kick, guard in a different order than assumed →
  checked against the game's own input test.
- **Wolf's sky had a hard line under the clouds at high scales** → the game streams a dithered row over the
  panorama's top that the hi-res sky didn't take → the hi-res sky takes that row too.
- **Name entry's letters black** → the colour blocks the game reads are never set, in MAME or in the port → coloured
  as on the cabinet, only for those letters.
- **Widescreen select showed 4:3 between bars for player 2 or a challenger** → the select has three layouts and only
  player 1's was recognised → each layout gets its painted sides.
- **Half-built scenes and flashes at scene changes** → VF takes down and builds scenes over several frames → scene sync
  holds the last whole picture until the new 3D's polygon count settles (at most 12 frames).
- **Smooth motion stuck at 60 pictures a second after a run of scene changes** → the governor counted deliberate holds
  as missed pictures → it now judges only steady play.
- **Frames raced after a slow moment** → the lost time ran as frames back to back → no more frames per pass than the
  screen's pace; the rest is dropped.
- **Sound gaps under Wine** → its device runs about 5% slow and the queue overflowed → over 300 ms queued, the game
  waits for the sound instead of dropping it.
- **Sarah's stage looked flat under the fighters** → the game pulses a light from below on her stage, setting its
  length as well as its direction, and MAME (and the port) normalised every light vector, so the undersides stayed one
  shade → a light pointing up (only that one) keeps its length, as the Model 1's geometry uses it, and the undersides
  pulse with the floor as on the cabinet. Every other light is unchanged.
- **A GPU picture that wasn't the CPU's on another platform** → a GPU's division, fused multiply-adds and
  float-to-integer conversion each differ from the CPU's → integer division, no fusing, x86's conversion, and a
  self-test at every start (see above).
- **Players' setups** → portable mode (a `portable.txt` beside the program keeps everything in its folder) and a ROM
  folder of the player's own (`--roms`, `M1_ROMS` or `roms_folder` in `settings.ini`), only read, so an update can't
  touch it.
- **A crash at exit on Linux** → a widescreen write ran past the end of each line in the tile chip → found with ASan
  and fixed. ASan now runs clean at several scales and shapes.

## Reintegrating upstream after v1.3.2

The upstream production changes through `a48890d` (the v1.4.0 randomizer and subsequent menu, replay and GPU fixes) are
adapted to the multi-game runtime rather than restoring the original VF-only machine. VF's gameplay state owns
the random and secret fighters, deterministic name seeds, ROM patches for moves, strands and voices, ranking
names, and the Kage mask and language options. Its video customization is supplied through game callbacks;
the shared board does not need to identify a VF character or know its record addresses.

The host keeps the common menu interaction, frame pacing, graphics-driver selection, GPU composition and
renderer rechecks. VF's own host supplies its fighter settings and ranking-name sidecar. VR retains its existing
record-based interpolation and sky-scroll handling.

`make test-vf-gameplay` checks the settings, mask unlock, code patch restoration, reset state and ranking-name
round trip without a ROM. The same test binary accepts a local VF regions directory to check the named seed
catalog, seeded fighter reproducibility, randomized gameplay and ROM restoration; `--native` runs that gameplay
through startup-recompiled code. The existing per-game lockstep tools remain the CPU and RAM cross-check.
The solo random-opponent check also found an indirect dispatch destination missing from VF's address map;
that observed edge is included so the check runs without interpreter fallback.

The shared camera-cut extrapolation follows upstream's updated curve. It intentionally changes two sampled
VR smooth-motion pictures around a cut; record-based matching and the unblended 1x pictures are unchanged.
The disabled upstream `VF_POSE_EASE` / `VF_JOINTLOG` diagnostic experiments are not carried over; normal stance
blending remains the game's own.

Reintegration checks passed: VF's 18,000-frame native lockstep and both 22,000-frame VR course scenarios,
the shared and per-game tests, and the randomized VF test on both the interpreter and native code (337
named fighters, with no native fallback). The randomized interpreter run also passed AddressSanitizer.
At 1x, 150 VF and 100 VR sampled frames matched the pre-integration runner byte for byte. Isolated desktop
runs checked Metal rendering, GPU 2D composition and menu/restart behavior without changing the player's saves.
The SDL-GPU edge shaders still need Windows/Linux runtime validation; this Mac exercises their CPU fallback.

## VR's extended scenery distance

VR does its distance culling in the V60 program, before submitting objects to the shared renderer. Its routine
at `FF7C1E` selects visible cells from a 5x5 neighborhood of the camera in a 16x16 course grid, each cell 128 units
wide. At `FF7DC5` it finishes the scenery list at `501CC0`; the camera's cell and visibility mask are still in
R21 and R22. The game's separate near-cell lists support collision and streaming and must not be enlarged.

An earlier maximum-distance implementation expanded the CPU's list through an in-memory ROM patch,
set all scenery visibility bits, and raised the global polygon budget. That increased emulated CPU
work and could also change dynamic-object submissions. After an attract-mode car-position discrepancy
was reported, this coupling was removed rather than compensating with simulation timing changes.

VR still observes the completed-list write, but only remembers which cells the arcade selected.
At the first static-scenery matrix write (`FF8500`, called from `FF8469`, return address `FF846F`),
it reads additional static object records from the course table at `501948`. Those objects are attached
to the matrix's display-list offset as host-only metadata, without inserting CPU commands or writing
ROM, RAM, visibility bits or the polygon budget. All 256 cells are considered, nearest first, without
distance or directional culling. A selected cell is not necessarily submitted: `FF84A2` skips it when
the cumulative polygon cost at `5011A0` exceeds the budget at `5011B0`. The host follows that same
per-cell budget check, including each object's cost in the third record word, and supplements skipped
selected cells too. Otherwise nearby buildings disappear when they enter the arcade's selection,
then pop back in when the budget permits them.

The shared renderer draws these objects with that matrix and its material/light state. Interpolated
frames snapshot the extra objects with their matrix slot, so they use the same camera interpolation
as the world. Additional geometry has its own depth-sort accumulator, leaving the original objects'
inherited sort depths unchanged. Static scenery bypasses the arcade budget and command-buffer limit,
but does not change which cars the CPU submits. Renderer geometry allocations have explicit bounds
checks to prevent full-course geometry from overflowing the finite point/quad buffers.

The desktop's Display > Draw distance choice defaults to Full, saves the choice in VR's settings, and applies it to
the next list without restarting. Updated regression assertions cover all 256 camera cells,
nearest-first additional objects, unchanged ROM/RAM and Arcade budgets, and CPU/register equality
between distance modes during real-ROM runs. The ROM-free test checks budgets that stop after the
first cell, exactly at a cell boundary, and beyond the whole selected list. With local ROMs,
`build/bin/model1_video_test build/vr --scenery-distance` checks the Acropolis grid and race start
with smooth motion on and off: every course-table object must be submitted exactly once, including
objects from cells skipped by the arcade's budget. This is an automated coverage check, not visual
verification.

Animated trackside props have separate visibility checks. Full distance overrides reads of
`501AA0..501ABC` at the render-only checks `FFD77A`, `FFD928`, `FFDABA` and `FFC1B2` (flags).
Windmills (`FFB769`), animated start/goal banners (`FFB0EB`), roadside light poles (`FFAAC4`)
the building beside the windmill (`FFB679`) and palm trees (`FFDC4D`) instead use `501AC0..501ADC`.
These routines also check collisions: their newly admitted, originally invisible instances
receive a far result only for those collision queries (`FFB797`, `FFB19D`, `FFB1BD`, `FFAAED`,
`FFB6A7`, `FFDC94`), so extending
their drawing does not enable distant collisions. Originally visible instances keep their real
results. Scene-enable checks, stored masks, stored budgets and object command sizes are unchanged.
Only these props' drawing calls bypass the submission-budget comparison, even with LOD On.
Each part's matrix is attributed to its R25 record rather than a stale car record for smooth motion.

`build/bin/model1_video_test build/vr --attract-scenery` runs 6,000 attract frames per configuration
on the interpreter and native backend, covering both distance settings and smooth/detail modes.
The original three-part check at `FFD928` covers the large wheel, not the windmills; checking its
submissions alone missed the windmill and banner culling. The expanded regression checks actual
pixels from windmills, banners, flags, light poles, palms and the windmill's neighboring building beyond
their original visibility masks, including 84
rear-facing distant-banner samples with smooth motion. It also compares all 16 cars' positions,
speed, progress and collision state at 2,859 matched simulation updates; partial CPU updates at
video-frame boundaries are deliberately not compared. Full draws both windmill parts on all
990 calls, including 821 outside the original mask. Light poles remain submitted on all 12,608
calls (11,164 outside the mask), and the neighboring building on all 990 calls (821 outside).
The smooth checks detect visible distant poles in 62 pixel samples and the building in nine.
Palms remain submitted on all 20,790 calls (18,415 outside their original mask), with visible
distant palms in 122 smooth pixel samples and 121 with smooth motion off. Their collision
query retains its real result inside the original mask; outside it the non-hit result prevents
new impacts, crash sounds and detached fronds. All five palm drawing calls bypass the submission
budget in Full mode, including the existing impact-animation parts. Arcade mode is unchanged.
The fixed arrow boards at Big Forest's pit exit have their own render-only wide-mask check
at `FFD0E2`. Full bypasses that check and the drawing budget at caller `FFD19C`, with R25
matrix ownership. This handler has no collision queries. `build/bin/model1_video_test build/vr --direction-signs` checks
6,000 frames including a Beginner race on both backends, with smoothing and LOD alternated.
It verifies all 5,328 board calls (4,248 outside the original mask), visible distant boards in
249 active-race pixel samples, and unchanged simulation across 2,699 matched updates.
Breakable boards keep their original gate at `FFCC8C`: it freezes impact detection, motion,
breakage and sound together outside the arcade mask. Full adds their missing meshes to the
host-only scenery supplement, using each board's saved translation, three rotations and
animation offsets without executing any extra board logic. Supplemental objects carry an
optional local transform and owner; the renderer composes it with the current camera, and
retained-list hashes include the saved pose. Originally visible boards retain their arcade
drawing path, with R25 ownership at caller `FFCEC9`. The direction-sign regression also
compares every breakable board's state bytes at matched simulation boundaries in races and
attract mode, and checks actual distant-board pixels across both backends and motion modes.
Static coverage is checked against
the course table captured when its list was written, not the next scene's table during camera cuts.
The smooth Full runs retain every static course object across all 2,186 scenery lists.

The initial 7x7 implementation was validated with the gameplay regression, which checks boundaries and masks,
then compares actual rendered polygon counts at matching cameras using local ROMs on the interpreter and native
runner. In the 6,000-frame Beginner run, 2,361 matching-camera frames draw more geometry, with up to 1,589 extra
quads; the ranking's camera can now see the distant suspension bridge that the arcade culls.
The same test passes AddressSanitizer; Arcade's comparison picture matches the pre-change runner byte for byte.
Native lockstep still passes VF's 18,000 frames and both 22,000-frame VR course scenarios with Extended enabled.
The native desktop menu was also checked through both choices and a saved Arcade choice across a relaunch.

## VR's model detail (LOD)

Display > LOD defaults to Off and persists as `lod = off` in settings. Off keeps the near-detail
car bodies and separate wheels at every distance; On restores the arcade's distance-based
models. Static scenery already has a single detail level in its course-cell records, so it
remains full detail in both modes. Draw distance independently controls scenery visibility.

The TGP still executes its original program. Only the completed distance results read at
`FEFBF5` and `FF0D3B`, the two car-rendering detail decisions, are treated as near when LOD is off.
Collision, proximity and other TGP results remain unchanged. The normal model and damage
selection code still runs, retaining car colours, animation and crash geometry.

Full detail also bypasses the submission-budget comparisons at `FF848A` and `FF84A2` through
a read override; the stored budget and the per-object command sizes are unchanged. Otherwise,
more detailed cars could consume the arcade budget before scenery or other cars are submitted.
The renderer-only full-course supplement follows the same budget policy to avoid duplicates.
Changes apply to newly built lists without a restart.

`make test-vr-gameplay` covers defaults, toggling, reset retention, scoped distance/budget reads,
and scenery coverage under both budget policies. With local ROMs,
`build/bin/vr_gameplay_test build/vr --lod` (or `--lod-native`) compares rendered detail while
checking car positions, speed, track progress and collision state against arcade LOD on every
frame, then verifies that switching LOD back on restores matching polygon counts.

## VR's crash livery

Standalone car-colour selection replaces the player's normal model pointers, but the arcade's
collision and recovery routines also select red-only models directly (`FD377A` and `FD4A39`).
Applying the normal pointers at frame end cannot stop those models appearing in a list already
written that frame.

Object effects now receive the matrix's recorded owner, including when smooth motion is disabled.
For the standalone player only, VR remaps the red paint materials (`02`, `EE`, `F6`) on those
models to the selected livery's B0 body paint (texture slot 1). Geometry, damage animation,
non-paint materials and lighting flags are unchanged. Opponents, linked-cabinet mode, attract
scenes and the default red livery are excluded.

`make test-model1-video` checks every texture-word value against all seven non-default paint
selections. With local ROMs, `build/bin/model1_video_test build/vr --crash-livery` injects the
three arcade impact states and checks actual rendered material callbacks with the interpreter
and native recompiler, with smooth motion both off and on. It verifies green paint, unchanged
detail materials and opponent appearances, and that all three original impact models still draw.

## VR's foreground SEGA logo

The spinning blue SEGA logo is screen-space 3D, not scenery. Sorting it into the world's polygons
let close-up cars paint over it during attract mode, with either draw-distance setting and with or
without smooth motion. A car crossing the eye could even retain a negative sort key after clipping.
Correcting only negative keys was insufficient: a close car's positive key could also precede the logo.

VR now identifies its logo model in `video_traits::foreground_objects`. The shared sorter draws those
objects after world geometry, keeping their own depth order and preserving the foreground flag through
clipping. World depth keys, car/scenery ordering and other games are unchanged; CPU and GPU rasterizers
consume the same sorted sequence.

`make test-model1-video` checks foreground ordering and flag preservation through clipping without ROMs.
With local regions, `build/bin/model1_video_test build/vr` checks the affected close-up attract views
with both distance settings and both motion modes, plus enlarged and widescreen rendering. It compares
the full picture with an isolated logo, requires every visible logo pixel to survive, reproduces missing
pixels when the foreground rule is disabled, and checks that pixels outside the logo are unchanged.
The six ROM-backed cases cover 552 views. A hidden native-host run at 2x widescreen also drew 4,187
Metal pictures, checked 140 against the CPU with zero differences, and reported no GPU errors or fallbacks.

Specular highlights are rejected when the polygon normal faces away from the light. Without that
check, the reflection formula produces a second highlight on the spinning logo's reverse side,
briefly washing out its dark shade. `build/bin/model1_video_test build/vr --logo-shading` checks
reverse-facing logo pixels in original-frame and interpolated pictures; the unit tests also check
that front-facing highlights remain enabled.

## VR's animated palm ownership

Animated palms use the same matrix-writing instruction at `FF8500` as cars, but their own object
record is in R25. R19 still points to the last car tested for a collision. Treating every use of
that writer as a car part paired palm animation frames with car parts; when models or part counts
changed, interpolation could carry a palm along with the car instead of keeping its own motion.

Matrix-writer metadata can now qualify a writer by its return address at SP. VR lists the five
animated-scenery call sites using R25 before the ordinary R19 writer. Car, pit-crew and static-world
ownership keep their existing rules. This changes only smooth rendering metadata, not game logic,
ROM code or draw distance, and the original palm animation remains.

The video regression checks all five caller overrides, the car and world fallbacks, and a changing-model
palm beside a car gaining a part. Its old shared-car ownership reproduces a 2.5-unit midpoint error;
separate ownership keeps the palm on its own path. With local ROMs,
`build/bin/model1_video_test build/vr --motion` checks early-attract palm records and interpolation with both draw-distance modes, then
compares native and interpreter ownership and 2x widescreen pictures through the affected sequence.
Both distance modes pass 347 views and 2,109 palm-part pairings each; 142 native/interpreter
in-between pictures match pixel for pixel, without interpreter fallback. The foreground-logo and sky
regressions still pass.

## VR's language setting

VR now adds Language to the shared Game page, while Draw distance stays in Display. The choice is
staged until the menu closes or Reset game is selected, and a pending choice is also saved at quit.
Toggling back to the original language does not restart the game or change an existing U.S. region.
Japanese selects the arcade's Japan
country; English selects Export. Existing settings keep their language until the player changes it.

The country is the low byte of EEPROM word 13, copied to `40DC9B` at boot. Both VF and VR use the
SEGA header and byte-swapped CRC-16/CCITT, but VF checks words 8-63 while VR checks words 5-63.
A shared EEPROM editor preserves this distinction and both the real I/O board's native words
and the stand-in board's big-endian bytes. Language edits leave the other assignments and rankings
alone. ROM-free tests cover both layouts, checksum coverage, invalid choices and U.S. preservation;
the local-ROM language regression checks reset persistence in interpreter and native execution.

## VR's CPU speed

Other cars in VR sometimes jumped sideways. Car
positions in RAM were smooth, but the game updates cars 0-7 in one loop that ran across a video-frame
boundary; the boundary's place drifted, so a car was sometimes updated after the 3D list was built and
drawn one step behind its neighbours. In 3,000 attract frames car 4 changed phase 117 times. Raising
the emulated V60's speed moved the boundary through the loop (car 5 at 1.1x, cars 6-7 at 1.25-1.33x)
until, from 1.5x, every car kept a steady phase over 6,000 frames apart from scene cuts.

MAME's V60 charges 8 cycles to every instruction, "just an average"; the 16 MHz V60 is rated at about
3.5 MIPS. The game profile now has `cpu_cycles_per_instruction`, applied to the per-line instruction
budget so interpreted and native runs match. VR uses 4.5 (1.78x); VF keeps 8 and runs exactly as before.

## The TGP's rounding

VR's attract race differed from a cabinet video: at the tunnel exit the green car (car 0) drove
beside the blue/white one, where the cabinet's blue/white car exits alone. The camera script and its
timing matched exactly, and the result did not depend on CPU speed (8 to 18 cycles per instruction),
EEPROM, the analog inputs, or nudging a car mid-race. Cars 1-7 follow scripted lines; only car 0,
driven by the player's physics, was sensitive, and to float rounding.

Both VR's and VF's TGP programs execute `stmh 0x360a0007` at startup, and bits 1-2 of the mode
register are the rounding mode (3: toward zero). MAME applied the field only to `cfxd`, the
float-to-integer conversion. `mb86233_device::run` now sets the host rounding mode from the register
for the TGP's float ALU and restores the host's afterwards.

That was not the cause of the difference, though. At race start the game does not clear several of
car 0's fields, so each race inherits them from the race before. The one that matters is car 0's
rank index (record offset +0x123): the boot code clears battery RAM, the first medium-course race
runs with 0, and the next attract race sets it to 15, so every later medium-course race differs from
the first. SEGA AGES Virtua Racing shows the later race from its first attract loop (the green car
close behind on the bridge, past the red car in the tunnel), with either rounding; the other video
was from MAME, which like a fresh board shows the first. The port follows SEGA AGES: VR's gameplay
hooks set the rank index to 15 right after boot.

## Bringing up Star Wars Arcade

Star Wars Arcade (SWA) was added in stages: load the board correctly, boot on the interpreter,
check the cabinet's controls, implement the missing sound board, play repeatable games, then
recompile them. Reusing Virtua Racing's game registration and build rules was useful; copying its
assumptions about controls, code addresses or gameplay state was not.

### ROM layout before game-specific workarounds

The runtime ZIP loader and the development region builder must construct the same memory. SWA's
program is loaded at `0xf80000` and mirrored at `0x000000` and `0x080000`; the sound program is
byte-swapped and reloaded at `0x80000`. These are explicit operations in both loaders, not padding
that happens to contain a useful copy. The profile and ROM tables also include the TGP program,
polygon/data ROMs and the extra sound board's Z80 and MPEG data.

The supported archive is MAME 0.280's US set, with `315-5711.bin` CRC32 `c5ddb8fc`. That release
marks the TGP program `BAD_DUMP`. Checking its checksum establishes which dump was tested, not
that its geometry is hardware-perfect. Newer TGP dumps and the Japanese `swaj` program are not
covered by these tables.

### Use the geometry program already in the archive

The archive contains a program for the MB86233, so a complete SWA-specific high-level geometry
simulation was not the starting point. The V60 interpreter runs the game while the existing
MB86233 core executes the supplied TGP program. SWA has no geometry HLE implementation.

Reaching a title screen alone would not establish that the FIFO or scheduling works. The boot
test repeats two 1,800-frame cold boots and compares CPU/picture checkpoints, polygon counts,
TGP work and interrupt counts. Every frame checks conservation of input/output FIFO words,
queue peaks within 16 words, no empty-result waits and no TGP execution-limit hits. Input waits
are expected: the geometry program waits when the CPU has not supplied more commands.
Separate timing checks exercise the timer at period times 2,048 clocks and vblank at line 384
of 424, and the ROM-backed boot checks the audio sample count against elapsed board time.

With this set, the first polygons appear at frame 1106. By frame 1800 the TGP has consumed
24,559 input words and returned 24,750 output words, with both queues drained. Those are useful
reproducibility observations, not a measurement of cabinet latency: the TGP still runs on demand,
not on a cycle-accurate parallel scheduler.

### Let the cabinet's input test settle the wiring

The two seats are Pilot and Gunner, not two copies of VR's driving controls. Both seats' firing
buttons share the I/O board's player 1 byte: Pilot laser/torpedo are `0x01`/`0x02`, Gunner
laser/torpedo `0x04`/`0x08`, and the Pilot's view button is `0x10`. Start is system bit `0x10`;
the host's two Start controls operate that same cabinet button. Routing player two to a separate
player byte would silently lose the Gunner's buttons.

The Pilot's stick uses ADC channels 0 and 1, throttle channel 2, and the Gunner's stick channels
4 and 5. The ROM-backed control test uses the host's routing and analog conversion, then checks
the cells and markers on the game's own INPUT TEST pages. This tests what the game receives,
not merely whether the host changed a byte.

The factory-calibrated stick range is 27-227, centred on 127; throttle is 28 (FAST) to 228
(SLOW), centred on 128. MAME's throttle range stops at 200, which only reaches about -72 on
the game's test; the host uses the full calibrated travel. Out-of-range values can produce
INVALID VALUE rather than extra travel. X is reversed by the hardware mapping; Y follows the
cabinet, so forward dives, with an optional host inversion. The cabinet's recalibration procedure
has not been tested: the host mapping assumes the factory calibration.

### The missing hardware was the MPEG sound board

SWA retains the Model 1 sound board (68000, YM3438 and two MultiPCMs), but adds Sega's
Z80 Digital Sound Board. Loading its ROMs without implementing the board cannot produce its
music. The port follows MAME's `dsbz80.cpp` and MPEG Layer I/II decoder, keeping their attribution
in [the MAME source inventory](../third_party/mame/README.md).

Every byte sent on the 68000 sound board's serial link also reaches the DSB's i8251 receiver:
31.25 kbaud, one byte per 320 microseconds. The Z80 program interprets the commands and controls
start/end addresses, looping, volume and pan; it is not replaced with a host-side song lookup.
Addresses can be latched while a stream is playing, which matters for loops and multi-part music.
The board is instantiated only when its `mpegcpu` and `mpeg` regions are present, leaving VR
and VF on their existing sound path. Its 32 kHz output is resampled into the 48 kHz host mix
and follows the music-volume control.

A listening check found a bug that successful MPEG decoding did not catch. The resampler's last
interpolation in a scanline slice could need a source sample the Z80 had not delivered yet:
samples arrive at instruction boundaries. Keeping the resampler two source samples behind
production fixed the distortion. Before that, signal-to-error was only about 20 dB. The attract
test now compares the mixed theme against the stream's own decode at the board's volume:
over 60 dB required, about 68 dB measured. A separate comparison with ffmpeg's MP2 decoder
agreed within 2 LSB. Comparing the mixer to its own decoder checks the resampling and mixing;
the independent decoder comparison checks a different part of the chain.

The theme is not expected immediately at boot. The earlier Star Destroyer fly-by uses the
ordinary sound chips; the DSB receives stop commands. Command `01` starts the stereo theme at
`0x334000` around frame 3202, with the opening crawl's logo. Stub-program tests cover UART
pacing, receiver enable/overrun and register latching; ROM-backed tests cover the real trigger,
1,152 samples per MPEG frame and the mixed audio. Gameplay tests also require an in-game
stream outside the theme range and a loop without another trigger.

One early hypothesis was that the main game waited for sound during boot. A silent run disproved
that: its RAM matches the sound-enabled run through frame 8000. Headless coverage and lockstep
can therefore run silently, while separate gameplay tests exercise the sound-enabled path.
Hardware verification remains open for the Z80 clock (MAME's 4 MHz estimate), the volume
register's top bit, ignored ports `0xea`/`0xeb`, and the final music/effects balance.

### Observe game state instead of borrowing VR's addresses

The first gameplay probes paired screenshots with RAM observations. VR's camera/mode addresses
do not identify SWA gameplay: the shared camera-based `past_boot()` check stays false even when
SWA is flying. Rather than invent camera addresses to satisfy that assertion, the script checks
state the screen helped identify.

| Battery RAM address | Observation used by the checks |
|---|---|
| `0x40f008` | Sequence: 1-2 attract, 11-13 credited/playing, 14 game over and rankings |
| `0x40f334` | Time remaining, little-endian 16-bit, in 1/30 second units |
| `0x40f410`, `0x40f510` | Pilot and Gunner scores, 32-bit BCD |
| `0x40f100` | Ten Pilot ranking entries, eight bytes each |
| `0x40f150` | Ten team ranking entries, twelve bytes each |

The script inserts coins at frames 1300 and 1340 and opens the mode menu with Start at 1400.
Centred selects PILOT; pulling back during mode selection chooses PILOT & GUNNER, which needs
a third credit. Start confirms the mode and mission. Flying uses a seeded random sequence of
stick targets, eased toward each target, with throttle changes, laser bursts and torpedoes.
All of this is cabinet input: it does not write a score or force a gameplay state.

The checks require credit acceptance, the expected mission's initial clock, a decreasing flight
clock, a Pilot score, game over and return to attract. The Gunner run must also score for that
seat. Ranking checks happen on return to attract, after name entry is finished, not as soon as
game over starts. The team uses a separate table and the BCD sum of both players' scores;
looking for the Pilot's score in the solo table was the wrong test.

With seed 12345, Rebel Attack's flight starts at frame 3206. The solo script reaches
game over around frame 16,700 and returns to attract around 17,700. The original centred-Gunner
run finished at similar times; moving its aim changes the outcome and extends the run (below). These runs verify a final
score in the rankings, not deliberate entry of a particular name. These gameplay tests run for
22,000 frames; the later continuation test needs 36,000. Minimum-length enforcement is enabled
with checks, not for short diagnostic runs.

### Training needed a different ending check

The `training` scenario uses the same Pilot controls, but pulls the stick back during frames
1510-1539 in the mission menu. Screenshots confirmed the highlighted Training choice and
TRAINING MODE in flight. The initial clock gives a persistent automated discriminator:
120 seconds for Training, versus 180 for Rebel Attack. Merely reaching a flight would have
allowed a broken selector to pass by playing the default mission.

The raw clock includes a fractional part, so the mission check compares whole seconds rather
than demanding exactly `120 * 30` or `180 * 30`. In the observed Training run the initial
value was 3615, not 3600. Flight starts at frame 3506; the Pilot scores, game over is reached
at 10732 with time still left, and attract returns at 10736.

Reusing Rebel Attack's ranking requirement initially failed. Screenshots showed the Training
ending goes through GAME OVER without name entry; the final score is not added to the normal
ranking table. Training now checks that both ranking tables remain unchanged. The baseline is
captured after boot, when credit acceptance is observed, not at frame one: cold boot initializes
the tables, so comparing with uninitialized battery RAM would falsely report a Training change.
This verifies an ended Training run, not successful completion of all its objectives.

### Native recompilation: low ROM and unusual returns

SWA executes from `0x0c0000-0x0fffff`, outside the analyser's original code ranges. Adding
`0x000000-0x0fffff` to code discovery enabled the shared recompiler. VR and VF output was checked
with and without the change using identical hints; their listings and maps were unchanged.
Regenerating coverage can independently change a map, so a comparison using different coverage
recordings would not isolate the analyser change.

The next failures were missing entries, not divergent V60 arithmetic. SWA sometimes pushes a
return address and branches instead of making an ordinary call. Its reset pushes a PSW and
the next address, then runs `RETIS`. In native code, a return unwinds a host call frame; when
there is no matching caller, dispatch eventually reaches the runtime, which needs that destination
as an entry. The first examples were `0x0fff75` and `0x0ff90c`.

A special analyser rule for returns to the next instruction was tried and removed. Treating
every pushed PC-relative address as a function would also catch data addresses and change the
other games. The existing explicit-hint mechanism was sufficient: the runtime logs missing
destinations as `entry` lines in `build/cov/swa/runtime_entries.txt`; the next recompile includes
them. Recompile, run, collect and repeat until strict lockstep reports no interpreter fallbacks.
Adding entries can expose further destinations formerly hidden inside an interpreted region,
so one successful collection pass is not necessarily the last.

The shipped `assets/games/swa/swa_map.txt` contains addresses, not ROM code. Both `make swa` and
`make swa-native` use startup-native execution. The generated-C++ build remains a development
alternative. All five scenarios (default Rebel Attack, `gunner`, `training`, `continue`,
`progression`) run 36,000 frames
of strict native and generated-C++ lockstep: CPU registers after every scanline, battery/work
RAM and rendered frames after every frame, with no runtime interpreter fallback. This does not
mean every opcode is emitted directly as host instructions; supported handler calls remain
part of the recompiler's normal implementation.

Manual play extended the map beyond scripted coverage: successive logged sessions found 94
and 51 previously missing addresses. Each expansion retained existing entries and was checked
again against the scripted scenarios. `REC_VERBOSE=1` reports new fallback destinations; the
entry file collects them automatically, provided its parent directory exists. These logs record
addresses, not the player's inputs, so they cannot replay the session or prove that newly covered
manual paths match the interpreter. Likewise, zero GPU errors in a log is not a CPU/GPU image
comparison when that comparison was disabled.

### Automating the Gunner's aim

The original team script fired the Gunner's weapons but left the stick centred. A Gunner score
proved that firing and hits worked; it did not prove either aiming axis worked. The shared
`script_inputs` only had room for three analog channels, whereas the Gunner uses ADC 4 and 5.
The input array now covers all eight channels, with every unused channel defaulting to -1
(leave unchanged). The headless runner, coverage collector, lockstep and tests all apply the
whole array, separately from the three digital input ports.

This change had a subtle compatibility trap: assigning three values to a larger `std::array`
zero-initializes the remaining elements rather than retaining their -1 defaults. VR's qualifying
driver used those assignments. It now sets only channels 0-2 explicitly, preserving inactive
channels and its existing steering/pedal sequence.

The team scenario holds the Gunner centred until frame 3700, then cycles each axis to its low
and high endpoints for 120 frames, keeping the other centred: right, left, down, up. It uses
no extra random numbers, so the Pilot's seeded steering and throttle are unchanged. Checks
require both scores and all four directions in the calibrated Gunner input fields, signed Y/X
at `0x40f340`/`0x40f342`. Controlled observations gave Y endpoints -4550/+4550 and X
-6400/+6400, zero at centre. The gameplay checks demand substantial travel in both signs,
not merely a one-bit change.

Those RAM fields alone would still only establish that the game received the axes. A separate
regression compares five cold-boot team runs, identical until frame 3700: neutral, X low, X high,
Y low and Y high. For the controlled interval the Pilot is centred and both players release
their firing buttons. At frame 4000 the Gunner's yellow-green reticle is identified separately
from the green Pilot label. Low/high X moves it right/left by 141 pixels, and low/high Y moves
it down/up by 93. The test requires over 100 horizontal or 70 vertical pixels, at most two
pixels of cross-axis displacement, and an identical picture outside small bounds around the
old and new reticles. Thus swapped axes, wrong inversion, ignored channels and accidentally
steering the Pilot cannot pass just by producing a score.

ROM-free input-sequence checks also require repeatable sweeps, unchanged Pilot flying controls
and unused channels left at -1. `test-swa-gameplay` includes the on-screen isolation check and
now runs the moving-Gunner scenario with sound on native code too, requiring no interpreter
fallback. With seed 12345, four directions have been observed by frame 4062, game over comes
at 19614 and attract at 20557, still inside the 22,000-frame test. Its team score is checked in
the rankings as before. The coverage recording is updated for this changed run, and strict
native and generated-C++ lockstep cover it.

### Continuing after mission failure

The next script needed feedback rather than another absolute frame schedule. Screenshots and
battery-RAM snapshots identified a continue offer as sequence `0x40f008 == 13`, subsequence
`0x40f009 == 12` and timer `0xffff`. The unset timer matters: a failed mission need not have
an ordinary zero clock. Subsequence 1 was observed during the following mode/intro transition;
these observations are not a complete decoding of every game state.

The `continue` scenario uses the normal seeded solo Rebel Attack until that first offer.
It centres the Pilot and releases firing, inserts a coin 30 frames after detection, presses
Start for YES at +150, then confirms the centred PILOT choice 180 frames after the mode
transition is observed. The reference machine supplies the observations for both sides of
lockstep. No game state is written, and the underlying Pilot random sequence keeps advancing.

The credit counter at `0x40f0e0` corrected an early assumption. A new solo game costs two credits,
but a solo continue costs only one: a two-coin probe left one credit after resumption. The
final script inserts exactly one, observes it accepted, then requires zero remaining credits.
Accepting continue also resets the Pilot score rather than preserving it. A fresh 180-second
clock alone is not enough: checks require the reset score, spent credit, resumed countdown,
a new positive score and a second continue offer. That offer gets no further coin or Start,
so the script verifies expiration, final game over, name-entry completion and the second
attempt's score in the solo rankings.

With seed 12345 the first offer appears at frame 14506, holding BCD score `5001`; the coin is
observed at 14543. The reset clock/score is observed at 15001, countdown at 15746 and a new hit
at 16269. The second offer arrives at 27045 with BCD score `5482`, game over at 31775 and attract
with the final ranking at 32718. The checked scenario therefore requires 36,000 frames.
At that point, coverage and both lockstep targets used 36,000 for all four scenarios through
the existing per-game configuration; the original three sound-enabled gameplay runs remain 22,000.

Two negative runs suppress either the new coin or Start after the first offer. Neither can
reach a fresh 180-second clock or satisfy the continuation checks. Positive sound-enabled runs
compare final battery/work RAM hashes between interpreter and native execution, with no
native fallback. Continue coverage and runtime entries extend the shipped map while retaining
all existing entries; strict native and generated-C++ lockstep compare registers, RAM and
rendered frames throughout. Extending the older scenarios to 36,000 also reaches later
attract playback: their coverage had to be refreshed, and strict runs exposed additional
pseudo-call return entries beyond the original 22,000-frame window. This validates one solo
continuation after failure, not winning the mission, unlimited continues or the PILOT & GUNNER
continuation path.

### Reaching a successful objective with cabinet inputs

Random flying established reproducibility and failure handling, but did not establish a
successful objective. A centred Pilot firing continuously at fast throttle improved the
opening fight without finishing it. Slow throttle ended sooner through damage. The useful
change was selecting PILOT & GUNNER and steering the independently movable Gunner reticle.

The first successful driver keeps the Pilot centred at fast throttle (ADC 28), holds both
laser buttons and pulses both torpedoes for six frames every 90 frames. Starting at frame
3300, it renders every fourth frame and selects the blue pixel nearest the screen centre
inside a bounded gameplay region. The Gunner ADC values follow from the measured reticle
slopes: 1.41 horizontal and 0.93 vertical pixels per input unit, with both axes inverted and
clamped to the calibrated 27-227 travel. This is deliberately a simple visual heuristic,
not a general enemy recognizer: blue engine details can attract it too. It only observes
rendered pixels and sends cabinet inputs; it does not modify enemy data, scores or timers.

Screenshots established what the observed transitions meant. With seed 12345, sequence 13's
subsequence changes from 3 to 4 at frame 8568, and the screen says "Proceed to engage the Super
Star Destroyer." The timer pauses at raw value 2630. At frame 9574 subsequence 5 begins; the
screen shows the new flight and "Time Extend 60", and the timer becomes 4430, exactly 1800
ticks higher. The next-stage clock then counts down. This proves completion of the opening
objective and entry into the next stage, not destruction of the Super Star Destroyer or a
complete Rebel Attack victory. The exploratory run later failed in that next stage.

The initial interpreter-only probe became the shared `progression` scenario, usable by the
runner, coverage collector and both lockstep backends. Its visual feedback is sampled before
emitting the following frame's inputs: frame 3301 reads frame 3300, then every fourth frame.
This preserves the successful probe's one-frame feedback delay even when tools render every
frame. Integer ratios express the measured reticle calibration, with stable scan order for
equally near target pixels. Inputs do not depend on the random seed. At the default 1x, 4:3
settings the shared driver reproduces the original briefing and next-stage timings; resumed
countdown is first observed at frame 9576.

With checks enabled, progression requires credit acceptance, the 180-second mission, flight,
both seats scoring, the ordered flight/briefing/next-stage transitions, the exact 60-second
addition and resumed countdown. It fails if the game ends before next-stage flight. Unlike
the failure/ranking scenarios, its goal is successful progression, not game over; the minimum
checked run is 10,000 frames. The existing per-game coverage/lockstep configuration records
36,000 frames, including the later playback after that goal has been met.

`swa_gameplay_test build/swa --progression` (also `--objective`) exercises this shared driver.
Interpreter runs with and without sound and a native run with sound must agree on final
battery/work RAM, with no native fallback. A second driver with a different seed must emit
the same inputs throughout. A negative run with both seats' firing disabled must reach flight
but neither success transition within 10,000 frames, and cannot satisfy the scenario's checks.
These checks also run in `make test-swa-gameplay`. Progression coverage and missing native
return entries extend the shipped map while preserving old entries; strict native and
generated-C++ lockstep validate registers, RAM and rendered frames for the new path too.

### Investigating the Super Star Destroyer stage

The first progression driver enters the destroyer stage but does not complete it. With the
default settings and centred Pilot at fast throttle, shields are exhausted and failure
subsequence 11 begins at frame 11358, well before the remaining clock expires. Slower speeds,
constant stick deflections, short climbs, timed turns, seeded evasive steering and aiming at
bright engine structures did not produce a successful route. These experiments are not new
success checkpoints, and the existing opening-objective regression remains unchanged.

An explicitly approved easier-settings experiment used only the cabinet's Test and Service
inputs, without loading or saving the user's battery RAM or EEPROM files. From cold boot,
after 1300 frames, a ten-frame Test press and 60-frame release opens the test menu.
Seven Service presses (each with the same press/release spacing), then Test, open GAME
ASSIGNMENT. The three sliders are GAME DIFFICULTY #1 (PLAYER), #2 (ENEMY) and #3 (TIMER),
all initially 3/5. Starting on EXIT, Service selects the first slider; three Test presses
cycle it through 4/5 and 5/5 to EASY 1/5. Repeat Service and three Test presses for each of
the remaining two sliders. Two Service presses then Test return to the main menu; four
Service presses and Test exit test mode.

Screenshots confirmed all three settings at 1/5. With this setup, the initial clock is
6015 ticks (200 whole seconds, rather than 180), and the first stage transition adds
1950 ticks (65 seconds, rather than 60). Those differences are intentional operator settings,
not a reason to relax the default progression scenario's exact 60-second bonus check.
In the centred fast run, the first briefing appears at local gameplay frame 9524,
next-stage entry at 10530 and failure at 13692. Other tested easier-setting routes also
failed; easier settings alone have not established completion of the second objective.

A short-climb variant additionally exposed a repeatable renderer fault. After the same
operator setup, the normal progression inputs are used except for Pilot Y=227 during the
first 60 frames of subsequence 5, then Y=127 again. AddressSanitizer on the renderer identifies
an invalid read at `m_tgp_ram[tex_adr - 0x40000]` in `model1::push_object`. A diagnostic run
observed texture address `0x7d082d7f`, outside the texture RAM range `0x40000-0xfffff`, with
polygon cursor `0x2d7d18` and flags `0xbebad975`. The existing special-case rejection of
texture `0xffffffff` did not cover this request.

Tracing the CPU writes established that this was not a misaligned display list. At machine
frame 17693 (including the operator-menu setup), command 1 at list-buffer 1 offset `0x720`
contains texture `0x7d082d7d`, polygon `0x082d7d08` and size zero. The ROM emitter at
`0x0c197f` copies the texture from `4[R0]`; `0x0c1984` copies the polygon from `[R0]`.
Here R0 is `0x0d93ce`, an address inside executable program code, not an object descriptor.
The renderer's two texture-increment flags account for the eventual `0x7d082d7f`.

The observed caller explains that bad pointer. At `0x0e463a`, the ROM loads the 16-bit
counter at `0x40[R20]` into R5, decrements it and stores it back. At `0x0e474e` it tests only
the **byte** sign before selecting the object. Once an underflowed counter's low byte becomes
nonnegative again, the test admits it despite the remaining upper bits. The byte-sized
doubling at `0x0e4752` preserves those bits; at the fault R5 changes from `0xff3c` to
`0xff78`. The indexed `movea.w` at `0x0e4755` then scales by four:
`0x995ee + 4 * 0xff78 = 0xd93ce`. The previous rendered frame similarly used `0xd93d6`.
This traces the malformed descriptor to actual ROM instructions in this execution; it does
not establish what original hardware displays for these out-of-range requests, or justify
patching the ROM's counter, scores, timers or game state.

The host renderer now rejects out-of-range object texture addresses with an explicit
diagnostic containing the frame, original request and current cursors. It also bounds the
initial vertices and each subsequent polygon against the selected ROM/RAM allocation, and
checks texture increments before access. VF's legitimate `0x3ffff` pre-increment seed is
accepted, but an actual read must still fall within `0x40000-0xfffff`.
A malformed stream stops that object, preserving
already decoded geometry and restoring temporary view offsets; subsequent valid objects
still render. No guessed hardware address mask or fabricated geometry is applied.

`make test-swa-video` includes the real-input reproducer as well as the name-entry check.
The reproducer performs the operator sequence above, uses the shared progression driver,
applies the 60-frame climb, confirms the exact executable-code descriptor is emitted, and
continues to local gameplay frame 14500, past the former crash. Renderer-instrumented
AddressSanitizer passes the same route. Synthetic tests cover both observed malformed
requests, texture boundaries and increments, truncated ROM/RAM vertices and records,
unterminated streams, valid termination at the final word, and visible valid geometry before
and after a rejected request. Shared VR HUD/video checks and the sound-enabled SWA
progression regression also pass. The second objective remains unfinished; this is a
renderer safety fix, not evidence of a successful new route.

With the crash fixed, route discovery resumed using the same real operator settings and
ordinary controls, without infinite shields, frozen timers or RAM writes. Tests included
red target-box aiming, two-axis evasive orbits and smaller course-preserving variants,
Pilot tracking of the existing blue-pixel target at four gains, torpedo pulse periods of
12/30/60/180 frames, throttle changes and radar-image steering with both polarities.
None reached a verified second-objective completion.

Some apparent improvements were misleading. A 360-frame orbit with amplitude 60 survived
until local frame 18976, but screenshots showed empty space outside the trench for much of
that time. Pilot tracking at half the Gunner ADC offset, with 30-frame torpedo pulses,
survived to timeout and failure at frame 19870; its scores stopped increasing and it also
went off course. Longer survival alone is not route progress. The briefing explicitly
identifies the **Main Engine** as the target, so the unresolved problem remains following
the trench to that objective, not merely avoiding damage.

Read-only RAM sampling and CPU write tracing also located the authoritative shield word
at `0x40f772` (16-bit); the high byte at `0x40f31f` is a copied display/state value, not
the authoritative counter. The ROM copies the word at `0x0c43e9`, and subtracts damage
at `0x0c7f66`. The centred easy run shows repeated `0x40` deductions through caller
`0x0c736b`, plus occasional `0x100` deductions through `0x0c7f33`; the latter follows
a negative geometry-query result in the routine at `0x0c7ef7`. These observations are
diagnostics only: no shield values were changed, and no experimental route was promoted
into the shared progression driver.

### Tracing the destroyer course

The next investigation measured **forward position**, rather than treating survival time or
score as evidence of following the trench. The ship object is addressed by
`0x400000 | read16(0x40f408)`; it was `0x40bc80` in these runs. Its offsets
`+0x0c`, `+0x10` and `+0x14` hold float32 world X/Y/Z. The 16-bit angles at
`+0x18`, `+0x1a` and `+0x1c` are pitch, yaw and roll, in 65536 units per revolution.
The movement routine at `0x0c69dc` transforms the forward velocity by those angles and
integrates the resulting position. The integer world origin at
`0x40f736/0x40f73a/0x40f73e` was `(-281600, -56649, 384000)`.
Those values allow trajectories to be compared in local trench coordinates.

With the approved easy settings, stage 5 starts at local gameplay frame 10530.
At frame 10560 the ship is approximately `(-281650, -55439, 379550)`, or
`(-50, 1210, -4450)` relative to the origin. Centred inputs hold yaw almost zero
and advance down +Z. By frame 11340 the ship reaches local Z=51010 with shield
word still 1472. Near local Z=55200 it encounters an obstruction; later positions and
headings show repeated reversals in the same region, not continuous passage along the trench.

Tracing the actual terrain queries at `0x0c6f7d` explains why looking only at the ship
centre misses the first collision. At machine frames 15320–15326 the ROM tests three
swept segments, including a wingtip in the adjacent grid cell. The centre still lies on
one side of the cell boundary while that wing reaches the neighbouring wall. The two
queried cells encode collision meshes `0x58d` and `0x5a3`. The read-only decoder reproduces
an obstruction for the swept wing envelope, although the corresponding centre-only
segment is clear.

The active collision-grid selector at `0x40f77a` is 6; the dispatch selector at
`0x40f77b` is 25. The table at program-ROM `0x0a06d2` selects the grid at `0x081a40`.
The lookup at `0x0c6e6c` subtracts the world origin and indexes an eight-column,
224-row grid with 1024-unit cells and a 4096-unit offset. Each word contains two
16-bit collision-mesh indices; `0xffff` means no mesh. Coprocessor data-ROM word
`0x10` points to the mesh directory. Each directory pair contains a quad count and
word address; each quad contains four XYZ vertices, a plane equation and one extra
word. This is the format already decoded by the shared `intercept` implementation.
It is not a table of ready-made Pilot waypoints.

A session-local planner used that geometry to derive candidate paths, checking swept
segments with an expanded envelope and exploring lateral and vertical changes. A separate
experimental driver reads the ship's pose and supplies clamped normal Pilot ADC inputs;
it never writes the pose, shields, timers, scores or stage state. In a checked run,
the first obstruction is passed with shields unchanged: local Z advances from 55295
at gameplay frame 11400 to 76698 at 11700, with the shield word still 1472.
The path follower subsequently reaches approximately local Z=102000 while shields remain,
before losing the course at another constriction. This is measurable down-trench progress,
unlike the earlier empty-space evasive runs.
Repeating that run produced exactly the same 41 sampled pose/shield records; the maximum
sampled forward distance with shields remaining was 101702.88 units. The ordinary
sound-enabled progression regression still matches silent, interpreter and native execution.

Neither the offline path nor the feedback controller is a verified solution. Larger
clearance margins, different lookahead distances, feedback gains and slower throttles
did not establish second-objective completion. The mesh planner's ideal swept path
does not guarantee the real ship can track it through the game's steering dynamics and
collision responses. No experimental follower replaces the shared progression scenario.
That localized the next investigation to the roughly 90000–102000-unit section,
rather than another blind search over whole-stage inputs.

### Correcting the experimental trench follower

Two-frame pose samples and the ROM's actual collision-result branch at `0x0c712a`
revealed an earlier divergence. The original follower first hits terrain at gameplay
frame 11711, local Z approximately 77438, with mesh `0x618`. Immediately before the
hit, altitude is 1122 while the future waypoint is 944. The collision changes the
heading, and later collisions compound the error. The roughly 102000-unit reversal
was therefore not the first tracking failure.

The steering trace explains why increasing a generic feedback gain was not enough.
The two Pilot axes have different semantics:

- `0x0c66e8` reads Pilot Y through `0x0f3376`, multiplies its normalized value by
  `0x92` (146), negates it and stores an **absolute pitch target**. The subsequent
  response moves actual pitch towards that target, approximately one sixteenth of
  the angular error per update, with a step limit. Treating pitch input as a command
  proportional to desired-minus-actual pitch introduces an unwanted angular offset.
- Pilot X instead changes an **integrated yaw target**. `0x0c67a2` adjusts the
  normalized input by forward speed; `0x0c6712` then incorporates auxiliary steering
  state and subtracts six times the result from the yaw target. Actual yaw follows
  that target with a different response. It is not the same controller as pitch.

The ROM also calibrates and dead-zones the ADC values before steering. At `0x0c0709`
it subtracts the stored axis centre, multiplies by 32, divides by the stored scale
and adds 128. The operator setup used here has centre 128 and scale 96, with input
inversion enabled. `0x0f3376` maps calibrated values below 128 relative to 128, maps
values above 136 relative to 136, and returns zero in between; the result is limited
to -56 through 56. Small changes around raw stick centre therefore do not produce
the assumed proportional steering command.

The session-only correction searches the legal 27–227 ADC range through this
read-only calibration mapping. Pitch selects the input closest to
`-desired_pitch / 146`, rather than feeding back pitch error through the old stick
gain. Yaw uses heading-error feedback to request a normalized turning input, accounts
for the ROM's speed-dependent gain, and inverts the same calibration/dead-zone
mapping. Its normalized request is limited to +/-28, below the strong-input
threshold of the separate roll-manoeuvre path. This remains an approximate follower,
not a replacement implementation of the game's flight dynamics.

With the existing 240-unit-clearance candidate path and 2000-unit lookahead, the
corrected controller clears both the first divergence and the later constriction.
An explicit check requires successive local-Z checkpoints while the shield word is
greater than 1 and no greater than the stage-entry value 1472. It also rejects any
terrain-hit callback from gameplay frame 11001 through the last checkpoint:

| Local-Z checkpoint | Gameplay frame | Actual local Z | Shield word | Terrain hits in checked interval |
|---|---|---|---|---|
| 110000 | 12211 | 110075.25 | 960 | 0 |
| 120000 | 12387 | 120103.91 | 576 | 0 |
| 130000 | 12557 | 130090.44 | 320 | 0 |

The maximum per-frame forward distance with that live-shield condition is
138102.06 units, compared with the old follower's sampled 101702.88. Rendered
checkpoint images show continued flight inside the trench, not escape into empty
space. Two complete runs produce byte-identical telemetry and the same per-frame
pose/angle, analog-input and shield hash, `a70c08e620116d35`. The ordinary progression
regression still matches interpreter, silent and sound-enabled native execution.

This is **not second-objective completion**. Shield loss remains through other ROM
callers, including `0x0e3fde`, `0x0e3666` and `0x0c7f33`; it must not all be labelled
terrain collision. The run eventually fails at frame 13062. The useful next trace is
those remaining damage sources and target engagement along the now-traversable
section. The corrected probe, candidate path, logs and checkpoint images remain
session-local; the production progression scenario is unchanged. All steering still
uses ordinary cabinet inputs, with no writes to shields, timers, scores or ship pose.

### Separating trench damage and improving target engagement

The next probe kept the same operator settings, candidate path and Pilot controller,
and traced the remaining shield deductions before changing Gunner aiming. There are
three distinct mechanisms in the measured section:

| Damage return address | Deduction | Observed source |
|---|---|---|
| `0x0e3fde` | `0x80` | Fixed expanding hazard, updated by `0x0e3f08` |
| `0x0e3666` | `0x80` | Another fixed hazard, updated by `0x0e354c` |
| `0x0c7f33` | `0x100` | Moving collision sphere, including turret bullets and a larger projectile |

These are the low 16 bits of the damage argument; the upper register bits can be
stale float data. The fixed hazards test horizontal separation against 512 units
and vertical separation against their current radius. Examples are local
`(512, 512, 86528)` and `(512, -512, 110080)`, with radius 768 at impact.
The moving-object test follows the linked sphere lists and compares distance with
the sphere radius. Its `+0x2a` field is a **list link**, not the firing object's ID.

To establish provenance rather than guess from colour, the probe records allocations
at `0x0c0a30`, invalidates old records whenever a slot is reused, and records the
owner when `0x0e59ff` finishes creating a turret bullet. In the control run, the hit
at gameplay frame 11957 comes from a radius-30 bullet created at frame 11879 by
turret `0x406140`. The later impacts at 12281, 12427 and 12737 likewise trace to
turret bullets. The improved run avoids the first and third of those impacts, but
still encounters bullets from other turrets.

The radius-180 impact in the improved run is a different projectile, allocated
through return address `0x0e494b` at frame 12519 and hitting at 12537. Its creation
routine at `0x0e4945` sets a forward speed of 240 and radius 180, then links it into
the moving-object list. It must not be called another radius-30 turret bullet or
a terrain collision.

Destroying an object also does not necessarily remove its danger immediately.
Hazard `0x405e20` enters its destruction routine at frame 12320 but still damages
the ship at 12346. A turret's destruction animation also does not undo an already
fired bullet: the improved run's impact at 12263 comes from turret `0x405e70`, fired
at 12207, one frame before that turret's destruction animation finishes at 12264.
The remaining damage
cannot be solved merely by counting scored hits.

Two visual alternatives were measured and rejected. Recognizing red target boxes
reduced live-shield progress to about 130222 units; connected red-pixel components
also did worse than the existing blue-pixel heuristic. Neither reliably distinguishes
the dangerous objects from other red geometry and effects.

The successful session-local alternative uses the game's own **read-only shootable
list**, from `0x40ed04` to the pointer at `0x40ed00`, with explicit pointer bounds.
The probe captures object-to-camera matrices as the ROM emits display-list commands,
matches the object IDs against that list, and projects their translated origins.
It selects a reachable forward target using depth plus an angular-offset penalty.
This avoids mistaking arbitrary coloured effects for targets.

Projection calibration matters here too. The live display-list header specifies a
200-pixel zoom on both axes, not the initial experimental estimate of 300. Its viewport
maps to centre `(248, 191)` after the renderer's Y conversion. The resulting screen
coordinates are converted through the already measured Gunner reticle slopes,
1.41 horizontal and 0.93 vertical pixels per ADC unit, then clamped to 27–227.
The chosen input is held between actual scene updates. Continuous lasers and the
existing torpedo pulses remain unchanged. The probe never injects a hit, changes a
target's health or writes the shootable list.

At matching forward checkpoints, the result is:

| Local-Z checkpoint | Control shield | Improved shield | Control turret destructions | Improved turret destructions | Control / improved turret shot attempts |
|---|---|---|---|---|---|
| 110000 | 960 | 1216 | 2 | 9 | 38 / 10 |
| 120000 | 576 | 832 | 2 | 12 | 47 / 13 |
| 130000 | 320 | 832 | 2 | 13 | 49 / 14 |

Destructions are observed at the ROM's completed turret-destruction branch, not
inferred from score. Shot counts are attempts at the turret creation routine's
entry, not a claim that every allocation succeeded. Across the complete runs,
turret destructions increase from 2 to 15 and shot attempts decrease from 64 to 23,
although the runs have different lengths. The matched checkpoints are the stronger
comparison.

The session regression explicitly requires at least 832 shield units and 13 turret
destructions, with no more than 14 turret shot attempts, at local Z=130000. It also
requires live-shield progress beyond 145000 and the existing no-terrain-hit check
from gameplay frame 11001 through the 130000 checkpoint. Two runs pass with
byte-identical telemetry and hash `fc56a468eb6db911`. Maximum per-frame live-shield
distance increases from 138102.06 to **148893.06** units. Checkpoint images still show
flight inside the trench. A negative control holds the Gunner at ADC `(127, 127)`
through normal inputs; the improvement assertion rejects it at the 130000 checkpoint
with shield 320, zero turret destructions and 53 shot attempts. The ordinary
sound-enabled progression regression continues to match silent, interpreter and
native execution.

This remains an experimental diagnostic, not a new shared progression checkpoint.
The next terrain hit occurs at frame 12773, after the 130000 checkpoint, and the
run eventually fails at frame 13240 without completing the second objective.
Remaining work is to handle the larger launcher's projectile, engage hazards early
enough for their dangerous death phase to finish, and resolve the subsequent
deflection/collision sequence. The targeting probe, repeatable session make target,
baseline, rejected candidates and verified evidence are preserved outside the
worktree; the production driver is unchanged.

### Avoiding the large launcher volley

The radius-180 projectile that hit at gameplay frame 12537 now has a traced
launcher, not just an allocation site. At the end of creation (`0x0e4a56`), register
R11 identifies the projectile and R20 identifies its owner. The offending owner is
`0x405ce0`, updated by `0x0e3f08`, at world
`(-280576, -59721, 517632)`: local `(1024, -3072, 133632)`. Its firing phase is
`0x0e3b39`, reaching the shared projectile creator through `0x0e3c7d`.
The adjacent launcher is a separate source.

The launcher's byte countdown at `+0x44` resets to 64 and emits seven shots as it
falls through 60, 56, 52, 48, 44, 40 and 36. In this run those launches occur every
eight gameplay frames, from 12519 through 12567. The first projectile,
`0x404890`, starts at `(-280536, -59716, 517132)` with approximate velocity
`(0, 2.95, -239.98)`, then hits 18 frames later. This is moving-sphere damage,
not a terrain collision or the fixed hazard's expanding-radius test.

Prioritizing launcher targets did not prevent this impact. One initial candidate
selected the other launcher handler; even after correcting that selection, the
target-origin capture did not supply the offending launcher as a reachable target
before the hit. This does not establish that it is generally invulnerable or
unshootable. The successful alternative leaves Gunner engagement unchanged and
alters only the Pilot's candidate path.

The session-local path generator tested bounded offsets against the decoded
collision mesh with the existing 240-unit swept-clearance check. Horizontal
offsets of either 512 units and a positive-Y offset of 384 units were rejected.
A negative-Y offset of **384 units** passed: it ramps in smoothly over local
Z=124000–128000, holds through 134000, and returns to the original course by
138000. The follower tracks this path through ordinary calibrated Pilot inputs;
there are no shield, time, projectile, collision or game-state writes.

The gameplay assertion checks the ROM's actual collision callbacks, not just the
offline mesh estimate. It requires at least seven shots from this exact launcher,
zero large-projectile impacts throughout the run, no terrain hits in the modified
124000–138000 interval, shield at least 832 at 135000, and live-shield progress
past 150000. The original 130000 engagement assertions also remain enabled.
Two complete runs must produce byte-identical telemetry and per-frame pose/input/
shield hashes. Removing only the path offset is the negative control: the same
launcher again produces the frame-12537 impact and the avoidance assertion fails.

With the dodge, the ship retains shield **832** through the former impact and the
135000 checkpoint, and reaches maximum live-shield local Z=**161199.00**.
The volley still fires; it has been avoided, not disabled. Checkpoint screenshots
also confirm continued trench flight. Sampled projectile-centre distances are
diagnostic only, not a continuous swept-collision proof.

This does not solve the rest of the course. The fixed hazard at `0x0e3666` still
deducts 128 shield units at frame 12644, followed by the first later terrain hit
at 12759, beyond the modified interval. The run eventually fails at 13568 without
completing the second objective. The probe, path generator, repeatable
`test-swa-stage2-projectile` session make target, baseline, rejected candidates,
negative control and verified evidence are preserved outside the worktree.
The ordinary sound-enabled progression regression still matches interpreter,
silent and native execution; the shared production driver remains unchanged.

### Avoiding the fixed hazard at frame 12644

The next shield deduction comes from object `0x405c40`, updated by `0x0e354c`,
at world `(-280576, -60745, 522752)`: local `(1024, -4096, 138752)`.
It is a fixed expanding hazard, not another launcher projectile. Its expansion
phase byte at `+0x42` is 2 and its current and maximum radius words at
`+0x38/+0x3a` are both 768 at impact.

The ROM's collision branch at `0x0e3600` measures distance in the X/Z plane
against **512**, then converts the absolute Y difference to an integer and
compares it against the **current radius**. The previous description had these
dimensions reversed; it is corrected above. At gameplay frame 12644, the old
launcher-dodge route places the ship at approximately
`(-280582.62, -61464.33, 522260.69)`. Horizontal distance is about 491 units and
vertical separation is only 719.33, so both tests pass. The damage call through
`0x0e365b`, returning to `0x0e3666`, deducts 128 shield units: 832 becomes 704.

Read-only snapshots immediately before and after that call show unchanged ship
position and angle bytes. The branch sets the ship's temporary invulnerability
word to `0x1e01`; it does not directly knock the ship away. This rules out an
immediate positional/rotational kick, not every possible later effect of damage.
Avoiding this hazard also does not by itself remove the subsequent terrain hits.

The successful session-local adjustment adds another **384-unit negative-Y
offset** to the already verified launcher-dodge course. A smooth ramp starts at
local Z=135000 and reaches the full offset at 138000, holds through 139000, and
returns to the previous course by 142000. The modified segments pass the same
240-unit swept mesh-clearance check. Larger vertical and horizontal candidates
were rejected by that check. A smaller 192-unit descent also avoided the hazard,
but its full run reached only 156260.81 units with live shields, so the 384-unit
candidate was retained. Gunner engagement and all firing inputs are unchanged.

The new regression observes the exact hazard at seven update samples inside the
512-unit horizontal collision window, with its radius fully expanded to 768.
The minimum sampled vertical separation is **1119.35** units; the actual ROM
collision callback records **zero impacts** from this hazard throughout the run.
These sampled distances supplement, rather than replace, the collision checks.

| Measurement | Launcher dodge only | With fixed-hazard dodge |
|---|---|---|
| Shield at local Z=140000 | 704 | **832** |
| Fixed-hazard impacts | 1, at frame 12644 | **0** |
| Terrain hits in the modified 135000–142000 interval | 0 | **0** |
| Maximum live-shield local Z | 161199.00 | **162800.75** |

The existing engagement and launcher-volley assertions still pass: the identified
launcher fires its seven-shot volley and no large projectile hits. Two complete
hazard-dodge runs have byte-identical telemetry and hash `f1db41bef0a9c43b`.
The negative control removes only this additional offset, restores the original
frame-12644 hazard hit with unchanged pose and shield 704, and correctly fails the
new avoidance assertion. The session make targets
`test-swa-stage2-hazard` and `test-swa-stage2-hazard-negative`, path generator,
courses, traces, disassembly and screenshots are preserved outside the worktree.
The ordinary progression test still matches interpreter, silent and sound-enabled
native execution. There are no ROM, shield, timer, collision or game-state patches.

This is not yet a clean passage to 150000. The first later terrain hit is at
frame 12737, and ten terrain callbacks occur between local Z=142000 and 150000.
The ship reaches 150023.56 with shield 640 but eventually fails at frame 13636.
The next separate problem is the follower's tracking through that later
constriction, not the now-avoided fixed hazard. The shared production driver
remains unchanged.

### Clearing the frame-12737 terrain collision and handing over to a human

The first terrain hit after the fixed-hazard dodge is a real ceiling intersection,
not a wing-footprint guess. At frame 12737, the ROM's terrain query addresses mesh
`0x84c` and moves from local-to-cell Y=-5128.86 to -5105.82, crossing its Y=-5120
plane. The follower has begun climbing because its 2000-unit lookahead points at
a higher part of the planned path. Reducing lookahead alone, slowing the throttle,
and adding pitch-lag compensation did not establish a clean passage; some also
introduced a previously avoided turret-bullet hit. Those experiments were rejected.

Inspection of the supposedly clear path exposed two defects in the **session-local
offline planner**, not changes needed to the game's collision execution:

- A triangle stored as a quad with a repeated vertex has a zero-length edge.
  Counting its zero determinant as a positive sign rejected otherwise entirely
  negative-winding interior intersections. The original planned climb from
  `(-1280, -5200, 144640)` to `(-1280, -5072, 145152)` actually crosses a
  repeated-vertex ceiling triangle in mesh `0x84b`.
- Five parallel swept lines do not prove clearance around their centre. A centre
  line and its offset can remain on opposite sides of a sidewall for the entire
  segment without either line crossing it.

The checker now accepts either consistent nonzero sign while treating zero edges
as neutral. It also probes radial centre-to-offset segments at each endpoint and
the midpoint, in addition to the parallel sweeps. ROM-backed Python regressions
cover the missed triangle and a sidewall at `(-512, -6016, 145152)`. These are
conservative sampled checks, not a general continuous swept-volume proof; the
actual game's collision callbacks remain the gameplay acceptance test.

A bounded local planner regenerates the corridor from local Z=139520 to 160000
with 320-unit clearance samples, passing lower through the constriction instead
of attempting the false ceiling opening. The earlier path is preserved. Beyond
160000 it holds the final lane/altitude without claiming that unplanned tail is
safe. The successful run uses the **unchanged** calibrated Pilot follower and
Gunner engagement; no precision-controller or slower-throttle experiment is enabled.

| Check | Old hazard-dodge route | Corrected corridor |
|---|---|---|
| Original terrain hit | Frame 12737, mesh `0x84c` | No hit at that frame |
| Terrain callbacks in local Z=140000–150000 | Nonzero | **258 queries, zero hits** |
| Shield at local Z=150000 | 640 | **832** |
| Maximum live-shield local Z | 162800.75 | **174637.12** |

The new session regression requires that exact zero-hit interval, active terrain
queries and shield at least 832 at 150000, as well as all previous engagement,
fixed-hazard and launcher-volley checks. Two complete runs have byte-identical
telemetry, identical exported input recordings and hash `9d6f8542a701766a`.
The old course is an expected-failure control: it reproduces the frame-12737
intersection and fails the passage assertion. The session targets are
`test-swa-stage2-terrain` and `test-swa-stage2-terrain-negative`. The first later
terrain collision is now frame 12973, beyond the 160000 planning boundary; the
run eventually fails at 13902. This still does not complete the second objective.

For human investigation, the probe now records the entire cabinet-input prefix,
including the real Test/Service EASY setup. The desktop host's experimental
`--replay-inputs` option executes that prefix from a cold boot, without rendering
or frame pacing, and pauses for a manual handoff. The current
`build/swa-trench.inputs` stops after gameplay frame **12670**, or **16600 machine
frames**, with shield 832 before the constriction. Both native and interpreter
hosts verify its battery/work-RAM hash `c16a83c62483b8db` in about 13–14 seconds.

Pressing P resumes with the existing keyboard/gamepad controls for both seats.
The first live-input sample is neutral with no buttons held, confirming that
recorded steering and firing no longer drive the game. Desktop trace assertions
check that no game frame advances while paused, then frames advance after P.
A deliberately bad checksum refuses handoff. Replay mode skips existing
battery/EEPROM saves and suppresses saves/settings writes even after manual play;
before/after file checks also pass with the host test's `M1_SAVE` enabled.
Relaunching the command retries from the same phase; F3 remains an ordinary reset.
This replays real inputs, not a partial RAM snapshot or forced game state.

Manual play can be kept with `--record-inputs FILE`, valid only with
`--replay-inputs`. Each live frame's cabinet inputs are captured at the same
point the replayed rows are applied: after the host has read the controls and
immediately before the frame runs. That point is shared by paced frames,
run-ahead frames and Tab fast-forward. The file holds the prefix plus those live
rows and the RAM hash after the last one. It is written on P (pause), quit, and
a reset, which ends the recording. The output name must be new: a file that is
already there is refused at launch, after a pause had once written over a
checkpoint the player meant to keep. A hidden 578-live-frame recording replays to
its hash `5b97ea5c5f687609` on both native and interpreter hosts, so a player's
run becomes a new, verifiable handoff point.

From that handoff a human player completed the trench objective. Replaying the
recording, the shield fell from 832 to 576 about a third of a second after the
handoff, through `0x0c7f33` (a moving collision sphere such as a projectile), then three
`c736b` hits followed. Subsequence 5 became 6 at machine frame 17,961, near trench
Z≈576,500, with no fire button held in the preceding six seconds; this suggests
reaching the trench's end completes it, though the ROM condition is not yet traced.
The next stage began at 19,394 and its flight at 22,016. A second recording
continued from there. Two `0x0c7f33` moving-collision hits at 22,330 (384→1) and
24,174 ended in the continue offer at 24,644. These were not surface collisions:
the ship was near or at Y=2000, the highest altitude it reached in that stage. A third recording took the continue;
it restarts that stage with shield 1792 and the score reset, after its briefing
(subsequence 1→2→3). Its flight took eight more `0x0c7f33` hits, again mostly at
Y=2000, before the next continue offer.

Further attempts at that stage ended the same way, once with the timer at zero and
256 shield left. Its briefing sets the objective: 24 TIE fighters in 200 seconds
(6015 in the game's 1/30 s units); that run destroyed 18. Ground targets add score but
not to the count, and no RAM counter of the TIEs left was found. The difficulty was
already at its easiest. Since a cabinet has two seats, a Gunner assist was added
(`src/games/swa/gunner_assist.h`). Native code skips the interpreter's instruction
hooks, so it reads only RAM, with `peek16`, which never triggers watch hooks:
- the camera's rotation, nine floats stored column by column at `0x40f570`, and the
  coming frame's eye at `0x40f700`, both fitted against the interpreter's projected
  objects;
- the shootable objects' list, from `0x40ed04` up to the pointer at `0x40ed00`. Each
  entry has its world position at +0x0c and its handler at +0x24. The list also
  holds the surface's static guns and towers (handlers such as `e0f62` and `e161a`)
  alongside the flying ones (`cc9f9`, `ccfde`, `cb2a8`, `cbd69`, `cc3a0`). The assist
  therefore aims only at entries whose position changed within the last second.

The nearest moving target within 500–20,000 units of view depth is projected onto
the Gunner stick: `127 - 141.8·x/z` and `127 + 215·y/z`, the reticle moving 1.41 and
0.93 pixels per step, inverted. The laser is held while a target is in reach. It
writes only the analog channels and the I/O bits, before the frame is captured, so
recordings hold its inputs. Replaying the continued attempt with PILOT & GUNNER
kept on the mode menu and the assist on, the stage objective completed at frame
34,742 (subsequence 3→4→5, time bonus), with 1,536 shield at 38,400. The same run
without it died at 37,211. `--replay-until N` hands over after a recording's first N
frames, so a player can rewind to before a mistake; replayed prefixes are exact by
determinism. A run that used it and the assist (30,000 frames replayed, 29,464 live)
verified on replay with the assist off.

With those, a human player finished the game. Phase 3 completed with the assist
at 31,872 frames. The Death Star stage that follows starts with a surface trench,
then a tunnel whose walls and fins cost 64 shield on contact. The ship can stay
wedged against a fin and take one such hit every 60 frames; the collision runs on
the TGP's own program, so this is taken to be the original behaviour. The tunnel
ends at shooting rings guarding the reactor. One attempt died there and took the
game's continue, which restarts the stage with shield 1792 and 200 s. The reactor
fell at frame 54,561 (subsequence 7→8), followed by the escape, the Death Star's
explosion and the "Congratulations!" ending. That cold-boot recording, 57,549
frames with RAM hash `1e390d36a3331dbe`, replays exactly. It used only cabinet
inputs, with no RAM writes or cheats.

That recording, of which the earlier ones are exact prefixes, is committed as
`tests/games/swa/recordings/manual-trench-to-continue.inputs`; it was the
continued attempt's 38,123 frames when last replaced. Each replacement was an
exact extension of the previous file, so earlier checkpoints remain asserted. `test-swa-recording` replays it on the
interpreter silently and on native code with sound. It asserts the three handoff
checksums and the exact list of sequence/subsequence/shield changes from the
trench handoff on. Its first native run fell back to the interpreter 1,569 times:
the scripts never reached the trench's or next stage's code. The same inputs
became SWA's `recording` scenario for `model1cov` and both lockstep tools. Coverage
plus two runtime-entry passes, using the shipped map as a hint so no entry was
lost, added 101 entries and 1,313 edges, removed none, and left zero fallbacks.
The continued attempt then fell back 62 times; one more coverage pass added 3
entries and 338 edges. Since the recording is 38,123 frames, SWA's coverage and
lockstep lengths became 38,400 for all six scenarios.

The finished game's recording shares the first one's inputs up to the continue offer at 24,755,
then takes a different continue; the later handoffs it was played from (44,637 and 50,523) are
exact prefixes of it too. It is committed as
`tests/games/swa/recordings/manual-complete-game.inputs`, and `test-swa-recording` replays both
recordings. On the interpreter its six handoff checksums and 72 game-state changes matched at the
first try. Natively it fell back 7,476 times, from `0c8dce` on: phase 3's completion, the Death
Star and the ending had never been recompiled. A `complete` coverage scenario replays it for
57,600 frames (`swa_COV_FRAMES_complete`; the Makefile's coverage rule takes a per-scenario
length). Four regenerations, each adding the runtime entries the last run noted (2,448, then 144,
then 7,489 as newly native callers reached further, then 2), left zero fallbacks. The map grew
from 14,420 entries and edges to 16,764 (1,524 functions). The same scenario was then added
to both lockstep tools at its own length (`swa_LOCK_FRAMES_complete`), alongside the six
38,400-frame scenarios. Native code and the generated C++ were identical to the interpreter
through all 57,600 frames, so the Death Star stage and the ending are compared frame by frame.

The host/parser tests, ordinary desktop-startup smoke test, SWA controls test,
gamepad test and sound-enabled progression regression pass. The latter still
matches interpreter, silent and native execution. Evidence, rejected candidates,
corrected planner and recording are preserved in the session; only the gated
replay facility is added to the desktop host. Production scripted progression
and the game's collision emulation remain unchanged.

### The name-entry priority bug scripts did not notice

A manual screenshot showed the rotating yellow name-entry letters broken into fragments over
the background photograph. The renderer's above-HUD pass (display-list command `0x41`) used a
brightness mask made from the whole composited picture. That included the photograph: bright
background pixels were incorrectly restored over foreground letters, while dark areas let them
through. A gameplay-state test could pass with that picture.

The mask now samples only the composited category-1 HUD tile layers (7, 5, 3 and 1). Bright HUD
features still mask foreground polygons, preserving cockpit struts and radar lines; the photograph
and ordinary 3D scene cannot. This fixes the mask's source without claiming the remaining
brightness-based rule is a hardware-accurate priority model.

The reproduced name-entry frame regained 3,802 pixels, and a sampled gameplay/cockpit frame was
pixel-identical before and after. Synthetic tests cover bright backgrounds, bright HUD features
and dark HUD pixels at 1x/2x and 4:3/16:9. The ROM-backed test reaches real name entry at frame
15750 and checks at 1x and 2x that the photograph does not mask its yellow letters. SWA, VR and
VF gameplay regressions passed, and the desktop hosts were rebuilt.

### SWA's model detail (LOD)

Display > LOD defaults to Off and persists as `lod = off`, as in VR. Session probes that logged
the display-list objects while the camera approached showed ship and scenery models stepping
through up to five versions with distance. Three drawing routines choose them the same way: TGP
command `0x22` returns a distance, read by `in.w [FP], R0` at `0CDFD8`, `0D1D38` and `0E7FD3`;
anything above `0x6FFF` is culled; otherwise the value shifted right by 5 indexes the byte table
at `23FE6E` (level 0 below `0x180`, 1 from `0x180`, 2 from `0x2A0`, 3 from `0x460`, 4 from
`0x700`), and the level picks an entry in the object's per-type model table at `CA81E`.

With LOD off, `copro_result` returns 0 for an in-range result read at those three addresses, so
the routine takes level 0, the nearest and most detailed model. Out-of-range values still cull,
the visibility test (command `0x32`) is untouched, and the distance is not stored: only the
display-list entry changes. A list entry names a model rather than copying its polygons, and the
renderer has no polygon cap, so unlike VR there is no submission budget to bypass.

Star Destroyers choose by rank instead, and that choice is kept. `0DBFF6` sorts them by distance
and stores each one's level in `3A[object]` from the byte table at `DC0C2`: the nearest gets -1
(the near model, with separately drawn hull parts), the next two 0, the rest 2, and `7F` hides
one. The first three also join a list at `6D80` the game uses, so the stored byte has to stay.
A read hook for 8- and 16-bit data reads by a range of instructions changes only what the
drawing routine reads at `0DC336` (`movz.bw 3A[R20], R17`): -1 unless the byte is `7F`. The
routine leaves the level in R17 for `DCE13`, which counts down the near Star Destroyer's 14 turret
timers and fires their shots only for -1. For a Star Destroyer drawn near only by this setting,
the hook makes the turret enable mask, read at `0DCE68` (`and.h 44[R20], R0`), read all set:
the game's own path for a disabled turret, which writes nothing. Each hooked read must be the
operand itself (`R20` plus the field's offset): an interrupt taken at the instruction reads its
vector with the same PC, and changing that read jumped the CPU into the program ROM's data.

The same routine then gives every Star Destroyer level 2 while the game's mode byte `7009[R25]`
is 2. That mode covers the attract mode's scenes, including the start of its X-wing demonstration,
so with only the hook above those Star Destroyers stayed simple. With LOD off, the routine's read
of the byte at `0DC341` (`cmp.b #2, 7009[R25]`) reads 0. The turret mask also reads all set in that
mode, because the game never runs the turrets there.

The large Star Destroyers of the fleet that `0D6540` draws (in the attract mode, and the one
`0EEC62` sets up) use a third scheme. Two TGP results, shifted right by 15 and capped at 3, pick an
entry in the table at `D6E12`. The routine stores the entry in the object: the model at `2[R20]`
and its data at `6[R20]`. `C1869` then draws it. The far entry is `23C01A`/`53698` (29 polygons)
and the near one `23C016`/`5360F` (197). One ship of the fleet, the one whose index matches
(`D662B`), is drawn in full instead: its stern `23C022`/`53777` (1,237 polygons, with the lit
engines) and then its hull `23C01E`/`536AD` (305), with the same matrix. The near entry was still
much plainer than that ship, which is what the third-person asteroid field of the attract mode
showed (from frame 33,300), its big Star Destroyers next to the X-wing.

With LOD off every fleet ship is drawn as that full ship. The object keeps what the game stored.
Only `C1869`'s 32-bit reads of those two fields, at `0C18AF` and `0C18AA`, return the hull
instead of either entry. This uses the existing `read32_value` hook, over RAM A, where the
objects are. `matrix_objects` then adds the stern to that matrix command, written at `0C18C1`,
as an extra drawn with the command's own matrix, so smooth motion moves it with the hull. All four
models share one frame: the three hulls span about ±3,050 in X and −5,150 to 4,900 in Z, and the
stern the rear of that, Z −5,120 to −3,951.

The game logic is unchanged, but its timing is not quite: more models take the CPU longer, so
the point within the game's update where a frame ends can move. At about one frame end in a
hundred where Star Destroyers are on screen, some object bytes still hold their previous values,
and the stack and the display list's end pointer differ more often. Each difference is gone by
the next frame end. No difference persisted in the attract mode or in either recording, through
the whole game. A session probe also compared the game RAM (without the stack and the end
pointer) each time the main loop reached its wait for the next frame (`0C067C`), with LOD on
and off. All 5,900 such points in 12,000 attract-mode frames matched. Input replay, and recording from it, therefore use the arcade's models: a
recording's RAM checksum covers the stack.

`make test-swa-recording` replays both human recordings on the interpreter with arcade LOD and on
native code with LOD off. Every game-state change matches at the same frame, the interpreter's RAM
checksums match exactly, and native code's game RAM (all but the stack and the display list's end)
matches the interpreter's at each checkpoint. About 17,500 detail results are replaced on the
native run, and Star Destroyers are drawn near about 8,850 times.

The Death Star's surface has its own distant detail, which the hooks above do not reach: of the
four cell layers described under the draw distance below, the two farther ones read a coarse map
(`B40AE`, cells of `1 << 12`, mask `0xF`) instead of the fine one (`B0962`, cells of `1 << 11`, mask
`0x1F`). A session probe found that each coarse cell's model is a simpler version of exactly the 2 x 2
fine cells it covers (fine cells 2x and 2x + 1 by 2z and 2z + 1). The coarse cell's centre sits half a
fine cell past theirs. With LOD off, `matrix_objects` attaches those four fine models, at their
offsets and turns, to each coarse cell's matrix command at `0EADAE`. The `object` hook then leaves
out the coarse map's own models, which nothing else draws: in the attract mode's surface demo the
probe counted 92,624 coarse cells replaced and the same number of coarse models left out. Draw
distance Full's added cells are coarse cells too, so with LOD off they get their fine cells
in the same way. This is renderer-only, like the draw distance. The whole-game recording's native
run gives about 1.2 million coarse cells their fine cells.

### SWA's draw distance

Display > Draw distance defaults to Full and persists as `draw_distance = extended` (or
`arcade`), the same key and values as VR's. Distant ships are not the limit: the distance cull
above (`0x6FFF`) never fires in the attract mode and fires about 1.5 times a frame over the Death
Star. The renderer has no far clip either. What ends the view is the Death Star's surface, the
only scenery drawn with a fixed reach.

`0EAA2D` draws the surface in four layers of square cells around the camera's cell. Each call
to `0EACBD` passes a band light-scale table in R28 (`99BE0`, `99C20`, `99C60`, `99CA0`). The
setup at `0EAE46` takes a cell map (R10), its mask and row shift (R4, R7) and the cell shift
(R6) from the table at `E9816`, and picks R19, the cell list for the heading (`1A[R20]`, 0 to 31)
in a table chosen by the camera's height. A list is a band count, then per band a cell count
and signed `(dx, dz)` byte pairs. A cell's map record starts with its type: 0 (flat), or 3 (turned
by the angle that follows), then texture and polygon words. The cell's matrix command is written at
`0EADAE`. After each band, `0EAE2D`-`35` read a light vector from the TGP that the band's scale
has multiplied. The farthest layer ends about 9 cells out, where flat ground meets space. Its
scales fall from 0.65 to 0, so its last bands are drawn unlit and flat grey.

Full changes only what is drawn:

- At `0EADAE`, for the far layer's first flat cell, `matrix_objects` attaches the map's cells
  past the list's reach, out to 18 cells. They are kept within the list's own direction (its
  mean, plus the widest spread and a margin of 0.7 cells), nearest first. Each is the cell's map
  model, under that cell's matrix composed with a translation by whole cells (`<< R6`) and the
  turn of a type-3 cell. The cells are cached per list.
- `copro_result` replaces a band's light vector at `0EAE2D`-`35` when its scale is below 0.45.
  The replacement is the direction the near bands gave, at 0.45.

A session probe compared every far-layer cell the game drew over the whole-game recording's
surface (243,000 cells) with its first flat cell's matrix composed this way. All matched within
`1e-6` relative error, and none of the added cells repeats a cell the game drew. The added
cells sort with the game's own by depth. The Death Star's body behind them (polygon data
`11EDC25`, from z 128,000) and the walls and trenches in front of them order correctly in the
whole-frame and the blended (60/120 FPS) pictures.

No game RAM changes. The light values and the added cells reach only the display list and the
renderer, so replays keep the player's choice. `make test-swa-recording` runs the native replays
of both recordings with Full, and every game RAM check matches the interpreter's arcade run.
It adds about 860,000 cells over the whole game and lights about 17,900 far bands.
`make test-swa-profile` checks the added-cell selection without the ROM.

#### The asteroid field

The asteroid field repeats every 131,072 units in X, and in Z while `77C4[R25]` bit 15 is set
(the attract mode from frame 6969 on). The game keeps one copy of each object in the period
around the camera (`7724`/`7728`, or a fixed point before mode 3):
- The asteroid handlers `DB200`/`DB502` rebuild C/14 from 30/34 every frame.
- The manager `DBEDC` does the same for the Star Destroyers (`DD612`). It does this only while
  its `30[R20]` and `77C4` share a bit, read at `0DBEE0`, and the mode `7009` is below 4.

The game then culls each object against the view with TGP command 0x32. Asteroids use radius
1,024, read at `0DB278`, `0DB3C6`, `0DB574` and `0DB672`. Star Destroyers use radius 8,000,
read at `0DBF96`; a culled one gets `48[obj]` = −1, so its detail byte is 7F and it is not
drawn. This causes two kinds of pop-in:
- An object leaving the box behind the camera reappears a period away in front of it, about
  65,000 units out, in a single frame.
- A Star Destroyer is far bigger than 8,000 units, so one entering from a screen edge is culled
  while part of it is already in view (frames 8008 and 8690 of the attract mode, for example).

Full draws, in the renderer only, every repeating object within 262,144 units (2 periods) of
the camera:
- **Drawn objects.** At each repeating object's matrix command (`0C18C1`, `0C191B`, `0C1995`,
  `0C1A12`, `0C1AE6`, the bare `0C1A63`) and each model `C199B`/`C1A69` adds to it, the new
  model is copied to its other periods. Each copy is translated by the camera rotation
  (`7570[R25]`) applied to the period offset.
- **Culled objects.** At the view test's negative result, the object is recorded and drawn where
  it is as well as where it repeats:
  - an asteroid with its model (`6[obj]`, `[2[obj]]`) and its rotation, which is TGP 0x15 by
    −`40[obj]` then 0x16 by −`42[obj]`, scaled by `38[obj]`;
  - a Star Destroyer with the camera rotation scaled by 2.48 and the models it was last drawn
    with under its own matrix (`C1A1D`).
  - The view translation is the camera rotation of the position plus the list's view offset.
    That offset is the translation of an asteroid's (`C1869`) or Star Destroyer's own matrix,
    less the camera rotation of that object's position.
- **Parent matrix.** All copies are drawn with the stars' matrix command (model `1F46A4`, written
  at `0C18C1` or `0C1AE6` near the list's end). It is the camera rotation alone, in every list of
  the field. `matrix_objects` names that command, and the list's extras for it are replaced
  whenever more copies arrive.
  - Smooth motion blends that slot with the camera's motion. An earlier version hung copies on
    an asteroid's matrix instead, and the asteroid's tumble, blended between two lists, swung
    copies 100,000 units away visibly.
- **Fading in.** A copy beyond 196,608 units (1.5 periods) is darkened toward black, to nothing
  at 262,144 (`render_object::fade`, applied to its polygons' colours on the CPU and the GPU).
  With a hard edge instead, a Star Destroyer crossing it popped in about 40 pixels long, and
  edges further out only made the pop smaller at a higher cost.
- **View cone.** A copy whose bounding sphere (its model's farthest vertex, scaled by its
  matrix) lies outside a cone of x/z ±1.5 and y/z ±1 is not drawn. The 4:3 picture spans about
  ±0.48 by ±0.38, so this leaves room for wider pictures and smooth motion's turns. It keeps
  the cost per picture near the 1.5-period version's.
- **Before Z folds.** The stage script sets `77C4` to 1 at `C3B05` (mode 2), then to 8001 at
  `C3C51`/`C38C4` (mode 3). Until bit 15 is set, objects fold in X only, so they can be several
  periods from the camera in Z. At the switch (frame 6969 of the attract mode) the game moves
  three Star Destroyers and four asteroids a whole period in Z, one of them to about 2,700 units
  in front of the camera. The renderer starts Z-periodic copies in attract sequence 3/subsequence
  2, before the game sets its Z-wrap bits, and keeps them through sequence 3 so the ships are
  already there when the game folds their positions. The range of periods tried follows each
  object's offset from the camera rather than a fixed ±2.
  Merely enabling copies early was insufficient: the cockpit scene first appears at frame 6072,
  `DBE82` initializes the ships around frame 6097, and their normal drawing does not provide
  cached models until around frame 6140. Waiting for either initialization or cached models
  leaves a visible fleet pop. Before initialization, the renderer reads the seven placements
  directly from the manager's ROM table at `DF2EC`, including `DD550`'s Y offset from `773A`.
  It observes the manager's initialization read at `DBE87` and then uses its initialized ship
  list. Both paths draw the intact models from the same ROM records used by `DC336` and its
  component routines. Their view matrix uses the camera rotation at `7570` and eye at `7700`. This ends when the manager
  starts updating the ships, so the pre-drawn ships are replaced by the normal renderer copies
  without being drawn twice. First-time culled ships can also use those intact models instead
  of waiting for another ship to populate the cache.
- **Asteroid-cluster lead-in (Full only).** The 38 cluster placements at `DF38A` through `DF7B2`
  (exclusive), 28 bytes each, also initialize after the cockpit appears. They are pre-drawn from
  their ROM positions, scale and model before initialization. Reads of the last angular velocity
  at `DB1DD` and `DB46D` associate each placement with its initialized object. From then on, the
  live position and rotation are used; objects whose handlers change are not resurrected.
  Each display list records which clusters were already drawn or culled, so pre-drawing only
  supplies those not yet handled by the normal path.
  Both fleet and cluster pre-drawing require demo 1 at `7770[R25]`, which `C3828` alternates
  with demo 2. Sequence 3/subsequence 2 alone also identifies the Death Star demo, where these
  ROM placements do not belong. Changing demos clears the latch and cluster associations.
  The prefetch latch is disabled in Arcade, and all added copies, pre-drawing and distance fading
  pass through the Full-only matrix callback. Arcade retains the original visibility timing.
- **Full-detail rendering cost.** Removing pop-in exposes many complete ship models at once.
  Copied models cache their vertex bounds; the renderer tests their tight component boxes against
  the actual, possibly interpolated viewport. Fully clipped objects and polygons skip shading,
  while still updating the arcade's shared sort-depth register. Within
  one extras batch, copies with identical models, materials and rotations reuse polygon colours;
  distance fade still applies separately. Per-object effects bypass this colour cache. The cache
  is cleared between batches so changes to the light, palette and interpolation cannot leak.
  Fully inside copied polygons bypass recursive clipping, and cached colours also bypass normal
  transformation and normalization. Large finite-depth lists use four stable radix passes,
  preserving descending depth, foreground-last and address-based ties (including signed zero).
  Small lists and nonfinite depths keep the comparison sort.
  Lists record whether they contain above-HUD objects. The smooth compositor then chooses a
  whole picture before drawing, instead of rendering all the geometry in parts and rendering it
  again after discovering radar blips. HUD stencil snapshots and restores run in independent row
  bands using the existing worker pool.
  These avoid redundant work without substituting simpler models or reducing draw distance.

`make test-swa-wrap` checks rendered ship and cluster positions on every frame from the first cockpit picture
at 6072 through 6180, and from 6968 through 6972 in 16:9, with both LOD settings on the interpreter
and native backend. It requires ships and clusters before their objects exist, as well as before
model caching, and checks that the cluster handoff does not duplicate origins.
Every in-view ship or cluster above 17%
brightness must have a predecessor within 4,096 world units after removing camera rotation,
well below the 131,072-unit periodic offset. Each frame's game RAM also matches an arcade-distance
run using the same LOD setting; native runs have no interpreter fallback. The Arcade run must have
no periodic extras and no early ships or clusters. After switching the Full run to Arcade, its
rendered picture must match the Arcade-only run pixel for pixel.
The run continues through frame 29000 into the second attract demo, checking every 100 frames
that neither Full nor Arcade draws asteroid clusters in the Death Star scene and that prefetch
remains disabled there, on both backends and with both LOD settings.
Optimized copies also match the uncached, unbounded path pixel for pixel over the opening and
Z-fold transition, with additional 3x-scale snapshots. Bounds and the depth-sort ordering have
focused tests, including mixed-sign depths, signed zero, and the comparison/radix threshold.
Smooth pictures at 1x and 3x also match whole-frame drawing at three fractional moments across
seven opening and later-field snapshots, and above-HUD scenes must execute just one below-HUD
and one above-HUD pass, not render the whole scene twice.
A local Metal probe at 3x, AA off and smooth full-detail rendering, compared the previous and
optimized paths at frames 6142, 6250, 6972, 8000 and 9000. Opening/transition render times fell
from approximately 5.5–6.1 ms to 3.6–4.0 ms, and later-field times from 16.5–18.2 ms to
4.5–5.2 ms. Polygon counts and rendered pixel hashes matched at every sampled moment, and Metal
matched the CPU's pixels. These are rendering measurements, not whole-application FPS guarantees.

A session probe fitted the rotation to every drawn asteroid over 37 matrices (total error
`3e-4`). Every finished list of the attract mode's field (frames 6100–9700) has the stars. A
second probe compared each list's drawn objects, copies included, with the list before's,
turned by the stars' matrix. With Full, no Star Destroyer or asteroid appears on screen at more
than 17% brightness without being in the list before; the only objects that do are the game's
own TIE fighters, explosions and debris. No game RAM changes, and `make test-swa-recording` checks that copies of both kinds are
drawn over the whole game.

### Building, running and testing SWA

The US `swa.zip` set has a registered profile, checked ROM tables and a host that recompiles the game
to native code when it starts, as Virtua Racing's does:

```sh
make swa-regions                   # write the development regions to build/swa/
make swa                           # build build/bin/swa (native recompilation at startup)
make test-swa-profile test-swa-romset
make test-swa-boot                  # two 1800-frame cold boots; needs model1io.zip too
make test-swa-controls              # buttons and axes against the game's own input test
make test-swa-dsb                   # the Digital Sound Board, stubbed and in the attract sequence
make test-swa-gameplay              # Rebel Attack (both seats) and Training, to game over and back
make test-swa-video                 # name-entry priority and real-input destroyer crash regression
make test-swa-recording             # recorded human runs: continues, and the whole game to its ending
make swa-coverage                   # record those runs as hints for the recompile
make swa-native-lockstep            # native code against the interpreter, all scenarios, the whole game
make swa-lockstep                   # the same for the generated C++
build/bin/swa --arcade --scale 1 --aspect 4:3
```

Keep your own `swa.zip` and `model1io.zip` in `roms/`. The runtime ZIP loader and the development
loader both place the program at `0xf80000`, `0x000000` and `0x080000`, and reload the byte-swapped
sound program at `0x80000`. The TGP program, polygon/data ROMs and DSB Z80/MPEG ROMs are also loaded
and checked. These tables target MAME 0.280's US set, including `315-5711.bin` with CRC32
`c5ddb8fc` (`BAD_DUMP` in that release); loading it is not a claim of verified geometry
compatibility. Newer sets with a different TGP checksum and the Japanese `swaj` program are
not supported by these tables.

#### Replay to manual play

The SWA desktop host accepts a recorded cabinet-input prefix. With the current
session-generated trench recording (not shipped with the game):

```sh
build/bin/swa build/swa --replay-inputs build/swa-trench.inputs
```

It cold-boots, fast-forwards those inputs without rendering, verifies the final
battery/work-RAM checksum, and **pauses at the handoff**. Press **P** to take over
both seats with the usual keyboard or gamepad controls; no scripted steering or
firing remains active. Esc cancels during replay and opens the menu afterward.
Relaunch the command to retry the same phase; F3 is an ordinary game reset.

To keep your play, add `--record-inputs`:

```sh
build/bin/swa build/swa --replay-inputs build/swa-trench.inputs --record-inputs build/swa-manual.inputs
```

The output file holds the replayed prefix plus every live frame you play, with the
RAM checksum after your last frame. It is written each time you pause with **P**,
when you quit, and when a reset (F3 or the menu) ends the recording. Replaying
that file with `--replay-inputs` reproduces your run exactly and hands over at the
point you stopped, so a checkpoint can be extended by recording again from it.
Pause right after an interesting moment (a hit, a target, the end of the trench) to
mark it; the frame count in the header tells where you stopped. Each pause updates
the same file, so copy it to keep an earlier mark. The output name must be new:
`--record-inputs` refuses a file that is already there, so an earlier recording (or
the one being replayed) is never written over.

To rewind, add `--replay-until N`: only the recording's first N frames are replayed
before the handoff, e.g. a few seconds (60 frames each) before a fatal hit. A prefix
has no checksum of its own to verify; a new `--record-inputs` file from it gets its
own checksum and replays exactly like any other.

```sh
build/bin/swa build/swa --replay-inputs build/swa-continue-try2.inputs --replay-until 30000 --record-inputs build/swa-retry.inputs
```

One player can fly with both seats firing: **Esc → game menu → Gunner assist: On**
(or `gunner_assist = on` in `settings.ini`). In PILOT & GUNNER mode, the default on the
start and continue mode menus while the Pilot stick is centred, the Gunner's stick and
laser then aim at the nearest moving enemy in reach. It drives only the cabinet's
inputs, which are recorded, so recordings made with it replay without it.

**Esc → Display → LOD** defaults to Off (`lod = off`): ships, scenery, the Death Star's
surface and every Star Destroyer keep their most detailed models at every distance. On
restores the arcade's simpler distant models. The game plays the same either way.

**Esc → Display → Draw distance** defaults to Full (`draw_distance = extended`): the
Death Star's surface reaches further, and in the asteroid field asteroids and Star
Destroyers no longer pop in. Arcade draws what the cabinet does. Only the picture
changes, so replays keep the choice.

Replay mode ignores existing battery/EEPROM saves and never writes saves or
settings, including after manual play. It uses arcade mode, 4:3, 1x and whole-frame
rendering for reproduction; normal launches are unchanged. A missing, malformed,
wrong-game or checksum-divergent recording is an explicit error, not a handoff.
It cannot be combined with `M1_SCRIPT`. This is input replay, **not a save state**:
the game must still execute every preceding frame, and recordings may need
regenerating when emulation changes.

The text format starts with `M1INPUT 1 swa FRAME_COUNT RAM_HASH_HEX`, followed by
one row per machine frame: three decimal pressed-bit bytes, then eight decimal
raw ADC values (`-1` leaves a channel unchanged). It permits no memory writes.
The current recording hands over after gameplay frame 12670, with shield 832,
just before the next constriction; replay takes about 14 seconds on the development
machine. The session-local route probe can export these prefixes, including the real
Test/Service inputs that select EASY settings. `make test-host-input-replay`
checks the format's bounds, truncation and rejection paths.

**This is not finished support yet.** Scripted games play from coins to game over (below), natively
and on the interpreter, but camera/mode RAM identification remains to be done: those addresses are
left unset, and the original boot sequence is shown rather than skipped. Release packaging remains
VR-only.

SWA draws a new 3D picture every two video frames (30 a second) throughout play, like VR. It
starts at Frame rate 30 (Arcade), each picture as the game drew it; choose 60 or 120 under the
menu's Frame rate to blend between them. The blending pairs the parts by their place in the list alone (no
table of the ships or the camera yet). Its parts are taken as separate ships and shots: copies of
a model pair by the least total move, each part moves as one rigid motion, fast parts (a shot or
fighter passing the camera) are drawn as the game has them, and a shot appearing is no cut.
Much of play is still drawn unblended. Replays always run at 30 FPS.

Foreground polygons (display-list command `0x41`) are masked only by the HUD layers' bright
features, not by the background picture or the regular 3D scene. This keeps the name-entry letters
in front of the photograph while preserving cockpit/radar features. `test-model1-video` checks
this separation at 1x/2x and 4:3/16:9; `test-swa-video` also reaches the real name-entry screen
and checks that the photograph does not cut holes in the letters. It also reproduces the
destroyer-stage malformed object through real cabinet controls and verifies execution continues
past the former crash. Object decoding bounds-checks texture and polygon accesses and reports
invalid requests explicitly; it does not patch ROM execution or claim second-objective completion.

#### Native recompilation

SWA runs its program from the low ROM copy (`0x0c0000`–`0x0fffff`), so the code analyser
(`src/rt/analyse.cpp`) treats `0x000000`–`0x0fffff` as code as well as Model 1's usual ranges; VR's
and VF's recompiles are unchanged by it. `assets/games/swa/swa_map.txt` is made from the scripted
runs (`make swa-coverage`) and the entries the runtime noted when native code fell back to the
interpreter (`build/cov/swa/runtime_entries.txt`). Most of those are pseudo-calls: the game pushes a
return address and branches (and its reset pushes one and runs `RETIS`), so the code returned to has
no caller in the native code and must be an entry of its own. The map was regenerated until the
lockstep noted no more. `swa-native-lockstep` and `swa-lockstep` run seven scenarios with
`--strict` (no fallback), comparing registers, RAM and rendered frames with the interpreter. Six
run to frame 38,400: the five scripted ones and `recording`, a human's recorded run, whose length
set that figure. The seventh, `complete`, is a human's recorded run of the whole game and runs to
frame 57,600, through the Death Star and the ending. Coverage replays the same seven.
`test-swa-gameplay` plays all five scenarios natively with sound. Code the scripts never reach
(other missions' branches, the test menu) may still fall back to the interpreter, which is correct
but slower, and is noted for the next map.

The shipped map also includes entries collected during manual play. These remove the recorded
fallbacks, but the console log does not record inputs, so it cannot replay or validate that manual
session. All scripted scenarios are rechecked in strict native lockstep after expanding the map.

#### Controls

The ROM's input test names the seats **Pilot** (player 1) and **Gunner** (player 2). Both seats'
buttons are on the I/O board's player 1 byte, so the host routes player 2 there rather than to the
player 2 byte; there is one Start button (the Pilot's, `0x10`), and Start 2 is never read.

| Control | Pilot (player 1) | Gunner (player 2) | Keys P1 / P2 | Pad |
|---|---|---|---|---|
| Flight stick X / Y | ADC 0 / 1 | ADC 4 / 5 | arrows / IJKL | left stick, d-pad |
| Throttle | ADC 2 | - | W faster, S slower | right stick Y (up: faster) |
| Laser | `0x01` | `0x04` | Z / U | RT |
| Torpedo | `0x02` | `0x08` | X / O | LT |
| View (VR button) | `0x10` | - | C | Y |
| Start, coin | system `0x10`, coin slots | same Start | 1 / 2, 5 / 6 | Start, LB |

Analog values are scaled to the factory calibration that the input test (page 2) reads as ±100:
sticks 27–227 centred on 127, throttle 28 (FAST) to 228 (SLOW) centred on 128. Stick X is reversed
on the hardware (low values are right), as MAME's `PORT_REVERSE`; stick Y is not, so pushing forward
dives, as on the cabinet (Game > Invert flight Y swaps that). MAME's throttle range (28–200) stops at
-72 on SLOW; the host uses the full calibrated travel. Values outside the calibration make the input
test show INVALID VALUE. The test menu's own calibration (page 2, Start) has not been exercised;
the host's ranges assume the factory values. The `SWA_*`
environment variables of `model1run` set raw port values.

The V60 interpreter and ROM-backed MB86233 reach polygon-rendered attract mode with the existing
sound board enabled, without a SWA HLE implementation. `test-swa-boot` checks two deterministic
1800-frame cold boots, FIFO word conservation and queue bounds, no empty-result reads or TGP
execution-limit hits, scanline/timer IRQ cadence, and audio pacing. With the supplied set, the
first polygons appear at frame 1106; by frame 1800 the TGP has consumed 24,559 input words and
returned 24,750 output words, with both queues drained.

#### Sound

SWA keeps the Model 1 sound board (68000, YM3438, two MultiPCMs) and adds Sega's Z80 Digital Sound
Board, which plays the music as MPEG-1 Layer II from its own ROMs (`src/machine/dsbz80.*`, after
MAME's `dsbz80.cpp`; the decoder `src/machine/mpeg_audio.*` is MAME's). As in MAME, every byte the
68000 sends on its serial link also reaches the DSB's i8251 (31.25 kbaud, one byte per 320 µs), and
the Z80 program picks out its commands; it sets start/end addresses (latched while a stream plays,
for loops and multi-part songs), volume and pan, and the stream plays at 32 kHz. Its output is
resampled into the host's 48 kHz mix at MAME's gain (1.0), under the music volume. The board is
created only when the `mpegcpu`/`mpeg` regions are loaded, so VR and VF are unchanged.

The DSB plays the Star Wars theme in the attract sequence: command `01` starts a stereo 32 kHz stream
at `0x334000` with the opening crawl's STAR WARS logo (frame 3202 from a cold boot; the logo is up
at 3204). Before that the sound board plays the Star Destroyer fly-by on its own chips, and the DSB
only receives stop commands. The DSB's samples reach the 48 kHz mix through a linear resampler kept
two source samples behind the Z80 (it delivers them at instruction boundaries); without that lead,
the last interpolation of a scanline's slice often ran past the newest sample, distorting the
music (about 20 dB signal-to-error).

`test-swa-dsb` checks the UART's pacing, receiver enable and overrun and the register latching on a
stub program, then runs the attract sequence: the theme must start with the logo, MPEG frames must
decode at 1152 samples per 32 kHz frame, and, with the other boards silent under the theme, the
mixed output must match the stream's own decode at the board's volume (over 60 dB; it measures
about 68). The stream's decode also matches ffmpeg's MP2 decoder within 2 LSB. Unverified against
hardware: the Z80 clock (MAME's 4 MHz guess), the volume register's top bit and the ports
`0xea`/`0xeb` the program also writes (ignored, as in MAME).

#### Scripted play

`src/games/swa/script.cpp` is SWA's scripted run for `model1run` (`M1_SCRIPT=1`), `model1cov` and the
tests. From a cold boot it inserts two coins during the attract's Star Destroyer fly-by (frames 1300
and 1340; SWA needs 2 credits), presses Start to open the mode menu, confirms PILOT with Start and the
Rebel Attack mission with Start, then flies with a seeded random stick and throttle, firing the
lasers in bursts and a torpedo every five seconds. The `gunner` scenario inserts a third coin
(PILOT & GUNNER needs 3 credits), pulls the stick back to pick that mode and fires the Gunner's
buttons too. From frame 3700 the Gunner's stick repeats right, left, down and up, holding each for
120 frames with the other axis centred. Channels 4/5 carry these inputs independently of the
Pilot's seeded controls on 0–2; unused channels remain unchanged.
The `training` scenario stays in PILOT mode, then pulls the stick back on frames 1510–1539 to
select Training before confirming the mission. It uses the same seeded flying and firing.
The `continue` scenario starts a solo Rebel Attack, waits for the failed mission's continue
offer, inserts one coin, presses Start for YES and confirms PILOT with Start. It reads the
reference machine's state to time those inputs; it never writes game RAM. A solo continue costs
one credit, unlike the two-credit fresh game. The resumed attempt must consume the credit,
reset the score, start a fresh 180-second clock, count down and score again. The second continue
offer receives no further coins or Start presses and expires before final game over and ranking.
With seed 12345 the first offer appears at 14506, the fresh clock at 15001, the second offer at
27045, game over at 31775 and attract at 32718.

With checks on, the original scenarios must be credited, select the expected mission (the initial clock shows
180 s for Rebel Attack, 120 s for Training), start the flight (the clock counting down), score
(and, for `gunner`, the Gunner score and all four aim directions), reach game over and return to attract. Rebel Attack must leave
the final score in the rankings. With seed 12345 its flight starts at frame 3206; the solo run reaches
game over at frame 16782 and attract at 17725. The moving-Gunner run reaches those at 19614 and
20557. Training instead must leave the rankings unchanged: this run
has no name entry, reaches game over at frame 10732 and returns to attract at 10736, after starting
the flight at 3506. The checks read battery RAM found by watching runs against the
screen, not from the program's code:

| Address | Observed meaning |
|---|---|
| `0x40f008` | sequence: 1–2 attract, 11–13 credited and playing, 14 game over and rankings |
| `0x40f009` | observed subsequence with sequence 13: 1 mode/intro, 3 opening flight, 4 next-objective briefing, 5 next stage, 12 continue offer (unset clock) |
| `0x40f0e0` | credit counter |
| `0x40f334` | time left, 16-bit, in 1/30 s (the HUD's TIME) |
| `0x40f340`, `0x40f342` | calibrated Gunner Y/X inputs, signed 16-bit |
| `0x40f410`, `0x40f510` | Pilot and Gunner scores, 32-bit BCD |
| `0x40f100` | PILOT rankings: ten 8-byte entries, BCD score and name |
| `0x40f150` | PILOT & GUNNER rankings: ten 12-byte entries, the team's total and both names |

Read-only destroyer-route tracing additionally identified the ship object through the 16-bit
pointer at `0x40f408`: its address is `0x400000 | pointer`. Offsets `+0x0c/+0x10/+0x14`
are float32 world X/Y/Z; `+0x18/+0x1a/+0x1c` are 16-bit pitch/yaw/roll angles.
The authoritative shield word is `0x40f772`. These are diagnostic observations, not cheats
or new progression checkpoints. See [the course investigation](#tracing-the-destroyer-course)
for the collision-grid trace, the experimental follower's distinct pitch/yaw control
models, and its reproducible live-shield passage beyond local Z=130000.
[Damage-source tracing and experimental target engagement](#separating-trench-damage-and-improving-target-engagement)
raise the shield word at that checkpoint from 320 to 832.
[Tracing and dodging the larger launcher volley](#avoiding-the-large-launcher-volley)
preserves that shield through local Z=135000 and extends live-shield progress to
161199 units, using a mesh-checked Pilot descent rather than suppressing the shot.
[Avoiding the next fixed expanding hazard](#avoiding-the-fixed-hazard-at-frame-12644)
extends the descent, retains shield 832 through local Z=140000, and reaches
162800 units with live shields.
[Correcting the constriction planner](#clearing-the-frame-12737-terrain-collision-and-handing-over-to-a-human)
then gives zero terrain hits through Z=150000 with shield still 832 and maximum
live-shield progress of 174637 units. Later terrain collisions remain: this probe
still does not complete the second objective and is not part of the shared scenario.

A human player has since completed the trench objective from that handoff, recorded with
`--record-inputs`. `test-swa-recording` replays the committed recording
(`tests/games/swa/recordings/manual-trench-to-continue.inputs`) on the interpreter and on native
code with sound. It requires the RAM checksums at the trench handoff (16,600 frames), next-stage
arrival (21,635), continue offer (24,755) and the continued attempt's end (38,123), and every
sequence, subsequence and shield change from the handoff on: the trench objective at frame 17,961,
next-stage flight at 22,016, two moving-collision hits and the continue offer at 24,644, then the
continue (that stage again, full shield), eight more hits and the next offer at 37,987. The same inputs
are the `recording` scenario of the coverage and lockstep tools, so the native map covers the
manually reached code with no interpreter fallback.

The test also replays a second recording,
`tests/games/swa/recordings/manual-complete-game.inputs`: a player finishing the game from
the same continue offer, using `--replay-until` rewinds and the Gunner assist (both are only
cabinet inputs in the file). It checks the RAM checksums at 16,600, 21,635 and 24,755 frames
(shared with the first recording), at the Death Star's continue offer (44,637), in its tunnel
after continuing (50,523) and at the ending (57,549). It also checks all 72 sequence,
subsequence and shield changes from the trench handoff on, including phase 3's objective at
31,872, the Death Star's tunnel at 35,902, the reactor at 54,561 and the ending at 56,742.
Both recordings must replay with no interpreter fallback; the test takes about two minutes. The
`complete` coverage scenario replays the same inputs, so the native map covers the whole game.

`test-swa-gameplay` runs the original three scenarios to frame 22,000, `continue` to 36,000
and `progression` to 10,000
with sound. In the default run it also checks
that the DSB plays the in-game music during the flight (a looping stream outside the attract theme;
it loops about every 3,340 frames) and that a silent run matches one with sound. The scripted Rebel
Attack runs end by the clock; Training ends earlier. Later stages, the trench run, successful
mission completion and team-mode continuation are not comprehensively checked. Last, it plays
all five scenarios again on native code from the shipped map, with no interpreter fallback.
The continued run must have the same final battery/work RAM hash as its interpreter run.
Negative tests withhold either the continue coin or Start: neither may resume flight or pass
the scenario. For a focused continuation check:

```sh
make build/bin/swa_gameplay_test
build/bin/swa_gameplay_test build/swa --continue
```

The gameplay test also compares five controlled runs: centred Gunner, then each axis at its two
extremes with the Pilot centred and firing released. At frame 4000 the reticle must move over
100 pixels horizontally or 70 vertically in the correct direction, without moving the other axis
or changing the picture outside the old/new reticles. The observed shifts are ±141 and ±93 pixels.
This checks independent in-game aiming, not just score or ADC delivery. To run this focused check:

```sh
make build/bin/swa_gameplay_test
build/bin/swa_gameplay_test build/swa --aim
```

The shared `progression` scenario completes the opening Rebel Attack objective.
It selects PILOT & GUNNER, holds the Pilot centred at fast throttle, fires both
lasers and pulses both torpedoes every 90 frames. Every fourth frame, a simple visual heuristic
aims the Gunner at the blue target pixel nearest the screen centre, using the measured reticle
calibration. These are ordinary inputs; no scores, enemies, timers or game states are changed.
Its controls are independent of the random seed. The Super Star Destroyer briefing appears at frame 8568, followed at 9574 by
the next stage and a 60-second bonus (raw clock 2630 to 4430). The next-stage clock must then
count down (first observed at frame 9576). These checkpoints are required by `--scenarios`,
with a minimum of 10,000 frames, instead of requiring game over or a ranking entry.
The gameplay test runs it on the interpreter with and without sound and on native code with
sound, requiring identical final battery/work RAM hashes and no native fallback. Repeated
drivers with different seeds must emit identical controls. A matching run without firing
must not reach either success transition within 10,000 frames and must fail the scenario.
Coverage and strict native/generated-C++ lockstep include `progression` for 38,400 frames,
including playback after its required objective has completed.
This verifies one successful objective, not completion of Rebel Attack. The blue-pixel aiming
heuristic can also target engine details; its success checks have been validated at 1x, 4:3.

```sh
make build/bin/swa_gameplay_test
build/bin/swa_gameplay_test build/swa --progression
# --objective remains an alias for the same focused checks.

make build/bin/model1run
mkdir -p build/swa-progression-frames
M1_SCRIPT=1 build/bin/model1run --game swa --scenario=progression \
  build/swa 10000 600 build/swa-progression-frames
```

For headless screenshots and FIFO diagnostics:

```sh
make build/bin/model1run
mkdir -p build/swa-boot
M1_TGP_TRACE=1 M1_WAV=build/swa-boot/audio.wav \
  build/bin/model1run --game swa build/swa 1800 300 build/swa-boot
```

The runner enables the sound board only when `M1_WAV` is set. The game runs the same without it
(the main CPU's bytes to the board are dropped): `test-swa-gameplay` checks that a silent run's RAM
matches one with sound after 8000 frames of play, so the silent coverage runs follow the same game. FIFO input waits are normal and resume when more data arrives. The TGP
still runs on demand, not on a cycle-accurate parallel scheduler: these checks establish a
repeatable boot and the runtime's timing, not hardware-accurate TGP latency or geometry.

### What the tests establish, and what remains

The main local checks are:

- `make test-swa-profile test-swa-romset`: profile and loader layout.
- `make test-swa-boot`: repeated interpreter boot, FIFO invariants, IRQ timing and audio pacing.
- `make test-swa-controls`: button routing and analog ranges against the game's input test.
- `make test-swa-dsb`: serial receiver, MPEG controls, real music trigger and audio comparison.
- `make test-swa-gameplay`: all five scenarios with sound, silent/sound RAM agreement, and
  native runs with no interpreter fallback; also deterministic Gunner inputs and on-screen
  independent aiming, plus continuation checks with the coin or Start withheld. An
  shared visual progression driver completes the opening objective, with a no-firing control.
- `make test-model1-video test-swa-video`: synthetic HUD priority and real name-entry rendering.
- `make test-swa-recording`: two human recordings replayed on the interpreter (arcade LOD, exact
  RAM checksums) and native code (LOD off and Draw distance Full, the same game RAM), with exact game-state transitions. The first runs through the trench objective,
  the next stage, a continue and the following offer. The second plays the whole game, through
  two continues, to the ending.
- `make swa-native-lockstep swa-lockstep`: all seven scenarios against the interpreter:
  six for 38,400 frames each, and the recorded whole game for 57,600.

These are ROM-backed local checks where applicable, not evidence that every cabinet behaviour
or every mission branch has been validated. The Gunner's buttons and axes are checked in the
input test and scripted play, and a solo failed-mission continue/restart is checked.
The opening objective and next-stage entry are now checked in all three execution backends, and a
recorded human run checks the trench objective and the following stage's start. Later
objectives, mission victories and team-mode continuation still lack comprehensive checks.
Camera/mode RAM is still unidentified, so boot skipping and SWA-specific widescreen
behaviour are not established. Release packaging remains VR-only.

The useful pattern was to keep three claims separate: the program runs reproducibly, native code
matches the interpreter, and the result matches the cabinet. Each needs different evidence.
An input test, a decoder comparison, a frame-level priority regression and a CPU lockstep are
complementary; none substitutes for the others.

## Where to look

- `src/rt/native.cpp`, `src/rt/analyse.cpp`: the start-up recompiler and code discovery. `assets/games/vf/vf_map.txt` is its
  map.
- `tools/recomp/` (`v60recomp`, `emit.py`): the ahead-of-time C++ recompiler (`make rec`).
- `tools/model1lock.cpp`: the lockstep checker. Start here to see what "identical" means.
- `src/machine/`: the Model 1 board (`model1*.cpp`): memory map, TGP glue, video, sky, sound, the I/O board.
- `src/host/main.cpp`, `scene_sync.h`, `gpu_compose.h`: the host, pacing, governor, scene sync and GPU compositing.
- `src/gpu/`, `src/machine/model1_gpu.cpp`: the GPU renderer (Metal, SDL_GPU and their shaders).
- `tools/model1run.cpp`, `tools/model1script.cpp`: the headless runner and the scripted test harness. `tools/model1cov.cpp` makes
  the coverage runs the map comes from.
- `src/games/swa/`, `tests/games/swa/`: SWA's profile, ROM layout, controls, scripted scenarios and
  ROM-backed checks. `assets/games/swa/swa_map.txt` is its native address map.
- `src/machine/dsbz80.*`, `src/machine/mpeg_audio.*`: SWA's extra sound board and MPEG decoder.
- `tests/model1_video_test.cpp`: shared rendering regressions, including SWA's name-entry priority.
- `git log`: each commit message explains what changed, why, and how it was checked.
