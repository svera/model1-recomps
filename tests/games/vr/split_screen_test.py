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

    solo = run(["--split-screen"], "10:1,40:status")
    assert "cabinet 2 compiled" not in solo
    assert "1 cabinet(s) running, 0/0 link exchanges" in solo
    assert not (data / "two-cabinets/2").exists()

    cancelled = run(["--split-screen"], "10:1,100:2,150:f3,200:status,240:close")
    assert "join requested" in cancelled and "cabinet 2 compiled" not in cancelled
    assert re.search(r"cabinets=1 .*join=0 role=0", cancelled)

    # Player 1's course selection must not be reset by a player 2 join request.
    queued = run(["--split-screen"], "10:1,100:2,200:status,300:status")
    assert "join requested" in queued and "player 2 joined" not in queued
    assert "cabinet 2 compiled" not in queued
    assert re.search(r"cabinets=1 .*phase=10 join=1 role=0", queued)
    assert not (data / "two-cabinets/2").exists()

    joined = run(["--split-screen", "--split-layout", "top-bottom"],
                 "10:2,40:p,60:f7,80:resize,100:p,120:status")
    assert joined.count("cabinet 2 compiled") == 1
    assert "player 2 joined, two cabinets running" in joined
    assert re.search(r"2 cabinet\(s\) running, ([1-9]\d*)/\1 link exchanges", joined)
    for player in ("1", "2"):
        for name in ("nvram.sav", "eeprom_93c46.sav", "settings.ini"):
            assert (data / "two-cabinets" / player / name).stat().st_size > 0

    # Cabinet 1 was saved as master, but the next split-screen launch must boot solo.
    reload = run(["--split-screen"], "10:status")
    assert "cabinet 2 compiled" not in reload
    assert re.search(r"cabinets=1 .*join=0 role=0", reload)

    reset = run(["--split-screen"],
                "10:2,50:f3,70:status,90:2,120:status,150:close", frames=1000)
    assert reset.count("cabinet 2 compiled") == 2
    assert reset.count("player 2 joined, two cabinets running") == 2
    assert "reset complete, 1 cabinet(s) running" in reset
    assert re.search(r"cabinets=1 .*join=0 role=0", reset)

    windows = run(["--two-cabinets"])
    assert "cabinet 2 compiled" in windows
    assert re.search(r"2 cabinet\(s\) running, ([1-9]\d*)/\1 link exchanges", windows)

print("On-demand cabinets: solo allocation, deferred join, linked boot, saved roles, reset/rejoin and two-window compatibility passed")
