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

    def run(options, events="", frames=120, environment=None):
        last_pass = max((int(event.split(":")[0]) for event in events.split(",") if event), default=0)
        if not any(event.endswith(":close") for event in events.split(",")):
            last_pass = max(frames, last_pass + 20)
            events += ("," if events else "") + f"{last_pass}:close"
        # VSync changes the number of simulation frames per event-loop pass.
        env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER=str(max(frames, last_pass * 4 + 100)),
                   M1_SAVE="1", M1_EVENTS_AT=events, M1_INPUT_TRACE="1",
                   SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x1209/0x0001")
        if environment:
            env.update(environment)
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
    assert "cabinet 1 vsync 1" in solo
    assert "cabinet 2 compiled" not in solo
    assert "1 cabinet(s) running, 0/0 link exchanges" in solo
    assert not (data / "two-cabinets/2/nvram.sav").exists()

    player1_eeprom = data / "eeprom_93c46.sav"

    def language(path):
        return int.from_bytes(path.read_bytes()[26:28], sys.byteorder) & 0xff

    def car_color_value(path):
        return int.from_bytes(path.read_bytes()[0x22:0x24], sys.byteorder) >> 8

    japanese_eeprom = player1_eeprom.read_bytes()
    assert language(player1_eeprom) == 0
    english = run([], "10:escape,12:down,14:down,16:down,18:down,20:return,22:down,24:right,26:escape,28:escape,80:close", frames=1000)
    assert "reset complete, 1 cabinet(s) running" in english
    assert language(player1_eeprom) != 0

    # Migrate the interim cabinet-1 saves only when the original single-player files are absent.
    interim = data / "two-cabinets/1"
    interim.mkdir(parents=True)
    for name in ("nvram.sav", "eeprom_93c46.sav"):
        (interim / name).write_bytes((data / name).read_bytes())
        (data / name).unlink()
    run([], "40:close")
    assert language(player1_eeprom) != 0
    assert (data / "nvram.sav").stat().st_size > 0

    # The original single-player settings win over stale cabinet-1 settings.
    (interim / "eeprom_93c46.sav").write_bytes(japanese_eeprom)
    run([], "40:close")
    assert language(player1_eeprom) != 0
    assert (interim / "eeprom_93c46.sav").read_bytes() == japanese_eeprom

    settings_menu = run([], "10:escape,20:down,30:return,40:down,50:right,60:escape,70:escape,100:close")
    assert "1 cabinet(s) running, 0/0 link exchanges" in settings_menu
    assert "antialiasing = 0" in (data / "settings.ini").read_text()

    graphics_menu = run([], "10:escape,12:down,14:return,16:down,18:down,20:down,22:down,24:down,26:down,28:down,30:down,32:right,34:up,36:up,38:up,40:up,42:up,44:up,46:right,48:escape,50:escape,60:close")
    assert "1 cabinet(s) running, 0/0 link exchanges" in graphics_menu
    graphics_settings = (data / "settings.ini").read_text()
    assert "screen_filter = 1" in graphics_settings
    assert "renderer_3d = 0" in graphics_settings

    controls_menu = run([], "10:escape,12:down,14:down,16:down,18:return,20:down,22:return,24:down,26:return,28:down,30:down,32:down,34:down,36:down,38:down,40:down,42:return,44:t,46:escape,48:escape,50:escape,52:escape,54:close")
    assert "1 cabinet(s) running, 0/0 link exchanges" in controls_menu
    player2_settings = (data / "two-cabinets/2/settings.ini").read_text()
    assert "p1_key_start = T" in player2_settings

    cancelled = run(["--split-screen"], "10:1,10:t,10:f3,40:status,60:close")
    assert "join requested" in cancelled and "cabinet 2 compiled" not in cancelled
    assert re.search(r"cabinets=1 .*join=0 role=0", cancelled)

    disconnected_while_queued = run(["--split-screen"], "10:1,10:pad2,10:disconnect2,80:status")
    assert "player 2 disconnected before joining; cancelled queued join" in disconnected_while_queued
    assert "cabinet 2 compiled" not in disconnected_while_queued
    assert re.search(r"cabinets=1 .*join=0 role=0", disconnected_while_queued)

    # Player 2 can join while Player 1 is choosing a course, not just during attract.
    course_join = run(["--split-screen"], "10:1,80:status,100:t,200:status,220:close")
    assert re.search(r"cabinets=1 .*phase=10 join=0 role=0", course_join)
    assert "player 2 joined, two cabinets running" in course_join
    assert re.search(r"cabinets=2 .*phase=10 join=0 role=1", course_join)

    joined = run(["--split-screen", "--split-layout", "top-bottom"],
                 "10:t,40:p,60:f7,80:resize,100:p,120:status")
    assert joined.count("cabinet 2 compiled") == 1
    assert "cabinet 1 vsync 1" in joined
    assert "player 2 joined, two cabinets running" in joined
    assert re.search(r"2 cabinet\(s\) running, ([1-9]\d*)/\1 link exchanges", joined)
    assert (data / "settings.ini").stat().st_size > 0
    for name in ("nvram.sav", "eeprom_93c46.sav"):
        assert (data / name).stat().st_size > 0
    for name in ("nvram.sav", "eeprom_93c46.sav", "settings.ini"):
        assert (data / "two-cabinets" / "2" / name).stat().st_size > 0
    player2_eeprom = data / "two-cabinets/2/eeprom_93c46.sav"
    assert car_color_value(player2_eeprom) == 1
    assert car_color_value(player1_eeprom) == 0

    before_removed_color = car_color_value(player2_eeprom)
    removed = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:down,92:return,120:status,160:close", frames=1000)
    assert "player 2 removed from menu; returned to solo play" in removed
    assert "reset complete, 1 cabinet(s) running" in removed
    assert re.search(r"cabinets=1 .*join=0 role=0", removed)
    assert car_color_value(player2_eeprom) == before_removed_color

    removed_rejoined = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:down,92:return,120:status,140:t,200:status,220:close", frames=1000)
    assert "player 2 removed from menu; returned to solo play" in removed_rejoined
    assert re.search(r"cabinets=1 .*join=0 role=0", removed_rejoined)
    assert removed_rejoined.count("player 2 joined, two cabinets running") == 2
    assert re.search(r"cabinets=2 .*join=0 role=1", removed_rejoined)
    assert car_color_value(player2_eeprom) == before_removed_color

    # In solo mode Remove Player 2 is absent, so the item after Game is Quit.
    solo_quit = run([], "10:escape,12:down,14:down,16:down,18:down,20:down,22:return,40:t", frames=1000)
    assert "player 2 join requested" not in solo_quit
    assert "cabinet 2 compiled" not in solo_quit

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

    before_language = player2_eeprom.read_bytes()
    game_settings = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,93:down,94:return,96:right,98:escape,100:escape,102:escape,140:status,160:close", frames=1000)
    assert "reset complete, 2 cabinet(s) running" in game_settings
    assert game_settings.count("cabinet 2 compiled") == 1
    assert re.search(r"cabinets=2 .*role=1", game_settings)
    assert player2_eeprom.read_bytes() != before_language

    before_color = player2_eeprom.read_bytes()
    before_player1_color = player1_eeprom.read_bytes()
    car_color = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,93:down,94:return,96:down,98:right,100:escape,102:escape,104:escape,140:status,160:close", frames=1000)
    assert "reset complete, 2 cabinet(s) running" in car_color
    assert car_color.count("cabinet 2 compiled") == 1
    assert re.search(r"cabinets=2 .*role=1", car_color)
    assert player2_eeprom.read_bytes() != before_color
    assert car_color_value(player2_eeprom) == 2
    assert player1_eeprom.read_bytes() == before_player1_color

    before_saved_color = player2_eeprom.read_bytes()
    saved_car_color = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,93:down,94:return,96:down,98:right,99:save,100:escape,102:escape,104:escape,140:status,160:close", frames=1000)
    assert "reset complete, 2 cabinet(s) running" in saved_car_color
    assert saved_car_color.count("cabinet 2 compiled") == 1
    assert re.search(r"cabinets=2 .*role=1", saved_car_color)
    assert player2_eeprom.read_bytes() != before_saved_color
    assert car_color_value(player2_eeprom) == 3
    assert player1_eeprom.read_bytes() == before_player1_color

    def machine_setting(path, shift):
        return (int.from_bytes(path.read_bytes()[0x20:0x22], sys.byteorder) >> shift) & 0xff

    def race_type(path):
        return int.from_bytes(path.read_bytes()[0x22:0x24], sys.byteorder) & 0xff

    for _ in range(2):
        expected_race_type = (race_type(player1_eeprom) + 1) % 2
        shared_race_type = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,94:down,95:down,96:right,97:save,98:escape,100:escape,140:status,160:close", frames=1000)
        assert "reset complete, 2 cabinet(s) running" in shared_race_type
        assert re.search(r"cabinets=2 .*role=1", shared_race_type)
        assert race_type(player1_eeprom) == expected_race_type
        assert race_type(player2_eeprom) == expected_race_type
        assert car_color_value(player2_eeprom) == 3

    for _ in range(4):
        expected_course = (machine_setting(player1_eeprom, 0) + 1) % 4
        shared_course = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,91:down,92:down,94:down,95:down,96:right,97:save,98:escape,100:escape,140:status,160:close", frames=1000)
        assert "reset complete, 2 cabinet(s) running" in shared_course
        assert re.search(r"cabinets=2 .*role=1", shared_course)
        assert machine_setting(player1_eeprom, 0) == expected_course
        assert machine_setting(player2_eeprom, 0) == expected_course
        assert car_color_value(player2_eeprom) == 3

    # Shared settings are absent from each player's page: Difficulty follows Car color.
    before_difficulty = machine_setting(player2_eeprom, 8)
    difficulty = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,93:down,94:return,96:down,98:down,100:right,102:escape,104:escape,106:escape,160:close", frames=1000)
    assert "reset complete, 2 cabinet(s) running" in difficulty
    assert machine_setting(player2_eeprom, 8) == (before_difficulty + 1) % 4
    assert machine_setting(player1_eeprom, 0) == expected_course
    assert machine_setting(player2_eeprom, 0) == expected_course

    # Race type is absent solo: Course select follows Car color, and the saved race type stays unchanged.
    # A shared course chosen solo must also apply to a cabinet that joins later.
    expected_course = (expected_course + 1) % 4
    solo_course = run([], "10:escape,12:down,14:down,16:down,18:down,20:return,21:down,22:down,23:down,24:right,30:escape,32:escape,60:status,80:t,140:status,160:close", frames=1000)
    assert "reset complete, 1 cabinet(s) running" in solo_course
    assert "player 2 joined, two cabinets running" in solo_course
    assert machine_setting(player1_eeprom, 0) == expected_course
    assert machine_setting(player2_eeprom, 0) == expected_course
    assert race_type(player1_eeprom) == expected_race_type
    assert race_type(player2_eeprom) == expected_race_type

    # A staged Bluetooth connection can lose its hotplug event. Use two identical
    # SDL pads and a real held Start button instead of the direct join test hook.
    late_controller = run([], "10:1,40:latepads,250:status,260:close", frames=1000)
    assert "player 2 join requested" in late_controller
    assert "player 2 joined, two cabinets running" in late_controller
    assert re.search(r"cabinets=2 .*join=0 role=1", late_controller)

    late_course_join = run([], "10:1,30:status,40:latepads,250:status,260:close", frames=1000)
    assert re.search(r"cabinets=1 .*phase=10 join=0 role=0", late_course_join)
    assert "player 2 joined, two cabinets running" in late_course_join
    assert re.search(r"cabinets=2 .*phase=10 join=0 role=1", late_course_join)

    # A tap released before the host polls must not depend on the periodic scan.
    tapped_controller = run([], "10:1,40:tappads,80:status,90:close", frames=1000)
    assert "button=start down" in tapped_controller
    assert "button=start up" in tapped_controller
    assert "player 2 join requested" in tapped_controller
    assert "player 2 joined, two cabinets running" in tapped_controller
    assert re.search(r"cabinets=2 .*join=0 role=1", tapped_controller)

    for first, second in ((1, 2), (2, 1)):
        claimed = run([], f"10:testpads,30:tap{first},80:status,100:tap{first},"
                      f"160:status,180:tap{second},240:status,260:close", frames=1000)
        assert claimed.count("claimed player 1; starting solo play") == 1
        assert len(re.findall(r"cabinets=1 .*phase=10 join=0 role=0", claimed)) == 2
        assert claimed.count("player 2 joined, two cabinets running") == 1
        assert re.search(r"cabinets=2 .*phase=10 join=0 role=1", claimed)
        claim = re.search(r"controller=(\d+) button=start down", claimed)
        assert claim
        assert f"controller {claim[1]} claimed player 1" in claimed

    def picture_shape(player=1):
        path = data / "settings.ini" if player == 1 else data / "two-cabinets/2/settings.ini"
        return int(re.search(r"^picture_shape = (\d+)$", path.read_text(), re.MULTILINE)[1])

    run(["--aspect", "32:9"], "40:close")
    assert picture_shape() == 5
    wide_reload = run([], "10:t,80:status,100:close")
    assert "player 2 joined, two cabinets running" in wide_reload
    assert picture_shape() == picture_shape(2) == 5

    shape_menu = "10:escape,12:down,14:return,16:down,18:down,20:down"
    run(["--aspect", "21:9"], shape_menu + ",22:right,24:escape,26:escape,40:close")
    assert picture_shape() == 5
    run([], shape_menu + ",22:right,24:escape,26:escape,40:close")
    assert picture_shape() == 0
    run([], shape_menu + ",22:left,24:escape,26:escape,40:close")
    assert picture_shape() == 5

    def settings(player=1):
        path = data / "settings.ini" if player == 1 else data / "two-cabinets/2/settings.ini"
        return dict(re.findall(r"^(\w+) = (.*)$", path.read_text(), re.MULTILINE))

    def display_change(index, joined=False):
        events = ["10:t"] if joined else []
        events += ["100:escape", "102:down", "104:return"]
        events += [f"{106 + i * 2}:down" for i in range(index)]
        at = 108 + index * 2
        events += [f"{at}:right", f"{at + 2}:escape", f"{at + 4}:escape", f"{at + 20}:close"]
        return ",".join(events)

    for index, key in ((5, "draw_distance"), (6, "lod"), (7, "background_filter")):
        before = settings()[key]
        run([], display_change(index))
        assert settings()[key] != before
        run([], "10:t,80:close")
        assert settings()[key] == settings(2)[key]
        run([], display_change(index, joined=True))
        assert settings()[key] == settings(2)[key] == before

    hotkeys = run(["--window", "--aspect", "21:9"], "10:t,80:f5,82:f6,84:f9,86:f12,100:f12,120:close")
    assert settings()["scale"] == settings(2)["scale"] == "2"
    assert settings()["picture_shape"] == settings(2)["picture_shape"] == "5"
    assert settings()["frame_timings"] == settings(2)["frame_timings"] == "1"
    for name in ("vr_0001.png", "vr_0002.png"):
        assert (data / "Screenshots" / name).read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
    run([], "10:f12,40:close")
    assert (data / "Screenshots/vr_0003.png").exists()

    fullscreen = run(["--fullscreen"], "20:status,40:close")
    assert re.search(r"status .*fullscreen=1", fullscreen)
    assert settings()["fullscreen"] == "1"
    fullscreen = run([], "20:status,40:close")
    assert re.search(r"status .*fullscreen=1", fullscreen)
    assert settings()["fullscreen"] == "1"
    fullscreen = run(["--window"], "20:status,40:close")
    assert re.search(r"status .*fullscreen=0", fullscreen)
    assert settings()["fullscreen"] == "0"
    for key in ("f11", "f", "alt-return"):
        run([], f"10:{key},30:close")
        assert settings()["fullscreen"] == "1"
        run([], f"10:{key},30:close")
        assert settings()["fullscreen"] == "0"
    run([], "10:escape,12:f11,14:escape,30:close")
    assert settings()["fullscreen"] == "1"
    run(["--window"], "30:close")

    normal = run([], "160:status,180:close", frames=1000)
    accelerated = run([], "10:tab,160:up-tab,162:status,180:close", frames=1000)
    normal_frames = int(re.search(r"status frame=(\d+)", normal)[1])
    accelerated_frames = int(re.search(r"status frame=(\d+)", accelerated)[1])
    assert accelerated_frames > normal_frames * 1.5, (normal_frames, accelerated_frames)
    assert "two cabinets perf:" in accelerated
    run([], "10:f9,40:close")
    assert settings()["frame_timings"] == "0"

    # Arcade mode uses the single-cabinet host shared with SWA.
    run(["--arcade", "--aspect", "32:9"], "40:close")
    assert picture_shape() == 5
    run(["--arcade"], "40:close")
    assert picture_shape() == 5
    run(["--arcade"], "10:f6,40:close")
    assert picture_shape() == 0
    run(["--arcade", "--aspect", "21:9"], "10:f6,40:close")
    assert picture_shape() == 5

    # The automatic host must honor saved Arcade play and switch both cabinets together.
    arcade_reload = run([], "40:status,60:close")
    assert re.search(r"status .*arcade=1", arcade_reload)
    assert re.search(r"status .*icon=1", arcade_reload)

    def play_style_events(join=False):
        events = ["10:t"] if join else []
        events += ["200:escape", "202:down", "204:down", "206:down", "208:down", "210:return"]
        at = 214  # Play style is the first row in both solo and multiplayer.
        events += [f"{at}:right", f"{at + 40}:status", f"{at + 60}:close"]
        return ",".join(events)

    switched_pc = run([], play_style_events())
    assert re.search(r"status .*arcade=0", switched_pc)
    switched_arcade = run([], play_style_events(join=True), frames=2000)
    assert "reset complete, 2 cabinet(s) running" in switched_arcade
    assert re.search(r"cabinets=2 .*arcade=1", switched_arcade)
    assert settings()["arcade_mode"] == settings(2)["arcade_mode"] == "1"
    for path in (player1_eeprom, player2_eeprom):
        assert int.from_bytes(path.read_bytes()[16:18], sys.byteorder) >> 8 != 26
    switched_pc = run([], play_style_events())
    assert re.search(r"status .*arcade=0", switched_pc)
    assert settings()["arcade_mode"] == "0"
    both_directions = play_style_events(join=True).rsplit(",", 1)[0]
    both_directions += (",320:escape,322:down,324:down,326:down,328:down,330:return,"
                        "340:right,380:status,400:close")
    switched_both = run([], both_directions, frames=2000)
    assert re.search(r"cabinets=2 .*arcade=1", switched_both)
    assert re.search(r"cabinets=2 .*arcade=0", switched_both)
    assert settings()["arcade_mode"] == settings(2)["arcade_mode"] == "0"
    for path in (player1_eeprom, player2_eeprom):
        assert int.from_bytes(path.read_bytes()[16:18], sys.byteorder) >> 8 == 26

    original_settings = (data / "settings.ini").read_text()
    (data / "settings.ini").unlink()
    deck = run([], "30:status,40:close", environment={"SteamDeck": "1"})
    assert re.search(r"status .*fullscreen=1", deck)
    assert settings()["picture_shape"] == "2"
    deck_override = run(["--window", "--aspect", "4:3"], "30:status,40:close",
                        environment={"SteamDeck": "1"})
    assert re.search(r"status .*fullscreen=0", deck_override)
    assert settings()["picture_shape"] == "1"
    (data / "settings.ini").write_text(original_settings)

    # A same-build crash stays on CPU until explicitly retried; older builds try again.
    build = re.search(r"^two cabinets: build (.+)$", solo, re.MULTILINE)[1]
    original_settings = re.sub(r"^renderer_3d = \d+$", "renderer_3d = 2",
                               original_settings, flags=re.MULTILINE)
    (data / "settings.ini").write_text(original_settings)
    marker = data / "gpu-start.crashed"
    marker.write_text(build)
    crashed = run([], "40:close")
    assert "graphics driver closed the game while starting the GPU" in crashed
    assert marker.read_text() == build
    assert settings()["renderer_3d"] == "2"
    retry = run([], "10:escape,12:down,14:return,16:down,18:down,20:right,22:escape,24:escape,40:close")
    assert not marker.exists()
    assert settings()["renderer_3d"] == "2"
    marker.write_text("an older build")
    retried = run([], "40:close")
    assert "older build's GPU marker; retrying" in retried
    assert not marker.exists()

print("Local multiplayer: saves, joins, shared settings, display choices, shortcuts, fullscreen, play style, Steam Deck defaults, icon and GPU crash recovery passed")
