"""Wing War's world ROM set: MAME's src/mame/sega/model1.cpp, ROM_START(wingwar)."""

ROMS = [
    ('epr-16729.14', 0x7edec2cc, 'maincpu', 0x200000, 'byte16'),
    ('epr-16730.15', 0xbab24dee, 'maincpu', 0x200001, 'byte16'),
    ('epr-16953.4', 0xc821a920, 'maincpu', 0xfc0000, 'raw'),
    ('epr-16953.4', 0xc821a920, 'maincpu', 0x000000, 'raw'),
    ('epr-16952.5', 0x03a3ecc5, 'maincpu', 0xfe0000, 'raw'),
    ('epr-16952.5', 0x03a3ecc5, 'maincpu', 0x020000, 'raw'),
    ('mpr-16738.6', 0x51518ffa, 'maincpu', 0x1000000, 'byte16'),
    ('mpr-16737.7', 0x37b1379c, 'maincpu', 0x1000001, 'byte16'),
    ('mpr-16736.8', 0x10b6a025, 'maincpu', 0x1100000, 'byte16'),
    ('mpr-16735.9', 0xc82fd198, 'maincpu', 0x1100001, 'byte16'),
    ('mpr-16734.10', 0xf76371c1, 'maincpu', 0x1200000, 'byte16'),
    ('mpr-16733.11', 0xe105847b, 'maincpu', 0x1200001, 'byte16'),
    ('315-5711.bin', 0x6a21f304, 'tgp_program', 0x0000, 'raw'),
    ('epr-17126.7', 0x50178e40, 'sndcpu', 0x000000, 'wordswap'),
    ('epr-16752.8', 0x6541c48f, 'sndcpu', 0x020000, 'wordswap'),
    ('epr-16752.8', 0x6541c48f, 'sndcpu', 0x080000, 'wordswap'),
    ('mpr-16753.32', 0x324a8333, 'pcm1', 0x000000, 'raw'),
    ('mpr-16754.33', 0x144f3cf5, 'pcm1', 0x200000, 'raw'),
    ('mpr-16755.4', 0x4baaf878, 'pcm2', 0x000000, 'raw'),
    ('mpr-16756.5', 0xd9c40672, 'pcm2', 0x200000, 'raw'),
    ('mpr-16743.26', 0xa710d33c, 'polygons', 0x000000, 'word32'),
    ('mpr-16744.27', 0xde796e1f, 'polygons', 0x000002, 'word32'),
    ('mpr-16745.28', 0x905b689c, 'polygons', 0x400000, 'word32'),
    ('mpr-16746.29', 0x163b312e, 'polygons', 0x400002, 'word32'),
    ('mpr-16747.30', 0x7353bb12, 'polygons', 0x800000, 'word32'),
    ('mpr-16748.31', 0x8ce98d3a, 'polygons', 0x800002, 'word32'),
    ('mpr-16749.32', 0x0e36dc1a, 'polygons', 0xc00000, 'word32'),
    ('mpr-16750.33', 0xe4f0b98d, 'polygons', 0xc00002, 'word32'),
    ('mpr-16741.39', 0x84b2ffd8, 'copro_data', 0x000000, 'byte32'),
    ('mpr-16742.40', 0xe9cc12bb, 'copro_data', 0x000001, 'byte32'),
    ('mpr-16739.41', 0x6c73e98f, 'copro_data', 0x000002, 'byte32'),
    ('mpr-16740.42', 0x44b31007, 'copro_data', 0x000003, 'byte32'),
]

REGIONS = {
    'copro_data': (0x200000, 0x00),
    'tgp_program': (0x2000, 0x00),
}

# Wing War requires the Advanced Model 1 I/O board BIOS, not the shared Model 1 I/O firmware.
USE_IOBOARD = False
USE_ADVANCED_IOBOARD = True
OPTIONAL = []
