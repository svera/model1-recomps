"""Nonvisual LAN checks through the desktop's real menu/event path."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


binary, regions = (Path(arg).resolve() for arg in sys.argv[1:3])
count = int(sys.argv[3]) if len(sys.argv) > 3 else 2
time_scale = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0
assert 2 <= count <= 4
assert time_scale > 0


def menu_events(first):
    events = [f"{first}:escape"]
    events += [f"{first + 2 + i * 2}:down" for i in range(5)]
    return events + [f"{first + 14}:return"]


with tempfile.TemporaryDirectory(prefix="vr-lan-desktop-") as directory:
    root = Path(directory)
    processes = []
    logs = []
    for player in range(count):
        data = root / str(player)
        data.mkdir()
        for region in regions.glob("*.bin"):
            (data / region.name).symlink_to(region)
        # No GPU startup verification, captures, screenshots or pixel comparisons.
        (data / "settings.ini").write_text(
            "renderer_3d = 0\nscale = 1\nantialiasing = 1\nfps = 30\n")
        events = [f"{1000 + (int(at) - 10) * 40}:{name}"
                  for at, name in (event.split(":", 1) for event in menu_events(10))]
        if player == 0:
            events += ["2000:return", "6500:return", "20000:1", "21000:1", "55000:status",
                       "60000:escape", "61000:status", "62000:status", "64000:escape",
                       "69000:status", "70000:p", "71000:status", "72000:status",
                       "74000:p", "78000:escape", "79000:status", "82000:status",
                       "90000:status", "92000:close"]
        elif player == 1:
            events += ["2000:down", "2200:return", "4500:down", "4700:return",
                       "20000:1", "21000:1", "52000:escape", "53000:status", "54000:escape",
                       "55000:status", "57000:p", "61000:status", "62000:status",
                       "69000:status", "71000:status", "71500:p", "72000:status",
                       "85000:close"]
        else:
            events += ["2000:down", "2200:return", f"{4500 + player * 200}:down",
                       f"{4700 + player * 200}:return", "20000:1", "21000:1", "55000:status",
                       "61000:status", "62000:status"]
            if player == 2:
                events += ["69000:status", "71000:status", "72000:status", "80000:close"]
            else:
                events += ["65000:close"]
        events = [f"{int(int(at) * time_scale) if int(at) >= 20000 else at}:{name}"
                  for at, name in (event.split(":", 1) for event in events)]
        env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER="-1", M1_SAVE="1",
                   M1_EVENTS_MS=",".join(events),
                   SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x1209/0x0001")
        log = (root / f"{player}.log").open("w+")
        logs.append(log)
        processes.append(subprocess.Popen([str(binary), str(data)], env=env,
                                          stdout=log, stderr=log))
    try:
        for process in processes:
            assert process.wait(timeout=360) == 0
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
        for process in processes:
            if process.poll() is None:
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
        outputs = []
        for log in logs:
            log.seek(0)
            output = log.read(128 * 1024)
            if log.read(1):
                output += "\nDesktop log truncated at 128 KB; investigate repeated diagnostics.\n"
            outputs.append(output)
            log.close()
        for output in outputs:
            print(output)
    identities = []
    for output in outputs:
        assert "Model 1 renderer: invalid" not in output, output
        prepared = re.search(r"LAN: preparing cabinet (\d+),", output)
        assert prepared, output
        player = int(prepared[1])
        identities.append(player)
        boot_complete = f"LAN: cabinet {player} boot complete; showing game"
        assert output.count(boot_complete) == 1, output
        assert output.index(f"LAN: preparing cabinet {player}") < output.index(boot_complete)
    assert sorted(identities) == list(range(1, count + 1)), identities
    host_status = re.findall(
        r"LAN: status cabinet=(\d+) running=(\d+) ticks=(\d+) phase=(\w+) "
        r"participants=(\d+) menu=(\d+) frame=(\d+)", outputs[0])
    assert len(host_status) >= 7, host_status
    assert any(int(row[2]) > 100 and row[1] == "1" for row in host_status)
    assert any(row[4] == str(count) and row[3] == "14" for row in host_status), host_status
    for output in outputs:
        assert f"participants={count}" in output, output
    assert host_status[1][2] == host_status[2][2], host_status
    assert host_status[1][6] == host_status[2][6], host_status
    assert int(host_status[3][2]) > int(host_status[2][2]), host_status
    assert host_status[4][2] == host_status[5][2], host_status
    assert host_status[-2][0] == "1" and host_status[-2][5] == "1", host_status
    for output in outputs[:2]:
        for value in (3, 2, 1):
            assert f"LAN: resuming in {value}..." in output, output
        assert "LAN: paused by host" in output, output
        paused_status = re.findall(r"LAN: status .*frame=(\d+) ready=\d+ paused=1 countdown=0", output)
        assert len(paused_status) >= 4, output
        assert paused_status[0] == paused_status[1], paused_status
        assert paused_status[2] == paused_status[3], paused_status
    guest_status = re.findall(
        r"LAN: status .*ticks=(\d+) .*menu=(\d+) frame=(\d+) ready=\d+ paused=(\d+)", outputs[1])
    assert guest_status[0][1] == "1" and guest_status[0][3] == "0", guest_status
    assert int(guest_status[1][0]) > int(guest_status[0][0]), guest_status
    assert host_status[-1][0] == "0", host_status  # guest closed: returned to room
    assert host_status[-1][3] in ("03", "07", "10"), host_status  # can start again from the room
    assert "linked race has ended" in outputs[0]
    if count > 2:
        assert re.search(r"LAN: status cabinet=1 running=1 .*racers=2", outputs[0]), outputs[0]
        assert f"cabinet {identities[-1]} disconnected" in outputs[0], outputs[0]
    for player in range(count):
        # Session master/slave roles must never replace the standalone EEPROM.
        eeprom = (root / str(player) / "eeprom_93c46.sav").read_bytes()
        assert eeprom[12] == 0
    local_env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER="2000",
                     M1_EVENTS_AT="10:pad2,120:status,140:disconnect2,180:status,200:close",
                     SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x1209/0x0001")
    local = subprocess.run([str(binary), str(root / "0")], env=local_env,
                           capture_output=True, text=True, timeout=120)
    print(local.stderr)
    assert local.returncode == 0, local.stderr
    assert "player 2 joined" in local.stderr
    assert re.search(r"status .*cabinets=2 .*role=1", local.stderr)
    assert "player 2 controller disconnected; returned to solo play" in local.stderr
    assert re.search(r"status .*cabinets=1 .*role=0", local.stderr)
    print(f"LAN desktop ({count} racers): create/find/join/start, host-only pause, "
          "3-2-1 resume, guest loss, last-racer teardown and saves passed")
    print("Local split-screen join/disconnect still works after a LAN session")
