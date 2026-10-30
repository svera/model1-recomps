# MAME files

From MAME (https://github.com/mamedev/mame), BSD-3-Clause, unchanged:

- `v60/*.hxx`: the NEC V60 core's addressing modes and opcodes (copyright-holders: Farfetch'd, R. Belmont)
- `v60/v60d.cpp`, `v60/v60d.h`: the V60 disassembler (same holders)

Code ported from MAME (with the original headers kept) lives in `src/`:

- `src/core/v60_device.*`: the V60 core's glue (v60.cpp, v60.h)
- `src/machine/model1_tgp.cpp`: the Model 1 TGP coprocessor simulation, MAME 0.210 `machine/model1.cpp` (Olivier Galibert)
- `src/machine/model1_video.cpp`: the Model 1 rasterizer, current `sega/model1_v.cpp` (Olivier Galibert)
- `src/machine/s24tile.h`: the System 24 tile chip, `sega/segaic24.cpp` (Olivier Galibert)
- `src/machine/model1.cpp`: memory map and IRQ glue, `sega/model1.cpp`
- `src/machine/m1comm.h`: two-cabinet shared-memory simulation, following `sega/m1comm.cpp`
  (Ariane Fugmann); the loopback transport is new project code.

BSD-3-Clause: redistribution in source and binary form requires keeping the copyright notice,
the list of conditions and the disclaimer; see https://opensource.org/license/bsd-3-clause.

Also from MAME, ported into `src/`:

- `src/core/mb86233.*`: the Fujitsu MB86233 (TGP) core (Olivier Galibert)
- `src/machine/model1_tgp_lle.cpp`: the current Model 1 coprocessor glue, `sega/model1_m.cpp`
- `src/machine/multipcm.*`: the YMW-258-F / 315-5560 MultiPCM, `sound/gew.cpp` and `sound/multipcm.cpp` (Miguel Angel Horna)
- `src/machine/m1audio.*`: the Model 1 sound board map, `shared/segam1audio.cpp` (R. Belmont)
- `src/machine/dsbz80.*`: the Sega Z80 Digital Sound Board, `sega/dsbz80.cpp` (R. Belmont, Olivier Galibert)
- `src/machine/mpeg_audio.*`: the MPEG audio Layer I/II decoder, `sound/mpeg_audio.cpp` (Olivier Galibert)
