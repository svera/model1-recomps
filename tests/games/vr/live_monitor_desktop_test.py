"""ROM-backed desktop checks for Live Monitor layouts and lifecycle."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


binary, regions = (Path(arg).resolve() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="vr-live-monitor-") as directory:
    data = Path(directory)
    for region in regions.glob("*.bin"):
        (data / region.name).symlink_to(region)

    def run(events, frames=1000, shot=0, save=True, options=()):
        env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER=str(frames),
                   M1_SAVE="1", M1_EVENTS_AT=events,
                   SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x1209/0x0001")
        if shot:
            env["M1_SHOT_AT"] = str(shot)
        if not save:
            env.pop("M1_SAVE")
        result = subprocess.run([str(binary), str(data), "--scale", "1", *options],
                                env=env, capture_output=True, text=True, timeout=120)
        print(result.stderr, end="")
        assert result.returncode == 0, result.stderr
        return result.stderr

    def set_layout(value, renderer=0):
        (data / "settings.ini").write_text(
            f"live_monitor = {value}\nrenderer_3d = {renderer}\nscale = 1\nfps = 30\n")

    def assert_monitor(log, layout, racers=2):
        assert f"layout={layout} audio=muted" in log
        assert re.search(rf"Live Monitor: status role=3 id=0 racers={racers} inputs=ff/ff/ff ticks=[1-9]\d*", log)
        monitor = data / "live-monitor"
        for name in ("nvram.sav", "eeprom_93c46.sav", "settings.ini"):
            assert (monitor / name).stat().st_size > 0
        # EEPROM save words use host byte order, unlike the machine's byte view.
        saves = [(data, 1), (monitor, 3)]
        if racers == 2:
            saves.append((data / "two-cabinets/2", 2))
        for folder, role in saves:
            image = (folder / "eeprom_93c46.sav").read_bytes()
            assert int.from_bytes(image[12:14], sys.byteorder) >> 8 == role
        assert "p1_pad = none" in (monitor / "settings.ini").read_text()

    set_layout(0)
    solo = run("40:status,60:close")
    assert "cabinet 3 compiled" not in solo
    assert not (data / "live-monitor").exists()

    for mode, name in ((1, "above"), (2, "window")):
        set_layout(mode)
        solo = run("40:status,60:close", shot=10, options=("--aspect", "21:9"))
        assert_monitor(solo, name, racers=1)
        assert "cabinet 2 compiled" not in solo
        assert "cabinets=1" in solo
        if mode == 1:
            bounds = re.findall(r"picture bounds view=(\d+) window=(\d+)x(\d+) "
                                r"x=([\d.]+) y=([\d.]+) w=([\d.]+) h=([\d.]+)", solo)
            assert len(bounds) == 2, solo
            for view, width, height, x, y, w, h in bounds:
                composition_w = min(int(width), int(height) * 64 / 27)
                assert abs(float(w) - composition_w) <= 1, solo
                assert abs(float(h) - composition_w * 27 / 128) <= 1, solo
            shutil.copyfile("build/host_window_at.png", "build/live_monitor_solo_at.png")
    set_layout(2)
    closed_solo = run("40:status,60:close-monitor,100:status,120:close")
    assert re.search(r"Live Monitor: status role=3 id=0 racers=1 inputs=ff/ff/ff ticks=[1-9]\d*", closed_solo)
    assert "reset complete, 1 cabinet(s) running" in closed_solo
    assert "live_monitor = 0" in (data / "settings.ini").read_text()
    enabled_solo = run("40:escape,42:down,44:return,46:up,48:up,50:left,"
                       "52:escape,54:escape,100:status,140:close")
    assert_monitor(enabled_solo, "window", racers=1)
    assert "reset complete, 1 cabinet(s) running" in enabled_solo
    assert "cabinet 2 compiled" not in enabled_solo
    assert not (data / "two-cabinets/2/nvram.sav").exists()
    (data / "settings.ini").write_text(
        "live_monitor = 2\nrenderer_3d = 2\nscale = 1\nfps = 60\nscreen_filter = 2\n")
    rendered_solo = run("40:f5,60:f6,80:resize,100:status,120:close")
    assert_monitor(rendered_solo, "window", racers=1)
    assert "cabinet 2 compiled" not in rendered_solo

    set_layout(1)
    above = run("10:2,140:status,180:close", shot=80)
    assert "Live Monitor: above players, player audio only (monitor muted)" in above
    assert_monitor(above, "above")
    shutil.copyfile("build/host_window_at.png", "build/live_monitor_above_at.png")

    set_layout(2)
    window = run("10:2,140:status,180:close", shot=80)
    assert "Live Monitor: spectator window, player audio only (monitor muted)" in window
    assert_monitor(window, "window")
    assert (Path("build") / "host_monitor_at.png").stat().st_size > 0
    shutil.copyfile("build/host_monitor_at.png", "build/live_monitor_window_at.png")

    # Display -> Up (Back) -> Up (Live Monitor), change window to Off.
    disabled = run("10:2,40:escape,42:down,44:return,46:up,48:up,50:right,"
                   "52:escape,54:escape,100:status,140:close")
    assert "reset complete, 2 cabinet(s) running" in disabled
    assert "Live Monitor: status" not in disabled
    assert "live_monitor = 0" in (data / "settings.ini").read_text()

    # Enable using the menu, then close just the spectator window.
    enabled = run("10:2,40:escape,42:down,44:return,46:up,48:up,50:left,"
                  "52:escape,54:escape,100:status,120:close-monitor,160:status,200:close")
    assert_monitor(enabled, "window")
    assert enabled.count("reset complete, 2 cabinet(s) running") == 2
    assert "live_monitor = 0" in (data / "settings.ini").read_text()

    # Switching an existing monitor between layouts must not reset the race.
    set_layout(1)
    switched = run("10:2,40:escape,42:down,44:return,46:up,48:up,50:right,"
                   "52:escape,54:escape,100:status,140:close")
    assert_monitor(switched, "window")
    assert "reset complete" not in switched

    switched_back = run("10:2,40:escape,42:down,44:return,46:up,48:up,50:left,"
                        "52:escape,54:escape,100:status,140:close")
    assert_monitor(switched_back, "above")
    assert "reset complete" not in switched_back

    set_layout(1)
    removed = run("10:2,80:f3,120:status,160:2,220:status,260:close")
    assert "reset complete, 1 cabinet(s) running" in removed
    assert removed.count("cabinet 3 compiled") == 1
    assert_monitor(removed, "above", racers=1)
    assert_monitor(removed, "above")

    # Autosave the isolated monitor without relying on the final synchronous save.
    for name in ("nvram.sav", "eeprom_93c46.sav"):
        (data / "live-monitor" / name).unlink()
    autosaved = run("10:2,80:autosave,100:status,140:close", save=False)
    assert_monitor(autosaved, "above")

    # The monitor must also survive the shared GPU/filter/scale paths.
    (data / "settings.ini").write_text(
        "live_monitor = 2\nrenderer_3d = 2\nscale = 1\nfps = 60\nscreen_filter = 2\n")
    rendered = run("10:2,60:f5,100:resize,120:status,160:close")
    assert_monitor(rendered, "window")
    assert "two cabinets: cabinet " + str(data / "live-monitor") + " 3D renderer:" in rendered or (
        "The GPU is unavailable or failed its picture check" in rendered)

    set_layout(1, renderer=2)
    race = run("10:2,40:tab", frames=2400, shot=2200, options=("--aspect", "21:9"))
    assert "2400 frames, 2 cabinet(s) running" in race
    bounds = [(int(view), int(width), int(height), float(x), float(y), float(w), float(h))
              for view, width, height, x, y, w, h in re.findall(
                  r"picture bounds view=(\d+) window=(\d+)x(\d+) x=([\d.]+) y=([\d.]+) w=([\d.]+) h=([\d.]+)", race)]
    assert len(bounds) == 3, race
    for view, width, height, x, y, w, h in bounds[:2]:
        composition_w = min(width, height * 64 / 27)
        composition_h = composition_w * 27 / 64
        assert abs(w - composition_w / 2) <= 1, race
        assert abs(h - composition_h / 2) <= 1, race
        assert abs(w / h - 64 / 27) < 0.01, race
        assert abs(x - ((width - composition_w) / 2 + (view - 1) * composition_w / 2)) <= 1, race
        assert abs(y - ((height - composition_h) / 2 + composition_h / 2)) <= 1, race
    _, width, height, x, y, w, h = bounds[2]
    composition_w = min(width, height * 64 / 27)
    composition_h = composition_w * 27 / 64
    assert abs(w - composition_w) <= 1 and abs(h - composition_h / 2) <= 1, race
    assert abs(x - (width - composition_w) / 2) <= 1, race
    assert abs(y - (height - composition_h) / 2) <= 1, race
    shutil.copyfile("build/host_window_at.png", "build/live_monitor_race_at.png")

    def monitor_aspects(log):
        return [(float(aspect), int(shape)) for aspect, shape in
                re.findall(r"Live Monitor: status .*aspect=([\d.]+) picture_shape=(\d+)", log)]

    for mode in (1, 2):
        set_layout(mode)
        wide = run("10:2,80:status,100:f6,140:status,160:close", options=("--aspect", "16:9"))
        aspects = monitor_aspects(wide)
        assert len(aspects) == 2, wide
        expected = (32 / 9, 128 / 27) if mode == 1 else (16 / 9, 64 / 27)
        assert abs(aspects[0][0] - expected[0]) < 0.01 and aspects[0][1] == 3
        assert abs(aspects[1][0] - expected[1]) < 0.01 and aspects[1][1] == 4
        players = [(int(player), float(aspect), int(shape)) for player, aspect, shape in
                   re.findall(r"two cabinets: picture player=(\d+) aspect=([\d.]+) picture_shape=(\d+)", wide)]
        assert len(players) == 4, wide
        for player, aspect, shape in players:
            expected_player = (16 / 9 if shape == 3 else 64 / 27) / (1 if mode == 1 else 2)
            assert abs(aspect - max(4 / 3, expected_player)) < 0.01, wide
        assert "picture_shape = 4" in (data / "live-monitor/settings.ini").read_text()

    set_layout(2)
    fitted = run("10:2,80:status,100:resize-monitor,140:status,160:close")
    aspects = monitor_aspects(fitted)
    assert len(aspects) == 2, fitted
    assert abs(aspects[0][0] - 4 / 3) < 0.00001 and aspects[0][1] == 0
    assert abs(aspects[1][0] - 16 / 9) < 0.00001 and aspects[1][1] == 0

    set_layout(1)
    fitted_above = run("10:2,80:resize,100:status,120:close")
    assert monitor_aspects(fitted_above) == [(round(32 / 9, 6), 0)]
    players = re.findall(r"two cabinets: picture player=(\d+) aspect=([\d.]+) picture_shape=0", fitted_above)
    assert len(players) == 2, fitted_above
    assert all(abs(float(aspect) - 16 / 9) < 0.00001 for _, aspect in players), fitted_above

    for layout, expected in (("side-by-side", 4 / 3), ("top-bottom", 32 / 9)):
        set_layout(0)
        split = run("10:2,80:status,100:close",
                    options=("--aspect", "16:9", "--split-screen", "--split-layout", layout))
        players = re.findall(r"two cabinets: picture player=(\d+) aspect=([\d.]+) picture_shape=3", split)
        assert len(players) == 2, split
        assert all(abs(float(aspect) - expected) < 0.01 for _, aspect in players), split

    set_layout(0)
    solo = run("80:status,100:close", options=("--aspect", "16:9"))
    players = re.findall(r"two cabinets: picture player=(\d+) aspect=([\d.]+) picture_shape=3", solo)
    assert len(players) == 1 and abs(float(players[0][1]) - 16 / 9) < 0.00001, solo

    def language(folder):
        image = (folder / "eeprom_93c46.sav").read_bytes()
        return int.from_bytes(image[26:28], sys.byteorder) & 0xff

    folders = (data, data / "two-cabinets/2", data / "live-monitor")
    for mode in (1, 2):
        set_layout(mode)
        run("10:2,100:close")
        previous = language(data)
        # Game -> Language, outside both player submenus.
        changed = run("10:2,80:escape,82:down,84:down,86:down,88:down,90:return,"
                      "92:down,94:down,96:down,98:right,100:escape,102:escape,"
                      "160:status,180:close")
        assert "reset complete, 2 cabinet(s) running" in changed
        expected = 0 if previous else 2
        assert all(language(folder) == expected for folder in folders), changed
        run("10:2,100:close")
        assert all(language(folder) == expected for folder in folders)

    for mode, name in ((1, "above"), (2, "window")):
        for start in ("tappads", "2"):
            set_layout(mode)
            reset_race = run(
                f"10:{start},40:tab,800:status,810:f3,850:status,870:close",
                frames=5000)
            racers = 1 if start == "tappads" else 2
            assert re.search(rf"status frame=\d+ cabinets={racers} .*phase=14 ", reset_race)
            assert "reset complete, 1 cabinet(s) running" in reset_race
            assert re.search(r"status frame=\d+ cabinets=1 .*phase=03 ", reset_race)
            assert_monitor(reset_race, name, racers=1)
            if racers == 2:
                assert_monitor(reset_race, name, racers=2)

print("Live Monitor desktop layouts, menu persistence, isolated saves and lifecycle passed")
