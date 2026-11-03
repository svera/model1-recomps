import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile
import zlib


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('romset', ROOT / 'tools/romset.py')
romset = importlib.util.module_from_spec(spec)
spec.loader.exec_module(romset)


class RomsetToolTest(unittest.TestCase):
    def build(self, data, filename='program.bin'):
        primary = b'new!'
        title = SimpleNamespace(
            ROMS=[('program.bin', zlib.crc32(primary), 'tgp_program', 0, 'raw')],
            REGIONS={'tgp_program': (4, 0)},
            OPTIONAL=[],
        )
        with tempfile.TemporaryDirectory() as tmp:
            archive, output = Path(tmp) / 'test.zip', Path(tmp) / 'regions'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr(filename, data)
            with (patch.object(romset, 'BOARD_ROMS', []),
                  patch.object(romset, 'COMMON_REGIONS', {}),
                  patch.object(romset, 'load_game', return_value=title),
                  patch.object(romset, 'find_optional', return_value=None),
                  patch.object(romset.sys, 'argv', ['romset.py', '--game', 'swa', str(archive), str(output)]),
                  contextlib.redirect_stdout(io.StringIO())):
                romset.main()
            return (output / 'tgp_program.bin').read_bytes()

    def test_primary_dump_is_unchanged(self):
        self.assertEqual(self.build(b'new!'), b'new!')

    def test_merged_archive_uses_crc_lookup(self):
        self.assertEqual(self.build(b'new!', filename='clone/renamed.bin'), b'new!')

    def test_unknown_dump_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, 'CRC .* expected'):
            self.build(b'bad!')

    def test_unsupported_dump_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, 'CRC .* expected'):
            self.build(b'old!')


if __name__ == '__main__':
    unittest.main()
