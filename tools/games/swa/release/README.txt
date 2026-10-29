STAR WARS ARCADE RECOMP (preview)
=================================

An unofficial fan project, not made, approved or supported by SEGA or Lucasfilm.
No game files are included. You need your own MAME "swa" set (swa.zip, the US version) and the
Model 1 I/O board's program (model1io.zip).

FIRST LAUNCH
Start Star Wars Arcade Recomp. It asks for your swa.zip, checks it and copies it into its
user-data folder, so you only do this once. Put model1io.zip next to your swa.zip first: it is
copied too. The sticks and buttons go through the I/O board, so without model1io.zip the game
starts but you can't fly or fire.

On Steam Deck / Linux, put swa.zip and model1io.zip in the package's roms folder instead: Game
Mode has no file picker. Then add StarWarsArcade.sh as a Non-Steam Game and set Start In to the
extracted StarWarsArcade folder.

Keep your ROMs in a folder of your own? Add  roms_folder = <your folder>  to settings.ini, or
start the game with  --roms "<your folder>".

The macOS preview is not notarized; if Gatekeeper blocks it, Control-click the app, choose Open,
then confirm. Windows may show SmartScreen for the same reason.

CONTROLS
Two players share one cabinet: the Pilot flies and fires, the Gunner aims a second laser.
These are the defaults for a new configuration; open the menu with Esc or View/Select, then
Controls, to see or change them.
Pilot keys: arrows fly, W/S throttle faster/slower, Z laser, X torpedo, C changes the view,
1 starts and 5 inserts a coin. Gunner keys: I/J/K/L aim, U laser, O torpedo, 2 starts and
6 inserts a coin. Gamepad: left stick or d-pad flies, right stick up/down throttles, right
trigger fires the laser, left trigger a torpedo, Y / Triangle changes the view, Start starts
and the left bumper inserts a coin. Coins are only needed in Arcade play style: PC play style
is free play.

SETTINGS
Game: play style, invert flight Y, the test menu's difficulty and advertise sound settings, and
the arcade's own service menu (9 moves, F2 selects).
Cheats > Gunner assist: in PILOT & GUNNER mode, the Gunner's laser aims at the nearest enemy by
itself, so one player can fly with both seats firing.
Display: sharpness, picture shape, frame rate, LOD, draw distance and the widescreen cockpit.
Wide picture shapes widen the view without stretching the 2D art; the X-wing cockpit is
continued out to the screen edges (Display > Widescreen cockpit).
Star Wars Arcade recompiles your ROM's V60 program to native code at startup. Use --interp to
run on the Model 1 interpreter instead.

CREDITS
Built on MAME's work on the Model 1 (the V60 CPU, TGP, sound and video), Musashi, ymfm, sljit,
SDL, RmlUi and FreeType: see LICENSES.txt.
