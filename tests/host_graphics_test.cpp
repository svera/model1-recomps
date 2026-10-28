#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>

static int colour_delta = 0;
static int changed_pixels = 0;

static SDL_Surface *read_pixels(SDL_Renderer *renderer, const SDL_Rect *rect)
{
	auto *surface = SDL_RenderReadPixels(renderer, rect);
	assert(surface);
	const int count = changed_pixels < 0 ? surface->w * surface->h : changed_pixels;
	for (int i = 0; i < count; i++) {
		Uint8 r, g, b, a;
		const int x = i % surface->w, y = i / surface->w;
		assert(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
		r = r <= 255 - colour_delta ? r + colour_delta : r - colour_delta;
		assert(SDL_WriteSurfacePixel(surface, x, y, r, g, b, a));
	}
	return surface;
}

#define SDL_RenderReadPixels read_pixels
#include "../src/host/gpu_compose.h"
#undef SDL_RenderReadPixels
#include "../src/host/menu.h"
#include <RmlUi/Core.h>

static void compose_checks(SDL_Renderer *renderer)
{
	gpu_compose compose;
	assert(compose.init(renderer));
	std::string why;
	assert(compose.self_check(why));
	colour_delta = model1::GPU_ROUNDING_COLOUR;
	changed_pixels = -1;
	assert(compose.self_check(why));
	colour_delta++;
	changed_pixels = int(model1::GPU_ROUNDING_PX);
	assert(compose.self_check(why));
	changed_pixels++;
	assert(!compose.self_check(why));
	assert(why == "65 of 36288 pixels differ from the CPU's by more than rounding");
	colour_delta = changed_pixels = 0;
	assert(compose.self_check(why));
	compose.release();
}

static void menu_checks(SDL_Window *window, SDL_Renderer *renderer)
{
	game_menu menu;
	menu.render();
	menu.toast("No overlay yet");
	assert(menu.init(window, renderer, "assets"));
	auto *ctx = Rml::GetContext("menu");
	assert(ctx && ctx->GetNumDocuments() == 2);
	auto *overlay = ctx->GetDocument(1);
	auto *toast = overlay->GetElementById("toast");
	auto *fps = overlay->GetElementById("fps");
	assert(toast && fps);

	menu.toast("GPU < CPU & driver", 0.2);
	SDL_Delay(250);
	menu.render();
	assert(overlay->IsVisible());
	assert(toast->GetNumChildren() == 1);
	assert(toast->GetFirstChild()->GetNumChildren() == 1);
	menu.open({ "Test", "", { { "Resume", "", {}, {}, [&] { menu.close(); } } } });
	menu.render();
	assert(!overlay->IsVisible());
	SDL_Delay(250);
	menu.render();
	menu.close();
	menu.render();
	assert(overlay->IsVisible());
	SDL_Delay(250);
	menu.render();
	assert(!overlay->IsVisible());
	assert(toast->GetNumChildren() == 0);

	menu.set_fps("60 FPS");
	menu.render();
	assert(overlay->IsVisible() && fps->IsVisible());
	menu.toast("GPU fallback", 0.02);
	menu.render();
	SDL_Delay(40);
	menu.render();
	assert(overlay->IsVisible() && fps->IsVisible());
	assert(toast->GetNumChildren() == 0);
	menu.set_fps("");
	menu.render();
	assert(!overlay->IsVisible());

	menu.toast("Retry from the menu");
	menu.render();
	menu.toast("");
	menu.render();
	assert(!overlay->IsVisible());
}

int main()
{
	SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
	assert(SDL_Init(SDL_INIT_VIDEO));
	auto *window = SDL_CreateWindow("Graphics regression tests", 1280, 720, SDL_WINDOW_HIDDEN);
	assert(window);
	auto *renderer = SDL_CreateRenderer(window, nullptr);
	assert(renderer);
	compose_checks(renderer);
	menu_checks(window, renderer);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	printf("Host graphics: compose rounding boundaries, notice timing, menu pause and FPS coexistence passed\n");
}
