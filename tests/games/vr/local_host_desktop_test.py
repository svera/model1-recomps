"""Nonvisual checks for the shared local-cabinet host."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


vr, wingwar, regions, test_host = (Path(arg).resolve() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="vr-local-host-") as directory:
    data = Path(directory)
    for region in regions.glob("*.bin"):
        (data / region.name).symlink_to(region)
    (data / "settings.ini").write_text("renderer_3d = 0\n", encoding="utf-8")
    env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER="2000", M1_SAVE="1",
               M1_NO_GPU_RENDERER="1", SDL_RENDER_DRIVER="software")
    for key in ("M1_SHOT_AT", "M1_EVENTS_MS", "M1_SCRIPT", "M1_GPU_CHECK", "M1_GPU3D_CHECK"):
        env.pop(key, None)

    def run(events, *options, binary=vr):
        result = subprocess.run(
            [str(binary), str(data), "--scale", "1", *options],
            env=dict(env, M1_EVENTS_AT=events), capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stderr
        return result.stderr

    solo = run("40:status,60:close", "--split-screen", "--split-layout", "top-bottom")
    assert "1 cabinet(s) running, 0/0 link exchanges" in solo
    assert "cabinet 2 compiled" not in solo
    assert "on-demand split-screen top-bottom" in solo

    (data / "settings.ini").write_text("renderer_3d = 0\nlive_monitor = 1\n", encoding="utf-8")
    monitor = run("40:status,60:close")
    assert "Live Monitor: status role=" in monitor
    (data / "settings.ini").write_text("renderer_3d = 0\nlive_monitor = 0\n", encoding="utf-8")

    joined = run("10:2,120:status,160:f3,240:status,260:close")
    assert "on-demand split-screen side-by-side" in joined
    assert "player 2 joined, two cabinets running" in joined
    assert re.search(r"cabinets=2 .*join=0 role=1", joined)
    assert re.search(r"cabinets=1 .*join=0 role=0", joined)
    assert "reset complete, 1 cabinet(s) running" in joined

    solo_options = run("10:escape,12:down,14:down,16:down,18:down,20:down,22:return,24:return,26:escape,28:escape,40:close",
                       binary=test_host)
    assert "test host: option cabinet 0" in solo_options
    assert (data / "local-menu-test.txt").read_text() == "option0"

    linked_options = run("10:2,100:escape,102:down,104:down,106:down,108:down,110:down,112:down,114:return,116:return,118:return,120:escape,122:escape,124:escape,140:close",
                         binary=test_host)
    assert "test host: option cabinet 2" in linked_options
    assert "test host: custom link created" in linked_options
    assert "test host: loaded option0 for cabinet 0" in linked_options
    assert (data / "local-menu-test.txt").read_text() == "option0"
    assert (data / "two-cabinets/2/local-menu-test.txt").read_text() == "option2"

    sound = run("10:escape,12:down,14:down,16:return,18:down,20:down,22:down,24:return,26:escape,28:escape,40:close",
                binary=test_host)
    assert "test host: sound cabinet 0" in sound
    capture = run("10:escape,12:down,14:down,16:down,18:down,20:down,22:return,24:down,26:return,28:t,30:escape,32:escape,40:close",
                  binary=test_host)
    assert "test host: captured cabinet 0" in capture
    linked_sound = run("10:2,100:escape,102:down,104:down,106:return,108:down,110:down,112:down,114:down,116:return,118:return,120:escape,122:escape,124:escape,140:close",
                       binary=test_host)
    assert "test host: sound cabinet 2" in linked_sound
    assert "test host: loaded option2 for cabinet 2" in linked_sound

    unsupported = subprocess.run(
        [str(wingwar), str(data), "--split-screen"],
        env=env, capture_output=True, text=True, timeout=30)
    assert unsupported.returncode == 2
    assert "require a game supporting local multiplayer" in unsupported.stderr

print("Local host: solo, join, reset and unsupported-game gating passed")
