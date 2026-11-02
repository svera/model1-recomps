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
        last_pass = max((int(event.split(":")[0]) for event in events.split(",") if event), default=0)
        if not any(event.endswith(":close") for event in events.split(",")):
            last_pass = max(frames, last_pass + 20)
            events += ("," if events else "") + f"{last_pass}:close"
        # VSync changes the number of simulation frames per event-loop pass.
        env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER=str(max(frames, last_pass * 4 + 100)),
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
    english = run([], "10:escape,12:down,14:down,16:down,18:down,20:return,22:return,24:right,26:escape,28:escape,30:escape,80:close", frames=1000)
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
    game_settings = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,94:return,96:right,98:escape,100:escape,102:escape,140:status,160:close", frames=1000)
    assert "reset complete, 2 cabinet(s) running" in game_settings
    assert game_settings.count("cabinet 2 compiled") == 1
    assert re.search(r"cabinets=2 .*role=1", game_settings)
    assert player2_eeprom.read_bytes() != before_language

    before_color = player2_eeprom.read_bytes()
    before_player1_color = player1_eeprom.read_bytes()
    car_color = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,94:return,96:down,98:right,100:escape,102:escape,104:escape,140:status,160:close", frames=1000)
    assert "reset complete, 2 cabinet(s) running" in car_color
    assert car_color.count("cabinet 2 compiled") == 1
    assert re.search(r"cabinets=2 .*role=1", car_color)
    assert player2_eeprom.read_bytes() != before_color
    assert car_color_value(player2_eeprom) == 2
    assert player1_eeprom.read_bytes() == before_player1_color

    before_saved_color = player2_eeprom.read_bytes()
    saved_car_color = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,94:return,96:down,98:right,99:save,100:escape,102:escape,104:escape,140:status,160:close", frames=1000)
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
        shared_race_type = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,94:down,96:right,97:save,98:escape,100:escape,140:status,160:close", frames=1000)
        assert "reset complete, 2 cabinet(s) running" in shared_race_type
        assert re.search(r"cabinets=2 .*role=1", shared_race_type)
        assert race_type(player1_eeprom) == expected_race_type
        assert race_type(player2_eeprom) == expected_race_type
        assert car_color_value(player2_eeprom) == 3

    for _ in range(4):
        expected_course = (machine_setting(player1_eeprom, 0) + 1) % 4
        shared_course = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,94:down,95:down,96:right,97:save,98:escape,100:escape,140:status,160:close", frames=1000)
        assert "reset complete, 2 cabinet(s) running" in shared_course
        assert re.search(r"cabinets=2 .*role=1", shared_course)
        assert machine_setting(player1_eeprom, 0) == expected_course
        assert machine_setting(player2_eeprom, 0) == expected_course
        assert car_color_value(player2_eeprom) == 3

    # Shared settings are absent from each player's page: Difficulty follows Car color.
    before_difficulty = machine_setting(player2_eeprom, 8)
    difficulty = run([], "10:t,80:escape,82:down,84:down,86:down,88:down,90:return,92:down,94:return,96:down,98:down,100:right,102:escape,104:escape,106:escape,160:close", frames=1000)
    assert "reset complete, 2 cabinet(s) running" in difficulty
    assert machine_setting(player2_eeprom, 8) == (before_difficulty + 1) % 4
    assert machine_setting(player1_eeprom, 0) == expected_course
    assert machine_setting(player2_eeprom, 0) == expected_course

    # A shared course chosen solo must also apply to a cabinet that joins later.
    expected_course = (expected_course + 1) % 4
    expected_race_type = (expected_race_type + 1) % 2
    solo_course = run([], "10:escape,12:down,14:down,16:down,18:down,20:return,22:down,23:right,24:down,25:right,26:escape,28:escape,60:status,80:t,140:status,160:close", frames=1000)
    assert "reset complete, 1 cabinet(s) running" in solo_course
    assert "player 2 joined, two cabinets running" in solo_course
    assert machine_setting(player1_eeprom, 0) == expected_course
    assert machine_setting(player2_eeprom, 0) == expected_course
    assert race_type(player1_eeprom) == expected_race_type
    assert race_type(player2_eeprom) == expected_race_type

print("Local multiplayer: default solo launch, original save preservation/migration, controller join/disconnect, deferred join, linked boot, saved roles, reset/rejoin, player 2 settings, shared race type and course selection passed")
