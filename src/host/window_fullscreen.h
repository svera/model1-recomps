#pragma once
#include <SDL3/SDL.h>
#include <cstdio>
#include <string>

class window_fullscreen
{
public:
#ifdef _WIN32
	static constexpr bool borderless = true;
#else
	static constexpr bool borderless = false;
#endif
	static constexpr SDL_WindowFlags creation_flags(bool enabled)
	{
		return enabled && !borderless ? SDL_WINDOW_FULLSCREEN : 0;
	}

	bool active(SDL_Window *window) const
	{
		return m_borderless || (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	}

	bool set(SDL_Window *window, bool enabled)
	{
		if constexpr (!borderless) return SDL_SetWindowFullscreen(window, enabled);
		if (!enabled) return m_borderless ? restore(window) : SDL_SetWindowFullscreen(window, false);
		if (m_borderless) return true;

		SDL_Rect bounds;
		if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &bounds)) return false;
		if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)
			if (!SDL_SetWindowFullscreen(window, false) || !SDL_SyncWindow(window)) return false;
		m_maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
		m_bordered = (SDL_GetWindowFlags(window) & SDL_WINDOW_BORDERLESS) == 0;
		if (m_maximized && (!SDL_RestoreWindow(window) || !SDL_SyncWindow(window))) {
			const std::string error = SDL_GetError();
			if (!SDL_MaximizeWindow(window))
				fprintf(stderr, "fullscreen: could not restore maximized window: %s\n", SDL_GetError());
			return SDL_SetError("%s", error.c_str());
		}
		if (!SDL_GetWindowPosition(window, &m_previous.x, &m_previous.y) ||
			!SDL_GetWindowSize(window, &m_previous.w, &m_previous.h)) {
			const std::string error = SDL_GetError();
			if (m_maximized && !SDL_MaximizeWindow(window))
				fprintf(stderr, "fullscreen: could not restore maximized window: %s\n", SDL_GetError());
			return SDL_SetError("%s", error.c_str());
		}

		m_borderless = true;
		// VF1 upstream 0e2c5dd: one extra row avoids AMD Vulkan's fullscreen presentation path.
		if (!SDL_SetWindowBordered(window, false) ||
			!SDL_SetWindowPosition(window, bounds.x, bounds.y) ||
			!SDL_SetWindowSize(window, bounds.w, bounds.h + 1) || !SDL_SyncWindow(window)) {
			const std::string error = SDL_GetError();
			restore(window);
			return SDL_SetError("%s", error.c_str());
		}
		fprintf(stderr, "fullscreen: borderless window at %d,%d, %d x %d\n",
			bounds.x, bounds.y, bounds.w, bounds.h + 1);
		fflush(stderr);
		return true;
	}

private:
	bool restore(SDL_Window *window)
	{
		bool ok = true;
		auto check = [&](bool result, const char *operation) {
			if (!result) {
				fprintf(stderr, "fullscreen: restore %s failed: %s\n", operation, SDL_GetError());
				ok = false;
			}
		};
		check(SDL_SetWindowBordered(window, m_bordered), "border");
		check(SDL_SetWindowSize(window, m_previous.w, m_previous.h), "size");
		check(SDL_SetWindowPosition(window, m_previous.x, m_previous.y), "position");
		if (m_maximized) check(SDL_MaximizeWindow(window), "maximized state");
		check(SDL_SyncWindow(window), "sync");
		if (ok) m_borderless = false;
		return ok;
	}

	bool m_borderless = false, m_maximized = false, m_bordered = true;
	SDL_Rect m_previous{};
};
