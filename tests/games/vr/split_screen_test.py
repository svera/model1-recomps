"""Real-ROM desktop checks for on-demand split-screen cabinets."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


binary, regions = (Path(arg).resolve() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="vr-split-screen-") as directory:
    data = Path(directory)
    for region in regions.glob("*.bin"):
        (data / region.name).symlink_to(region)

    def run(options, events="", frames=120):
        env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER=str(frames),
                   M1_SAVE="1", M1_EVENTS_AT=events)
        result = subprocess.run(
            [str(binary), str(data), "--scale", "1", *options],
            env=env, capture_output=True, text=True, timeout=120)
        print(result.stderr, end="")
        assert result.returncode == 0, result.stderr
        return result.stderr

    removed_flag = subprocess.run(
        [str(binary), str(data), "--two-cabinets"],
        capture_output=True, text=True, timeout=30)
    assert removed_flag.returncode == 2
    assert "unknown option --two-cabinets" in removed_flag.stderr

    solo = run([], "10:1,40:status")
    assert "cabinet 2 compiled" not in solo
    assert "1 cabinet(s) running, 0/0 link exchanges" in solo
    assert not (data / "two-cabinets/2/nvram.sav").exists()

    settings_menu = run([], "10:escape,20:down,30:return,40:down,50:right,60:escape,70:escape,100:close")
    assert "1 cabinet(s) running, 0/0 link exchanges" in settings_menu
    assert "antialiasing = 0" in (data / "settings.ini").read_text()

    graphics_menu = run([], "10:escape,12:down,14:return,16:down,18:down,20:down,22:down,24:down,26:down,28:right,30:up,32:up,34:up,36:up,38:right,40:escape,42:escape,60:close")
    assert "1 cabinet(s) running, 0/0 link exchanges" in graphics_menu
    graphics_settings = (data / "settings.ini").read_text()
    assert "screen_filter = 1" in graphics_settings
    assert "renderer_3d = 0" in graphics_settings

    controls_menu = run([], "10:escape,12:down,14:down,16:down,18:return,20:down,22:return,24:down,26:return,28:down,30:down,32:down,34:down,36:down,38:down,40:down,42:return,44:t,46:escape,48:escape,50:escape,52:escape,54:close")
    assert "1 cabinet(s) running, 0/0 link exchanges" in controls_menu
    player2_settings = (data / "two-cabinets/2/settings.ini").read_text()
    assert "p1_key_start = T" in player2_settings

    cancelled = run(["--split-screen"], "10:1,100:t,150:f3,200:status,240:close")
    assert "join requested" in cancelled and "cabinet 2 compiled" not in cancelled
    assert re.search(r"cabinets=1 .*join=0 role=0", cancelled)

    disconnected_while_queued = run(["--split-screen"], "10:1,40:pad2,60:disconnect2,80:status")
    assert "player 2 disconnected before joining; cancelled queued join" in disconnected_while_queued
    assert "cabinet 2 compiled" not in disconnected_while_queued
    assert re.search(r"cabinets=1 .*join=0 role=0", disconnected_while_queued)

    # Player 1's course selection must not be reset by a player 2 join request.
    queued = run(["--split-screen"], "10:1,100:t,200:status,300:status")
    assert "join requested" in queued and "player 2 joined" not in queued
    assert "cabinet 2 compiled" not in queued
    assert re.search(r"cabinets=1 .*phase=10 join=1 role=0", queued)
    assert not (data / "two-cabinets/2/nvram.sav").exists()

    joined = run(["--split-screen", "--split-layout", "top-bottom"],
                 "10:t,40:p,60:f7,80:resize,100:p,120:status")
    assert joined.count("cabinet 2 compiled") == 1
    assert "player 2 joined, two cabinets running" in joined
    assert re.search(r"2 cabinet\(s\) running, ([1-9]\d*)/\1 link exchanges", joined)
    assert (data / "settings.ini").stat().st_size > 0
    for name in ("nvram.sav", "eeprom_93c46.sav"):
        assert (data / "two-cabinets" / "1" / name).stat().st_size > 0
    for name in ("nvram.sav", "eeprom_93c46.sav", "settings.ini"):
        assert (data / "two-cabinets" / "2" / name).stat().st_size > 0

    disconnected = run(["--split-screen"], "10:pad2,80:disconnect2,100:status", frames=300)
    assert "player 2 joined, two cabinets running" in disconnected
    assert "player 2 controller disconnected; returned to solo play" in disconnected
    assert re.search(r"cabinets=1 .*join=0 role=0", disconnected)

    # Cabinet 1 was saved as master, but the next split-screen launch must boot solo.
    reload = run(["--split-screen"], "10:status")
    assert "cabinet 2 compiled" not in reload
    assert re.search(r"cabinets=1 .*join=0 role=0", reload)

    reset = run(["--split-screen"],
                "10:t,50:f3,70:status,90:t,120:status,150:close", frames=1000)
    assert reset.count("cabinet 2 compiled") == 2
    assert reset.count("player 2 joined, two cabinets running") == 2
    assert "reset complete, 1 cabinet(s) running" in reset
    assert re.search(r"cabinets=1 .*join=0 role=0", reset)

    saved_controls = run([], "10:t,80:status")
    assert "player 2 join requested" in saved_controls

print("Local multiplayer: default solo launch, controller join/disconnect, deferred join, linked boot, saved roles and reset/rejoin passed")
