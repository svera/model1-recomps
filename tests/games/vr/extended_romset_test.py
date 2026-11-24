import subprocess
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
ROM_DIR = Path(sys.argv.pop(1)).resolve()
TEST_BIN = Path(sys.argv.pop(1)).resolve()


class ExtendedRomsetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.temp.name)
        cls.io = ROM_DIR / 'model1io.zip'
        with zipfile.ZipFile(ROM_DIR / 'vr.zip') as archive:
            cls.members = [(i.filename, archive.read(i)) for i in archive.infolist()]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def archive(self, name, mutate):
        path = self.path / name
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for filename, data in self.members:
                archive.writestr(filename, mutate(filename, data))
        return path

    def load(self, archive, regions):
        return subprocess.run([str(TEST_BIN), 'vr', str(archive), str(self.io), str(regions)],
                              cwd=ROOT, text=True, capture_output=True)

    def test_tuned_data_is_accepted_by_both_loaders(self):
        def mutate(name, data):
            if name == 'epr-14882.14':
                data = bytearray(data)
                data[(0x2840b0 - 0x200000) // 2] ^= 1
            return data
        archive = self.archive('tuned.zip', mutate)
        regions = self.path / 'tuned'
        subprocess.run([sys.executable, 'tools/romset.py', '--game', 'vr', str(archive), str(regions),
                        str(self.io)], cwd=ROOT, check=True, capture_output=True)
        result = self.load(archive, regions)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_renamed_boot_rom_is_accepted_by_both_loaders(self):
        archive = self.path / 'renamed.zip'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as out:
            for name, data in self.members:
                out.writestr('renamed-boot.rom' if name == 'epr-14878a.4' else name, data)
        regions = self.path / 'renamed'
        subprocess.run([sys.executable, 'tools/romset.py', '--game', 'vr', str(archive), str(regions),
                        str(self.io)], cwd=ROOT, check=True, capture_output=True)
        result = self.load(archive, regions)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_wrong_size_is_rejected(self):
        archive = self.archive('short.zip', lambda name, data: data[:-1] if name == 'mpr-14898.39' else data)
        result = self.load(archive, self.path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('incorrect size', result.stderr)

    def test_wrong_boot_rom_is_rejected(self):
        def mutate(name, data):
            return data[:-1] + bytes([data[-1] ^ 1]) if name == 'epr-14879a.5' else data
        result = self.load(self.archive('wrong-boot.zip', mutate), self.path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('epr-14879a.5', result.stderr)

    def test_invalid_course_directory_is_rejected(self):
        def mutate(name, data):
            if name == 'epr-14882.14':
                data = bytearray(data)
                data[(0x278d70 + 10 * 24 + 28 - 0x200000) // 2] ^= 1
            return data
        result = self.load(self.archive('bad-directory.zip', mutate), self.path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('invalid course directory', result.stderr)

    def test_damaged_zip_entry_is_rejected(self):
        archive = self.archive('damaged.zip', lambda name, data: data)
        with zipfile.ZipFile(archive) as source:
            entry = source.getinfo('mpr-14898.39')
        with archive.open('r+b') as out:
            out.seek(entry.header_offset + 26)
            name_len, extra_len = struct.unpack('<HH', out.read(4))
            position = entry.header_offset + 30 + name_len + extra_len + entry.compress_size // 2
            out.seek(position)
            byte = out.read(1)
            out.seek(position)
            out.write(bytes([byte[0] ^ 1]))
        result = self.load(archive, self.path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('damaged', result.stderr)


if __name__ == '__main__':
    unittest.main()
