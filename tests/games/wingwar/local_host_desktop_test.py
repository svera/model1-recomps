"""Nonvisual Wing War local-cabinet lifecycle checks."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary, regions = (Path(arg).resolve() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="wingwar-local-host-") as directory:
    data = Path(directory)
    for region in regions.glob("*.bin"):
        (data / region.name).symlink_to(region)
    (data / "settings.ini").write_text("renderer_3d = 0\n", encoding="utf-8")
    env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER="800", M1_SAVE="1",
               M1_NO_GPU_RENDERER="1", SDL_RENDER_DRIVER="software")
    for key in ("M1_SHOT_AT", "M1_EVENTS_MS", "M1_SCRIPT", "M1_GPU_CHECK", "M1_GPU3D_CHECK"):
        env.pop(key, None)

    def run(events, quit_after=800):
        result = subprocess.run(
            [str(binary), str(data), "--scale", "1", "--split-screen"],
            env=dict(env, M1_EVENTS_AT=events, M1_QUIT_AFTER=str(quit_after)),
            capture_output=True, text=True, timeout=300)
        assert result.returncode == 0, result.stderr
        return result.stderr

    reset = run("10:2,100:status,130:f3,180:status,200:close")
    assert "player 2 joined, two cabinets running" in reset, reset
    assert "reset complete, 1 cabinet(s) running" in reset, reset
    assert re.search(r"cabinets=1 .*join=0 role=0", reset), reset
    joined = run("10:pad2,100:status,130:disconnect2,180:status,200:close")
    assert "player 2 joined, two cabinets running" in joined, joined
    assert re.search(r"cabinets=2 .*join=0 role=1", joined), joined
    assert "player 2 controller disconnected; returned to solo play" in joined, joined
    assert re.search(r"cabinets=1 .*join=0 role=0", joined), joined
    saved_link = run("10:pad2,100:status,120:close")
    assert "player 2 joined, two cabinets running" in saved_link, saved_link
    assert re.search(r"cabinets=2 .*phase=0c .*role=1", saved_link), saved_link
    restored = run("40:status,60:close")
    assert re.search(r"cabinets=1 .*join=0 role=0", restored), restored
    selection = run("1:tab,10:1,50:status,60:pad2,70:status,80:close", 5000)
    assert "player=1 phase=0c subphase=03 role=0 participants=0" in selection, selection
    assert "player 2 joined, two cabinets running" in selection, selection
    for player in (1, 2):
        assert f"player={player} phase=0c subphase=03 role={player} participants=2" in selection, selection

    queued = run("1:tab,10:1,1300:status,1310:pad2,1320:status,1330:disconnect2,1340:status,1350:close", 7000)
    assert "player=1 phase=0c subphase=0f role=0 participants=0" in queued, queued
    assert "player 2 join requested" in queued, queued
    assert re.search(r"cabinets=1 .*join=1 role=0", queued), queued
    assert "player 2 disconnected before joining; cancelled queued join" in queued, queued
    assert re.search(r"cabinets=1 .*join=0 role=0", queued), queued
    delayed = run("1:tab,10:1,1300:pad2,1320:status,3900:status,3910:close", 18000)
    assert re.search(r"cabinets=1 .*join=1 role=0", delayed), delayed
    assert "player 2 joined, two cabinets running" in delayed, delayed
    assert re.search(r"cabinets=2 .*join=0 role=1", delayed), delayed

    def assert_active_game(log):
        for player in (1, 2):
            assert f"player={player} phase=0c subphase=0f role={player} participants=2" in log, log

    active_reset = run("1:tab,10:pad2,900:status,910:f3,920:status,930:close", 5000)
    assert_active_game(active_reset)
    assert "reset complete, 1 cabinet(s) running" in active_reset, active_reset
    assert re.search(r"cabinets=1 .*join=0 role=0", active_reset), active_reset
    active_disconnect = run("1:tab,10:pad2,900:status,910:disconnect2,920:status,930:close", 5000)
    assert_active_game(active_disconnect)
    assert "player 2 controller disconnected; returned to solo play" in active_disconnect, active_disconnect
    assert re.search(r"cabinets=1 .*join=0 role=0", active_disconnect), active_disconnect

print("Wing War local host: join, disconnect, reset and return to solo passed")
