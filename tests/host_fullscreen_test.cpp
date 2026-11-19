#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <string>
#include "../src/host/cabinet_display.h"

namespace mock {
SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
SDL_Rect rect{120, 80, 960, 720}, normal = rect;
const SDL_Rect display{-1920, 0, 1920, 1080};
std::string fail;
int native_calls = 0;

bool operation(const char *name)
{
	if (fail != name) return true;
	fail.clear();
	return SDL_SetError("test failure: %s", name);
}
SDL_WindowFlags get_flags(SDL_Window *) { return flags; }
SDL_DisplayID get_display(SDL_Window *) { return 1; }
bool get_bounds(SDL_DisplayID, SDL_Rect *out)
{
	if (!operation("bounds")) return false;
	*out = display;
	return true;
}
bool get_position(SDL_Window *, int *x, int *y) { *x = rect.x; *y = rect.y; return true; }
bool get_size(SDL_Window *, int *w, int *h) { *w = rect.w; *h = rect.h; return true; }
bool set_position(SDL_Window *, int x, int y)
{
	if (!operation("position")) return false;
	rect.x = x; rect.y = y;
	return true;
}
bool set_size(SDL_Window *, int w, int h)
{
	if (!operation("size")) return false;
	rect.w = w; rect.h = h;
	return true;
}
bool set_bordered(SDL_Window *, bool bordered)
{
	if (!operation("border")) return false;
	if (bordered) flags &= ~SDL_WINDOW_BORDERLESS;
	else flags |= SDL_WINDOW_BORDERLESS;
	return true;
}
bool restore(SDL_Window *)
{
	if (!operation("restore")) return false;
	rect = normal;
	flags &= ~SDL_WINDOW_MAXIMIZED;
	return true;
}
bool maximize(SDL_Window *)
{
	if (!(flags & SDL_WINDOW_MAXIMIZED)) normal = rect;
	rect = display;
	flags |= SDL_WINDOW_MAXIMIZED;
	return true;
}
bool sync(SDL_Window *) { return operation("sync"); }
bool set_fullscreen(SDL_Window *, bool enabled)
{
	++native_calls;
	if (enabled) flags |= SDL_WINDOW_FULLSCREEN;
	else flags &= ~SDL_WINDOW_FULLSCREEN;
	return true;
}
}

#define SDL_GetWindowFlags mock::get_flags
#define SDL_GetDisplayForWindow mock::get_display
#define SDL_GetDisplayBounds mock::get_bounds
#define SDL_GetWindowPosition mock::get_position
#define SDL_GetWindowSize mock::get_size
#define SDL_SetWindowPosition mock::set_position
#define SDL_SetWindowSize mock::set_size
#define SDL_SetWindowBordered mock::set_bordered
#define SDL_RestoreWindow mock::restore
#define SDL_MaximizeWindow mock::maximize
#define SDL_SyncWindow mock::sync
#define SDL_SetWindowFullscreen mock::set_fullscreen
// Exercise the Windows policy using mocked window operations, even on non-Windows builders.
#ifndef _WIN32
#define _WIN32
#endif
#include "../src/host/window_fullscreen.h"

static void check_rect(SDL_Rect actual, SDL_Rect expected)
{
	assert(actual.x == expected.x && actual.y == expected.y &&
		actual.w == expected.w && actual.h == expected.h);
}

int main()
{
	SDL_Window *window = nullptr;
	window_fullscreen fullscreen;
	const SDL_Rect original = mock::rect;
	assert(window_fullscreen::creation_flags(true) == 0);
	assert(!fullscreen.active(window));
	assert(fullscreen.set(window, true));
	assert(fullscreen.active(window));
	assert(!(mock::flags & SDL_WINDOW_FULLSCREEN));
	assert(mock::flags & SDL_WINDOW_BORDERLESS);
	assert(mock::native_calls == 0);
	check_rect(mock::rect, {-1920, 0, 1920, 1081});
	assert(fullscreen.set(window, true));
	assert(fullscreen.set(window, false));
	assert(!fullscreen.active(window));
	check_rect(mock::rect, original);
	assert(!(mock::flags & SDL_WINDOW_BORDERLESS));

	mock::maximize(window);
	assert(fullscreen.set(window, true));
	assert(!(mock::flags & SDL_WINDOW_MAXIMIZED));
	assert(fullscreen.set(window, false));
	assert(mock::flags & SDL_WINDOW_MAXIMIZED);
	mock::restore(window);
	check_rect(mock::rect, original);

	for (const char *failure : {"bounds", "border", "position", "size", "sync"}) {
		mock::fail = failure;
		assert(!fullscreen.set(window, true));
		assert(std::string(SDL_GetError()) == std::string("test failure: ") + failure);
		assert(!fullscreen.active(window));
		assert(!(mock::flags & SDL_WINDOW_BORDERLESS));
		check_rect(mock::rect, original);
	}
	mock::maximize(window);
	mock::fail = "restore";
	assert(!fullscreen.set(window, true));
	assert(mock::flags & SDL_WINDOW_MAXIMIZED);
	mock::restore(window);
	check_rect(mock::rect, original);

	assert(fullscreen.set(window, true));
	mock::fail = "border";
	assert(!fullscreen.set(window, false));
	assert(fullscreen.active(window));
	assert(fullscreen.set(window, false));
	assert(!fullscreen.active(window));
	check_rect(mock::rect, original);
	assert(mock::native_calls == 0);

	const auto aligned = screen_filter_rect({239.3333f, 0, 1441.3333f, 1081});
	assert(aligned.x == 239 && aligned.y == 0 && aligned.w == 1442 && aligned.h == 1081);
	printf("Fullscreen: borderless geometry, repeat toggles, maximized restore, failures and filter coordinates passed\n");
}
