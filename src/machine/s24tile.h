// Sega System 24 tilemap chip (315-5242 family) as Model 1 uses it for its 2D layers: four
// 64x64 maps of 8x8 4bpp tiles, per-layer scroll, line scroll and the 128-pixel window mask.
// Ported from MAME's segaic24.cpp (BSD-3-Clause, copyright-holders: Olivier Galibert).
#pragma once
#include "mame_lite.h"

class segas24_tile
{
public:
	enum { DRAW_OPAQUE = 0x10000 };

	segas24_tile() : char_ram(0x80000 / 2), tile_ram(0x10000 / 2) {}

	uint16_t tile_r(offs_t o) const { return tile_ram[o & 0x7fff]; }
	uint16_t char_r(offs_t o) const { return char_ram[o & 0x3ffff]; }
	uint32_t tile_code(uint16_t entry) const { return entry & tile_mask; }
	// Writes note the map entries and tiles they change, so update() decodes only those again.
	void tile_w(offs_t o, uint16_t d, uint16_t m)
	{
		o &= 0x7fff;
		const uint16_t v = (tile_ram[o] & ~m) | (d & m);
		if (v == tile_ram[o]) return;
		tile_ram[o] = v;
		if (o < 0x4000) { m_entry_dirty[o] = 1; m_entries_dirty = true; }
		else if (o < 0x4800) m_line_revision[(o - 0x4000) >> 9] = ++m_revision;
		else if (o >= 0x5000 && o < 0x5008) m_scroll_revision[o - 0x5000] = ++m_revision;
		else if (o >= 0x6000 && o < 0x7000) m_mask_revision[(o - 0x6000) >> 11] = ++m_revision;
	}
	uint64_t layer_revision(int layer) const
	{
		const int map = layer >> 1, pair = map & 2;
		const uint16_t ctrl = tile_ram[0x5004 + pair];
		uint64_t revision = std::max({m_map_revision[map], scroll_revision(map),
			line_revision(map), m_mask_revision[map >> 1]});
		// Split windows can source pixels from either map in the pair.
		if (ctrl & 0x6000) revision = std::max(revision, m_map_revision[map ^ 1]);
		return revision;
	}
#if defined(__ANDROID__) || defined(__APPLE__) || defined(M1_ALWAYS_FRAME_TIMINGS)
	struct layer1_changes {
		int entries = 0, direct = 0, graphics = 0;
		bool paired_map = false, scroll = false, line = false, mask = false, full_decode = false;
	};
	layer1_changes layer1_changes_since(uint64_t revision) const
	{
		layer1_changes changes;
		changes.paired_map = (tile_ram[0x5004] & 0x6000) && m_map_revision[1] > revision;
		changes.scroll = scroll_revision(0) > revision;
		changes.line = line_revision(0) > revision;
		changes.mask = m_mask_revision[0] > revision;
		changes.full_decode = m_full_decode_revision > revision;
		if (changes.full_decode) {
			changes.entries = 0x1000;
		} else if (m_map_revision[0] > revision) {
			for (const auto &entry : m_map0_entry_revision) {
				const bool direct = entry.direct > revision, graphics = entry.graphics > revision;
				changes.entries += direct || graphics;
				changes.direct += direct;
				changes.graphics += graphics;
			}
		}
		return changes;
	}

	// Conservative screen bounds: a changed map row can affect any column in these bands.
	uint64_t layer1_dirty_bands(uint64_t revision, int band_count) const
	{
		uint64_t bands = 0;
		const int vscroll = tile_ram[0x5004] & 511;
		for (int band = 0; band < band_count; band++) {
			const int sy = (band * 8 + vscroll) & 511;
			for (int y : {sy >> 3, ((sy + 7) & 511) >> 3}) {
				const auto *row = m_map0_entry_revision + y * 64;
				if (std::any_of(row, row + 64, [revision](const auto &e) { return e.direct > revision; })) {
					bands |= uint64_t{1} << band;
					break;
				}
			}
		}
		return bands;
	}

	bool layer1_band_drawing_supported() const
	{
		return !(tile_ram[0x5004] & 0xe000);
	}

	void draw_layer1_band(bitmap_rgb32 &bitmap, int band)
	{
		const auto *mask = &tile_ram[0x6000];
		const int y0 = band * 8;
		const bool line = tile_ram[0x5000] & 0x8000;
		const int step = line ? 1 : 8;
		for (int y = y0; y < y0 + 8; y += step) {
			const int x = (-tile_ram[line ? 0x4000 + y : 0x5000]) & 511;
			const int sy = (tile_ram[0x5004] + y) & 511;
			const int split = std::min(496, 512 - x);
			draw_rect(0, bitmap, mask, 1, 0, 0, x, sy, 0, y, split, y + step);
			if (split < 496) draw_rect(0, bitmap, mask, 1, 0, 0, 0, sy, split, y, 496, y + step);
		}
	}
#endif
	void char_w(offs_t o, uint16_t d, uint16_t m)
	{
		o &= 0x3ffff;
		const uint16_t v = (char_ram[o] & ~m) | (d & m);
		if (v == char_ram[o]) return;
		char_ram[o] = v;
		m_char_dirty[o >> 4] = 1;
		m_chars_dirty = true;
		char_gen++;
	}
	// For drawing a picture again from an earlier frame's 2D: the RAMs swapped with a kept copy (the
	// maps are all decoded again at the next update). char_gen counts the tile graphics' changes, so
	// a copy of them is made only when they changed.
	uint64_t char_gen = 0;
	void swap_rams(std::vector<uint16_t> &tiles, std::vector<uint16_t> &chars) { tile_ram.swap(tiles); char_ram.swap(chars); m_dirty = true; }

	// One map as a 512x512 picture (after update()), for exporting the artwork.
	void render_map(int l, uint32_t *out) const
	{
		for (size_t i = 0; i < size_t(512) * 512; i++) out[i] = m_pens[pix[l][i] & 0x1fff];
	}

	// Map entries (w x h, as the tile RAM holds them) as a picture, 8 pixels per tile; after update().
	void render_entries(const uint16_t *e, int w, int h, uint32_t *out) const
	{
		for (int ty = 0; ty < h; ty++)
			for (int tx = 0; tx < w; tx++) {
				uint16_t val = e[ty * w + tx];
				uint32_t code = val & tile_mask, color = (val >> 7) & 0xff;
				const uint16_t *c = &char_ram[(code * 16) & 0x3ffff];
				for (int y = 0; y < 8; y++) {
					uint32_t row = (uint32_t(c[y * 2]) << 16) | c[y * 2 + 1];
					for (int x = 0; x < 8; x++)
						out[size_t(ty * 8 + y) * (w * 8) + tx * 8 + x] = m_pens[(color * 16 + ((row >> (28 - 4 * x)) & 0xf)) & 0x1fff];
				}
			}
	}

	// Decodes the four maps into 512x512 pen and flag planes; call once per frame before draw(). The
	// planes hold pen numbers, not colours (a palette change needs nothing done), and are decoded
	// again only where map entries or the tiles they show changed (all of it the first time, and
	// after swap_rams).
	void update(const uint32_t *pens)
	{
		m_pens = pens;
		if (pix[0].empty()) {
			for (int l = 0; l < 4; l++) { pix[l].resize(512 * 512); flags[l].resize(512 * 512); }
			m_dirty = true;
		}
		unsigned decoded_maps = 0;
		if (m_dirty) {
			for (int e = 0; e < 0x4000; e++) decode_entry(e);
			decoded_maps = 0xf;
		} else if (m_entries_dirty || m_chars_dirty) {
			if (m_chars_dirty)
				for (int e = 0; e < 0x4000; e++)
					if (m_char_dirty[(tile_ram[e] & tile_mask) & 0x3fff]) m_entry_dirty[e] |= 2;
			for (int e = 0; e < 0x4000; e++)
				if (m_entry_dirty[e]) {
					decode_entry(e);
					decoded_maps |= 1u << (e >> 12);
				}
		}
		for (int map = 0; map < 4; map++)
			if (decoded_maps & (1u << map)) m_map_revision[map] = ++m_revision;
#if defined(__ANDROID__) || defined(__APPLE__) || defined(M1_ALWAYS_FRAME_TIMINGS)
		if (m_dirty) {
			m_full_decode_revision = m_map_revision[0];
		} else if (decoded_maps & 1) {
			for (int e = 0; e < 0x1000; e++) {
				if (m_entry_dirty[e] & 1) m_map0_entry_revision[e].direct = m_map_revision[0];
				if (m_entry_dirty[e] & 2) m_map0_entry_revision[e].graphics = m_map_revision[0];
			}
		}
#endif
		if (m_dirty || m_entries_dirty || m_chars_dirty) std::fill(m_entry_dirty.begin(), m_entry_dirty.end(), 0);
		if (m_dirty || m_chars_dirty) std::fill(m_char_dirty.begin(), m_char_dirty.end(), 0);
		m_dirty = m_entries_dirty = m_chars_dirty = false;
	}

	void draw(bitmap_rgb32 &bitmap, const rectangle &cliprect, int layer, int lpri, int flags_in)
	{
		uint16_t hscr = tile_ram[0x5000 + (layer >> 1)];
		uint16_t vscr = tile_ram[0x5004 + (layer >> 1)];
		uint16_t ctrl = tile_ram[0x5004 + ((layer >> 1) & 2)];
		const uint16_t *mask = &tile_ram[layer & 4 ? 0x6800 : 0x6000];
		uint16_t tpri = layer & 1;
		layer >>= 1;

		if (vscr & 0x8000)
			return;

		if (ctrl & 0x6000) {
			// Window/scroll split modes, drawn through the generic scrolled path.
			if (layer & 1)
				return;
			int sy = vscr & 0x1ff;
			if (hscr & 0x8000) {
				const uint16_t *hscrtb = &tile_ram[0x4000 + 0x200 * layer];
				for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
					rectangle c = cliprect; c.min_y = c.max_y = y;
					uint16_t h = hscrtb[y] & 0x1ff;
					int l1 = layer;
					if (((ctrl & 0x6000) >> 13) == 1) {
						uint16_t v = (-vscr) & 0x1ff;
						if (!((-vscr) & 0x200)) l1 ^= 1;
						if (y >= v) l1 ^= 1;
						draw_scrolled(bitmap, c, l1, -h, sy, tpri, flags_in);
					} else {
						rectangle c1 = c, c2 = c;
						if (c1.max_x >= h) c1.max_x = h - 1;
						if (c2.min_x < h) c2.min_x = h;
						if (!(hscrtb[y] & 0x200)) l1 ^= 1;
						draw_scrolled(bitmap, c1, l1, -h, sy, tpri, flags_in);
						draw_scrolled(bitmap, c2, l1 ^ 1, -h, sy, tpri, flags_in);
					}
				}
			} else {
				int sx = -(hscr & 0x1ff);
				rectangle c1 = cliprect, c2 = cliprect;
				if (((ctrl & 0x6000) >> 13) == 1) {
					uint16_t v = (-vscr) & 0x1ff;
					if (c1.max_y >= v) c1.max_y = v - 1;
					if (c2.min_y < v) c2.min_y = v;
					if (!((-vscr) & 0x200)) layer ^= 1;
				} else {
					uint16_t h = hscr & 0x1ff;
					if (c1.max_x >= h) c1.max_x = h - 1;
					if (c2.min_x < h) c2.min_x = h;
					if (!(hscr & 0x200)) layer ^= 1;
				}
				draw_scrolled(bitmap, c1, layer, sx, sy, tpri, flags_in);
				draw_scrolled(bitmap, c2, layer ^ 1, sx, sy, tpri, flags_in);
			}
			return;
		}

		int win = layer & 1;
		if (hscr & 0x8000) {
			const uint16_t *hscrtb = &tile_ram[0x4000 + 0x200 * layer];
			vscr &= 0x1ff;
			for (int y = 0; y < 384; y++) {
				uint16_t h = (-hscrtb[y]) & 0x1ff;
				if (h + 496 <= 512)
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, h, vscr, 0, y, 496, y + 1);
				else {
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, h, vscr, 0, y, 512 - h, y + 1);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, 0, vscr, 512 - h, y, 496, y + 1);
				}
				vscr = (vscr + 1) & 0x1ff;
			}
		} else {
			hscr = (-hscr) & 0x1ff;
			vscr = (+vscr) & 0x1ff;
			if (hscr + 496 <= 512) {
				if (vscr + 384 <= 512)
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, vscr, 0, 0, 496, 384);
				else {
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, vscr, 0, 0, 496, 512 - vscr);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, 0, 0, 512 - vscr, 496, 384);
				}
			} else {
				if (vscr + 384 <= 512) {
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, vscr, 0, 0, 512 - hscr, 384);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, 0, vscr, 512 - hscr, 0, 496, 384);
				} else {
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, vscr, 0, 0, 512 - hscr, 512 - vscr);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, 0, vscr, 512 - hscr, 0, 496, 512 - vscr);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, 0, 0, 512 - vscr, 512 - hscr, 384);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, 0, 0, 512 - hscr, 512 - vscr, 496, 384);
				}
			}
		}
	}

	std::vector<uint16_t> char_ram, tile_ram;
	int m_ox = 0;   // destination x of the arcade's column 0 (widescreen draws centre the picture)

	// Widescreen: draws a layer across [-ext, 496 + ext) by letting the 512-pixel map wrap, the
	// way it scrolls anyway (the split modes through draw_wide_split).
	void draw_wide(bitmap_rgb32 &bitmap, int layer, int flags_in, int ext, int slide_distance = 0,
				   const float *slide_rows = nullptr)
	{
		uint16_t hscr = tile_ram[0x5000 + (layer >> 1)];
		uint16_t vscr = tile_ram[0x5004 + (layer >> 1)];
		uint16_t ctrl = tile_ram[0x5004 + ((layer >> 1) & 2)];
		const uint16_t *mask = &tile_ram[layer & 4 ? 0x6800 : 0x6000];
		uint8_t tpri = uint8_t((layer & 1) | 0x10);
		int l = layer >> 1;
		if (vscr & 0x8000) return;
		if (ctrl & 0x6000) { draw_wide_split(bitmap, l, hscr, vscr, ctrl, layer & 1, flags_in, ext, slide_distance, slide_rows); return; }
		int win = l & 1;
		const uint16_t *hscrtb = &tile_ram[0x4000 + 0x200 * l];
		for (int y = 0; y < 384; y++) {
			int sy, h;
			if (hscr & 0x8000) { h = (-hscrtb[y]) & 0x1ff; sy = (vscr + y) & 0x1ff; }
			else { h = (-hscr) & 0x1ff; sy = (vscr + y) & 0x1ff; }
			const uint16_t *src = &pix[l][size_t(sy) * 512];
			const uint8_t *srct = &flags[l][size_t(sy) * 512];
			uint32_t *dst = &bitmap.pix(y, m_ox - ext);
			for (int x = -ext; x < 496 + ext; x++, dst++) {
				int mx = std::clamp(x, 0, 495);
				uint16_t m = mask[y * 4 + (mx >> 7)];
				if (win) m = ~m;
				if (m & (0x8000 >> ((mx & 127) >> 3))) continue;
				int i = (h + x) & 511;
				if (srct[i] == tpri || (flags_in & DRAW_OPAQUE)) {
					*dst = m_pens[src[i] & 0x1fff];
					if (m_coord) m_coord[size_t(y) * bitmap.width() + m_ox + x] =
						uint32_t(l) << 18 | uint32_t(sy * 512 + i);
				}
			}
		}
	}

	// The split modes of draw(), widened. Mode 1 picks the map pair by row, so the sides just wrap
	// the 512-pixel map; the others put the two maps side by side as one 1024-pixel strip that
	// scrolls together, so column x reads strip column (x - h) & 1023, the left map in its top half.
	void draw_wide_split(bitmap_rgb32 &bitmap, int l, uint16_t hscr, uint16_t vscr, uint16_t ctrl,
						 uint16_t tpri, int flags_in, int ext, int slide_distance = 0,
						 const float *slide_rows = nullptr)
	{
		if (l & 1) return;
		const uint8_t want = uint8_t(tpri | 0x10);
		const int mode = (ctrl & 0x6000) >> 13;
		const uint16_t *hscrtb = &tile_ram[0x4000 + 0x200 * l];
		const int sy = vscr & 0x1ff;
		for (int y = 0; y < 384; y++) {
			const uint16_t hs = (hscr & 0x8000) ? hscrtb[y] : hscr;
			int h = hs & 0x1ff;
			if (slide_distance > 0) h += (h * ext + slide_distance / 2) / slide_distance;
			if (slide_rows && slide_distance > 0)
				h = int(std::floor(slide_rows[y] * (1.0f + float(ext) / slide_distance) + 0.5f));
			int base = l;
			if (mode == 1) {
				const uint16_t v = (-vscr) & 0x1ff;
				if (!((-vscr) & 0x200)) base ^= 1;
				if (y >= v) base ^= 1;
			} else if (!(hs & 0x200))
				base ^= 1;
			const size_t row = size_t((y + sy) & 511) * 512;
			uint32_t *dst = &bitmap.pix(y, m_ox - ext);
			for (int x = -ext; x < 496 + ext; x++, dst++) {
				// A sliding overlay is one copy of the artwork, not a repeating panorama.
				if (slide_distance > 0 && (x < h || x >= h + 512)) continue;
				const int m = mode == 1 ? base : (((x - h) & 1023) >= 512 ? base : base ^ 1);
				const size_t i = row + ((x - h) & 511);
				if (flags[m][i] == want || (flags_in & DRAW_OPAQUE))
					*dst = m_pens[pix[m][i] & 0x1fff];
			}
		}
	}

private:
	static constexpr uint32_t tile_mask = 0x3fff;
	std::vector<uint16_t> pix[4];
	std::vector<uint8_t> flags[4];
	const uint32_t *m_pens = nullptr;
	bool m_dirty = true;                        // everything to decode
	uint64_t m_revision = 0;
	uint64_t m_map_revision[4]{}, m_line_revision[4]{}, m_scroll_revision[8]{}, m_mask_revision[2]{};
	uint64_t scroll_revision(int map) const
	{
		return std::max({m_scroll_revision[map], m_scroll_revision[4 + map], m_scroll_revision[4 + (map & 2)]});
	}
	uint64_t line_revision(int map) const
	{
		return tile_ram[0x5000 + map] & 0x8000 ? m_line_revision[map] : 0;
	}
#if defined(__ANDROID__) || defined(__APPLE__) || defined(M1_ALWAYS_FRAME_TIMINGS)
	struct entry_revision { uint64_t direct, graphics; };
	entry_revision m_map0_entry_revision[0x1000]{};
	uint64_t m_full_decode_revision = 0;
#endif
public:
	bool all_to_decode() const { return m_dirty || pix[0].empty(); }
private:
	std::vector<uint8_t> m_entry_dirty = std::vector<uint8_t>(0x4000), m_char_dirty = std::vector<uint8_t>(0x4000);
	bool m_entries_dirty = false, m_chars_dirty = false;

	// one map entry (map e >> 12, its 64x64 place e & 0xfff) into the planes
	void decode_entry(int e)
	{
		const int l = e >> 12, ty = (e >> 6) & 63, tx = e & 63;
		const uint16_t val = tile_ram[e];
		const uint32_t code = val & tile_mask, color = (val >> 7) & 0xff;
		const uint8_t cat = (val & 0x8000) ? 1 : 0;
		const uint16_t *c = &char_ram[(code * 16) & 0x3ffff];
		uint16_t *pm = pix[l].data();
		uint8_t *fm = flags[l].data();
		for (int y = 0; y < 8; y++) {
			const uint32_t row = (uint32_t(c[y * 2]) << 16) | c[y * 2 + 1];
			const size_t base = size_t(ty * 8 + y) * 512 + tx * 8;
			for (int x = 0; x < 8; x++) {
				const uint8_t p = (row >> (28 - 4 * x)) & 0xf;
				pm[base + x] = uint16_t(color * 16 + p);
				fm[base + x] = p ? uint8_t(0x10 | cat) : 0;
			}
		}
	}
public:
	// When set (a buffer the size of the destination), draw() also records where each pixel it
	// draws came from: map << 18 | y * 512 + x. For replacing the sky with a hi-res picture, and
	// telling a widescreen curtain's pattern. (Per thread: layers draw in parallel.)
	static inline thread_local uint32_t *m_coord = nullptr;
	// a recorded pixel's pen (its colour's index within its palette)
	uint8_t pen_at(uint32_t coord) const { return uint8_t(pix[(coord >> 18) & 3][coord & 0x3ffff] & 0xf); }
private:

	void draw_scrolled(bitmap_rgb32 &bm, const rectangle &c, int l, int scrollx, int scrolly, uint16_t tpri, int fl)
	{
		uint8_t want = uint8_t(tpri | 0x10);
		// c is in arcade pixels (0..495); the destination is offset by m_ox
		const int x0 = std::max({c.min_x, 0, -m_ox}), x1 = std::min({c.max_x, 495, bm.width() - 1 - m_ox});
		for (int y = std::max(c.min_y, 0); y <= std::min({c.max_y, 383, bm.height() - 1}); y++)
			for (int x = x0; x <= x1; x++) {
				size_t i = size_t((y + scrolly) & 511) * 512 + ((x + scrollx) & 511);
				if (flags[l][i] == want || (fl & DRAW_OPAQUE)) {
					bm.pix(y, x + m_ox) = m_pens[pix[l][i] & 0x1fff];
					if (m_coord) m_coord[size_t(y) * bm.width() + x + m_ox] = uint32_t(l) << 18 | uint32_t(i);
				}
			}
	}

	void draw_rect(int l, bitmap_rgb32 &dm, const uint16_t *mask, uint16_t tpri, int fl, int win,
				   int sx, int sy, int xx1, int yy1, int xx2, int yy2)
	{
		tpri |= 0x10;
		const bool opaque = fl & DRAW_OPAQUE;
		const uint16_t *const pixels = pix[l].data();
		int dst_x = xx1;            // MAME takes the destination before the group shift
		mask += yy1 * 4;
		while (xx1 >= 128) { xx1 -= 128; xx2 -= 128; mask++; }
		for (int y = 0; y < yy2 - yy1; y++) {
			int dy = yy1 + y;
			if (dy >= dm.height()) break;
			const uint16_t *src = &pix[l][size_t((sy + y) & 511) * 512 + sx];
			const uint8_t *srct = &flags[l][size_t((sy + y) & 511) * 512 + sx];
			uint32_t *dst = &dm.pix(dy, dst_x + m_ox);
			uint32_t *crd = m_coord ? m_coord + size_t(dy) * dm.width() + dst_x + m_ox : nullptr;
			const uint16_t *mask1 = mask + y * 4;
			int llx = xx2, cur_x = xx1;
			while (llx > 0) {
				uint16_t m = *mask1++;
				if (win) m = ~m;
				int llx1 = llx >= 128 ? 128 : llx;
				while (cur_x < llx1) {
					const int end = m ? std::min(llx1, (cur_x + 8) & ~7) : llx1;
					const int count = end - cur_x;
					// Each window bit covers eight destination pixels, including partial runs at wraps.
					if (!(m & (0x8000 >> (cur_x >> 3)))) {
						if (crd) {
							const uint32_t coord = uint32_t(l) << 18 | uint32_t(src - pixels);
							for (int x = 0; x < count; x++)
								if (opaque || srct[x] == tpri) {
									dst[x] = m_pens[src[x] & 0x1fff];
									crd[x] = coord + x;
								}
						} else if (opaque) {
							for (int x = 0; x < count; x++) dst[x] = m_pens[src[x] & 0x1fff];
						} else {
							for (int x = 0; x < count; x++)
								if (srct[x] == tpri) dst[x] = m_pens[src[x] & 0x1fff];
						}
					}
					src += count; srct += count; dst += count;
					if (crd) crd += count;
					cur_x = end;
				}
				llx -= 128;
				cur_x = 0;
			}
		}
	}
};
