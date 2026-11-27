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

    def run(events, *options, binary=vr, frames=2000):
        result = subprocess.run(
            [str(binary), str(data), "--scale", "1", *options],
            env=dict(env, M1_EVENTS_AT=events, M1_QUIT_AFTER=str(frames)),
            capture_output=True, text=True, timeout=120)
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

    controllers = run("10:fourpads,20:tap4,100:tap1,180:tap3,260:tap2,340:status,360:tap3,400:status,420:close")
    assert "claimed player 1; starting solo play" in controllers
    assert "player 4 joined, 4 cabinets running" in controllers
    assert controllers.count("player 4 joined") == 1
    for player in range(1, 5):
        assert re.search(rf"player={player} .*participants=4", controllers)

    for monitor_mode, count in ((0, 4), (1, 3)):
        (data / "settings.ini").write_text(
            f"renderer_3d = 0\nlive_monitor = {monitor_mode}\n", encoding="utf-8")
        joins = "10:fourpads,20:tap1,100:tap2,180:tap3" + (",260:tap4" if count == 4 else "")
        events = joins + ",300:tab,1500:status,1501:unplug2,1520:status,1521:unplug1,1540:status"
        if count == 4:
            events += ",1541:unplug3,1560:status"
        race = run(events + ",1561:close", frames=20000)
        states = re.findall(
            r"player=(\d+) phase=([0-9a-f]+) .*participants=(\d+) machine_frame=(\d+)", race)
        assert len(states) == sum(range(1, count + 1)), race
        previous_frames = None
        offset = 0
        for remaining in range(count, 0, -1):
            batch = states[offset:offset + remaining]
            assert all(phase == "14" and int(participants) == count
                       for _, phase, participants, _ in batch), race
            current_frames = [int(frame) for _, _, _, frame in batch]
            if previous_frames is not None:
                removed = 1 if remaining == count - 1 else 0
                survivors = previous_frames[:removed] + previous_frames[removed + 1:]
                assert all(0 < after - before <= 100
                           for before, after in zip(survivors, current_frames)), race
            previous_frames = current_frames
            offset += remaining
        assert "reset complete" not in race, race
        if monitor_mode:
            assert re.search(r"Live Monitor: status .*racers=3 .*layout=above", race), race
    (data / "settings.ini").write_text("renderer_3d = 0\nlive_monitor = 0\n", encoding="utf-8")
    for player in range(2, 5):
        (data / f"two-cabinets/{player}/settings.ini").write_text("renderer_3d = 0\n", encoding="utf-8")

    disconnected = run("10:fourpads,20:tap1,100:tap2,180:tap3,260:tap4,"
                       "340:unplug2,420:status,440:tap4,480:status,"
                       "500:unplug1,580:status,600:unplug3,680:status,700:close")
    assert "player 2 controller disconnected; keeping split-screen session (3 player(s) remain)" in disconnected
    assert "player 1 controller disconnected; keeping split-screen session (2 player(s) remain)" in disconnected
    assert "player 1 controller disconnected; returned to solo play (1 player(s) remain)" in disconnected
    for count in (3, 2):
        assert re.search(rf"cabinets={count} .*join=0 role=1", disconnected)
        for player in range(1, count + 1):
            assert re.search(rf"player={player} .*participants={count}", disconnected), disconnected
    assert re.search(r"cabinets=1 .*join=0 role=0", disconnected)
    assert disconnected.count("player 4 joined") == 1
    assert "p1_key_start = none" in (data / "settings.ini").read_text()
    (data / "settings.ini").write_text("renderer_3d = 0\nlive_monitor = 1\n", encoding="utf-8")
    monitor_disconnect = run("10:fourpads,20:tap1,100:tap2,180:tap3,"
                             "260:unplug2,340:status,360:tap4,440:status,460:close")
    assert "keeping split-screen session (2 player(s) remain)" in monitor_disconnect
    for count in (2, 3):
        assert re.search(rf"Live Monitor: status .*racers={count} .*layout=above", monitor_disconnect)
    assert "player 3 joined, 3 cabinets running" in monitor_disconnect
    assert re.search(r"player=3 .*participants=3", monitor_disconnect)
    (data / "settings.ini").write_text("renderer_3d = 0\nlive_monitor = 0\n", encoding="utf-8")
    (data / "two-cabinets/2/settings.ini").write_text("renderer_3d = 0\n", encoding="utf-8")

    joined = run("10:2,120:status,160:f3,240:status,260:close")
    assert "on-demand split-screen side-by-side" in joined
    assert "player 2 joined, two cabinets running" in joined
    assert re.search(r"cabinets=2 .*join=0 role=1", joined)
    assert re.search(r"cabinets=1 .*join=0 role=0", joined)
    assert "reset complete, 1 cabinet(s) running" in joined

    for monitor_mode in (0, 1, 2):
        (data / "settings.ini").write_text(
            f"renderer_3d = 0\nlive_monitor = {monitor_mode}\n", encoding="utf-8")
        group = run("10:pad2,100:pad3,180:status,200:pad4,280:status,300:pad4,320:status,340:f3,420:status,440:close")
        assert "player 3 joined, 3 cabinets running" in group
        assert "player 4 joined, 4 cabinets running" in group
        assert re.search(r"player=3 .*participants=3", group)
        for player in range(1, 5):
            assert re.search(rf"player={player} .*participants=4", group)
        assert group.count("player 4 joined") == 1
        assert "reset complete, 1 cabinet(s) running" in group
        if monitor_mode:
            assert re.search(r"Live Monitor: status .*racers=3 .*layout=above", group)
            assert "Live Monitor: disabled for four players" in group
        assert (data / "two-cabinets/3/settings.ini").exists()
        assert (data / "two-cabinets/4/settings.ini").exists()
    (data / "settings.ini").write_text("renderer_3d = 0\nlive_monitor = 0\n", encoding="utf-8")

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
        [str(wingwar), str(data), "--split-screen", "--arcade"],
        env=env, capture_output=True, text=True, timeout=30)
    assert unsupported.returncode == 2
    assert "require a game supporting local multiplayer" in unsupported.stderr

print("Local host: one to four players, controller join order, monitor limits, reset and unsupported-game gating passed")
