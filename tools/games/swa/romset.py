"""Star Wars Arcade's US ROM set: MAME 0.280's ROM_START(swa), past the shared CPU board."""

ROMS = [
    ('epr-16669.14', 0x52e5e7e4, 'maincpu', 0x200000, 'byte16'),
    ('epr-16670.15', 0x1e7ecabd, 'maincpu', 0x200001, 'byte16'),
    ('epr-16668.5', 0x9e112425, 'maincpu', 0xf80000, 'raw'),
    # ROM_RELOAD: the program also occupies both low-ROM windows.
    ('epr-16668.5', 0x9e112425, 'maincpu', 0x000000, 'raw'),
    ('epr-16668.5', 0x9e112425, 'maincpu', 0x080000, 'raw'),
    ('315-5711.bin', 0xc5ddb8fc, 'tgp_program', 0x0, 'raw'),  # BAD_DUMP in MAME 0.280
    ('epr-16470.7', 0x7da18cf7, 'sndcpu', 0x00000, 'wordswap'),
    # ROM_RELOAD repeats the byte-swapped 68000 code.
    ('epr-16470.7', 0x7da18cf7, 'sndcpu', 0x80000, 'wordswap'),
    ('mpr-16486.32', 0x7df50533, 'pcm1', 0x000000, 'raw'),
    ('mpr-16487.33', 0x31b28dfa, 'pcm1', 0x200000, 'raw'),
    ('mpr-16484.4', 0x9d4c334d, 'pcm2', 0x000000, 'raw'),
    ('mpr-16485.5', 0x95aadcad, 'pcm2', 0x200000, 'raw'),
    ('epr-16471.2', 0xf4ee84a4, 'mpegcpu', 0x000000, 'raw'),
    ('mpr-16514.57', 0x3175b0be, 'mpeg', 0x000000, 'raw'),
    ('mpr-16515.58', 0x3114d748, 'mpeg', 0x200000, 'raw'),
    ('mpr-16476.26', 0xd48609ae, 'polygons', 0x000000, 'word32'),
    ('mpr-16477.27', 0xb979b082, 'polygons', 0x000002, 'word32'),
    ('mpr-16478.28', 0x80c780f7, 'polygons', 0x400000, 'word32'),
    ('mpr-16479.29', 0xe43183b3, 'polygons', 0x400002, 'word32'),
    ('mpr-16480.30', 0x3185547a, 'polygons', 0x800000, 'word32'),
    ('mpr-16481.31', 0xce8d76fe, 'polygons', 0x800002, 'word32'),
    ('mpr-16472.39', 0x5a0d7553, 'copro_data', 0x000000, 'byte32'),
    ('mpr-16473.40', 0x876c5399, 'copro_data', 0x000001, 'byte32'),
    ('mpr-16474.41', 0x5864a26f, 'copro_data', 0x000002, 'byte32'),
    ('mpr-16475.42', 0xb9266be9, 'copro_data', 0x000003, 'byte32'),
]

REGIONS = {
    'copro_data': (0x200000, 0x00),
    'tgp_program': (0x2000, 0x00),
    'polygons': (0xc00000, 0x00),
    'mpegcpu': (0x20000, 0x00),
    'mpeg': (0x800000, 0x00),
}

OPTIONAL = []
