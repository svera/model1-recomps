#include "../../../src/games/wingwar/profile.h"
#include "../../../src/games/wingwar/gameplay.h"
#include "../../../src/host/regions.h"
#include "../../../src/host/input_replay.h"
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>

struct model1_video_test
{
	inline static size_t old_horizon_gaps = 0;

	// Capture the production GPU submission without drawing or comparing any pixels.
	struct geometry_sink : gpu3d
	{
		model1 &machine;
		std::vector<model1::quad_t> quads;
		std::vector<model1::point_t> points;
		explicit geometry_sink(model1 &m) : machine(m) {}
		const char *name() const override { return "geometry only"; }
		bool draw(const gpu3d_poly *, size_t, const gpu3d_span *, size_t, size_t,
			uint32_t *, size_t, int, int, int, int, double *) override
		{
			quads = machine.m_rec_quads;
			points = machine.m_rec_points;
			return true;
		}
	};

	static void recording(model1 &m, const char *path = nullptr)
	{
		input_replay::recording inputs;
		if (path) {
			const auto error = input_replay::load(path, game::wingwar::ID, inputs);
			if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); std::abort(); }
		} else {
			game::script_inputs neutral;
			neutral.analog = {128, 128, 1, -1, -1, -1, -1, -1};
			inputs.frames.assign(4183, neutral);
			inputs.expected_ram = 0xfd7c1648feffc1fa;
		}
		input_replay::start(m, inputs);
		m.reset();
		m.set_smooth(true);
		for (const auto &frame : inputs.frames) {
			const auto error = input_replay::apply(m, frame);
			if (!error.empty()) { fprintf(stderr, "%s\n", error.c_str()); std::abort(); }
			m.run_frame();
			m.m_audio_out.clear();
		}
		assert(input_replay::ram_hash(m) == inputs.expected_ram);
		m.set_drawing(true);
		geometry_sink sink(m);
		m.set_gpu3d(&sink);
		m.set_gpu3d_on(true);
		m.m_gpu3d_verify = m.m_gpu3d_check = 0;
		const auto original = m.m_gameplay.object;
		std::array<float, 12> ground_matrix{};
		model1::view_t ground_view = *m.m_view;
		bool extend = true, found = false;
		m.m_gameplay.object = [&](uint32_t owner, uint32_t texture, uint32_t polygon, uint32_t size, const float *data) {
			if (polygon == 0xc7ca2 && texture == 0x46aab && size == 20 && data == m.polygon_data(false)) {
				found = true;
				const auto *matrix = m.render_matrix();
				std::copy_n(matrix, 12, ground_matrix.begin());
				ground_view = *m.m_view;
				if (!extend) return game::object_effects{};
			}
			return original(owner, texture, polygon, size, data);
		};
		size_t checks = 0, original_gaps = 0;
		const auto nvram = m.nvram(), work_ram = m.work_ram();
		for (float aspect : {16.0f / 9, 21.0f / 9}) {
			m.set_aspect(aspect);
			for (int scale : {1, 3}) {
				m.set_render_scale(scale);
				for (double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
					for (bool extended : {false, true}) {
						extend = extended;
						found = false;
						model1::gpu_parts parts;
						m.m_gpu3d_verify = m.m_gpu3d_check = 0;
						m.render_parts(m.m_logic[0].frame + 2 + fraction * (m.m_logic[1].frame - m.m_logic[0].frame), parts, false);
						assert(found && !sink.quads.empty());
						const auto &mt = ground_matrix;
						const auto inverse = glm::inverse(glm::dmat3(glm::dvec3(mt[0], mt[1], mt[2]),
							glm::dvec3(mt[3], mt[4], mt[5]), glm::dvec3(mt[6], mt[7], mt[8])));
						auto contains = [&](size_t index, double x, double y) {
							bool inside = false;
							for (int i = 0, j = 3; i < 4; j = i++) {
								const auto &a = sink.points[index * 4 + i], &b = sink.points[index * 4 + j];
								if ((a.fy > y) != (b.fy > y) && x < (b.fx - a.fx) * (y - a.fy) / (b.fy - a.fy) + a.fx)
									inside = !inside;
							}
							return inside;
						};
						for (double x = ground_view.lx1 + 1; x < ground_view.lx2; x += 8) {
							const double horizon = ground_view.yc - ground_view.viewy +
								(inverse[0][1] * (x - ground_view.xc - ground_view.viewx) / ground_view.zoomx + inverse[2][1]) *
								ground_view.zoomy / inverse[1][1];
							for (double dy : {0.5 / scale, 1.0, 2.0, 4.0, 8.0}) {
								bool covered = false;
								for (size_t q = 0; q < sink.quads.size(); q++) {
									if (sink.quads[q].obj != 0xc7ca2) continue;
									if (contains(q, (x + m.wide_ext()) * scale, (horizon + dy) * scale)) {
										assert((sink.quads[q].col & 0xffffff) != 0);
										covered = true;
									}
								}
								if (!extend) { original_gaps += !covered; continue; }
								if (!covered) {
									fprintf(stderr, "Countryside horizon gap: aspect %g scale %d alpha %g x%g y%g\n",
										aspect, scale, fraction, x, horizon + dy);
									assert(false);
								}
								checks++;
							}
						}
					}
				}
			}
		}
		assert(m.nvram() == nvram && m.work_ram() == work_ram);
		assert(checks > 1000 && original_gaps > 1000);
		printf("Wing War countryside recorded scene: %zu smooth horizon samples covered, %zu original gaps; game state unchanged\n",
			checks, original_gaps);
		m.m_gameplay.object = original;
		m.set_gpu3d(nullptr);
	}

	// Widescreen curtain rows: only a pattern repeated across the whole row (stripes, a panel's two
	// halves in two palettes) repeats into the sides; other art keeps its edge pixel where it is one
	// colour beside both edges (a curtain, a bar with letters), and a picture is not stretched (no
	// pixels rendered: the mapping is checked on a synthetic layer).
	static void curtains(model1 &m)
	{
		constexpr uint32_t KEY = 0xff000000;
		constexpr int W = model1::SCREEN_W, HALF = W / 2;
		m.set_aspect(21.0f / 9);
		m.m_wide_frame = true;
		const int ext = m.m_ext, w = W + 2 * ext;
		assert(ext > 0);
		auto stripe = [](int x, int y) { return ((x + y) % 12 + 12) % 12 < 5 ? 0x208020u : 0x104010u; };
		// diagonal streaks of pens repeating every 120 columns, cut into 248-column halves shown in a
		// lighter and a darker palette (Wing War's panels: art that doesn't wrap at the half)
		auto pen = [](int x, int y) { const int d = ((x - y) % 120 + 120) % 120; return (d * 37 + d * d / 7) % 13; };
		// (the darker palette merges some of the lighter one's colours)
		auto panel = [&](int ax, int y) {
			const int p = pen(ax % HALF, y);
			return ax < HALF ? 0x20a060u + uint32_t(p) * 0x000301u : 0x105030u + uint32_t(p / 2) * 0x000301u;
		};
		auto picture = [](int ax, int y) { return uint32_t(0x200000 | ((ax * ax / 7 + y * 3) & 0xff) << 8 | ((ax * 5 + y * y) & 0xff)); };
		// the tiles' pens: map 0 as the arcade screen, a tile of its own for each place
		auto tile_pen = [&](int ax, int y) {
			if (y < 60) return 1;
			if (y < 120) return stripe(ax + ext, y) == 0x208020u ? 2 : 3;
			if (y < 180) return 4;
			if (y >= 260 && y < 320 && ax >= 100 && ax < 110) return 15;
			if (y < 320) return pen(ax % HALF, y) + 1;
			return int((uint32_t(ax) * 2654435761u ^ uint32_t(y) * 40503u) >> 20) % 15 + 1;
		};
		for (int ty = 0; ty < model1::SCREEN_H / 8; ty++)
			for (int tx = 0; tx < W / 8; tx++) {
				const int code = 1 + ty * 64 + tx;
				m.m_tiles.tile_w(ty * 64 + tx, uint16_t(code), 0xffff);
				for (int r = 0; r < 8; r++)
					for (int half = 0; half < 2; half++) {
						uint16_t v = 0;
						for (int i = 0; i < 4; i++) v = uint16_t(v << 4 | tile_pen(tx * 8 + half * 4 + i, ty * 8 + r));
						m.m_tiles.char_w(code * 16 + r * 2 + half, v, 0xffff);
					}
			}
		static const std::vector<uint32_t> no_colours(0x2000);
		m.m_tiles.update(no_colours.data());
		model1::layer_cache c;
		c.px.allocate(w, model1::SCREEN_H);
		c.px.fill(KEY);
		c.coord.assign(size_t(w) * model1::SCREEN_H, ~0u);
		c.x0.assign(model1::SCREEN_H, 0);
		c.x1.assign(model1::SCREEN_H, 0);
		for (int y = 0; y < model1::SCREEN_H; y++)
			for (int x = ext; x < ext + W; x++) {
				const int ax = x - ext;
				const bool gap = (y >= 120 && y < 180) || (y >= 320 && y < 324) || (y >= 352 && y < 360);
				if (gap && ax >= 200) continue;                         // partial: no curtain
				c.coord[size_t(y) * w + x] = uint32_t(y * 512 + ax);
				if (y < 60) c.px.pix(y, x) = 0;                         // plain curtain
				else if (y < 120) c.px.pix(y, x) = stripe(x, y);        // stripes
				else if (y < 180) c.px.pix(y, x) = 7;
				else if (y >= 180 && y < 260) c.px.pix(y, x) = panel(ax, y);
				else if (y >= 260 && y < 320)                           // a panel with a letter on it
					c.px.pix(y, x) = ax >= 100 && ax < 110 ? 0xffffffu : panel(ax, y);
				else if (y >= 324 && y < 352)                           // a bar with letters: one colour at its edges
					c.px.pix(y, x) = ax >= 100 && ax < 110 ? 0xffffffu : 0x404040u;
				else if (y >= 320) c.px.pix(y, x) = picture(ax, y);    // a picture: not a pattern
			}
		m.find_curtains(c, 2, 0);
		for (int y = 0; y < model1::SCREEN_H; y++) {
			const uint32_t *row = &c.px.pix(y);
			// curtains of one colour and bars carry out their edge pixel; a panel with art at its edges
			// that isn't one pattern, and pictures, leave the sides alone
			const bool bar = y >= 324 && y < 352;
			assert(c.curtain[y] == (y < 60 || bar));
			for (int x = 0; x < w; x++) {
				const int ax = x - ext;
				if (y < 60 || y >= 260) {
					if (ax < 0 || ax >= W) assert(row[x] == KEY);
					if ((ax < 0 || ax >= W) && c.curtain[y]) assert(m.curtain_pixel(c, y, x) == row[x < ext ? ext : ext + W - 1]);
				} else if (y < 120) assert(row[x] == stripe(x, y));    // the stripes drawn on
				else if (y < 180) assert((ax < 0 || ax >= 200) == (row[x] == KEY));
				else                                                    // the streaks continued exactly
					assert(row[x] == (ax < HALF ? 0x20a060u + uint32_t(pen(ax, y)) * 0x000301u :
						0x105030u + uint32_t(pen(ax - HALF, y) / 2) * 0x000301u));
			}
			if (y >= 60 && y < 260 && (y < 120 || y >= 180)) assert(c.x0[y] == 0 && c.x1[y] == w);
		}
		// the stitch of a panel whose art doesn't repeat: no seam worse than where the plain wrap joins
		std::vector<uint8_t> art(size_t(80) * HALF);
		for (int r = 0; r < 80; r++)
			for (int x = 0; x < HALF; x++)
				art[size_t(r) * HALF + x] = uint8_t(int((uint32_t(x - r + 999) * 2654435761u) >> 21) % 7 + (x * x + r) % 5);
		const auto st = model1::stitch_pattern(art.data(), 80, HALF);
		assert(st.q > 0 && st.q < HALF && int(st.k.size()) == 80);
		long stitched = 0, wrapped = 0;
		for (int r = 0; r < 80; r++) {
			const int k = st.k[r];
			assert(k > 0 && k + st.q <= HALF && (!r || std::abs(k - st.k[r - 1]) <= 4));
			stitched += std::abs(art[size_t(r) * HALF + k - 1 + st.q] - art[size_t(r) * HALF + k]);
			wrapped += std::abs(art[size_t(r) * HALF + HALF - 1] - art[size_t(r) * HALF]);
		}
		assert(stitched < wrapped);
		m.set_aspect(4.0f / 3);
	}

	static void clipping(model1 &m)
	{
		for (float aspect : {4.0f / 3, 21.0f / 9}) {
			m.set_aspect(aspect);
			for (int scale : {1, 3}) {
				m.set_render_scale(scale);
				m.m_view->set_viewport(248, 192, 0, 495, 0, 383);
				m.m_view->set_zoom(320, 320);
				for (float extent : {1000.0f, 10000000.0f}) {
					for (int plane = 0; plane < 4; plane++) {
						model1::point_t outside, inside, clipped;
						outside.x = plane == 2 ? -extent : plane == 3 ? extent : extent / 2;
						outside.y = plane == 0 ? extent : plane == 1 ? -extent : extent / 2;
						outside.z = -extent / 3;
						inside.x = 0; inside.y = 0; inside.z = 100;
						m.m_clipfn[plane].m_clip(m.m_view.get(), &clipped, &outside, &inside);
						assert(clipped.z > 0 && std::isfinite(clipped.fx) && std::isfinite(clipped.fy));
						const float edge = m.m_view->hd_raster ? 1.0f : 0.0f;
						const float expected = plane == 0 ? m.m_view->ly1 : plane == 1 ? m.m_view->ly2 + edge :
							(plane == 2 ? m.m_view->lx1 : m.m_view->lx2 + edge) + m.m_view->ext;
						const float actual = plane < 2 ? clipped.fy : clipped.fx;
						assert(std::abs(actual - expected * scale) < 0.01f);
					}
				}
			}
		}
		puts("Ground horizon clipping: all four planes at 4:3/21:9 and 1x/3x passed");
	}
	static bool intersects(const glm::vec3 &ray, const std::array<glm::vec3, 4> &quad)
	{
		const glm::dvec3 direction(ray);
		for (const auto indices : {std::array{0, 1, 2}, std::array{0, 2, 3}}) {
			const glm::dvec3 a(quad[indices[0]]);
			const auto ab = glm::dvec3(quad[indices[1]]) - a, ac = glm::dvec3(quad[indices[2]]) - a;
			const auto p = glm::cross(direction, ac);
			const double determinant = glm::dot(ab, p);
			if (std::abs(determinant) < 1e-9) continue;
			const double u = glm::dot(-a, p) / determinant;
			const auto q = glm::cross(-a, ab);
			const double v = glm::dot(direction, q) / determinant;
			if (u >= 0 && v >= 0 && u + v <= 1 && glm::dot(ac, q) / determinant > 0) return true;
		}
		return false;
	}

	static void sky(model1 &m, uint32_t texture, uint32_t polygon, uint32_t size, size_t &rays, size_t &gaps)
	{
		const auto *data = m.polygon_data(true);
		const auto effect = m.m_gameplay.object(0, texture, polygon & 0x7fffff, size, data);
		assert(effect.point && !effect.hidden);
		assert(!m.m_gameplay.object(0, texture, polygon & 0x7fffff, size, m.polygon_data(false)).point);
		assert(!m.m_gameplay.object(0, texture + 1, polygon & 0x7fffff, size, data).point);
		assert(!m.m_gameplay.object(0, texture, 0, size, data).point);
		assert(!m.m_gameplay.object(0, texture, polygon & 0x7fffff, size + 1, data).point);
		m.m_wide_frame = false;
		assert(!m.m_gameplay.object(0, texture, polygon & 0x7fffff, size, data).point);
		m.m_wide_frame = true;
		std::array<uint32_t, 106> original_ram;
		std::copy_n(m.m_poly_ram.get() + 0xa7, original_ram.size(), original_ram.begin());
		using face = std::array<glm::vec3, 4>;
		std::array<std::vector<face>, 3> surfaces;
		for (int version = 0; version < 3; version++) {
			auto point = [&](int at) {
				model1::point_t p;
				p.x = data[at]; p.y = data[at + 1]; p.z = data[at + 2];
				const float y = p.y, z = p.z;
				if (version == 1) effect.point(p.x, p.y, p.z);
				if (version == 2) p.x *= 10000; // Infinite-width reference, without changing the horizon.
				assert(p.y == y && p.z == z);
				m.m_view->transform_point(&p);
				return glm::vec3(p.x, p.y, p.z);
			};
			auto left = point(0xa7), right = point(0xaa);
			for (uint32_t i = 0; i < size; i++) {
				const int at = 0xad + int(i) * 10;
				assert((std::bit_cast<uint32_t>(data[at]) & 3) == 1);
				const auto next_left = point(at + 4), next_right = point(at + 7);
				surfaces[version].push_back({left, right, next_right, next_left});
				left = next_left; right = next_right;
			}
		}
		assert(std::equal(original_ram.begin(), original_ram.end(), m.m_poly_ram.get() + 0xa7));
		for (float x : {m.m_view->lx1, m.m_view->lx1 + 1, m.m_view->lx2 - 1, m.m_view->lx2}) {
			for (float y = m.m_view->ly1; y <= m.m_view->ly2; y += 8) {
				const glm::vec3 ray((x - m.m_view->xc - m.m_view->viewx) / m.m_view->zoomx,
					(m.m_view->yc - y - m.m_view->viewy) / m.m_view->zoomy, 1);
				auto covered = [&](int version, glm::vec3 direction) {
					for (const auto &face : surfaces[version]) if (intersects(direction, face)) return true;
					return false;
				};
				// Exclude horizon-boundary rays, where extrapolating float vertices changes rounding.
				if (!covered(2, ray) ||
					!covered(2, ray + glm::vec3(0.25f / m.m_view->zoomx, 0, 0)) ||
					!covered(2, ray - glm::vec3(0.25f / m.m_view->zoomx, 0, 0)) ||
					!covered(2, ray + glm::vec3(0, 0.25f / m.m_view->zoomy, 0)) ||
					!covered(2, ray - glm::vec3(0, 0.25f / m.m_view->zoomy, 0))) continue;
				rays++;
				if (!covered(0, ray)) gaps++;
				if (!covered(1, ray)) {
					fprintf(stderr, "Sky gap at (%g,%g), ext %d\n", x, y, m.wide_ext());
					assert(false);
				}
			}
		}
	}

	static void ground(model1 &m, uint32_t texture, uint32_t polygon, uint32_t size, size_t &gaps, size_t &horizon_rays)
	{
		const auto *data = m.polygon_data(false);
		const auto effect = m.m_gameplay.object(0, texture, polygon, size, data);
		assert(effect.point && !effect.hidden);
		assert(!m.m_gameplay.object(0, texture, polygon, size, m.polygon_data(true)).point);
		assert(!m.m_gameplay.object(0, texture + 1, polygon, size, data).point);
		assert(!m.m_gameplay.object(0, texture, polygon, size + 1, data).point);
		const bool city = polygon == 0xc6e0f;
		const bool countryside = polygon == 0xc7ca2;
		const float left = city ? -5120 : countryside ? -8192 : -12800;
		const float right = city ? 12800 : countryside ? 10240 : 5120;
		const float near = city ? -3072 : countryside ? -8192 : -2560;
		const float far = city ? 23040 : countryside ? 10240 : 20480;
		const float ground_y = countryside ? 0 : -2;
		const auto *matrix = m.render_matrix();
		const glm::dmat3 rotation(glm::dvec3(matrix[0], matrix[1], matrix[2]),
			glm::dvec3(matrix[3], matrix[4], matrix[5]), glm::dvec3(matrix[6], matrix[7], matrix[8]));
		const auto inverse = glm::inverse(rotation);
		const glm::dvec3 translation(matrix[9], matrix[10], matrix[11]);
		const auto camera = -inverse * translation;
		// The ground strips are one-sided: an eye below the plane must not see their underside.
		if (camera.y <= ground_y) return;
		std::array<std::vector<std::array<glm::vec3, 4>>, 3> surfaces;
		for (int version = 0; version < 3; version++) {
			auto point = [&](uint32_t at) {
				model1::point_t p;
				p.x = data[at]; p.y = data[at + 1]; p.z = data[at + 2];
				const float x = p.x, y = p.y, z = p.z;
				if (version == 1) effect.point(p.x, p.y, p.z);
				if (version == 2) {
					const float width = std::hypot(model1::SCREEN_W / 2.0f + m.wide_ext(), model1::SCREEN_H / 2.0f) /
						(model1::SCREEN_W / 2.0f);
					if (x == left) p.x -= (right - left) * (width - 1) / 2;
					else if (x == right) p.x += (right - left) * (width - 1) / 2;
					if (z == near) p.z -= (far - near) * (width - 1) / 2;
					else if (z == far) p.z += (far - near) * (width - 1) / 2;
				}
				assert(p.y == y);
				if (x != left && x != right) assert(p.x == x);
				if (z != near && z != far) assert(p.z == z);
				m.m_view->transform_point(&p);
				return glm::vec3(p.x, p.y, p.z);
			};
			auto old_left = point(polygon), old_right = point(polygon + 3);
			for (uint32_t i = 0; i < size; i++) {
				const uint32_t at = polygon + 6 + i * 10;
				const auto flags = std::bit_cast<uint32_t>(data[at]);
				assert((flags & 3) == 1);
				const auto next_left = point(at + 4), next_right = point(at + 7);
				const int link = (flags >> 8) & 3;
				if (link) surfaces[version].push_back({old_right, old_left, next_left, next_right});
				if (link == 0 || link == 2) { old_left = next_left; old_right = next_right; }
				else if (link == 1) old_right = next_left;
				else old_left = next_right;
			}
		}
		m.m_pointpt = m.m_pointdb.get();
		m.m_quadpt = m.m_quaddb.get();
		float old_z = 0;
		m.push_object(texture, polygon, size, old_z, 0);
		surfaces[1].clear();
		for (auto *quad = m.m_quaddb.get(); quad < m.m_quadpt; quad++) {
			std::array<glm::vec3, 4> face;
			for (int i = 0; i < 4; i++) {
				const auto *p = quad->p[i];
				face[i] = glm::vec3(
					(p->fx / m.render_scale() - m.wide_ext() - m.m_view->xc - m.m_view->viewx) / m.m_view->zoomx,
					(m.m_view->yc - p->fy / m.render_scale() - m.m_view->viewy) / m.m_view->zoomy, 1);
			}
			surfaces[1].push_back(face);
		}
		auto check = [&](float x, float y, bool horizon) {
				const glm::vec3 ray((x - m.m_view->xc - m.m_view->viewx) / m.m_view->zoomx,
					(m.m_view->yc - y - m.m_view->viewy) / m.m_view->zoomy, 1);
				auto covered = [&](int version) {
					for (const auto &face : surfaces[version]) if (intersects(ray, face)) return true;
					return false;
				};
				const bool before = covered(0), after = covered(1);
				if (before && !after) {
					fprintf(stderr,"Lost ground %x at (%g,%g) frame %llu clipped %zu\n",
						polygon,x,y,(unsigned long long)m.frame_number(), surfaces[1].size());
					assert(false);
				}
				if (!before && after) gaps++;
				const auto direction = inverse * glm::dvec3(ray);
				const double distance = (ground_y - camera.y) / direction.y;
				if (distance > 0) {
					const auto hit = camera + distance * direction;
					// Outside the original rectangle there are no intentional interior cutouts.
					if (hit.x < left || hit.x > right || hit.z < near || hit.z > far) {
						if (!after) {
							fprintf(stderr, "Ground %x missing at (%g,%g), frame %llu, horizon %d\n",
								polygon, x, y, (unsigned long long)m.frame_number(), horizon);
							assert(false);
						}
						if (horizon) {
							horizon_rays++;
							if (!covered(2)) old_horizon_gaps++;
						}
					}
				}
		};
		const float inset = 0.5f / m.render_scale();
		for (float x : {m.m_view->lx1 + inset, m.m_view->lx1 + 1, m.m_view->lx2 - 1, m.m_view->lx2 - inset}) {
			for (float y = m.m_view->ly1 + inset; y <= m.m_view->ly2 - inset; y += 2) {
				// Rows less than half a render pixel from the horizon are checked separately.
				const double nx = inverse[0][1], ny = inverse[1][1], nz = inverse[2][1];
				const double value = nx * (x - m.m_view->xc - m.m_view->viewx) / m.m_view->zoomx +
					ny * (m.m_view->yc - y - m.m_view->viewy) / m.m_view->zoomy + nz;
				const double gradient = std::hypot(nx / m.m_view->zoomx, ny / m.m_view->zoomy);
				if (std::abs(value) < gradient * 0.5 / m.render_scale()) continue;
				check(x, y, false);
			}
		}
		const glm::dvec2 gradient(inverse[0][1] / m.m_view->zoomx, -inverse[1][1] / m.m_view->zoomy);
		const double length = glm::length(gradient);
		for (float x = m.m_view->lx1 + inset; x <= m.m_view->lx2 - inset; x += 8) {
			if (std::abs(gradient.y) < 1e-9) break;
			const double y = m.m_view->yc - m.m_view->viewy +
				(inverse[0][1] * (x - m.m_view->xc - m.m_view->viewx) / m.m_view->zoomx + inverse[2][1]) *
				m.m_view->zoomy / inverse[1][1];
			for (double side : {-1.0, 1.0}) {
				const double px = x + side * gradient.x / length * 0.5 / m.render_scale();
				const double py = y + side * gradient.y / length * 0.5 / m.render_scale();
				if (px >= m.m_view->lx1 + inset && px <= m.m_view->lx2 - inset &&
					py >= m.m_view->ly1 + inset && py <= m.m_view->ly2 - inset)
					check(float(px), float(py), true);
			}
		}
	}

	static void inspect(model1 &m, size_t &rays, size_t &gaps, std::array<size_t, 3> &land_gaps, size_t &horizon_rays)
	{
		m.set_current_render_list();
		m.m_wide_frame = true;
		m.m_view->init_translation_matrix();
		for (int at = 0; at < 0x8000;) {
			switch (m.readi(at)) {
			case 0: at += 2; break;
			case 1: case 0x41:
				if (m.readi(at + 4) == 0x8000a7 || m.readi(at + 4) == 0xc6e0f ||
					m.readi(at + 4) == 0xc6d4d || m.readi(at + 4) == 0xc7ca2) {
					std::array<float, 12> matrix;
					std::copy_n(m.render_matrix(), 12, matrix.begin());
					for (float degrees : {0.0f, -90.0f, -60.0f, -30.0f, 30.0f, 60.0f, 90.0f}) {
						auto rolled = matrix;
						const float angle = degrees * 3.14159265f / 180;
						for (int column = 0; column < 4; column++) {
							const float x = matrix[column * 3], y = matrix[column * 3 + 1];
							rolled[column * 3] = std::cos(angle) * x - std::sin(angle) * y;
							rolled[column * 3 + 1] = std::sin(angle) * x + std::cos(angle) * y;
						}
						m.m_view->set_translation_matrix(rolled.data());
						if (m.readi(at + 4) == 0x8000a7)
							sky(m, m.readi(at + 2), m.readi(at + 4), m.readi(at + 6), rays, gaps);
						else
							ground(m, m.readi(at + 2), m.readi(at + 4), m.readi(at + 6),
								land_gaps[m.readi(at + 4) == 0xc7ca2 ? 2 : m.readi(at + 4) == 0xc6d4d ? 1 : 0], horizon_rays);
					}
					m.m_view->set_translation_matrix(matrix.data());
				}
				at += 8; break;
			case 2: at = m.skip_direct(at); break;
			case 3:
				m.m_view->set_viewport(m.readi16(at + 4), 422 - m.readi16(at + 6),
					m.readi16(at + 8), m.readi16(at + 12),
					422 - m.readi16(at + 14), 422 - m.readi16(at + 10));
				at += 16; break;
			case 4: at += 6 + (m.readi(at + 4) + 1) * 2; break;
			case 5: case 6: at += 6 + m.readi(at + 4) * 2; break;
			case 7: case 8: at += 4; break;
			case 9: m.m_view->set_zoom(m.readf(at + 2) * 4, m.readf(at + 4) * 4); at += 6; break;
			case 0xa: at += 8; break;
			case 0xb: {
				float matrix[12];
				for (int i = 0; i < 12; i++) matrix[i] = m.readf(at + 2 + i * 2);
				m.m_view->set_translation_matrix(matrix);
				at += 26; break;
			}
			case 0xc: m.m_view->set_view_translation(m.readf(at + 2), m.readf(at + 4)); at += 6; break;
			case 0xf: return;
			default: assert(false);
			}
		}
		assert(false);
	}
};

int main(int argc, char **argv)
{
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s regions-dir [input-recording]\n", argv[0]);
		return 2;
	}
	model1 m(load_regions(argv[1], game::wingwar::ID));
	if (argc == 3) { model1_video_test::recording(m, argv[2]); return 0; }
	model1_video_test::curtains(m);
	model1_video_test::clipping(m);
	m.reset();
	std::array<size_t, 2> rays{}, gaps{};
	std::array<std::array<size_t, 3>, 2> land_gaps{};
	std::array<size_t, 2> horizon_rays{};
	for (int f = 0; f < 7200; f++) {
		m.run_frame();
		if (f < 700 || f % 8) continue;
		const auto nvram = m.nvram(), work_ram = m.work_ram();
		for (int i = 0; i < 2; i++) {
			m.set_aspect(i ? 21.0f / 9 : 16.0f / 9);
			for (int scale : {1, 3}) {
				m.set_render_scale(scale);
				model1_video_test::inspect(m, rays[i], gaps[i], land_gaps[i], horizon_rays[i]);
			}
		}
		assert(m.nvram() == nvram && m.work_ram() == work_ram);
	}
	for (int i = 0; i < 2; i++) {
		printf("Wing War sky %s: %zu edge rays covered, %zu original gaps filled\n", i ? "21:9" : "16:9", rays[i], gaps[i]);
		assert(rays[i] > 1000);
		printf("Wing War ground %s: %zu city, %zu coast and %zu countryside edge gaps filled\n",
			i ? "21:9" : "16:9", land_gaps[i][0], land_gaps[i][1], land_gaps[i][2]);
		assert(land_gaps[i][0] > 0 && land_gaps[i][1] > 0 && land_gaps[i][2] > 0);
		printf("Wing War ground horizon %s: %zu half-pixel horizon rays covered\n", i ? "21:9" : "16:9", horizon_rays[i]);
		assert(horizon_rays[i] > 1000);
	}
	assert(gaps[1] > 100);
	printf("Wing War horizon regression: %zu near-horizon rays missed by the previous extension\n",
		model1_video_test::old_horizon_gaps);
	assert(model1_video_test::old_horizon_gaps > 1000);
	m.set_aspect(4.0f / 3);
	assert(!game::wingwar::create_gameplay(m).object(0, 0x5b471, 0xa7, 10, m.polygon_data(true)).point);
	assert(!game::wingwar::create_gameplay(m).object(0, 0x46a75, 0xc6e0f, 8, m.polygon_data(false)).point);
	assert(!game::wingwar::create_gameplay(m).object(0, 0x46a68, 0xc6d4d, 5, m.polygon_data(false)).point);
	assert(!game::wingwar::create_gameplay(m).object(0, 0x46aab, 0xc7ca2, 20, m.polygon_data(false)).point);
	auto reproduction = std::make_unique<model1>(load_regions(argv[1], game::wingwar::ID));
	model1_video_test::recording(*reproduction);
}
