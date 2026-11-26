"""Nonvisual two-player LAN lifecycle through the desktop menu."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

binary, regions = (Path(arg).resolve() for arg in sys.argv[1:3])
test_worker = os.environ.get("M1_MULTIPLAYER_GAME_THREAD") is not None
settings = f"renderer_3d = 0\nscale = 1\nfps = {120 if test_worker else 30}\n"
with tempfile.TemporaryDirectory(prefix="wingwar-lan-desktop-") as directory:
    root = Path(directory)
    env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER="-1", M1_SAVE="1",
               M1_NO_GPU_RENDERER="1", SDL_RENDER_DRIVER="software",
               SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x1209/0x0001")
    for key in ("M1_SHOT_AT", "M1_EVENTS_AT", "M1_SCRIPT", "M1_GPU_CHECK", "M1_GPU3D_CHECK"):
        env.pop(key, None)

    seed = root / "seed"
    seed.mkdir()
    for region in regions.glob("*.bin"):
        (seed / region.name).symlink_to(region)
    (seed / "settings.ini").write_text(settings)
    initial = subprocess.run([str(binary), str(seed)], env=dict(env, M1_QUIT_AFTER="10"),
                             capture_output=True, text=True, timeout=120)
    assert initial.returncode == 0, initial.stderr

    def set_difficulty(record, value):
        record[0x17] = value
        crc = 0xdeb00
        for i in range(0x3b):
            if i < 0x3a:
                crc = (crc & 0xffffff00) | record[0xa + i]
            for _ in range(8):
                crc = ((crc << 1) ^ (0x10210000 if crc & 0x80000000 else 0)) & 0xffffffff
        record[8], record[9] = (crc >> 16) & 0xff, (crc >> 24) & 0xff
        record[0x44:0x80] = record[8:0x44]

    processes, logs = [], []
    for player in range(3):
        data = root / str(player)
        data.mkdir()
        for region in regions.glob("*.bin"):
            (data / region.name).symlink_to(region)
        (data / "settings.ini").write_text(settings)
        for name in ("nvram.sav", "eeprom_93c46.sav"):
            shutil.copyfile(seed / name, data / name)
        if player == 1:
            for name, offset in (("nvram.sav", 0xf800), ("eeprom_93c46.sav", 0)):
                path = data / name
                saved = bytearray(path.read_bytes())
                record = saved[offset:offset + 0x80]
                set_difficulty(record, 7)
                saved[offset:offset + 0x80] = record
                path.write_bytes(saved)
        events = ["1000:escape"] + [f"{1080 + i * 80}:down" for i in range(6)] + ["1640:return"]
        if player == 0:
            events += ["2200:return", "12000:return", "30000:1", "32000:1",
                       "50000:status", "65000:status", "70000:close"]
        elif player == 1:
            events += ["2200:down", "2400:return", "5000:down", "5200:return",
                       "30000:1", "32000:1", "50000:status", "55000:close"]
        else:
            events += ["2200:down", "2400:return", "7000:down", "7200:return", "16000:close"]
        log = (root / f"{player}.log").open("w+")
        logs.append(log)
        processes.append(subprocess.Popen([str(binary), str(data)],
            env=dict(env, M1_EVENTS_MS=",".join(events)), stdout=log, stderr=log))
    try:
        for process in processes:
            assert process.wait(timeout=300) == 0
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
            outputs.append(log.read())
            log.close()
        for output in outputs:
            print(output)

    for player, output in enumerate(outputs[:2], 1):
        if test_worker:
            ahead = re.search(r"(\d+) ahead frames \((\d+) LAN, (\d+) early\)", output)
            assert ahead and int(ahead[2]) > 0, output
        assert f"LAN: preparing cabinet {player}" in output, output
        assert f"LAN: cabinet {player} boot complete; showing game" in output, output
        assert re.search(rf"LAN: status cabinet={player} running=1 ticks=[1-9]\d* .*participants=2", output), output
        eeprom = (root / str(player - 1) / "eeprom_93c46.sav").read_bytes()
        nvram = (root / str(player - 1) / "nvram.sav").read_bytes()
        assert eeprom[0x24] == 0 and nvram[0xf824] == 0
        assert eeprom[0x17] == (7 if player == 2 else 0)
        assert nvram[0xf817] == eeprom[0x17]
    assert "ROOM FULL" in outputs[2], outputs[2]
    assert "Fewer than two players remain. The linked game has ended." in outputs[0], outputs[0]
    assert re.search(r"LAN: status cabinet=0 .*phase=02", outputs[0]), outputs[0]
    print("Wing War LAN: two cabinets connected, third rejected, disconnect and standalone saves passed")
