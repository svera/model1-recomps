VIRTUA RACING RECOMP (preview)
==============================

An unofficial fan project, not made, approved or supported by SEGA.
No game files are included. You need your own MAME "vr" set (vr.zip) and the Model 1 I/O
board's program (model1io.zip).

FIRST LAUNCH
Start Virtua Racing Recomp. It asks for your vr.zip, checks it and copies it into its user-data
folder, so you only do this once. Put model1io.zip next to your vr.zip first: it is copied too.
The wheel and pedals go through the I/O board, so without model1io.zip the game starts but you
can't steer or accelerate.

On Steam Deck / Linux, put vr.zip and model1io.zip in the package's roms
folder instead: Game Mode has no file picker. Then add VirtuaRacing.sh as a Non-Steam Game and set
Start In to the extracted VirtuaRacing folder.

Keep your ROMs in a folder of your own? Add  roms_folder = <your folder>  to settings.ini, or
start the game with  --roms "<your folder>".

The macOS preview is not notarized; if Gatekeeper blocks it, Control-click the app, choose Open,
then confirm. Windows may show SmartScreen for the same reason.

CONTROLS
These are the defaults for a new configuration. Saved bindings in settings.ini take
precedence and are kept when you update the app. An existing setup may still use
D-pad Up/Down for shifting.
Open the menu with Esc or View/Select, then Controls > Player 1. Keyboard: Left/Right steer,
Up accelerates, Down brakes, X/Z shift up/down, C cycles the four cameras, 1 starts,
and 5 inserts a coin. Gamepad: left stick X
steers, right trigger accelerates, left trigger brakes, A / Cross shifts up, B / Circle
shifts down, Y / Triangle cycles cameras, and D-pad Up inserts a coin in Arcade mode.
To see or change your actual controller bindings, open Controls > Player 1 > Pad buttons.
Check Coin for your Insert Coin button; the default is D-pad Up. Coins are only needed
in Arcade play style. The game starts with the configured Start control.
Controllers can be connected or reconnected during play, including over Bluetooth.
Allow about a second after the system connects it. Set Controls > Player 1 > Pad to
Any pad or your controller rather than None to enable it.
For manual transmission, hold Start on the course selection screen while confirming with Accelerator.
To choose a course with the keyboard, hold Left/Right to move gradually, release on the
course you want, then press Accelerator to confirm.

Display settings, antialiasing, sound, and saves are available from the menu. All wide picture
shapes, including 16:9 and 16:10, widen the view without stretching the HUD. Off-center HUD
elements follow the screen edges. Picture shape changes do not restart the game; the app
keeps the arcade's MONITOR TYPE at 4:3 NORMAL.
Display > LOD defaults to Off for full-detail models at every distance. Turn it On for the
arcade's simpler distant models and better performance. Draw distance controls scenery visibility.
Virtua Racing recompiles your ROM's V60 program to native code at startup. Use --interp to run on the
Model 1 interpreter instead.

LAN PLAY (EXPERIMENTAL)
Race with a friend on two computers, one player per computer, connected to the
same Wi-Fi or wired network. Both players need the same game version and ROM set.
No IP addresses or account are needed.

1. On both computers, choose PC play style, use one local player and turn Live
   Monitor off.
2. Wait for the demo or course-selection screen. Open the menu (Esc on a keyboard)
   and choose LAN.
3. One player chooses Create room. This player is the host.
4. The other player chooses Find rooms, then selects the host's room.
5. Once both players are ready, the host chooses Start linked race. Wait for the
   game to load, then press your usual Start button to race.

Choose race settings before creating the room. Both players use the host's
language, race type and course-selection settings for this session. Your own
display, sound, controls and car color stay unchanged.

Pausing: opening the host's menu pauses both players. Closing it resumes after
a 3-2-1 countdown. The host can also press P to pause or resume. The other player's
menu does not pause the race, and only the host can resume it.

Leaving: choose LAN > Leave room. If either player leaves, closes the game or
loses their connection, the race ends. The remaining player returns to a room
and can invite the other player to join again. LAN cannot be combined with
local split-screen or Live Monitor.

Room not appearing? Check that both computers are on the same network and running
the same game version. Allow local-network access or firewall permission if asked.
Try a regular home network instead of guest Wi-Fi, and disconnect any VPN.

CREDITS
Built on MAME's work on the Model 1 (the V60 CPU, TGP, sound and video), Musashi, ymfm, sljit,
SDL, RmlUi, FreeType and arcade-lan: see LICENSES.txt.
