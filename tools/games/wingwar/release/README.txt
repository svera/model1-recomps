WING WAR RECOMP (preview)
========================

An unofficial fan project, not made, approved or supported by SEGA.
No game files are included. You need your own MAME World set (wingwar.zip).
You also need model1io2.zip, which supplies Wing War's Advanced Model 1 I/O BIOS.

FIRST LAUNCH
Keep wingwar.zip and model1io2.zip together. Start Wing War Recomp and select wingwar.zip when
asked. It checks both archives and remembers them. On Steam Deck / Linux, put both archives in the
package's roms folder instead: Game Mode has no file picker. Then add WingWar.sh as a Non-Steam
Game and set Start In to the extracted WingWar folder.

Keep your ROMs in a folder of your own? Add  roms_folder = <your folder>  to settings.ini, or
start the game with  --roms "<your folder>".

The macOS preview is not notarized; if Gatekeeper blocks it, Control-click the app, choose Open,
then confirm. Windows may show SmartScreen for the same reason.

CONTROLS
Open the menu with Esc or View/Select, then Controls, to see or change bindings.
Keyboard: Left/Right yaw, Up/Down pitch, W/S throttle, Z machine gun, X missile, C smoke,
F5-F8 select views 1-4, 1 starts and 5 inserts a coin.
Gamepad: left stick or D-pad flies, right trigger controls throttle, A fires the machine gun,
B launches a missile, X releases smoke, Y / Triangle and the shoulder buttons and left-stick
click select the four views, Start starts and D-pad Up inserts a coin.

Wing War recompiles your ROM's V60 program to native code at startup. Use --interp to
run on the Model 1 interpreter instead.

CREDITS
Built on MAME's work on the Model 1 (the V60 CPU, TGP, sound and video), Musashi, ymfm, sljit,
SDL, RmlUi and FreeType: see LICENSES.txt.
