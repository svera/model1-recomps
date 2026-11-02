#pragma once
#include <SDL3/SDL.h>

enum class cabinet_layout { windows, side_by_side, top_bottom };

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

inline SDL_Rect cabinet_viewport(cabinet_layout layout, int width, int height, int player, bool second_player = true)
{
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

inline bool draw_cabinet(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Rect viewport, float aspect)
{
	if (viewport.w <= 0 || viewport.h <= 0) return true;
	const SDL_FRect dst = cabinet_picture_rect(viewport, aspect);
	return SDL_RenderTexture(renderer, texture, nullptr, &dst);
}
