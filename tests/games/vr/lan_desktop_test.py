"""Nonvisual two-process LAN checks through the desktop's real menu/event path."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


binary, regions = (Path(arg).resolve() for arg in sys.argv[1:])


def menu_events(first):
    events = [f"{first}:escape"]
    events += [f"{first + 2 + i * 2}:down" for i in range(5)]
    return events + [f"{first + 14}:return"]


with tempfile.TemporaryDirectory(prefix="vr-lan-desktop-") as directory:
    root = Path(directory)
    processes = []
    logs = []
    for player in range(2):
        data = root / str(player)
        data.mkdir()
        for region in regions.glob("*.bin"):
            (data / region.name).symlink_to(region)
        # No GPU startup verification, captures, screenshots or pixel comparisons.
        (data / "settings.ini").write_text(
            "renderer_3d = 0\nscale = 1\nantialiasing = 1\nfps = 30\n")
        events = menu_events(10)
        if player == 0:
            events += ["30:return", "700:return", "2000:1", "6500:status",
                       "7500:escape", "7800:status", "8000:status", "8500:escape",
                       "11000:status", "12000:p", "12400:status", "12700:status",
                       "13000:p", "14000:escape", "14200:status", "16000:status", "17000:close"]
        else:
            events += ["30:down", "32:return", "200:down", "202:return",
                       "2000:1", "6000:escape", "6200:status", "6300:escape",
                       "6500:status", "6800:p", "7800:status", "8000:status",
                       "11000:status", "12400:status", "12500:p", "12700:status",
                       "14500:close"]
        env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER="20000", M1_SAVE="1",
                   M1_EVENTS_AT=",".join(events),
                   SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x1209/0x0001")
        log = (root / f"{player}.log").open("w+")
        logs.append(log)
        processes.append(subprocess.Popen([str(binary), str(data)], env=env,
                                          stdout=log, stderr=log))
    try:
        for process in processes:
            assert process.wait(timeout=180) == 0
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    outputs = []
    for log in logs:
        log.seek(0)
        outputs.append(log.read())
        log.close()
    for output in outputs:
        print(output)
    assert "LAN: preparing cabinet 1" in outputs[0]
    assert "LAN: preparing cabinet 2" in outputs[1]
    host_status = re.findall(
        r"LAN: status cabinet=(\d+) running=(\d+) ticks=(\d+) phase=(\w+) "
        r"participants=(\d+) menu=(\d+) frame=(\d+)", outputs[0])
    assert len(host_status) >= 7, host_status
    assert any(int(row[2]) > 100 and row[1] == "1" for row in host_status)
    assert any(row[4] == "2" and row[3] == "14" for row in host_status), host_status
    assert re.search(r"participants=2", outputs[1]), outputs[1]
    assert host_status[1][2] == host_status[2][2], host_status
    assert host_status[1][6] == host_status[2][6], host_status
    assert int(host_status[3][2]) > int(host_status[2][2]), host_status
    assert host_status[4][2] == host_status[5][2], host_status
    assert host_status[-2][0] == "1" and host_status[-2][5] == "1", host_status
    for output in outputs:
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
    for player in range(2):
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
    print("LAN desktop: create/find/join/start, host-only pause, 3-2-1 resume, peer loss and saves passed")
    print("Local split-screen join/disconnect still works after a LAN session")
