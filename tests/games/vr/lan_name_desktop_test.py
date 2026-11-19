"""Nonvisual LAN name editing and persistence through the desktop menu."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


binary, regions = (Path(arg).resolve() for arg in sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="vr-lan-name-") as directory:
    data = Path(directory)
    for region in regions.glob("*.bin"):
        (data / region.name).symlink_to(region)
    settings = data / "settings.ini"
    settings.write_text("renderer_3d = 0\nscale = 1\nantialiasing = 1\nfps = 30\n")

    def run(extra):
        events = ["1000:escape"] + [f"{1100 + i * 100}:down" for i in range(5)]
        events += ["1700:return"] + extra
        env = dict(os.environ, M1_HIDDEN="1", M1_QUIT_AFTER="-1", M1_SAVE="1",
                   M1_EVENTS_MS=",".join(events),
                   SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x1209/0x0001")
        result = subprocess.run([str(binary), str(data)], env=env, text=True,
                                capture_output=True, timeout=90)
        assert result.returncode == 0, result.stderr
        match = re.search(r"^lan_name = (.+)$", settings.read_text(), re.MULTILINE)
        assert match, settings.read_text()
        return match.group(1)

    default = run(["3000:close"])
    assert re.fullmatch(r"RACER-\d{1,4}", default), default
    assert run(["3000:close"]) == default
    changed = run(["2000:down", "2100:down", "2200:return", "2300:text-pilot-42",
                   "2400:return", "3000:close"])
    assert changed == "PILOT-42", changed
    assert run(["3000:close"]) == changed
    assert run(["2000:down", "2100:down", "2200:return", "2300:text-other",
                "2400:escape", "3000:close"]) == changed
    assert run(["2000:down", "2100:down", "2200:return", "2300:text-bad@",
                "2400:return", "2500:text-good", "2600:return", "3000:close"]) == "GOOD"
    print("LAN name: random default, editing, cancellation and restart persistence passed")
