// Smooth motion above 1x. VF's game logic runs at 30 Hz: it builds a new display list every
// second frame, so the picture moves 30 times a second whatever the screen can show. In smooth
// mode each new list is kept (with the sky's scroll), and the picture is drawn for any moment
// between the last two: every object's matrix (its place, turn and the camera's) is blended
// with the same object's in the list before, and the sky's scroll likewise. The game itself runs
// untouched; the picture is one game frame (two video frames) behind it.
#include "model1.h"
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {

// An object in a list: its polygons' address, and which time that address comes in the list.
uint64_t object_key(uint32_t poly_adr, uint32_t nth) { return uint64_t(poly_adr) << 20 | nth; }

// How far a part is from the eye, for how far it may move between two lists: its distance, not its
// z. (SWA's ships and cells beside the camera, seen at the sides of a wide screen, have a small z
// and are as far as their x: an X-wing passing 300 units to the side at z 66, moving 5% of its
// distance, was 120% of its z, taken for a jump and held a list, then leapt, each list.)
float eye_distance(const float *pa, const float *pb)
{
	return std::max({ std::hypot(pa[9], pa[10], pa[11]), std::hypot(pb[9], pb[10], pb[11]), 1.0f });
}

// (records) A camera as the game keeps it: 12 floats, the rotation's columns, then the move.
glm::mat4 camera_matrix(const float *m)
{
	glm::mat4 r(1);
	for (int c = 0; c < 3; c++) r[c] = glm::vec4(m[c * 3], m[c * 3 + 1], m[c * 3 + 2], 0);
	r[3] = glm::vec4(m[9], m[10], m[11], 1);
	return r;
}

// A camera that turns over 25 degrees or jumps over 8 units in a game frame (a car's move is about 3)
bool camera_cut(const glm::mat4 &from, const glm::mat4 &to)
{
	const glm::mat3 turn = glm::mat3(to) * glm::transpose(glm::mat3(from));
	const float angle = std::acos(std::clamp((turn[0][0] + turn[1][1] + turn[2][2] - 1) / 2, -1.0f, 1.0f));
	const float jump = glm::length(glm::vec3(glm::affineInverse(to)[3]) - glm::vec3(glm::affineInverse(from)[3]));
	return angle > glm::radians(25.0f) || jump > 8;
}

// a turn as a rotation vector (the shorter way round) and back
glm::vec3 turn_vector(glm::quat q)
{
	if (q.w < 0) q = -q;
	const glm::vec3 v(q.x, q.y, q.z);
	const float s = glm::length(v);
	return s < 1e-7f ? 2.0f * v : v * (2 * std::atan2(s, q.w) / s);
}

glm::quat turn_quat(const glm::vec3 &v)
{
	const float a = glm::length(v);
	return a < 1e-7f ? glm::quat(1, 0.5f * v.x, 0.5f * v.y, 0.5f * v.z) : glm::angleAxis(a, v / a);
}

} // namespace

void model1::set_smooth(bool on)
{
	if (on == m_smooth) return;
	m_smooth = on;
	m_logic[0] = m_logic[1] = m_logic_prev = logic_frame{};
	for (int buffer = 0; buffer < 2; buffer++) {
		m_list_visibility[buffer].clear();
		m_list_visibility_frame[buffer] = ~0ull;
	}
	m_shown = false;
}

// A list's matrix slots: each matrix command (0xb) and the objects drawn with it. Swapped models
// (a hand, a face) keep their slot, so slots are what is matched between two lists.
// (video_traits::records) A word of a display list written: what the game draws with it, when
// it is a matrix command's (the record in the register, else the still world), and the camera.
void model1::list_written(int buffer, uint32_t word)
{
	if (m_gameplay.matrix_objects) {
		m_list_extra[buffer].erase(word);
		uint32_t at = word;
		auto extra = m_gameplay.matrix_objects(at);
		at &= 0x7fff;
		if (!extra.empty()) m_list_extra[buffer][at] = std::move(extra);
		else if (at != word) m_list_extra[buffer].erase(at);
	}
	if ((!m_smooth && !m_gameplay.object) || !m_video->records) return;
	const game::draw_records &r = *m_video->records;
	std::vector<uint32_t> &owners = m_list_owner[buffer];
	if (owners.empty()) owners.assign(0x8000, 0);
	uint32_t owner = 0;
	for (const game::matrix_writer &w : r.writers)
		if (m_maincpu.pc() == w.pc) {
			if (w.caller && read32(m_maincpu.reg(31)) != w.caller) continue;
			const uint32_t rec = m_maincpu.reg(w.record_register);
			owner = rec >= r.records_from && rec < r.records_to ? rec : 1;
			if (!m_smooth) break;
			memcpy(m_list_camera[buffer].data(), &m_ram_b[r.camera - 0x500000], sizeof m_list_camera[buffer]);
			m_list_has_camera[buffer] = true;
			if (m_list_visibility_frame[buffer] != m_frame) {
				m_list_visibility[buffer].clear();
				m_list_visibility_frame[buffer] = m_frame;
			}
			if (m_gameplay.record_visibility && m_list_visibility[buffer].empty())
				m_list_visibility[buffer] = m_gameplay.record_visibility(owner);
			break;
		}
	owners[word & 0x7fff] = owner;
}

void model1::index_objects(logic_frame &lf)
{
	const std::vector<uint32_t> &owners = m_list_owner[m_picture_buffer];
	lf.slots.clear();
	lf.above_hud = false;
	lf.visibility = m_list_visibility[m_picture_buffer];
	lf.slots.push_back({});                        // objects before the first matrix
	const uint16_t *l = lf.list.data();
	auto rdi = [&](int a) { return uint32_t(l[a & 0x7fff] | l[(a + 1) & 0x7fff] << 16); };
	auto rdf = [&](int a) { uint32_t v = rdi(a); float f; memcpy(&f, &v, 4); return f; };
	int off = 0;
	float light[3] = { 0, 0, 0 };
	bool has_light = false;
	for (int guard = 0; guard < 0x10000; guard++) {
		switch (rdi(off)) {
		case 0: off += 2; break;
		case 1: case 0x41:
			if (rdi(off) == 0x41) lf.above_hud = true;
			lf.slots.back().polys.push_back(rdi(off + 4));
			if (rdi(off) == 1 && lf.slots.back().visibility)
				lf.slots.back().objects.push_back({ rdi(off + 2), rdi(off + 4), rdi(off + 6) });
			lf.objects++;
			off += 8;
			break;
		case 2: {                                  // as skip_direct
			off += 18;
			for (;;) {
				const uint32_t t = rdi(off + 2) & 3;
				if (!t) break;
				off += t == 2 ? 12 : 20;
			}
			off += 4;
			break;
		}
		case 3: off += 16; break;
		case 4: off += 6 + (rdi(off + 4) + 1) * 2; break;
		case 5: case 6: off += 6 + rdi(off + 4) * 2; break;
		case 7: case 8: off += 4; break;
		case 9: case 0xc: off += 6; break;
		case 0xa:                                  // the light vector, as set_light_direction keeps it
			{
				const glm::vec3 v = view_t::light_vector(rdf(off + 2), rdf(off + 4), rdf(off + 6));
				light[0] = v.x; light[1] = v.y; light[2] = v.z;
				has_light = true;
			}
			off += 8;
			break;
		case 0xb: {
			slot sl;
			for (int i = 0; i < 12; i++) sl.mat[i] = rdf(off + 2 + 2 * i);
			memcpy(sl.light, light, sizeof light);
			sl.has_light = has_light;
			if (!owners.empty()) sl.owner = owners[off & 0x7fff];
			for (const auto &visibility : lf.visibility)
				if (visibility.owner == sl.owner) { sl.visibility = visibility; break; }
			if (auto it = m_list_extra[m_picture_buffer].find(off); it != m_list_extra[m_picture_buffer].end()) {
				sl.extra = it->second;
				for (const auto &object : sl.extra) sl.polys.push_back(object.polygon);
			}
			lf.slots.push_back(sl);
			off += 26;
			break;
		}
		default: return;
		}
	}
}

void model1::retain_culled_records(logic_frame &frame, const logic_frame &previous)
{
	if (!m_gameplay.record_visibility || !frame.has_camera || !previous.has_camera ||
		frame.frame - previous.frame != 2 || camera_cut(camera_matrix(previous.camera.data()), camera_matrix(frame.camera.data())))
		return;
	const glm::mat4 old_inverse = glm::affineInverse(camera_matrix(previous.camera.data()));
	const glm::mat4 camera = camera_matrix(frame.camera.data());
	std::unordered_map<uint32_t, bool> present;
	for (const auto &part : frame.slots) if (!part.polys.empty()) present[part.owner] = true;
	for (const auto &part : previous.slots) {
		if (part.retained || !part.visibility || part.objects.empty() || present.count(part.owner)) continue;
		const auto visibility = std::find_if(frame.visibility.begin(), frame.visibility.end(),
			[&](const game::record_visibility &v) { return v.owner == part.owner; });
		if (visibility == frame.visibility.end() || !visibility->culled) continue;
		const auto &from = part.visibility->position, &to = visibility->position;
		const glm::vec3 delta(to[0] - from[0], to[1] - from[1], to[2] - from[2]);
		if (glm::length(delta) > 8) continue;   // a reused record or a teleport, not continuous motion
		slot retained = part;
		glm::mat4 world = old_inverse * camera_matrix(part.mat);
		world[3] += glm::vec4(delta, 0);
		const glm::mat4 view = camera * world;
		for (int c = 0; c < 3; c++)
			for (int r = 0; r < 3; r++) retained.mat[c * 3 + r] = view[c][r];
		for (int r = 0; r < 3; r++) retained.mat[9 + r] = view[3][r];
		retained.visibility = *visibility;
		retained.retained = true;
		frame.slots.push_back(std::move(retained));
	}
}

// Instead of drawing the frame: keep its list when it is a new one. The object count stands in
// for the polygon count (the clean scene changes go by it); a frame with nothing in 3D is drawn,
// so a black curtain can be told from a scene being built.
void model1::record_frame()
{
	// No new list this frame (the game draws its 3D every other frame at times, as on the name
	// entry's turning ring): the board keeps showing the last one, so the picture carries on.
	int objects = m_logic[1].list.empty() ? 0 : m_logic[1].objects;
	if ((m_listctl[1] & 0x1f) == 0x1f) {
		const uint16_t *shown = picture_list();
		logic_frame lf;
		lf.list.assign(shown, shown + 0x8000);
		uint64_t h = 1469598103934665603ull;
		for (uint16_t w : lf.list) h = (h ^ w) * 1099511628211ull;
		lf.frame = int(m_frame);
		lf.hscr = m_tiles.tile_r(0x5002);
		lf.vscr = m_tiles.tile_r(0x5006);
		index_objects(lf);
		for (size_t i = 0; i < lf.slots.size(); i++)
			if (!lf.slots[i].extra.empty()) {
				h = (h ^ i) * 1099511628211ull;
				for (const auto &object : lf.slots[i].extra) {
					for (uint32_t value : { object.texture, object.polygon, object.size, object.owner })
						h = (h ^ value) * 1099511628211ull;
					if (object.transform)
						for (float value : *object.transform)
							h = (h ^ std::bit_cast<uint32_t>(value)) * 1099511628211ull;
				}
			}
		lf.hash = h;
		lf.camera = m_list_camera[m_picture_buffer];
		// (a list with nothing of a record or the world in it keeps an older list's camera)
		lf.has_camera = m_list_has_camera[m_picture_buffer] && m_video->records
			&& std::any_of(lf.slots.begin(), lf.slots.end(), [](const slot &sl) { return sl.owner != 0; });
		objects = lf.objects;
		static const bool log = getenv("M1_LOG_SMOOTH") != nullptr;
		if (lf.hash != m_logic[1].hash) {
			const logic_frame &z = m_logic[0], &a = m_logic[1];
			retain_culled_records(lf, a);
			lf.move = list_move(a, lf);
			// A missed game update carries the preceding motion, rather than stopping and catching up.
			lf.still = lf.move >= 0 && a.move > 0.01 && a.move < 0.3 && lf.move < 0.05 * a.move && !z.list.empty() && lf.frame - a.frame == a.frame - z.frame;
			lf.carried = lf.still ? a.move : 0;
			smooth_turn(lf, m_logic[1]);
			follow_records(lf, m_logic[1]);
			m_logic_prev = std::move(m_logic[0]);
			m_logic[0] = std::move(m_logic[1]);
			m_logic[1] = std::move(lf);
		}
	}
	// the sky's scroll, every frame, with the fractions the game dropped: the smooth sky goes by these
	const uint16_t h = m_tiles.tile_r(0x5002), v = m_tiles.tile_r(0x5006);
	sky_sample &sm = m_sky_hist[m_frame % SKY_HIST];
	sm = { int(m_frame), h, v, float(h & 0x1ff) + m_sky_frac_h, float(v & 0x3ff) + m_sky_frac_v };
	sky_settled(sm.lands_h, sm.lands_v);
	// and the next frame's, when the game's routine wrote this one: it runs again at the next vblank
	// from what is in its RAM now (the game moves the camera just after the routine, not before)
	if (m_sky_written == m_frame && m_sky_read_h && m_sky_read_v) {
		const sky_scroll next = sky_scroll_from_ram(!m_sky_between);
		sm.nx = next.hx;
		sm.ny = next.vy;
		sm.next_h = true;
		// The tile-pair game's height still changes during the next frame's CPU work.
		sm.next_v = m_video->sky_scroll->layout != game::sky_camera::format::tile_pair;
	}
	m_render_done = 0;            // the list's uploads are made at the next vblank, as when frames are skipped
	m_last_quads = objects;
	if (objects == 0) {
		// nothing in 3D (a curtain, a 2D screen): the next scene starts afresh, not blended from this
		m_logic[0] = m_logic[1] = m_logic_prev = logic_frame{};
		// (when the host puts pictures together on the GPU: drawn in parts, so the 2D screens and
		// curtains don't cost a whole picture drawn at the render scale and uploaded every frame)
		if (m_parts_mode) render_parts(double(m_frame), m_own_parts, false);
		else {
			screen_update(m_bitmap, m_bitmap.cliprect());
			m_unshown = true;
			m_frame_parts = false;
		}
	}
}

// How far the parts moved between two lists of the same parts (each slot's first model the same,
// in order): the median of their moves (a fast limb or two don't count), else -1.
double model1::list_move(const logic_frame &a, const logic_frame &b)
{
	if (a.slots.size() != b.slots.size() || a.slots.empty()) return -1;
	std::vector<double> d;
	for (size_t i = 0; i < a.slots.size(); i++) {
		const slot &sa = a.slots[i], &sb = b.slots[i];
		if (sa.polys.empty() != sb.polys.empty() || (!sa.polys.empty() && sa.polys[0] != sb.polys[0])) return -1;
		if (sa.polys.empty()) continue;
		const double dx = sb.mat[9] - sa.mat[9], dy = sb.mat[10] - sa.mat[10], dz = sb.mat[11] - sa.mat[11];
		d.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
	}
	if (d.empty()) return -1;
	std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
	return d[d.size() / 2];
}

void model1::smooth_cut()
{
	m_logic[0] = m_logic[1];
	m_logic_prev = logic_frame{};
}

// The picture for a moment `now`, in frames (the newest frame's number plus how far into it).
bool model1::render_smooth(double now, bool shown)
{
	m_draw_time = now - 2;     // the picture is a game frame (two video frames) behind
	// what redraw() needs to draw it again: the moment and the frame it's drawn in
	auto drawn = [&] {
		m_unshown = !shown || m_parts;                  // (in parts: frame() holds only the 3D)
		m_frame_parts = m_parts != nullptr;
		if (shown) { m_shown = true; m_shown_now = now; m_shown_frame = m_frame; }
	};
	if (!m_video->smooth_frames) {
		m_replaying = false;
		m_replay_older = false;
		m_replay_extra = false;
		screen_update(m_bitmap, m_bitmap.cliprect());
		drawn();
		return true;
	}
	if (m_logic[1].list.empty()) { screen_update(m_bitmap, m_bitmap.cliprect()); drawn(); return true; }
	const logic_frame &a = m_logic[0], &b = m_logic[1];
	double alpha = 1;
	if (!a.list.empty() && b.frame > a.frame) {
		// the picture is a span behind (the newer list is in hand when the older one's moment has
		// passed), and never less than a game frame: two lists a frame apart (a scene's first, as it
		// is built) went at a frame behind, and the picture stood still a frame when the next list
		// came two later
		const double span = b.frame - a.frame, behind = std::max(span, 2.0);
		alpha = std::clamp((now - behind - a.frame) / span, 0.0, 1.0);
	}
	m_alpha = float(alpha);
	if (!m_redrawing) m_last_alpha = m_alpha;
	m_replaying = true;
	// (SWA: the pair's pairing follows the camera's move from the pair before it. Its first
	// picture, a scene's or a redraw's after a gap, came before that move was fitted: paired in
	// order, a cut, then the next picture of the same pair, the move fitted by then, wasn't one.
	// That move is fitted first.)
	if (m_video->separate_parts && !m_logic_prev.list.empty() && m_logic_prev.frame < a.frame && !a.list.empty()
		&& std::none_of(std::begin(m_cam_move), std::end(m_cam_move), [&](const cam_move &c) { return c.to == a.frame; }))
		blend_slots(m_logic_prev, a, 0.5);
	m_smooth_stats[0] = m_smooth_stats[1] = m_smooth_stats[2] = 0;
	blend_slots(a, b, alpha);
	// A cut can't be blended: the older view stays until the newer one's moment (as the blend
	// would), so the cut lands when the game's sky, written two frames later, changes too. It
	// stood still meanwhile, a game frame (4 refreshes at 120 Hz) at every camera cut; now it
	// carries on from the list before it as it was moving, easing to half speed at the cut, three
	// quarters of a list on at most (straight on, a fast camera swung into views the game never
	// showed).
	// An update the game skipped (a list with nothing moved, record_frame) carries on the same
	// way, the move before it eased out over it (half a list's move), and the list after starts
	// from there, catching up the rest: no stop, no lurch.
	const bool cut = m_blend_cut;
	if (!cut && a.still && a.frame == m_still_carried && alpha < 1 && b.move > 0) {
		alpha += (1 - alpha) * std::min(0.5, 0.5 * a.carried / b.move);
		blend_slots(a, b, alpha);
		m_alpha = float(alpha);
		if (!m_redrawing) m_last_alpha = m_alpha;
	}
	// (and carries on through it until the next list is in, a frame late at times)
	// (Into a cut, three quarters of a list on, slowing to half speed as the cut lands: eased to a
	// stop, the last refreshes before every cut stood still)
	m_replay_older = (cut && alpha < 1) || b.still;
	m_replay_extra = false;
	if (m_replay_older && !m_logic_prev.list.empty() && b.frame - a.frame == a.frame - m_logic_prev.frame) {
		const double c = std::min(alpha, 1.0);
		blend_slots(m_logic_prev, a, 1 + (cut ? c - 0.25 * c * c : 0.5 * (1 - (1 - c) * (1 - c))));
		m_replay_extra = !m_blend_cut;
		// (Carried on, the eye can pass where the game never put it: into a cut where the ship hit
		// a wall in the Super Star Destroyer's tunnels, the eye carried on through the wall, the
		// wall seen from behind and left out, a hole for a picture. Such a picture is the older list
		// as it is, as the arcade shows it.)
		if (m_replay_extra) {
			const view_t view = *m_view;
			const auto done = m_render_done;
			m_extra_crossed = false;
			m_probing = true;
			tgp_render(m_bitmap, m_bitmap.cliprect(), RENDER_BELOW_HUD);
			m_probing = false;
			m_render_done = done;
			*m_view = view;
			if (m_extra_crossed) m_replay_extra = false;
			m_extra_crossed = false;
		}
		if (!cut && m_replay_extra) m_still_carried = b.frame;   // (the list after it catches up from there)
	}
	m_blend_cut = cut;
	if (cut && b.frame != m_cut_frame) m_cut_frame = b.frame;
	// After a cut the game moves the sky into place over a few frames (at most 4): the picture
	// before the cut used to stay until it stopped jumping, a picture held for 2-7 refreshes at
	// every replay's camera cut; now the new view shows at once, its sky where it settles
	// (smooth_sky_shift).
	// (the camera's RAM has the new view from the cut's frame on; the scroll the game writes
	// follows it a frame later, mixed with the old view's, and lands one or two frames after that)
	// Where it lands is too far to move the sky to (the tile layer moves 24 pixels at most, a
	// panorama 160; a cut swings it hundreds, up or down too): the sky as the game has it, as the
	// arcade shows it. The picture before the cut used to stay instead, 1-9 refreshes at a replay's
	// cuts (up to 100 ms with the hold before the cut), mostly for views looking down at the floor
	// with no sky in them.
	m_sky_settling_now = false;
	if (!m_replay_older && m_cut_frame >= 0 && int(m_frame) - 1 <= m_cut_frame + 4) {
		const sky_sample &cur = m_sky_hist[(m_frame - 1) % SKY_HIST];
		m_sky_settling_now = cur.frame == int(m_frame) - 1 && (cur.lands_h != cur.h || cur.lands_v != cur.v) && sky_reaches(cur);
	}
	if (!m_redrawing) {
		// testing: where the 3D is in this picture, on average (the trace's check that each picture
		// lies between its neighbours: a picture blended from the wrong pair stands out)
		const logic_frame &l = m_replay_older ? a : b;
		const bool own = !m_replay_older || m_replay_extra;
		double sum[3] = { 0, 0, 0 };
		int n = 0;
		for (size_t i = 0; i < l.slots.size(); i++) {
			if (l.slots[i].polys.empty()) continue;
			const float *mt = own && i < m_blend_ok.size() && m_blend_ok[i] ? m_blend[i].data() : l.slots[i].mat;
			for (int k = 0; k < 3; k++) sum[k] += mt[9 + k];
			n++;
		}
		for (int k = 0; k < 3; k++) m_pic_place[k] = n ? float(sum[k] / n) : 0;
	}
	const auto render_done = m_render_done;     // emulation state: drawing again mustn't touch it
	const int quads = m_last_quads;
	gpu_parts *const parts = m_parts;
	if (parts && m_logic[m_replay_older ? 0 : 1].above_hud) {
		// Parts cannot composite above-HUD objects. Choose the whole picture before drawing its 3D.
		m_parts = nullptr;
		m_parts_whole = true;
	}
	screen_update(m_bitmap, m_bitmap.cliprect());
	m_render_done = render_done;
	m_last_quads = quads;
	m_replaying = false;
	m_replay_older = false;
	m_replay_extra = false;
	drawn();
	m_parts = parts;
	static const bool log = getenv("M1_LOG_SMOOTH") != nullptr;
	if (log && alpha < 1)
		fprintf(stderr, "R %.2f a=%.2f unmatched %d jumped %d blended %d\n", now, alpha, m_smooth_stats[0], m_smooth_stats[1], m_smooth_stats[2]);
	m_alpha = 1;
	return true;
}

// As a frame starts while the picture on screen is the current one: what it was drawn from, in
// case frames run on without a new picture shown (a scene change held, the hold after a cut) and
// it has to be drawn again (redraw, at new settings while paused). Every frame in play: about
// 300 KB copied (the tile graphics only when they changed).
void model1::keep_still()
{
	still_state &s = m_still;
	s.frame = m_frame;
	s.logic[0] = m_logic[0];
	s.logic[1] = m_logic[1];
	s.logic_prev = m_logic_prev;
	s.cut_frame = m_cut_frame;
	std::copy(std::begin(m_sky_hist), std::end(m_sky_hist), s.sky_hist);
	s.tile_ram = m_tiles.tile_ram;
	if (s.char_gen != m_tiles.char_gen || s.char_ram.size() != m_tiles.char_ram.size()) {
		s.char_ram = m_tiles.char_ram;
		s.char_gen = m_tiles.char_gen;
	}
	std::copy(std::begin(m_pens), std::end(m_pens), s.pens.begin());
	s.palette = m_paletteram16_ram;
	s.color_xlat = m_color_xlat_ram;
	std::copy(std::begin(m_view->lightparams), std::end(m_view->lightparams), s.lightparams.begin());
	s.light = m_view->light;
	s.spec_enable = m_view->spec_enable;
	if (m_profile->camera_ram) memcpy(s.camera, &m_ram_b[*m_profile->camera_ram], 4);
	s.camera_wide = m_camera_wide;
	s.sky_now = m_sky_now; s.sky_last = m_sky_last;
	s.sky_base = m_sky_base; s.sky_last_tx0 = m_sky_last_tx0;
}

// Exchanges the kept state with the live one (twice: in to draw the kept moment, back out).
void model1::swap_still()
{
	still_state &s = m_still;
	std::swap(m_frame, s.frame);
	std::swap(m_logic[0], s.logic[0]);
	std::swap(m_logic[1], s.logic[1]);
	std::swap(m_logic_prev, s.logic_prev);
	std::swap(m_cut_frame, s.cut_frame);
	std::swap_ranges(std::begin(m_sky_hist), std::end(m_sky_hist), s.sky_hist);
	m_tiles.swap_rams(s.tile_ram, s.char_ram);
	m_tiles_updated = ~0ull;                         // decoded again from the RAMs now in place
	std::swap_ranges(std::begin(m_pens), std::end(m_pens), s.pens.begin());
	m_paletteram16_ram.swap(s.palette);
	m_color_xlat_ram.swap(s.color_xlat);
	m_paletteram16 = m_paletteram16_ram.data();
	m_color_xlat = m_color_xlat_ram.data();
	std::swap_ranges(std::begin(m_view->lightparams), std::end(m_view->lightparams), s.lightparams.begin());
	std::swap(m_view->light, s.light);
	std::swap(m_view->spec_enable, s.spec_enable);
	if (m_profile->camera_ram)
		std::swap_ranges(&m_ram_b[*m_profile->camera_ram], &m_ram_b[*m_profile->camera_ram + 4], s.camera);
	std::swap(m_camera_wide, s.camera_wide);
	std::swap(m_sky_now, s.sky_now); std::swap(m_sky_last, s.sky_last);
	std::swap(m_sky_base, s.sky_base); std::swap(m_sky_last_tx0, s.sky_last_tx0);
}

// The blended matrix of every slot in the newer list. Slots pair by position when the two lists
// have the same shape (most of a fight), else by the first object drawn in them. A picture where
// many slots jump is a camera cut: nothing is blended, so parts never come apart.
// The camera's move between two frames, fitted to the paired slots' places (rigid: a turn and a
// shift, least squares, the slots that don't follow it dropped and fitted again). A paired slot far
// off it (over 2% of its depth: paired with another copy of its model) is paired instead with the
// copy that follows the move, when one does; one that none follows, of a model with three or more
// copies in the frame (scenery, not a fighter's part, which moves by itself), is marked -2: drawn
// where the newer frame has it. VF2's (model2_smooth.cpp), here only for runs of copies that
// changed length. The number paired again.
int model1::repair_by_camera(const logic_frame &a, const logic_frame &b, std::vector<int> &pair, const std::vector<char> &changed_run)
{
	struct pt { double x, y, z; };
	auto first = [](const slot &sl) { return sl.polys.empty() ? 0u : sl.polys[0]; };
	// (SWA) Paired again first by the camera's last move, carried on: the Death Star's surface list
	// changes order as the camera crosses into the next cell, and pairing in order paired most of
	// it with other cells, a fit to those a move matching none (a cut that wasn't, or the cells
	// each blended off by its own error). Each slot pairs with the slot of its model that move
	// takes nearest to it, when one comes within 3% of its depth.
	const cam_move *pv = nullptr;
	double pv_scale = 1;
	if (m_video->separate_parts) {
		for (const cam_move &c : m_cam_move)
			if (c.fit && c.to <= a.frame && c.to >= a.frame - 4 && c.from < c.to && (!pv || c.to > pv->to)) pv = &c;
		if (pv) {
			const double scale = pv_scale = double(b.frame - a.frame) / (pv->to - pv->from);
			auto predicted = [&](const float *pa, const float *pb) {
				double d = 0;
				for (int r = 0; r < 3; r++) {
					const double o = pv->R[r][0] * pa[9] + pv->R[r][1] * pa[10] + pv->R[r][2] * pa[11] + pv->T[r] * scale - pb[9 + r];
					d += o * o;
				}
				return std::sqrt(d) / std::max({ std::fabs(double(pa[11])), std::fabs(double(pb[11])), 1.0 });
			};
			std::unordered_multimap<uint32_t, int> models;
			for (size_t o = 0; o < a.slots.size(); o++)
				if (!a.slots[o].polys.empty()) models.emplace(first(a.slots[o]), int(o));
			for (size_t k = 0; k < pair.size(); k++) {
				if (b.slots[k].polys.empty()) continue;
				const float *pb = b.slots[k].mat;
				double best_r = pair[k] >= 0 ? predicted(a.slots[pair[k]].mat, pb) : INFINITY;
				if (best_r <= 0.03) continue;
				int best = -1;
				const auto [from, to] = models.equal_range(first(b.slots[k]));
				for (auto it = from; it != to; ++it) {
					const double r = predicted(a.slots[it->second].mat, pb);
					if (r < best_r && r <= 0.03) best_r = r, best = it->second;
				}
				if (best >= 0) pair[k] = best;
			}
		}
	}
	std::vector<pt> P, Q;
	std::vector<int> K;
	for (size_t k = 0; k < pair.size(); k++) {
		const int q = pair[k];
		if (q < 0 || b.slots[k].polys.empty()) continue;
		const float *pa = a.slots[q].mat, *pb = b.slots[k].mat;
		P.push_back({ pa[9], pa[10], pa[11] });
		Q.push_back({ pb[9], pb[10], pb[11] });
		K.push_back(int(k));
	}
	if (P.empty()) return 0;
	// (SWA's Death Star surface: models that never move by themselves)
	std::vector<char> still(P.size());
	int n_still = 0;
	for (size_t i = 0; i < P.size(); i++) {
		const uint32_t f = first(b.slots[K[i]]);
		n_still += still[i] = m_video->still_model(f);
	}
	double R[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, T[3] = {};
	std::vector<char> fits(P.size(), 1);
	std::vector<char> use(P.size(), 1);
	auto residual = [&](const pt &p, const pt &q) {
		double o[3];
		for (int r = 0; r < 3; r++) o[r] = R[r][0] * p.x + R[r][1] * p.y + R[r][2] * p.z + T[r];
		const double dx = o[0] - q.x, dy = o[1] - q.y, dz = o[2] - q.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz) / std::max({ std::fabs(p.z), std::fabs(q.z), 1.0 });
	};
	std::vector<double> res(P.size());
	bool planar = false;
	// the best turn for the slots weighted: the orthonormal part of their covariance (Newton's
	// iteration for the polar decomposition), then the shift between the centres
	auto fit = [&](const std::vector<double> &w) {
		double cp[3] = {}, cq[3] = {}, H[3][3] = {}, n = 0;
		int used = 0;
		for (size_t i = 0; i < P.size(); i++)
			if (w[i] > 0) { cp[0] += w[i] * P[i].x; cp[1] += w[i] * P[i].y; cp[2] += w[i] * P[i].z; cq[0] += w[i] * Q[i].x; cq[1] += w[i] * Q[i].y; cq[2] += w[i] * Q[i].z; n += w[i]; used++; }
		if (used < 4) return false;
		for (int r = 0; r < 3; r++) cp[r] /= n, cq[r] /= n;
		for (size_t i = 0; i < P.size(); i++)
			if (w[i] > 0) {
				const double p[3] = { P[i].x - cp[0], P[i].y - cp[1], P[i].z - cp[2] }, q[3] = { Q[i].x - cq[0], Q[i].y - cq[1], Q[i].z - cq[2] };
				for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) H[r][c] += w[i] * q[r] * p[c];
			}
		// (scaled to about 1 first: each step only halves a large singular value, and SWA's places,
		// tens of thousands of units, left it 13 after 30 steps, a "turn" that matched nothing: every
		// part off it by many times its depth, each list a cut, its attract unblended)
		double M[3][3], norm = 0;
		for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) norm += H[r][c] * H[r][c];
		if (!(norm > 0)) return false;
		norm = std::sqrt(norm / 3);
		for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) M[r][c] = H[r][c] / norm;
		if (!planar) {
			double Mn[3][3];
			memcpy(Mn, M, sizeof Mn);
			for (int it = 0; it < 60; it++) {
				const double det = Mn[0][0] * (Mn[1][1] * Mn[2][2] - Mn[1][2] * Mn[2][1]) - Mn[0][1] * (Mn[1][0] * Mn[2][2] - Mn[1][2] * Mn[2][0]) + Mn[0][2] * (Mn[1][0] * Mn[2][1] - Mn[1][1] * Mn[2][0]);
				if (std::fabs(det) < 1e-30) return false;
				double C[3][3];   // the inverse's transpose: cofactors / det
				C[0][0] = (Mn[1][1] * Mn[2][2] - Mn[1][2] * Mn[2][1]) / det; C[0][1] = (Mn[1][2] * Mn[2][0] - Mn[1][0] * Mn[2][2]) / det; C[0][2] = (Mn[1][0] * Mn[2][1] - Mn[1][1] * Mn[2][0]) / det;
				C[1][0] = (Mn[0][2] * Mn[2][1] - Mn[0][1] * Mn[2][2]) / det; C[1][1] = (Mn[0][0] * Mn[2][2] - Mn[0][2] * Mn[2][0]) / det; C[1][2] = (Mn[0][1] * Mn[2][0] - Mn[0][0] * Mn[2][1]) / det;
				C[2][0] = (Mn[0][1] * Mn[1][2] - Mn[0][2] * Mn[1][1]) / det; C[2][1] = (Mn[0][2] * Mn[1][0] - Mn[0][0] * Mn[1][2]) / det; C[2][2] = (Mn[0][0] * Mn[1][1] - Mn[0][1] * Mn[1][0]) / det;
				for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) Mn[r][c] = 0.5 * (Mn[r][c] + C[r][c]);
			}
			// (a proper turn: places in a plane, the Death Star's trench, fit its mirror image as
			// well, and the iteration came out at that: the cells' axes turned inside out by it,
			// their faces culled, the trench gone, black, for a picture)
			const double det = Mn[0][0] * (Mn[1][1] * Mn[2][2] - Mn[1][2] * Mn[2][1]) - Mn[0][1] * (Mn[1][0] * Mn[2][2] - Mn[1][2] * Mn[2][0]) + Mn[0][2] * (Mn[1][0] * Mn[2][1] - Mn[1][1] * Mn[2][0]);
			if (det > 0) {
				memcpy(R, Mn, sizeof R);
				for (int r = 0; r < 3; r++) T[r] = cq[r] - (R[r][0] * cp[0] + R[r][1] * cp[1] + R[r][2] * cp[2]);
				return true;
			}
		}
		{
			// (places nearly in a plane, the Death Star's surface: their covariance nearly singular,
			// the iteration above went off, or came out a mirror. Horn's quaternion, the largest
			// eigenvector of a 4x4, is the best proper turn however they lie.)
			const double (&S)[3][3] = M;   // S[b][a]: q's b by p's a
			double N[4][4] = {
				{ S[0][0] + S[1][1] + S[2][2], S[2][1] - S[1][2], S[0][2] - S[2][0], S[1][0] - S[0][1] },
				{ S[2][1] - S[1][2], S[0][0] - S[1][1] - S[2][2], S[1][0] + S[0][1], S[0][2] + S[2][0] },
				{ S[0][2] - S[2][0], S[1][0] + S[0][1], -S[0][0] + S[1][1] - S[2][2], S[2][1] + S[1][2] },
				{ S[1][0] - S[0][1], S[0][2] + S[2][0], S[2][1] + S[1][2], -S[0][0] - S[1][1] + S[2][2] } };
			double V[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
			for (int sweep = 0; sweep < 30; sweep++)   // (Jacobi's rotations)
				for (int i = 0; i < 3; i++)
					for (int j = i + 1; j < 4; j++) {
						if (std::fabs(N[i][j]) < 1e-15) continue;
						const double th = (N[j][j] - N[i][i]) / (2 * N[i][j]);
						const double t = (th >= 0 ? 1 : -1) / (std::fabs(th) + std::sqrt(th * th + 1));
						const double c = 1 / std::sqrt(t * t + 1), sn = t * c;
						for (int k = 0; k < 4; k++) {
							const double a = N[k][i], b = N[k][j];
							N[k][i] = c * a - sn * b, N[k][j] = sn * a + c * b;
						}
						for (int k = 0; k < 4; k++) {
							const double a = N[i][k], b = N[j][k];
							N[i][k] = c * a - sn * b, N[j][k] = sn * a + c * b;
						}
						for (int k = 0; k < 4; k++) {
							const double a = V[k][i], b = V[k][j];
							V[k][i] = c * a - sn * b, V[k][j] = sn * a + c * b;
						}
					}
			int e = 0;
			for (int i = 1; i < 4; i++) if (N[i][i] > N[e][e]) e = i;
			const double w = V[0][e], x = V[1][e], y = V[2][e], z = V[3][e];
			const double R_[3][3] = {
				{ 1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y) },
				{ 2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x) },
				{ 2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y) } };
			memcpy(R, R_, sizeof R);
			for (int r = 0; r < 3; r++) T[r] = cq[r] - (R[r][0] * cp[0] + R[r][1] * cp[1] + R[r][2] * cp[2]);
			return true;
		}
	};
	// (SWA: started from the slots the camera's last move carries on. The surface's cells lie
	// nearly in a plane, and a few still paired with another copy of their model, a cell or two
	// along, steered the first fit off the rest: none came within 2% of it, the pair's cells each
	// unblended, drawn where the newer list has them beside cells blended a quarter of the way,
	// dark slabs across the surface for a picture.)
	auto robust = [&] {
		use = fits;
		if (pv) {
			memcpy(R, pv->R, sizeof R);
			for (int r = 0; r < 3; r++) T[r] = pv->T[r] * pv_scale;
			std::vector<char> carried(P.size());
			size_t n = 0;
			for (size_t i = 0; i < P.size(); i++) n += carried[i] = fits[i] && residual(P[i], Q[i]) <= 0.03;
			if (n >= 8) use = carried;
		}
		for (int pass = 0; pass < 3; pass++) {
			if (!fit(std::vector<double>(use.begin(), use.end()))) return false;
			std::vector<double> sorted;
			for (size_t i = 0; i < P.size(); i++) {
				res[i] = residual(P[i], Q[i]);
				if (fits[i]) sorted.push_back(res[i]);
			}
			std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
			const double med = sorted[sorted.size() / 2];
			for (size_t i = 0; i < P.size(); i++) use[i] = fits[i] && res[i] <= std::max(3 * med, 0.002);
		}
		return true;
	};
	auto still_on = [&] { int n = 0; for (size_t i = 0; i < P.size(); i++) n += still[i] && res[i] <= 0.02; return n; };
	// A few stationary parts at one origin (SWA's globe) cannot fit a camera from their places.
	// Their axes do determine it; fitting the moving ships instead invented cuts on the approach.
	auto stationary_axes = [&] {
		if (m_video->camera_anchors.empty()) return false;
		std::vector<char> anchors(P.size());
		for (size_t i = 0; i < P.size(); i++) {
			const uint32_t f = first(b.slots[K[i]]);
			anchors[i] = std::find(m_video->camera_anchors.begin(), m_video->camera_anchors.end(), first(a.slots[pair[K[i]]])) != m_video->camera_anchors.end() &&
				std::find(m_video->camera_anchors.begin(), m_video->camera_anchors.end(), f) != m_video->camera_anchors.end();
		}
		const auto found = std::find(anchors.begin(), anchors.end(), 1);
		if (found == anchors.end()) return false;
		const size_t at = size_t(found - anchors.begin());
		const float *ma = a.slots[pair[K[at]]].mat, *mb = b.slots[K[at]].mat;
		const glm::dmat3 ra(glm::make_mat3(ma)), rb(glm::make_mat3(mb));
		if (std::fabs(glm::determinant(ra)) < 1e-12) return false;
		const glm::dmat3 turn = rb * glm::inverse(ra);
		const glm::dmat3 check = glm::transpose(turn) * turn;
		if (std::fabs(glm::determinant(turn) - 1) > 0.001) return false;
		for (int c = 0; c < 3; c++)
			for (int r = 0; r < 3; r++)
				if (std::fabs(check[c][r] - (c == r ? 1.0 : 0.0)) > 0.001) return false;
		for (size_t i = 0; i < P.size(); i++)
			if (anchors[i] && (glm::length(glm::dvec3(P[i].x - P[at].x, P[i].y - P[at].y, P[i].z - P[at].z)) > 1 ||
				glm::length(glm::dvec3(Q[i].x - Q[at].x, Q[i].y - Q[at].y, Q[i].z - Q[at].z)) > 1)) return false;
		const glm::dvec3 shift = glm::dvec3(Q[at].x, Q[at].y, Q[at].z) - turn * glm::dvec3(P[at].x, P[at].y, P[at].z);
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++) R[r][c] = turn[c][r];
			T[r] = shift[r];
		}
		for (size_t i = 0; i < P.size(); i++) res[i] = residual(P[i], Q[i]);
		use = anchors;
		return true;
	};
	const bool anchored = stationary_axes();
	if (!anchored && (P.size() < 8 || !robust())) return 0;
	if (n_still < 8 && !anchored) n_still = 0;
	// (SWA: fitted again to the Death Star's surface alone when most of it came off the fit. The
	// cockpit's frame, the ships and the far scenery, half the list, are off the camera's move; as
	// the camera crossed into the next cell, the surface's list in another order and a few cells
	// paired with the wrong copy, the fit came off the surface, or under half the slots on it: the
	// move was dropped, dozens of cells drawn unblended, the surface shaking through the cockpit's
	// run. Alone it isn't always better: in the trench, its cells' z crossing 0 beside the camera,
	// the few there steer it. The fit that carries more of the surface is kept.)
	if (!anchored && n_still >= 8 && still_on() * 2 <= n_still) {
		double R0[3][3], T0[3];
		memcpy(R0, R, sizeof R), memcpy(T0, T, sizeof T);
		const std::vector<double> res0 = res;
		const std::vector<char> use0 = use;
		const int on0 = still_on();
		fits = still, planar = true;
		if (!robust() || still_on() <= on0) memcpy(R, R0, sizeof R), memcpy(T, T0, sizeof T), res = res0, use = use0;
		fits.assign(P.size(), 1), planar = false;
	}
	// (SWA: the camera's turn as the still models' own axes give it. The fit above has their places
	// only, and a few far apart with ships among them steer its turn: at the Super Star Destroyer's
	// tunnel end, 18-22 parts, it was half a degree off, the red girders' far ends a few pixels from
	// where either list put them, every picture between the lists, and each list's own picture
	// jumped. A still model's axes turn exactly as the camera does; the turn that carries most of
	// them, with the shift they agree on, is kept if it carries as many as the fit did.)
	bool by_axes = false;
	if (!anchored) {
		int on0 = 0;
		for (size_t i = 0; i < P.size(); i++) on0 += still[i] && res[i] <= 0.02;
		double bR[3][3], bT[3];
		int best = 0;
		for (size_t j = 0; j < P.size() && on0; j++) {
			if (!still[j] || res[j] > 0.05 || first(a.slots[pair[K[j]]]) != first(b.slots[K[j]])) continue;
			const glm::dmat3 ra(glm::make_mat3(a.slots[pair[K[j]]].mat)), rb(glm::make_mat3(b.slots[K[j]].mat));
			if (std::fabs(glm::determinant(ra)) < 1e-12) continue;
			const glm::dmat3 turn = rb * glm::inverse(ra), check = glm::transpose(turn) * turn;
			bool proper = std::fabs(glm::determinant(turn) - 1) <= 0.001;
			for (int c = 0; c < 3 && proper; c++)
				for (int r = 0; r < 3; r++) proper &= std::fabs(check[c][r] - (c == r ? 1.0 : 0.0)) <= 0.001;
			if (!proper) continue;
			double tR[3][3], tT[3] = {};
			for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) tR[r][c] = turn[c][r];
			const auto shifted = [&](size_t i, int r) { return (&Q[i].x)[r] - (tR[r][0] * P[i].x + tR[r][1] * P[i].y + tR[r][2] * P[i].z); };
			for (int r = 0; r < 3; r++) tT[r] = shifted(j, r);
			int n = 0;
			double sum[3] = {};
			for (size_t i = 0; i < P.size(); i++) {
				if (!still[i]) continue;
				double o[3];
				for (int r = 0; r < 3; r++) o[r] = shifted(i, r) - tT[r];
				const double d = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]) / std::max({ std::fabs(P[i].z), std::fabs(Q[i].z), 1.0 });
				if (d <= 0.002) { n++; for (int r = 0; r < 3; r++) sum[r] += shifted(i, r); }
			}
			if (n > best) {
				best = n;
				memcpy(bR, tR, sizeof bR);
				for (int r = 0; r < 3; r++) bT[r] = sum[r] / n;
			}
		}
		if (best && best >= on0) {
			memcpy(R, bR, sizeof R), memcpy(T, bT, sizeof T);
			by_axes = true;
			for (size_t i = 0; i < P.size(); i++) res[i] = residual(P[i], Q[i]);
		}
	}
	// A cut the parts' moves miss: the camera's own move in a frame, a turn of over 15 degrees or a
	// shift of over 8.5% of the scenery's depth (a game's fastest swing, a knockdown's, turns it 11
	// degrees and shifts it 7.7%). The parts mostly stay close (a cut to the same fighters from
	// another side: TIME OVER's far view to the one from above), and blending them drew views
	// between the two that the game never showed. Close in (the attract's last move, a replay's
	// camera swung in to the fighters) the scenery's depth is small: an ordinary swing passed 8.5%
	// pair after pair, and the close-up went unblended, stepping at the game's 30 a second while
	// the counter said 120. So a shift of 8.5-25% is a cut only out of a still camera (the last
	// pair's under 3%: a fight's camera speeding up from 4-6% to 10% was taken for one) and unlike
	// the last pair's (about as far, the same way: a swing carrying on); over 25%, or a turn over
	// 15 degrees, always. (Attract, 6000 frames: every cut was over 25% or 15 degrees, every swing
	// taken for one under 17%.)
	{
		const double tr = std::clamp((R[0][0] + R[1][1] + R[2][2] - 1) / 2, -1.0, 1.0);
		double mv = 0, dp = 0, d[3] = {};
		int n = 0;
		for (size_t i = 0; i < P.size(); i++)
			if (use[i]) {
				mv += std::sqrt((Q[i].x - P[i].x) * (Q[i].x - P[i].x) + (Q[i].y - P[i].y) * (Q[i].y - P[i].y) + (Q[i].z - P[i].z) * (Q[i].z - P[i].z));
				dp += anchored ? std::max(std::sqrt(P[i].x * P[i].x + P[i].y * P[i].y + P[i].z * P[i].z), 1.0)
					: std::max(std::fabs(P[i].z), 1.0);
				d[0] += Q[i].x - P[i].x; d[1] += Q[i].y - P[i].y; d[2] += Q[i].z - P[i].z; n++;
			}
		const double turn = std::acos(tr) * 180 / M_PI;
		if (n) for (double &v : d) v /= n;
		// (pairs can come out of order, the next frame's run ahead while the screen waits: the last
		// pair's move is the one that ended at this pair's older list)
		cam_move *slot = nullptr;
		for (cam_move &c : m_cam_move) if (c.to == b.frame) slot = &c;
		if (!slot) { slot = &m_cam_move[m_cam_move_next]; m_cam_move_next = (m_cam_move_next + 1) % int(std::size(m_cam_move)); }
		const double ratio = dp > 0 ? mv / dp : 0;
		*slot = { a.frame, b.frame, { d[0], d[1], d[2] }, turn, ratio };
		static const cam_move none;
		const cam_move *found = &none;
		for (const cam_move &c : m_cam_move)   // (that one, else the latest a few frames before it)
			if (c.to <= a.frame && c.to >= a.frame - 4 && c.to < b.frame && (found == &none || c.to > found->to)) found = &c;
		const cam_move &pv = *found;
		const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), plen = std::sqrt(pv.d[0] * pv.d[0] + pv.d[1] * pv.d[1] + pv.d[2] * pv.d[2]);
		const double diff = std::sqrt((d[0] - pv.d[0]) * (d[0] - pv.d[0]) + (d[1] - pv.d[1]) * (d[1] - pv.d[1]) + (d[2] - pv.d[2]) * (d[2] - pv.d[2]));
		const bool known = found != &none;
		const bool swing = known && n && diff < 0.5 * std::max(len, plen) && turn < 2 * pv.turn + 3;
		const bool moving = known && pv.ratio > 0.03;
		// (a turn the parts' places fit that their own axes didn't make: close in on a throw the
		// fighters' parts are most of the list, and the thrown one's swing over fit as a turn of
		// the camera's, 21 degrees, while the stage and the thrower kept theirs. The camera's turn
		// turns every part's axes with it, the fighters' too in a frame or two. A fit turned over
		// 5 degrees that the axes didn't follow isn't the camera's: nor is its move.)
		bool turned_all = true;
		if (turn > 5) {
			int with[2] = { 0, 0 }, n_axes = 0;
			for (size_t i = 0; i < P.size(); i++) {
				const float *ma = a.slots[pair[K[i]]].mat, *mb = b.slots[K[i]].mat;
				n_axes++;
				for (int conv = 0; conv < 2; conv++) {
					bool ok = true;
					for (int c = 0; c < 3 && ok; c++) {
						// (the axes as the triples or as the columns of the matrix: whichever the camera's turn turns)
						double u[3], v[3], w[3] = {};
						for (int r = 0; r < 3; r++) { u[r] = conv ? ma[3 * r + c] : ma[3 * c + r]; v[r] = conv ? mb[3 * r + c] : mb[3 * c + r]; }
						for (int r = 0; r < 3; r++) w[r] = R[r][0] * u[0] + R[r][1] * u[1] + R[r][2] * u[2];
						const double lw = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]), lv = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
						ok = lw > 0 && lv > 0 && (w[0] * v[0] + w[1] * v[1] + w[2] * v[2]) > 0.985 * lw * lv;   // (within 10 degrees)
					}
					with[conv] += ok;
				}
			}
			turned_all = std::max(with[0], with[1]) * 2 > n_axes;
		}
		m_camera_cut = (turn > 15 && turned_all) || ratio > 0.25 || (ratio > 0.085 && !swing && !moving && turned_all);
		m_camera_sure = known && !m_camera_cut;
		for (size_t i = 0; i < P.size(); i++) m_cam_res[K[i]] = float(res[i]);
		// (a fit most of the paired slots follow carries on to the next pair's)
		int inliers = 0;
		for (size_t i = 0; i < P.size(); i++) inliers += res[i] <= 0.02;
		slot->fit = !m_camera_cut && (inliers * 2 > int(P.size()) || (n_still && still_on() * 2 > n_still));
		memcpy(slot->R, R, sizeof R);
		memcpy(slot->T, T, sizeof T);
	}
	int repaired = 0;
	// (SWA: the Death Star's surface is a list of cells round the camera, in an order that changes
	// as the camera crosses into the next cell: the pairing in order left dozens of them unpaired or
	// paired with another cell, each drawn where the newer list has it while the cells beside it
	// were blended, seams and slabs across the trench in the pictures between. Each such slot is
	// paired with the slot of its model the camera's move carries onto it; one none does (a cell
	// come into the list) is moved with the camera, as scenery: pair -3, m_cam_older.)
	if (m_video->separate_parts && m_camera_sure) {
		// (fitted again by the scenery on it, each by its error over its depth: the far scenery,
		// tens of thousands of units out, steered the fit, and the cells beside the camera came
		// off it by 1-2%, drawn from places a crack apart from their neighbours')
		// (Only a fit that keeps them: in the trench the places' z isn't their depth, crossing 0
		// beside the camera, and the few slots there steered it off every other, none left on it.
		// A fit most slots are off isn't the camera's move: the trench's walls, cells of a few
		// models repeating, fit a move one cell along, and moved by it the walls were blended across
		// a cell and more, a wall missing or a grey slab across the screen.)
		// (Not when the still models' own axes gave the move: it carries them exactly, and the
		// weighted places turned it off them again, the tunnel's girders by half a degree.)
		auto on_it = [&] { int n = 0; for (double r : res) n += r <= 0.02; return n; };
		const int kept = on_it();
		std::vector<double> w(P.size());
		for (size_t i = 0; i < P.size(); i++) {
			const double depth = std::max({ std::fabs(P[i].z), std::fabs(Q[i].z), 200.0 });
			w[i] = res[i] <= 0.02 ? 1 / (depth * depth) : 0;
		}
		const std::vector<double> first_res = res;
		double first_R[3][3], first_T[3];
		memcpy(first_R, R, sizeof R), memcpy(first_T, T, sizeof T);
		if (!anchored && !by_axes && fit(w)) {
			for (size_t i = 0; i < P.size(); i++) res[i] = residual(P[i], Q[i]);
			if (on_it() < kept) res = first_res, memcpy(R, first_R, sizeof R), memcpy(T, first_T, sizeof T);
		}
		for (size_t i = 0; i < P.size(); i++) m_cam_res[K[i]] = float(res[i]);
		if (on_it() * 2 <= int(P.size()) && !(n_still && still_on() * 2 > n_still)) {
			for (cam_move &c : m_cam_move)
				if (c.from == a.frame && c.to == b.frame) c.res = m_cam_res;
			return 0;
		}
		std::unordered_set<uint32_t> scenery;   // (models with a copy the camera's move carries)
		for (size_t k = 0; k < b.slots.size(); k++)
			if (!b.slots[k].polys.empty() && pair[k] >= 0 && m_cam_res[k] >= 0 && m_cam_res[k] <= 0.01f) scenery.insert(first(b.slots[k]));
		// the older matrix the camera's move takes onto the newer one: turn^T (newer - shift)
		if (m_cam_older.size() < b.slots.size()) m_cam_older.resize(b.slots.size());
		auto by_camera = [&](size_t k) {
			const float *pb = b.slots[k].mat;
			auto &pa = m_cam_older[k];
			for (int c = 0; c < 4; c++) {
				double v[3];
				for (int r = 0; r < 3; r++) v[r] = pb[3 * c + r] - (c == 3 ? T[r] : 0);
				for (int r = 0; r < 3; r++) pa[3 * c + r] = float(R[0][r] * v[0] + R[1][r] * v[1] + R[2][r] * v[2]);
			}
		};
		// (each older slot's error off the camera's move the pair before, as it ended)
		const std::vector<float> *before = nullptr;
		for (const cam_move &c : m_cam_move)
			if (c.to == a.frame && c.res.size() == a.slots.size()) before = &c.res;
		// (an older slot off the camera's move the pair before, or not known to be on it)
		auto moving = [&](size_t o) { return !before || o >= before->size() || (*before)[o] < 0 || (*before)[o] > 0.05f; };
		std::vector<char> claimed;      // (older slots paired, made when a ship's copy is looked for)
		std::vector<size_t> ship;       // (ships moved with the camera all the same: none of their model near)
		for (size_t k = 0; k < b.slots.size(); k++) {
			if (b.slots[k].polys.empty()) continue;
			const uint32_t f = first(b.slots[k]);
			// With an exact stationary reference, a paired ship keeps its own motion, however
			// close it is to the camera's. The launch X-wing otherwise slid back each list.
			if (anchored && pair[k] >= 0 && !m_video->still_model(f) && first(a.slots[pair[k]]) == f) {
				m_cam_res[k] = -1;
				m_ship[k] = 1;
				continue;
			}
			if (pair[k] >= 0 && m_cam_res[k] >= 0 && m_cam_res[k] <= 0.02f) { by_camera(k); continue; }
			const float *pb = b.slots[k].mat;
			const pt q{ pb[9], pb[10], pb[11] };
			int best = -1;
			double bres = pair[k] >= 0 ? std::min(m_cam_res[k] * 0.25, 0.01) : 0.01;
			for (size_t o = 0; o < a.slots.size(); o++)
				if (first(a.slots[o]) == f && int(o) != pair[k]) {
					const float *pa = a.slots[o].mat;
					const double r = residual({ pa[9], pa[10], pa[11] }, q);
					if (r < bres) bres = r, best = int(o);
				}
			if (best >= 0) {
				pair[k] = best, m_cam_res[k] = float(bres), repaired++;
				if (!claimed.empty()) claimed[best] = 1;
				by_camera(k);
				continue;
			}
			// (a model that never moves by itself, the Death Star's surface, with no copy the camera's
			// move carries onto it: a cell come into the list, or the next one along at a finer model.
			// Paired as a ship with a copy of its model gone from the list, a cell or more away (cells
			// of a model repeat, 2048 and 4096 apart), it was blended across from there, the trench's
			// rim and the surface changing shape for a picture, all through the run. Moved with the
			// camera, every cell is moved by the one move and none parts from its neighbours.)
			if (m_video->still_model(f)) {
				by_camera(k);
				pair[k] = -3, m_cam_res[k] = 0, repaired++;
				continue;
			}
			// (its own model, blending: a ship, moving by itself. One that jumps, moving over 60% of
			// its depth, is another copy of the model, a cell that left the list, or a shot drawn at
			// the newer place anyway: moved with the camera, it lands on that place all the same. So
			// is one off the camera's move whose model has copies on it: the cells' models repeat. And
			// one whose older copy the camera's move carried the pair before: scenery doesn't set
			// off. That was another cell of its model a cell along, the newer one a finer model in
			// the older one's place, the cells nearing the camera (5 polygons to 77-1269) as it
			// crossed into the next: blended over from there, a slab swept across the surface.)
			const float was = before && pair[k] >= 0 ? (*before)[pair[k]] : -1;
			const bool set_off = was >= 0 && was <= 0.02f && m_cam_res[k] > 0.05f;
			if (pair[k] >= 0 && first(a.slots[pair[k]]) == f && !(m_cam_res[k] > 0.05f && scenery.count(f)) && !set_off) {
				const float *pa = a.slots[pair[k]].mat;
				const double dx = pb[9] - pa[9], dy = pb[10] - pa[10], dz = pb[11] - pa[11];
				const double depth = eye_distance(pa, pb);
				if (dx * dx + dy * dy + dz * dz <= 0.36 * depth * depth) continue;
			}
			// (a model with no copy on the camera's move is a ship, moving by itself: a TIE fighter
			// whose parts came in another order, the X-wing ahead. Moved with the camera, as scenery,
			// it was drawn from where the camera's move put it, a TIE closing on the camera from
			// nearer still, each list's pictures sliding back and then jumping on, copies left
			// behind. Paired with the nearest copy of its model, it blends from its own older place:
			// up to its depth away, a TIE passing the camera moving 70% of its depth in a list. A
			// model with copies on the camera's move pairs only with a copy that was off it the pair
			// before, moving by itself too: the TIEs flying alongside the camera are on its move.)
			const bool on_move = scenery.count(f) > 0;
			if (!on_move || before) {
				if (claimed.empty()) {
					claimed.assign(a.slots.size(), 0);
					for (size_t j = 0; j < b.slots.size(); j++)
						if (pair[j] >= 0 && size_t(pair[j]) < claimed.size() && j != k) claimed[pair[j]] = 1;
				}
				int near = -1;
				double nd = 0;
				for (size_t o = 0; o < a.slots.size(); o++) {
					if ((claimed[o] && int(o) != pair[k]) || first(a.slots[o]) != f) continue;
					if (on_move && !(o < before->size() && (*before)[o] > 0.05f)) continue;
					const float *pa = a.slots[o].mat;
					// (nor one the camera's move carried the pair before that's far off it now: scenery
					// doesn't set off. A surface cell a cell along, 30% of its depth off, was paired as a
					// ship and swept across the dark polygon under the surface, a slab for a picture.)
					if (!moving(o) && residual({ pa[9], pa[10], pa[11] }, q) > 0.2) continue;
					const double dx = pb[9] - pa[9], dy = pb[10] - pa[10], dz = pb[11] - pa[11];
					const double depth = eye_distance(pa, pb);
					const double d = dx * dx + dy * dy + dz * dz;
					if (d > (moving(o) ? 0.64 : 0.36) * depth * depth || (near >= 0 && d >= nd)) continue;
					near = int(o), nd = d;
				}
				// (or its own older slot, another model of a ship: the X-wing's models take turns)
				if (near < 0 && !on_move && pair[k] >= 0 && moving(pair[k]) && !scenery.count(first(a.slots[pair[k]]))) {
					const float *pa = a.slots[pair[k]].mat;
					const double dx = pb[9] - pa[9], dy = pb[10] - pa[10], dz = pb[11] - pa[11];
					const double depth = eye_distance(pa, pb);
					if (dx * dx + dy * dy + dz * dz <= 0.64 * depth * depth) near = pair[k];
				}
				if (near >= 0) {
					if (near != pair[k]) repaired++;
					pair[k] = near, claimed[near] = 1, m_ship[k] = 1;
					m_cam_res[k] = std::max(m_cam_res[k], 0.05f);   // (not one the camera's move carries)
					continue;
				}
				if (!on_move) ship.push_back(k);
			}
			by_camera(k);
			pair[k] = -3, m_cam_res[k] = 0, repaired++;
		}
		// (a ship's part with no older copy, a TIE's wings as it nears and its model gets finer, moves
		// with the nearest of the ship's parts paired, one model each, that moved by itself the pair
		// before too, at about its depth and within a fighter's span (400). Moved with the camera,
		// the wings closed on it from nearer still, filling the screen. Surface cells have many
		// models, or none moving near them: a far shot's neighbours, dragged along, tore the surface.)
		for (size_t k : ship) {
			const float *pk = b.slots[k].mat;
			if (b.slots[k].polys.size() != 1 || !before) continue;
			int near = -1;
			double nd = 0;
			for (size_t j = 0; j < b.slots.size(); j++) {
				if (pair[j] < 0 || b.slots[j].polys.size() != 1 || !(m_cam_res[j] > 0.05f) || scenery.count(first(b.slots[j]))) continue;
				if (size_t(pair[j]) >= before->size() || !((*before)[pair[j]] > 0.05f)) continue;
				const float *pj = b.slots[j].mat;
				const double z = std::min(std::fabs(pk[11]), std::fabs(pj[11]));
				const double dx = pk[9] - pj[9], dy = pk[10] - pj[10], dz = pk[11] - pj[11];
				const double d = dx * dx + dy * dy + dz * dz;
				if (std::fabs(dz) <= 0.5 * z && d <= std::min(2.25 * z * z, 400.0 * 400.0) && (near < 0 || d < nd)) near = int(j), nd = d;
			}
			if (near < 0) continue;
			// (its shift only: the part leading may be another model, turned and sized its own way)
			const float *A = a.slots[pair[near]].mat, *B = b.slots[near].mat;
			auto &pa = m_cam_older[k];
			for (int r = 0; r < 12; r++) pa[r] = pk[r] + (r >= 9 ? A[r] - B[r] : 0);
			m_ship[k] = 1;
		}
		std::vector<float> ended = m_cam_res;
		// (a ship moved with the camera wasn't on its move: the next pair's slot isn't set off from it)
		for (size_t k : ship) ended[k] = -1;
		for (cam_move &c : m_cam_move)
			if (c.from == a.frame && c.to == b.frame) c.res = ended;
		m_cam_carried = true;
		return repaired;
	}
	for (size_t i = 0; i < P.size(); i++) {
		const int k = K[i];
		// (only in a run of copies that changed length: VF1's fighters have parts in threes and
		// sixes, a hand's fingers, which move by themselves and pair right in order)
		if (res[i] <= 0.02 || !changed_run[k]) continue;
		const uint32_t f = first(b.slots[k]);
		int best = -1;
		double bres = std::min(res[i] * 0.25, 0.01);
		for (size_t q = 0; q < a.slots.size(); q++)
			if (first(a.slots[q]) == f && int(q) != pair[k]) {
				const float *pa = a.slots[q].mat;
				const double r = residual({ pa[9], pa[10], pa[11] }, Q[i]);
				if (r < bres) bres = r, best = int(q);
			}
		if (best >= 0) pair[k] = best, repaired++;
		else if (res[i] > 0.06) {
			int same = 0;
			for (const auto &o : b.slots) same += first(o) == f;
			if (same >= 3) pair[k] = -2, repaired++;
		}
	}
	return repaired;
}

// A part that turned far between two lists (a strand swinging over, a fast limb): its axes blended
// straight from one to the other shrink towards nothing (half way between two turned 90 degrees
// apart, 0.71 of their length; 180 apart, none), the strand drawn a pixel wide. Turned past 25
// degrees on any axis, its turn is blended as a turn and its axes' lengths as lengths.
static bool swung(const float *pa, const float *pb)
{
	for (int r = 0; r < 9; r += 3) {
		const float la = pa[r] * pa[r] + pa[r + 1] * pa[r + 1] + pa[r + 2] * pa[r + 2];
		const float lb = pb[r] * pb[r] + pb[r + 1] * pb[r + 1] + pb[r + 2] * pb[r + 2];
		const float dot = pa[r] * pb[r] + pa[r + 1] * pb[r + 1] + pa[r + 2] * pb[r + 2];
		if (la > 0 && lb > 0 && dot < 0.906f * std::sqrt(la * lb)) return true;
	}
	return false;
}
// (a matrix's turn and what's left, M = R P: R the nearest turn (its polar part, by averaging it
// with its inverse's transpose), P = R^T M its stretch, however the model's parts are scaled; the
// turn blended as a turn, the stretch straight)
static glm::mat3 polar_turn(glm::mat3 m)
{
	for (int it = 0; it < 20; it++) {
		const float d = glm::determinant(m);
		if (std::fabs(d) < 1e-12f) break;
		m = 0.5f * (m + glm::transpose(glm::inverse(m)));
	}
	return m;
}
static void turn_blend(const float *pa, const float *pb, double alpha, float *out)
{
	const glm::mat3 ma(pa[0], pa[1], pa[2], pa[3], pa[4], pa[5], pa[6], pa[7], pa[8]);
	const glm::mat3 mb(pb[0], pb[1], pb[2], pb[3], pb[4], pb[5], pb[6], pb[7], pb[8]);
	// (only a solid part: a shadow is its fighter pressed flat onto the floor, its axes far from a
	// turn's; taken for one it turned up off the floor)
	auto solid = [](const glm::mat3 &m) {
		const float v = glm::length(m[0]) * glm::length(m[1]) * glm::length(m[2]);
		return v > 0 && std::fabs(glm::determinant(m)) > 0.5f * v;
	};
	if (!solid(ma) || !solid(mb)) return;
	glm::mat3 ra = polar_turn(ma), rb = polar_turn(mb);
	const float da = glm::determinant(ra), db = glm::determinant(rb);
	// (one mirrored and the other not: no turn goes between them, every blend passes through flat;
	// the nearer one's axes as they are. Both mirrored: their turns with an axis flipped)
	if (da * db <= 0) {
		const float *near = alpha < 0.5 ? pa : pb;
		for (int k = 0; k < 9; k++) out[k] = near[k];
		return;
	}
	if (da < 0) { ra[2] = -ra[2]; rb[2] = -rb[2]; }
	const glm::mat3 sa = glm::transpose(ra) * ma, sb = glm::transpose(rb) * mb;
	const glm::quat qa = glm::normalize(glm::quat_cast(ra)), qb = glm::normalize(glm::quat_cast(rb));
	const glm::mat3 m = glm::mat3_cast(glm::slerp(qa, qb, float(alpha))) * (sa + (sb - sa) * float(alpha));
	for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) out[3 * c + r] = m[c][r];
}

// A rigid part's move from one list to the next taken as one motion, a turn about a line and a
// slide along it, and that motion's fraction (or, past 1, more of it) applied to the older place.
// Lerped, a big model turning by the camera (the Tantive IV's hull, its origin thousands of units
// from the part on screen) swung its near side across the view; carried on with the newer turn
// kept, by hundreds of units off it. False when the part isn't rigid (its stretch changed).
static bool rigid_blend(const float *pa, const float *pb, double alpha, float *out)
{
	const glm::mat3 ma(pa[0], pa[1], pa[2], pa[3], pa[4], pa[5], pa[6], pa[7], pa[8]);
	const glm::mat3 mb(pb[0], pb[1], pb[2], pb[3], pb[4], pb[5], pb[6], pb[7], pb[8]);
	const glm::mat3 ra = polar_turn(ma), rb = polar_turn(mb);
	const float da = glm::determinant(ra), db = glm::determinant(rb);
	if (std::fabs(std::fabs(da) - 1) > 0.01f || std::fabs(std::fabs(db) - 1) > 0.01f || da * db <= 0) return false;
	const glm::mat3 sa = glm::transpose(ra) * ma, sb = glm::transpose(rb) * mb;
	for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++)
		if (std::fabs(sa[c][r] - sb[c][r]) > 0.01f * (1 + std::fabs(sa[c][r]))) return false;
	const glm::dmat3 rd = glm::dmat3(rb) * glm::transpose(glm::dmat3(ra));
	const glm::dvec3 ta(pa[9], pa[10], pa[11]), tb(pb[9], pb[10], pb[11]);
	const glm::dvec3 td = tb - rd * ta;
	glm::dquat q = glm::normalize(glm::quat_cast(rd));
	if (q.w < 0) q = -q;
	const double half = std::acos(std::clamp(q.w, -1.0, 1.0));
	glm::dmat3 rs(1.0);
	glm::dvec3 t;
	if (half < 1e-5) t = ta + alpha * td;
	else {
		const glm::dvec3 u = glm::normalize(glm::dvec3(q.x, q.y, q.z));
		const glm::dvec3 par = glm::dot(u, td) * u, perp = td - par;
		const glm::dvec3 c = 0.5 * (perp + glm::cross(u, perp) / std::tan(half));   // a point on the turn's line
		rs = glm::mat3_cast(glm::angleAxis(2 * half * alpha, u));
		t = rs * (ta - c) + c + alpha * par;
	}
	const glm::dmat3 m = rs * glm::dmat3(ma);
	for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) out[3 * c + r] = float(m[c][r]);
	for (int r = 0; r < 3; r++) out[9 + r] = float(t[r]);
	return true;
}

// Keyed extras (renderer-only copies, their transforms relative to their slot's matrix) of each
// blended slot, paired with the nearest of the same key in the older list's slot. SWA's Full
// copies hang on the stars' matrix, the camera's turn alone: blending only that, the copies moved
// with the camera's turn but not its flight, a step each game frame. A copy that moved over 60% of
// its distance (the game moving its object a period on, a different copy) isn't paired.
void model1::pair_extras(const logic_frame &a, const logic_frame &b, const std::vector<int> &pair)
{
	std::unordered_multimap<uint64_t, int> older;
	std::unordered_multimap<uint32_t, int> same_model;
	for (size_t i = 0; i < b.slots.size(); i++) {
		if (!m_blend_ok[i] || pair[i] < 0) continue;
		const auto &to = b.slots[i].extra, &from = a.slots[pair[i]].extra;
		if (to.empty() || from.empty()) continue;
		older.clear();
		same_model.clear();
		for (size_t k = 0; k < from.size(); k++)
			if (from[k].key && from[k].transform) older.emplace(from[k].key, int(k)), same_model.emplace(from[k].polygon, int(k));
		if (older.empty()) continue;
		auto &pairs = m_extra_pair[i];
		pairs.assign(to.size(), -1);
		for (size_t k = 0; k < to.size(); k++) {
			if (!to[k].key || !to[k].transform) continue;
			const auto &tb = *to[k].transform;
			const glm::vec3 vb(tb[9], tb[10], tb[11]);
			float best = INFINITY;
			const auto [first, last] = older.equal_range(to[k].key);
			for (auto it = first; it != last; ++it) {
				const auto &ta = *from[it->second].transform;
				const glm::vec3 va(ta[9], ta[10], ta[11]);
				const float d = glm::length(vb - va);
				if (d < best && d <= 0.6f * std::max({ glm::length(va), glm::length(vb), 1.0f })) best = d, pairs[k] = it->second;
			}
			// (A copy of the same model far nearer than its key's: the game gave the copies other
			// sources (the attract's destroyer manager reordered), and the key's pair, a period away,
			// swept a star cruiser across the screen. The near one is the same copy, or so close
			// that blending to it moves nothing.)
			if (pairs[k] >= 0) {
				const auto [mf, ml] = same_model.equal_range(to[k].polygon);
				for (auto it = mf; it != ml; ++it) {
					const auto &ta = *from[it->second].transform;
					const float d = glm::length(vb - glm::vec3(ta[9], ta[10], ta[11]));
					if (d * 4 < best) best = d * 4, pairs[k] = it->second;
				}
			}
		}
		m_extra_from[i] = &from;
	}
}

// The extra `n` of the slot being drawn: its transform `local` (the newer list's), blended with
// its pair's in the older list as its slot's matrix is.
void model1::smooth_extra(size_t n, const std::array<float, 12> &local, float *out) const
{
	std::copy(local.begin(), local.end(), out);
	if (!m_replaying || (m_replay_older && !m_replay_extra) || m_replay_slot < 0 || m_replay_slot >= int(m_blend_ok.size()) ||
		!m_blend_ok[m_replay_slot] || size_t(m_replay_slot) >= m_extra_pair.size() || !m_extra_from[m_replay_slot]) return;
	const auto &pairs = m_extra_pair[m_replay_slot];
	if (n >= pairs.size() || pairs[n] < 0) return;
	const auto &from = *(*m_extra_from[m_replay_slot])[pairs[n]].transform;
	for (int k = 0; k < 12; k++) out[k] = float(from[k] + (local[k] - from[k]) * m_extra_alpha);
	if (swung(from.data(), local.data())) turn_blend(from.data(), local.data(), m_extra_alpha, out);
}

void model1::blend_slots(const logic_frame &a, const logic_frame &b, double alpha)
{
	m_blend.assign(b.slots.size(), {});
	m_blend_ok.assign(b.slots.size(), 0);
	m_blend_from.assign(b.slots.size(), {});
	m_blend_to.assign(b.slots.size(), {});
	m_blend_light.assign(b.slots.size(), {});
	m_extra_pair.assign(b.slots.size(), {});
	m_extra_from.assign(b.slots.size(), nullptr);
	m_extra_alpha = alpha;
	m_blend_cut = false;
	if (alpha >= 1 && !m_replay_older) return;
	if (a.slots.empty()) return;
	if (m_video->records && a.has_camera && b.has_camera) { blend_by_records(a, b, alpha); return; }
	// pair the slots in order, like a diff: the same first model pairs; a different model in the
	// same place (a hand, a face swapped) pairs too; slots that appeared or went are skipped
	std::vector<int> pair(b.slots.size(), -1);
	auto first = [](const slot &sl) { return sl.polys.empty() ? 0u : sl.polys[0]; };
	size_t i = 0, j = 0;
	const size_t na = a.slots.size(), nb = b.slots.size();
	auto dist = [&](size_t ia, size_t jb) {
		const float *pa = a.slots[ia].mat, *pb = b.slots[jb].mat;
		return double(pb[9] - pa[9]) * (pb[9] - pa[9]) + double(pb[10] - pa[10]) * (pb[10] - pa[10]) + double(pb[11] - pa[11]) * (pb[11] - pa[11]);
	};
	int repaired = 0;
	std::vector<char> changed_run(nb, 0);       // slots of a run that changed length
	while (i < na && j < nb) {
		if (first(a.slots[i]) == first(b.slots[j])) {
			// A run of slots of one model (copies of a piece of scenery): when one of them came or
			// went, pairing in order pairs a copy with the next one along, and the pictures between
			// the frames draw it halfway to that one's place. The shorter run is paired where it
			// lies nearest in the longer one; runs of the same length pair in order. (VF2's fix
			// for its stages' bushes; as there, the rest pair by the diff.)
			const uint32_t f = first(a.slots[i]);
			size_t ra = 1, rb = 1;
			if (f) {
				while (i + ra < na && first(a.slots[i + ra]) == f) ra++;
				while (j + rb < nb && first(b.slots[j + rb]) == f) rb++;
			}
			// (SWA's copies are ships, listed in no steady order: a fighter flying past the camera
			// was paired with one behind it, and every copy's swap taken for a cut. They pair the way
			// that moves them least in all (nearest first swapped two fighters crossing at speed);
			// the rest are new or gone.)
			if (m_video->separate_parts && f && (ra > 1 || rb > 1)) {
				const size_t n = std::min(ra, rb), m = std::max(ra, rb);
				// (copies at one place, as hyperspace's tunnel pieces in a ring, tie on the move; each
				// pairs with the one turned least from it, not its neighbour around the ring)
				auto cost = [&](size_t ks, size_t kl) {
					const size_t ia = ra <= rb ? i + ks : i + kl, jb = ra <= rb ? j + kl : j + ks;
					double turn = 0;
					for (int r = 0; r < 9; r++) turn += std::fabs(double(b.slots[jb].mat[r]) - a.slots[ia].mat[r]);
					return std::sqrt(dist(ia, jb)) + 1e-3 * turn;
				};
				std::vector<int> best(n, -1), cur(n, -1);
				std::vector<char> used(m, 0);
				if (m <= 8) {
					double best_c = -1;
					std::function<void(size_t, double)> search = [&](size_t k, double c) {
						if (best_c >= 0 && c >= best_c) return;
						if (k == n) { best_c = c; best = cur; return; }
						for (size_t l = 0; l < m; l++)
							if (!used[l]) { used[l] = 1; cur[k] = int(l); search(k + 1, c + cost(k, l)); used[l] = 0; }
					};
					search(0, 0);
				} else {
					struct cand { double d; size_t ks, kl; };
					std::vector<cand> c;
					for (size_t ks = 0; ks < n; ks++) for (size_t kl = 0; kl < m; kl++) c.push_back({ cost(ks, kl), ks, kl });
					std::sort(c.begin(), c.end(), [](const cand &x, const cand &y) { return x.d < y.d; });
					for (const cand &x : c)
						if (best[x.ks] < 0 && !used[x.kl]) { used[x.kl] = 1; best[x.ks] = int(x.kl); }
				}
				for (size_t k = 0; k < n; k++) {
					if (ra <= rb) pair[j + best[k]] = int(i + k);
					else pair[j + k] = int(i + best[k]);
				}
				i += ra; j += rb;
				continue;
			}
			if (ra == rb) { for (size_t k = 0; k < ra; k++) pair[j + k] = int(i + k); i += ra; j += rb; continue; }
			for (size_t k = 0; k < rb; k++) changed_run[j + k] = 1;
			const size_t n = std::min(ra, rb), slack = std::max(ra, rb) - n;
			size_t best = 0;
			double best_d = -1;
			for (size_t o = 0; o <= slack && o <= 64; o++) {
				double d = 0;
				for (size_t k = 0; k < n; k++) d += ra > rb ? dist(i + o + k, j + k) : dist(i + k, j + o + k);
				if (best_d < 0 || d < best_d) best_d = d, best = o;
			}
			for (size_t k = 0; k < n; k++) {
				if (ra > rb) pair[j + k] = int(i + best + k);
				else pair[j + best + k] = int(i + k);
			}
			if (best) repaired += int(n);
			i += ra; j += rb;
			continue;
		}
		size_t ka = 1, kb = 1;
		while (i + ka < na && ka <= 8 && first(a.slots[i + ka]) != first(b.slots[j])) ka++;
		while (j + kb < nb && kb <= 8 && first(b.slots[j + kb]) != first(a.slots[i])) kb++;
		const bool a_skip = i + ka < na && ka <= 8, b_skip = j + kb < nb && kb <= 8;
		if (a_skip && (!b_skip || ka <= kb)) i += ka;       // slots in the older list that went
		else if (b_skip) j += kb;                            // slots that are new
		else pair[j++] = int(i++);                           // the same place, a different model
	}
	m_camera_cut = false;
	m_camera_sure = false;
	m_cam_carried = false;
	m_cam_res.assign(nb, -1);
	m_ship.assign(nb, 0);
	repaired += repair_by_camera(a, b, pair, changed_run);
	// A slot carrying keyed copies (SWA's Full fleet and asteroids on the stars' matrix): paired
	// with the older list's carrier of its model. Repaired by the camera's move (its matrix no
	// scenery), its copies went unpaired, drawn where the newer list had them: a step each list.
	const auto carries = [](const slot &s) {
		for (const auto &e : s.extra) if (e.key && e.transform) return true;
		return false;
	};
	for (size_t i = 0; i < b.slots.size(); i++) {
		if ((pair[i] >= 0 && !a.slots[pair[i]].extra.empty()) || b.slots[i].polys.empty() || !carries(b.slots[i])) continue;
		int best = -1;
		for (size_t k = 0; k < a.slots.size(); k++)
			if (!a.slots[k].polys.empty() && first(a.slots[k]) == first(b.slots[i]) && carries(a.slots[k]) &&
				(best < 0 || a.slots[k].extra.size() > a.slots[best].extra.size())) best = int(k);
		if (best >= 0) pair[i] = best, m_cam_res[i] = -1;
	}
	static const bool logp = getenv("M1_LOG_SMOOTH") != nullptr;
	if (logp && repaired && !m_redrawing) fprintf(stderr, "P frame %d repaired %d\n", b.frame, repaired);
	int jumps = 0, paired = 0, missing = 0;
	std::vector<char> jumped(b.slots.size(), 0);
	for (size_t i = 0; i < b.slots.size(); i++) {
		if (pair[i] == -2) continue;                       // (scenery that came into the list: at the newer place)
		if (pair[i] == -1 && !b.slots[i].polys.empty()) missing++;
		if (pair[i] == -1 || b.slots[i].polys.empty()) continue;
		// (scenery the camera's move carries: SWA's surface cells beside the camera, flying low,
		// move several times their depth in a list and blend right as the camera's move does. Each
		// from the older place the fitted move gives: from their own, a hair off it, each cell
		// turned on its own line, cracks opening between them in the pictures between.)
		const bool follows = m_cam_carried && m_cam_res[i] >= 0 && m_cam_res[i] <= 0.02f;
		const float *pa = follows || pair[i] == -3 ? m_cam_older[i].data() : a.slots[pair[i]].mat, *pb = b.slots[i].mat;
		const float dx = pb[9] - pa[9], dy = pb[10] - pa[10], dz = pb[11] - pa[11];
		const float depth = m_video->separate_parts ? eye_distance(pa, pb) : std::max({ std::fabs(pa[11]), std::fabs(pb[11]), 1.0f });
		paired++;
		// a part that moved far, or turned sharply (a cut can keep the parts' places and swing the
		// camera round them: blending two far-apart turns squashes and stretches the shapes)
		bool turned = false;
		float axes_a = 0, axes_b = 0;
		if (m_video->separate_parts)
			for (int r = 0; r < 9; r++) axes_a += pa[r] * pa[r], axes_b += pb[r] * pb[r];
		for (int r = 0; r < 9 && !turned; r += 3) {
			const float la = pa[r] * pa[r] + pa[r + 1] * pa[r + 1] + pa[r + 2] * pa[r + 2];
			const float lb = pb[r] * pb[r] + pb[r + 1] * pb[r + 1] + pb[r + 2] * pb[r + 2];
			const float dot = pa[r] * pb[r] + pa[r + 1] * pb[r + 1] + pa[r + 2] * pb[r + 2];
			// A folding part's axis scaling through zero has no direction to compare.
			if (m_video->separate_parts && (la < 1e-6f * axes_a || lb < 1e-6f * axes_b)) continue;
			if (la > 0 && lb > 0 && dot < 0.77f * std::sqrt(la * lb)) turned = true;     // over ~40 degrees
		}
		// (SWA's shots and passing fighters move 30-50% of their depth in a list as they near the
		// camera, a TIE overtaking it close 70%: a jump there is 80%, or a turn. Blended, a shot
		// passing the camera came through the lens, a fighter's wrong pair swept across the screen:
		// drawn where the newer list has it. At 60% the overtaking TIE stood still a list, then leapt.)
		// (A carrier's own place isn't where its copies are: the stars' matrix passing the camera's
		// plane, its depth near 0, moved over 36% of it each list and the whole fleet stood still,
		// then leapt. Each copy is paired within 60% of its own distance.)
		const float far = m_video->separate_parts ? (i < m_ship.size() && m_ship[i] ? 0.64f : 0.36f) : 0.09f;
		const bool moved = !carries(b.slots[i]) && dx * dx + dy * dy + dz * dz > far * depth * depth;
		if ((moved || turned) && !follows) {    // fast, or a cut
			jumps++, jumped[i] = 1;
			if (m_video->separate_parts) continue;
		}
		for (int k = 0; k < 12; k++) m_blend[i][k] = float(pa[k] + (pb[k] - pa[k]) * alpha);
		static const bool no_turn = getenv("M1_NO_TURN_BLEND") != nullptr;   // TESTONLY
		if (!(m_video->separate_parts && rigid_blend(pa, pb, alpha, m_blend[i].data())) && !no_turn && swung(pa, pb))
			turn_blend(pa, pb, alpha, m_blend[i].data());
		memcpy(m_blend_from[i].data(), pa, 12 * sizeof(float));
		memcpy(m_blend_to[i].data(), pb, 12 * sizeof(float));
		if (pair[i] >= 0) {
			const slot &sa = a.slots[pair[i]];
			m_blend_light[i] = { sa.light[0], sa.light[1], sa.light[2], sa.has_light ? 1.0f : 0.0f };
		}
		m_blend_ok[i] = 1;
	}
	// A camera cut moves nearly every part at once (fast moves only a few): then, or when many
	// parts can't be paired (another scene), the newer frame is shown whole. A few new parts (a
	// hat knocked off, Jeff's cap and shades) are drawn where the newer frame has them, the rest
	// blended: as a cut, the picture carried on past the newer list, and the fighters' fast-turning
	// limbs came out stretched (a kick that knocked Pai's hat off), or, made rigid, apart.
	// (When the camera's move is known and is no cut, a part jumped only if it moved far off the
	// camera's move: close in (depth under 1, the camera a fighter's width away) its swing past
	// moved most parts over 30%, and fighters spinning in a throw turned most of theirs over 40
	// degrees, each taken for a cut: the picture stood 2 game frames and lurched.)
	int cut_jumps = jumps;
	if (m_camera_sure) {
		cut_jumps = 0;
		for (size_t i = 0; i < b.slots.size(); i++) cut_jumps += m_blend_ok[i] && m_cam_res[i] > 0.3f;
		// Moving ships can outnumber the globe and disagree with its camera. Stationary geometry
		// following the known move cannot vote for a cut just because the ships do.
		int stationary = 0, following = 0;
		for (size_t i = 0; i < b.slots.size(); i++)
			if (std::find(m_video->camera_anchors.begin(), m_video->camera_anchors.end(), first(b.slots[i])) != m_video->camera_anchors.end()) {
				stationary++;
				following += m_cam_res[i] >= 0 && m_cam_res[i] <= 0.02f;
			}
		if (stationary && following == stationary) cut_jumps = 0;
	}
	// (SWA's scenes have 3-9 parts: one shot fired was a cut, and two fighters and a shot passing
	// the camera, 3 of 5 parts. Its new and fast parts are drawn where they are; a cut moves nearly all.)
	m_blend_cut = (m_video->separate_parts ? cut_jumps * 4 > paired * 3 : cut_jumps * 2 > paired)
		|| (!m_video->separate_parts && missing * 8 > paired) || m_camera_cut;
	// Carried on past the newer list (blend_slots(older, newer, 1 to 1.5) at a cut, half a game frame
	// on at most): each part's own blend carried on grew the fast-turning ones (a kicking leg up to
	// twice its length) and moved each by itself, apart from the next (made rigid, a hand came off
	// its wrist). Every part is drawn as the newer list has it, all moved on together by the parts'
	// average move between the two lists: the whole picture carries on, nothing comes apart. The
	// parts that moved fast (a camera swinging into a cut, TIME OVER's; a body mid-throw, a kicking
	// leg) are left out of the average, carried with the rest: their own move carried on went into
	// views and poses the game never showed. (The newer list was held as it was until the cut when
	// any part moved fast: one kicking leg, and the picture stood a list before most cuts.)
	// (SWA's parts are ships and shots, each moving by itself: the average of a scene's few parts,
	// two shots flying past at ten times a ship's speed, carried the Tantive IV's hull at their pace
	// into the camera. Each carries on by its own motion, blended so above.)
	if (alpha > 1 && !m_blend_cut && !m_video->separate_parts) {
		double mv[3] = {};
		int n = 0;
		for (size_t i = 0; i < b.slots.size(); i++)
			if (m_blend_ok[i] && !jumped[i]) { for (int k = 0; k < 3; k++) mv[k] += m_blend_to[i][9 + k] - m_blend_from[i][9 + k]; n++; }
		for (size_t i = 0; i < b.slots.size(); i++)
			if (m_blend_ok[i]) {
				memcpy(m_blend[i].data(), m_blend_to[i].data(), 12 * sizeof(float));
				for (int k = 0; k < 3; k++) m_blend[i][9 + k] += float((alpha - 1) * mv[k] / std::max(n, 1));
			}
	}
	if (m_blend_cut) std::fill(m_blend_ok.begin(), m_blend_ok.end(), 0);
	else pair_extras(a, b, pair);
	m_smooth_stats[1] = jumps;
	static const bool logm = getenv("M1_LOG_SMOOTH") != nullptr;
	if (logm && (jumps || missing)) fprintf(stderr, "M jumps %d missing %d paired %d slots %zu/%zu\n", jumps, missing, paired, na, nb);
	for (size_t i = 0; i < b.slots.size(); i++)
		if (!b.slots[i].polys.empty()) m_smooth_stats[m_blend_ok[i] ? 2 : 0]++;
}

// (records) The camera's turn smoothed, as each list comes. The game turns its camera unevenly
// (in a bend, 0.5 degrees a game frame and then 0.06, never made up), so the road's lines jerked
// sideways. An alpha-beta filter: the turn expected at the last list's speed, a part of the miss
// taken in (0.4) and a part into the speed (0.1); a steady turn comes through as it is. Never
// over a degree from the camera's own (a car the camera follows stays near its place; the excess
// half into the speed): what is left is evened out, the camera's sweeps are not. Afresh at a cut,
// and where the camera swings back over a degree.
void model1::smooth_turn(logic_frame &lf, const logic_frame &before)
{
	lf.turn_age = 0;
	lf.turn_hold = 0;
	lf.turn_step = {};
	if (!lf.has_camera) return;
	const glm::mat4 cam = camera_matrix(lf.camera.data());
	const glm::quat q = glm::quat_cast(glm::mat3(cam));
	glm::quat est = q;
	glm::vec3 rate(0);
	lf.turn_age = 1;
	if (before.has_camera && before.turn_age > 0 && !camera_cut(camera_matrix(before.camera.data()), cam)) {
		lf.turn_age = 2;
		const glm::vec3 step = turn_vector(q * glm::inverse(glm::quat_cast(glm::mat3(camera_matrix(before.camera.data())))));
		lf.turn_step = { step.x, step.y, step.z };
		if (before.turn_age == 1)                    // the first move after a cut: its speed as it is
			rate = step;
		else {
			constexpr float ALPHA = 0.4f, BETA = 0.1f, LIMIT = glm::radians(1.0f);
			constexpr int TURN_HOLD = 8;
			const glm::vec3 last_rate(before.turn_rate[0], before.turn_rate[1], before.turn_rate[2]);
			const glm::vec3 last_step(before.turn_step[0], before.turn_step[1], before.turn_step[2]);
			// The camera swinging back over a degree from a turn of its own (swaying about a car it keeps
			// in place, after the bridge in the attract drive): its own turn, for a while after the last
			// swing. Evened out, the car swung on screen, also as the swaying slowed.
			const bool swing = glm::length(step) > LIMIT && glm::length(last_step) > LIMIT / 4 && glm::dot(step, last_step) < 0;
			lf.turn_hold = swing ? TURN_HOLD : std::max(before.turn_hold - 1, 0);
			if (lf.turn_hold > 0) {
				lf.turn = { q.w, q.x, q.y, q.z };
				lf.turn_rate = { step.x, step.y, step.z };
				return;
			}
			// Out of a swing, from the camera's speed now: the swing's (as it slowed) ran the turn on
			// ahead of the game's, and every car on screen with it.
			const glm::vec3 from_rate = before.turn_hold > 0 ? step : last_rate;
			const glm::quat expected = turn_quat(from_rate) * glm::quat(before.turn[0], before.turn[1], before.turn[2], before.turn[3]);
			const glm::vec3 miss = turn_vector(q * glm::inverse(expected));
			est = turn_quat(miss * ALPHA) * expected;
			rate = from_rate + miss * BETA;
			const glm::vec3 off = turn_vector(q * glm::inverse(est));
			const float apart = glm::length(off);
			if (apart > LIMIT) {
				const glm::vec3 excess = off * (1 - LIMIT / apart);
				est = turn_quat(excess) * est;
				rate += excess * 0.5f;
			}
		}
	}
	est = glm::normalize(est);
	lf.turn = { est.w, est.x, est.y, est.z };
	lf.turn_rate = { rate.x, rate.y, rate.z };
}

// (records) The cars the camera moves with. Turned with the world, a car the game's camera keeps
// still (it follows the lead car, swaying a little; every car then sways with it) wobbles on
// screen instead. Each car's way from the eye, as drawn and as turned, is measured for how
// unevenly it moves; one moving more evenly as drawn is eased (a quarter a list) to as drawn.
void model1::follow_records(logic_frame &lf, const logic_frame &before)
{
	std::vector<logic_frame::record_way> next;
	if (lf.turn_age == 2) {
		const glm::mat3 raw(camera_matrix(lf.camera.data()));
		const glm::mat3 steer = glm::mat3_cast(glm::quat(lf.turn[0], lf.turn[1], lf.turn[2], lf.turn[3])) * glm::transpose(raw);
		constexpr float KEEP = 0.75f;
		for (const slot &s : lf.slots) {
			if (s.owner <= 1 || s.polys.empty() || s.mat[11] <= 0) continue;
			if (std::any_of(next.begin(), next.end(), [&](const logic_frame::record_way &r) { return r.owner == s.owner; })) continue;
			logic_frame::record_way r;
			for (const auto &o : before.follows)
				if (o.owner == s.owner) r = o;
			r.owner = s.owner;
			const glm::vec3 at = glm::normalize(glm::vec3(s.mat[9], s.mat[10], s.mat[11]));
			const glm::vec3 turned = lf.turn_hold > 0 ? at : steer * at;
			if (r.seen >= 2) {
				r.raw_err = r.raw_err * KEEP + glm::length(at - 2.0f * r.raw[0] + r.raw[1]) * (1 - KEEP);
				r.steered_err = r.steered_err * KEEP + glm::length(turned - 2.0f * r.steered[0] + r.steered[1]) * (1 - KEEP);
				r.weight = std::clamp(r.weight + (r.raw_err <= r.steered_err ? 0.25f : -0.25f), 0.0f, 1.0f);
			}
			r.raw[1] = r.raw[0];
			r.raw[0] = at;
			r.steered[1] = r.steered[0];
			r.steered[0] = turned;
			r.seen = std::min(r.seen + 1, 2);
			next.push_back(r);
		}
	}
	lf.follows = std::move(next);
}

// A game whose draw records are known (video_traits::records: Virtua Racing). Each matrix in its
// lists came from a record (a car: its body, livery and wheels), from the still world, or from
// neither (the logo), and the camera it was drawn with is kept. So nothing is guessed: each
// record's parts are paired in the order the record drew them (each model's in order when it drew
// a different number); the still world is where the older camera saw it; a part of a record not
// paired (a wheel that changed model) moves with its record's first paired part; parts of no
// record are paired by model, and so is a still-world part that sat in its place on screen while
// the camera moved (the SEGA logo, drawn without the camera). A screen-space overlay
// (video_traits::foreground_objects: the logo) is paired by model whichever record drew it, never
// moves with a record and isn't turned. Each is blended on screen, from the
// older matrix to the newer (a car the camera follows stays put; blending the camera and the car
// apart swung it about), a part along a curve that leaves the older list at the last move's speed
// (the game's steps are uneven). Then all of it is turned about the eye to the camera's smoothed
// turn (smooth_turn). A camera_cut isn't blended.
void model1::blend_by_records(const logic_frame &a, const logic_frame &b, double alpha)
{
	auto to_mat = camera_matrix;
	auto from_mat = [](const glm::mat4 &m, float *out) {
		for (int c = 0; c < 3; c++) for (int e = 0; e < 3; e++) out[c * 3 + e] = m[c][e];
		for (int e = 0; e < 3; e++) out[9 + e] = m[3][e];
	};
	auto camera = [&](const logic_frame &l) { return to_mat(l.camera.data()); };
	const glm::mat4 ca = camera(a), cb = camera(b), ib = glm::affineInverse(cb);
	const glm::mat3 turn = glm::mat3(cb) * glm::transpose(glm::mat3(ca));
	const float angle = std::acos(std::clamp((turn[0][0] + turn[1][1] + turn[2][2] - 1) / 2, -1.0f, 1.0f));
	const float jump = glm::length(glm::vec3(ib[3]) - glm::vec3(glm::affineInverse(ca)[3]));
	m_blend_cut = camera_cut(ca, cb);
	auto first = [](const slot &sl) { return sl.polys.empty() ? 0u : sl.polys[0]; };
	const std::vector<uint32_t> &overlays = m_video->foreground_objects;
	auto overlay = [&](const slot &sl) { return !sl.polys.empty() && std::find(overlays.begin(), overlays.end(), sl.polys[0]) != overlays.end(); };
	using owners_t = std::unordered_map<uint32_t, std::vector<size_t>>;
	auto by_owner = [](const logic_frame &l) {
		owners_t o;
		for (size_t i = 0; i < l.slots.size(); i++) if (!l.slots[i].polys.empty()) o[l.slots[i].owner].push_back(i);
		return o;
	};
	// each part of y: x's part it is (-1: none), and where x had it on screen
	auto older_views = [&](const logic_frame &x, const logic_frame &y, std::vector<int> &match, std::vector<glm::mat4> &view, std::vector<char> &known) {
		const size_t ny = y.slots.size();
		match.assign(ny, -1);
		view.assign(ny, glm::mat4(1));
		known.assign(ny, 0);
		const owners_t ox = by_owner(x), oy = by_owner(y);
		for (const auto &[owner, ys] : oy) {
			auto it = ox.find(owner);
			if (it == ox.end()) continue;
			const std::vector<size_t> &xs = it->second;
			if (owner >= 2 && xs.size() == ys.size()) {
				for (size_t k = 0; k < ys.size(); k++) match[ys[k]] = int(xs[k]);
				continue;
			}
			std::unordered_map<uint32_t, std::vector<size_t>> models;
			for (size_t q : xs) models[first(x.slots[q])].push_back(q);
			std::unordered_map<uint32_t, size_t> nth;
			for (size_t i : ys) {
				const uint32_t f = first(y.slots[i]);
				auto m = models.find(f);
				const size_t k = nth[f]++;
				if (m != models.end() && k < m->second.size()) match[i] = int(m->second[k]);
			}
		}
		// a screen-space overlay (the SEGA logo): its own copy in the older list, whichever record
		// drew it (the game hands it from one to another)
		std::unordered_map<uint32_t, std::vector<size_t>> shown;
		for (size_t q = 0; q < x.slots.size(); q++) if (overlay(x.slots[q])) shown[first(x.slots[q])].push_back(q);
		std::unordered_map<uint32_t, size_t> nth_shown;
		for (size_t i = 0; i < ny; i++) {
			if (!overlay(y.slots[i])) continue;
			const uint32_t f = first(y.slots[i]);
			auto m = shown.find(f);
			const size_t k = nth_shown[f]++;
			match[i] = m != shown.end() && k < m->second.size() ? int(m->second[k]) : -1;
		}
		const glm::mat4 cx = camera(x), iy = glm::affineInverse(camera(y)), ix = glm::affineInverse(cx);
		std::unordered_map<uint32_t, int> uses;      // each model's parts in both lists
		for (const slot &sl : x.slots) if (!sl.polys.empty()) uses[sl.polys[0]]++;
		for (const slot &sl : y.slots) if (!sl.polys.empty()) uses[sl.polys[0]]++;
		for (size_t i = 0; i < ny; i++) {           // (one a record drew in the older list)
			if (y.slots[i].owner != 1 || match[i] >= 0 || y.slots[i].polys.empty() || uses[y.slots[i].polys[0]] != 2) continue;
			for (size_t q = 0; q < x.slots.size(); q++)
				if (!x.slots[q].polys.empty() && x.slots[q].polys[0] == y.slots[i].polys[0]) match[i] = int(q);
		}
		for (size_t i = 0; i < ny; i++) {
			const slot &sy = y.slots[i];
			if (sy.polys.empty()) continue;
			if (overlay(sy)) {
				if (match[i] >= 0) view[i] = to_mat(x.slots[match[i]].mat), known[i] = 1;
				continue;
			}
			if (sy.owner == 1) {
				// the still world: where the older camera saw it. Unless its model's copy in the older
				// list sat in its place on screen and the camera's move doesn't put it there: a part
				// drawn without the camera (the SEGA logo, which the game draws as it draws the track).
				// A model drawn once in each list that came nearer from its older copy than the
				// camera's move brings it is one too, moving on screen (the GOAL flying in)
				view[i] = cx * iy * to_mat(sy.mat);
				known[i] = 1;
				if (match[i] >= 0) {
					const glm::vec3 at(sy.mat[9], sy.mat[10], sy.mat[11]);
					const float close = 0.01f * std::max(std::fabs(at.z), 0.1f);
					const glm::mat4 own = to_mat(x.slots[match[i]].mat);
					const float by_own = glm::length(glm::vec3(own[3]) - at), by_camera = glm::length(glm::vec3(view[i][3]) - at);
					if (by_camera > close && (by_own <= close || (uses[sy.polys[0]] == 2 && by_own < 0.25f * by_camera))) view[i] = own;
					else match[i] = -1;
				}
				continue;
			}
			if (match[i] < 0) continue;
			view[i] = to_mat(x.slots[match[i]].mat);
			known[i] = 1;
			// a record's part that went far in the world: the record used for another thing
			if (sy.owner >= 2 && glm::length(glm::vec3((ix * view[i])[3]) - glm::vec3((iy * to_mat(sy.mat))[3])) > 20) match[i] = -1, known[i] = 0;
		}
		for (const auto &[owner, ys] : oy) {
			if (owner < 2) continue;
			int lead = -1;
			for (size_t i : ys) if (match[i] >= 0 && !overlay(y.slots[i])) { lead = int(i); break; }
			if (lead < 0) continue;
			// the record's move in the world, taken back from the newer part's place
			const glm::mat4 back = ix * view[lead] * glm::affineInverse(iy * to_mat(y.slots[lead].mat));
			for (size_t i : ys) if (match[i] < 0 && !overlay(y.slots[i])) { view[i] = cx * back * iy * to_mat(y.slots[i].mat); known[i] = 1; }
		}
	};
	std::vector<int> match, match_prev;
	std::vector<glm::mat4> from, prev;
	std::vector<char> known, known_prev;
	older_views(a, b, match, from, known);
	// the list before the older one, a game frame before it: the moves into the older list
	const logic_frame &p = m_logic_prev;
	const float u = float(alpha);
	const bool has_prev = u <= 1 && &a == &m_logic[0] && p.has_camera && !p.list.empty()
		&& p.frame < a.frame && a.frame - p.frame <= 2 * (b.frame - a.frame);
	if (has_prev) older_views(p, a, match_prev, prev, known_prev);
	const float gap = float(b.frame - a.frame) / float(std::max(a.frame - p.frame, 1));
	const size_t nb = b.slots.size();
	for (size_t i = 0; i < nb; i++) {
		const slot &sb = b.slots[i];
		if (sb.polys.empty() || !known[i]) continue;
		float older_m[12];
		from_mat(from[i], older_m);
		for (int k = 0; k < 12; k++) m_blend[i][k] = older_m[k] + (sb.mat[k] - older_m[k]) * u;
		// leaving the older list at its last move's speed, when the two moves are near alike
		bool have = false;
		glm::vec3 was(0);
		if (has_prev && match[i] >= 0 && known_prev[match[i]]) {
			was = glm::vec3(from[i][3]) - glm::vec3(prev[match[i]][3]);
			have = true;
		}
		if (have) {
			const glm::vec3 now(sb.mat[9] - older_m[9], sb.mat[10] - older_m[10], sb.mat[11] - older_m[11]);
			was *= gap;
			if (glm::length(was - now) <= 0.5f * std::max(glm::length(was), glm::length(now)) + 0.02f)
				for (int k = 0; k < 3; k++) m_blend[i][9 + k] += u * (1 - u) * (1 - u) * (was[k] - now[k]);
		}
		const slot &light_from = match[i] >= 0 ? a.slots[match[i]] : sb;
		memcpy(m_blend_from[i].data(), older_m, sizeof older_m);
		memcpy(m_blend_to[i].data(), sb.mat, sizeof older_m);
		m_blend_light[i] = { light_from.light[0], light_from.light[1], light_from.light[2], light_from.has_light ? 1.0f : 0.0f };
		m_blend_ok[i] = 1;
	}
	// The camera's turn smoothed (smooth_turn): everything drawn with the camera turned about the
	// eye from the blend's turn to the smoothed one, but for the cars the camera moves with
	// (follow_records, eased), going evenly from one list's smoothed turn to the next. Past the newer list
	// (before a cut), as at it. (A curve through them at the filter's turn speeds, which aren't the
	// way between them, sped up and slowed within each list: the picture swayed 15 times a second,
	// now ahead, now back.) Between two lists that both follow the camera's own turn (smooth_turn's
	// swing back), nothing is turned: they are the game's.
	if (!m_blend_cut && a.turn_age > 0 && b.turn_age == 2 && !(a.turn_hold > 0 && b.turn_hold > 0)) {
		const float t = std::min(u, 1.0f);
		const glm::quat qa(a.turn[0], a.turn[1], a.turn[2], a.turn[3]), qb(b.turn[0], b.turn[1], b.turn[2], b.turn[3]);
		const glm::mat3 smooth = glm::mat3_cast(turn_quat(turn_vector(qb * glm::inverse(qa)) * t) * qa);
		const glm::mat3 ra(ca), rb(cb);
		const glm::mat4 steer(smooth * glm::inverse(ra + (rb - ra) * t));
		for (size_t i = 0; i < nb; i++) {
			const slot &sb = b.slots[i];
			if (sb.polys.empty() || overlay(sb) || (sb.owner == 1 && match[i] >= 0)) continue;     // (drawn without the camera)
			if (!m_blend_ok[i]) {                    // not blended: as it is, but turned too
				memcpy(m_blend[i].data(), sb.mat, sizeof sb.mat);
				memcpy(m_blend_from[i].data(), sb.mat, sizeof sb.mat);
				memcpy(m_blend_to[i].data(), sb.mat, sizeof sb.mat);
				m_blend_light[i] = { sb.light[0], sb.light[1], sb.light[2], sb.has_light ? 1.0f : 0.0f };
				m_blend_ok[i] = 1;
			}
			float w = 0;                             // a car the camera moves with: as drawn
			for (const auto &r : a.follows) if (r.owner == sb.owner) w += r.weight * (1 - t);
			for (const auto &r : b.follows) if (r.owner == sb.owner) w += r.weight * t;
			from_mat((steer * (1 - w) + glm::mat4(w)) * to_mat(m_blend[i].data()), m_blend[i].data());
		}
	}
	static const bool logm = getenv("M1_LOG_SMOOTH") != nullptr;
	if (logm && m_blend_cut && !m_redrawing) fprintf(stderr, "M cut frame %d turn %.1f move %.2f\n", b.frame, glm::degrees(angle), jump);
	if (m_blend_cut) std::fill(m_blend_ok.begin(), m_blend_ok.end(), 0);
	for (size_t i = 0; i < nb; i++)
		if (!b.slots[i].polys.empty()) m_smooth_stats[m_blend_ok[i] ? 2 : 0]++;
}

// While replaying: the matrix for the object being drawn (the current slot's, blended).
bool model1::smooth_matrix(uint32_t, float *out)
{
	if ((m_replay_older && !m_replay_extra) || m_replay_slot < 0 || m_replay_slot >= int(m_blend_ok.size()) || !m_blend_ok[m_replay_slot]) return false;
	memcpy(out, m_blend[m_replay_slot].data(), 12 * sizeof(float));
	if (m_replay_extra) { m_shade_from = m_shade_to = m_shade_light = nullptr; return true; }   // (its own shading)
	m_shade_from = m_blend_from[m_replay_slot].data();
	m_shade_to = m_blend_to[m_replay_slot].data();
	m_shade_light = m_blend_light[m_replay_slot].data();
	return true;
}

// The game's sky scroll (its routine at the start of vblank) comes from the camera (the RAM in
// game::video_traits::sky_scroll): across, its heading (1/32 pixel, 2048 pixels for a turn); up
// and down, its height (whole pixels). Each is mixed with the last game frame's: halfway across on
// the frames between the game's 30 a second, and a quarter or three quarters up and down; then
// written to the sky map's scroll in whole pixels (h: the heading >> 5; v: a sum >> 1). When a
// write is that sum, the fraction it dropped is kept for smooth motion's sky (worked out again
// from the RAM it read: nothing writes those in between).
model1::sky_scroll model1::sky_scroll_from_ram(bool between) const
{
	const game::sky_camera &cam = *m_video->sky_scroll;
	auto rd = [&](uint32_t x) { return uint16_t(m_ram_b[x - 0x500000] | m_ram_b[x - 0x500000 + 1] << 8); };
	if (cam.layout == game::sky_camera::format::tile_pair) {
		const uint16_t h = rd(cam.heading), v = rd(cam.height);
		const uint16_t hs = between ? uint16_t(h + rd(cam.last_heading)) : h;
		const uint16_t vs = between ? uint16_t(v + rd(cam.last_height)) : v;
		sky_scroll s;
		s.h = uint16_t(((between ? hs >> 1 : hs) & 0x3ff) | (h & (between ? 0x8000 : 0)));
		if (!between) s.h &= 0x1ff;                 // direct path masks the sky's horizontal flags
		s.v = uint16_t(((between ? vs >> 1 : vs) & 0x3ff) | (v & 0xe000));
		// The direct path also carries the layer-disable bit from the game's control word.
		if (!between) s.v |= uint16_t((rd(cam.control) & 4) ? 0x8000 : 0);
		s.hx = float(s.h & 0x1ff) + (between ? (hs & 1) * 0.5f : 0);
		s.vy = float(s.v & 0x3ff) + (between ? (vs & 1) * 0.5f : 0);
		return s;
	}
	uint16_t r1 = rd(cam.heading);
	if (between) r1 = uint16_t(r1 - (int16_t(uint16_t(r1 - rd(cam.last_heading))) >> 1));
	uint16_t r3 = rd(cam.height), r4 = rd(cam.last_height);
	const uint16_t d = uint16_t(r3 - r4);
	if (between) r3 = uint16_t(r3 - (int16_t(d) >> 1));
	else r4 = uint16_t(r4 + (int16_t(d) >> 1) + (d & 1));   // (rounded: the bit shifted out added back)
	const uint16_t sum = uint16_t(r3 + r4);
	sky_scroll s;
	s.h = (r1 >> 5) & 0x3ff;
	s.v = uint16_t((((sum >> 1) + 0x21) & 0x3ff) | (r3 & 0xe000));
	s.hx = float(s.h & 0x1ff) + (r1 & 31) / 32.0f;
	s.vy = float(s.v & 0x3ff) + (sum & 1) * 0.5f;
	return s;
}

// Where the sky lands for the camera in RAM now: its own scroll, unmixed (h the heading >> 5, v as
// the routine's sum of two equal values)
void model1::sky_settled(int &h, int &v) const
{
	if (!m_video->sky_scroll) {         // (no camera known: where the tile chip has it)
		h = m_tiles.tile_r(0x5002) & 0x1ff;
		v = m_tiles.tile_r(0x5006) & 0x3ff;
		return;
	}
	const game::sky_camera &cam = *m_video->sky_scroll;
	auto rd = [&](uint32_t x) { return uint16_t(m_ram_b[x - 0x500000] | m_ram_b[x - 0x500000 + 1] << 8); };
	const bool tiles = cam.layout == game::sky_camera::format::tile_pair;
	h = (tiles ? rd(cam.heading) : rd(cam.heading) >> 5) & 0x1ff;
	v = (rd(cam.height) + (tiles ? 0 : 0x21)) & 0x3ff;
}

// Whether the sky can be drawn where it lands from where the game has it in a frame (the tile
// layer moves 24 pixels at most; a hi-res sky further)
bool model1::sky_reaches(const sky_sample &s) const
{
	const int dh = ((s.lands_h - s.h + 256) & 511) - 256, dv = ((s.lands_v - s.v + 512) & 1023) - 512;
	const double most = m_sky_now ? MAX_SKY_STEP : 24;
	return std::abs(dh) <= most && std::abs(dv) <= most;
}

void model1::sky_scroll_written(uint32_t a)
{
	if (!m_video->sky_scroll) return;   // (no camera known: no fractions, the scroll as written)
	m_sky_between = m_ram_b[m_video->sky_scroll->between - 0x500000] & 1;
	if (m_video->sky_scroll->layout == game::sky_camera::format::tile_pair) m_sky_between = !m_sky_between;
	const sky_scroll s = sky_scroll_from_ram(m_sky_between);
	if (a == 0x70a004) {
		m_sky_read_h = s.h == m_tiles.tile_r(0x5002);         // (anything else written: no fraction)
		m_sky_frac_h = m_sky_read_h ? s.hx - float(s.h & 0x1ff) : 0;
	} else {
		m_sky_read_v = s.v == m_tiles.tile_r(0x5006);
		m_sky_frac_v = m_sky_read_v ? s.vy - float(s.v & 0x3ff) : 0;
	}
	m_sky_written = m_frame;
}

// While replaying: how far to move the sky layer (arcade pixels) from where the tile chip has it
// now to where it was at the moment drawn: the game's own scroll with its fractions, each frame's
// (straight between them, as the 3D's blend; past the newest frame, toward the next one's, worked
// out as the newest ended). At a frame's own moment it is that frame's scroll, whose whole pixels
// are what the game drew. The sky's map x is -hscroll, its y +vscroll (1024 rows, in two maps).
void model1::smooth_sky_shift(float &dx, float &dy) const
{
	dx = dy = 0;
	if (!m_replaying) return;
	// The game writes the sky's scroll two frames after the display list it goes with (at a camera
	// cut the 3D changes at frame C and the sky at C+2): the sky drawn with the list for moment
	// m is the scroll at m+2, which smooth motion, two frames behind the game, has up to the
	// newest frame and the next.
	constexpr double SKY_LAG = 2;
	const double t = m_draw_time + SKY_LAG;
	const int newest = int(m_frame) - 1;
	if (newest < 0) return;
	const sky_sample &cur = m_sky_hist[newest % SKY_HIST];
	if (cur.frame != newest) return;
	// frame f's scroll less the newest's (unwrapped), or false when it's not kept
	auto at = [&](int f, bool horizontal, double &out) {
		if (f < 0 || f > newest || f <= newest - SKY_HIST) return false;
		const sky_sample &s = m_sky_hist[f % SKY_HIST];
		if (s.frame != f) return false;
		const double w = horizontal ? 512 : 1024, d = horizontal ? s.hx - cur.hx : s.vy - cur.vy;
		out = d - w * std::round(d / w);
		return true;
	};
	// A step from one frame's scroll to the next is a move, not a jump (a cut), when it's small, or
	// when it carries on at the speed of the step before it: a stage intro's fly-around turns the
	// camera 25-60 pixels a frame, which a fixed 24 took for cuts (the sky then stepped a frame at
	// a time while the 3D glided).
	auto steady = [&](double step, bool have_before, double before) {
		return std::fabs(step) <= 24 || (have_before && std::fabs(step) <= MAX_SKY_STEP && std::fabs(step - before) <= 24);
	};
	auto moment = [&](bool horizontal) {
		const int k = std::min(int(std::floor(t)), newest);
		double fr = t - k;
		double a, b, p;
		if (!at(k, horizontal, a)) return 0.0;
		if (fr <= 0) return a;
		const bool have_p = at(k - 1, horizontal, p);
		// a jump (a cut) isn't crossed: the scroll changes at the frame, as the game's
		if (k < newest) return at(k + 1, horizontal, b) && steady(b - a, have_p, a - p) ? a + (b - a) * fr : a;
		fr = std::min(fr, 1.0);
		if (horizontal ? cur.next_h : cur.next_v) {        // a == 0: the newest frame's
			const double w = horizontal ? 512 : 1024, d = horizontal ? cur.nx - cur.hx : cur.ny - cur.vy;
			b = d - w * std::round(d / w);
			return steady(b, have_p, a - p) ? b * fr : 0.0;
		}
		double pp;
		const bool have_pp = at(k - 2, horizontal, pp);
		return have_p && steady(a - p, have_pp, p - pp) ? a + (a - p) * fr : a;   // else at its speed
	};
	const int h_now = m_tiles.tile_r(0x5002) & 0x1ff, v_now = m_tiles.tile_r(0x5006) & 0x3ff;
	auto wrapped = [](double d, double w) { return d - w * std::round(d / w); };
	double h_t = cur.hx + moment(true), v_t = cur.vy + moment(false);
	if (m_sky_settling_now) {
		// just after a cut, while the game's mix with the old view's camera moves the sky into
		// place: where it lands
		h_t = cur.lands_h;
		v_t = cur.lands_v;
	}
	dx = float(wrapped(h_now - h_t, 512));               // sample at x + (h_now - h_t)
	dy = float(wrapped(v_t - v_now, 1024));              // and y + (v_t - v_now)
	if (std::fabs(dx) > MAX_SKY_STEP || std::fabs(dy) > MAX_SKY_STEP) dx = dy = 0;
}

void model1::smooth_state(float &alpha, int &older, int &newer, float *place) const
{
	alpha = m_last_alpha;
	if (place) for (int k = 0; k < 3; k++) place[k] = m_pic_place[k];
	older = m_logic[0].list.empty() ? -1 : m_logic[0].frame;
	newer = m_logic[1].list.empty() ? -1 : m_logic[1].frame;
}
