from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def check(binary, game, archives, error=None):
    result = subprocess.run(
        [binary, game, *map(str, archives), f'build/{game}'],
        capture_output=True, text=True,
    )
    if error is None:
        if result.returncode != 0:
            raise AssertionError(f'{game}: {result.stdout}{result.stderr}')
    elif result.returncode != 1 or error not in result.stderr:
        raise AssertionError(
            f'{game}: expected rejection with {error!r}, got '
            f'{result.returncode}: {result.stdout}{result.stderr}'
        )


def test_game(binary, game):
    advanced = game == 'wingwar'
    board = 'model1io2.zip' if advanced else 'model1io.zip'
    firmware = 'epr-16891.6' if advanced else 'epr-14869b.25'
    crc = 0xa33f84d1 if advanced else 0x2d093304
    error = (
        f'Missing or damaged {"Advanced " if advanced else ""}'
        f'Model 1 I/O BIOS {firmware} ({board}).'
    )
    with zipfile.ZipFile(Path('roms') / board) as source:
        data = source.read(next(info for info in source.infolist() if info.CRC == crc))
    with tempfile.TemporaryDirectory() as tmp:
        game_zip = Path(tmp) / f'{game}.zip'
        board_zip = Path(tmp) / board
        with zipfile.ZipFile(Path('roms') / f'{game}.zip') as source:
            with zipfile.ZipFile(game_zip, 'w', compression=zipfile.ZIP_DEFLATED) as target:
                for info in source.infolist():
                    if info.CRC not in (0x2d093304, 0xa33f84d1):
                        target.writestr(info, source.read(info))
        check(binary, game, [game_zip], error)
        with zipfile.ZipFile(board_zip, 'w') as target:
            target.writestr(firmware, b'wrong firmware')
        check(binary, game, [game_zip, board_zip], error)
        with zipfile.ZipFile(board_zip, 'w') as target:
            target.writestr(firmware, data)
        with board_zip.open('r+b') as target:
            target.seek(30 + len(firmware))
            target.write(bytes([data[0] ^ 0xff]))
        check(binary, game, [game_zip, board_zip], error)
        with zipfile.ZipFile(board_zip, 'w') as target:
            target.writestr('renamed-firmware.bin', data)
        check(binary, game, [game_zip, board_zip])
    print(f'{game}: missing, wrong and damaged I/O BIOS rejected; renamed valid BIOS accepted')


if __name__ == '__main__':
    for game in sys.argv[2:]:
        if (Path('roms') / f'{game}.zip').is_file():
            test_game(sys.argv[1], game)
