#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

enum class cabinet_layout { windows, side_by_side, top_bottom };

inline SDL_FRect screen_filter_rect(SDL_FRect picture)
{
	const float x = std::round(picture.x), y = std::round(picture.y);
	return {x, y, std::round(picture.x + picture.w) - x, std::round(picture.y + picture.h) - y};
}

inline Uint64 next_cabinet_picture(Uint64 deadline, Uint64 now, int fps)
{
	const Uint64 period = 1000000000ull / fps;
	if (!deadline) return now + period;
	if (now < deadline) return deadline;
	return deadline + ((now - deadline) / period + 1) * period;
}

inline SDL_Texture *create_cabinet_texture(SDL_Renderer *renderer, int width, int height)
{
	auto *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	if (!texture) return nullptr;
	// The framebuffer's top byte is not alpha.
	if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE)) {
		SDL_DestroyTexture(texture);
		return nullptr;
	}
	return texture;
}

inline SDL_Rect cabinet_viewport(cabinet_layout layout, int width, int height, int player,
	int player_count, bool live_monitor_above = false)
{
	if (layout != cabinet_layout::windows && player_count >= 3) {
		const int half_w = width / 2, half_h = height / 2;
		if (player_count == 3 && !live_monitor_above && player == 0)
			return {0, 0, width, half_h};
		const int slot = player_count == 3 ? (live_monitor_above ? (player == player_count ? 0 : player + 1) : player + 1) : player;
		return {slot % 2 ? half_w : 0, slot / 2 ? half_h : 0,
			slot % 2 ? width - half_w : half_w, slot / 2 ? height - half_h : half_h};
	}
	const bool second_player = player_count > 1;
	if (live_monitor_above) {
		const int split = height / 2;
		if (player == player_count) return {0, 0, width, split};
		if (!second_player) return player ? SDL_Rect{} : SDL_Rect{0, split, width, height - split};
		auto viewport = cabinet_viewport(cabinet_layout::side_by_side, width, height - split, player, player_count);
		viewport.y += split;
		return viewport;
	}
	if (layout != cabinet_layout::windows && !second_player)
		return player ? SDL_Rect{} : SDL_Rect{0, 0, width, height};
	if (layout == cabinet_layout::side_by_side) {
		const int split = width / 2;
		return {player ? split : 0, 0, player ? width - split : split, height};
	}
	if (layout == cabinet_layout::top_bottom) {
		const int split = height / 2;
		return {0, player ? split : 0, width, player ? height - split : split};
	}
	return {0, 0, width, height};
}

inline SDL_Rect cabinet_viewport(cabinet_layout layout, int width, int height, int player,
	bool second_player = true, bool live_monitor_above = false)
{
	if (live_monitor_above && !second_player && player == 1) return {};
	return cabinet_viewport(layout, width, height,
		live_monitor_above && player == 2 ? (second_player ? 2 : 1) : player,
		second_player ? 2 : 1, live_monitor_above);
}

inline SDL_FRect cabinet_picture_rect(SDL_Rect viewport, float aspect)
{
	if (viewport.w <= 0 || viewport.h <= 0) return {};
	SDL_FRect dst{float(viewport.x), float(viewport.y), float(viewport.w), float(viewport.h)};
	if (dst.w > dst.h * aspect) {
		const float width = dst.h * aspect;
		dst.x += (dst.w - width) / 2;
		dst.w = width;
	} else {
		const float height = dst.w / aspect;
		dst.y += (dst.h - height) / 2;
		dst.h = height;
	}
	return dst;
}

inline SDL_Rect cabinet_composition_viewport(cabinet_layout layout, int width, int height,
	int player, float aspect, int player_count, bool live_monitor_above = false)
{
	if (width <= 0 || height <= 0) return {};
	if (aspect <= 0) aspect = std::clamp(float(width) / height, 4.0f / 3, 32.0f / 9);
	const auto picture = cabinet_picture_rect({0, 0, width, height}, aspect);
	auto viewport = cabinet_viewport(layout, int(std::lround(picture.w)), int(std::lround(picture.h)),
		player, player_count, live_monitor_above);
	viewport.x += int(std::lround(picture.x));
	viewport.y += int(std::lround(picture.y));
	return viewport;
}

inline SDL_Rect cabinet_composition_viewport(cabinet_layout layout, int width, int height,
	int player, float aspect, bool second_player = true, bool live_monitor_above = false)
{
	if (live_monitor_above && !second_player && player == 1) return {};
	return cabinet_composition_viewport(layout, width, height,
		live_monitor_above && player == 2 ? (second_player ? 2 : 1) : player,
		aspect, second_player ? 2 : 1, live_monitor_above);
}

inline bool draw_cabinet(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Rect viewport, float aspect)
{
	if (viewport.w <= 0 || viewport.h <= 0) return true;
	const SDL_FRect dst = cabinet_picture_rect(viewport, aspect);
	return SDL_RenderTexture(renderer, texture, nullptr, &dst);
}
