#include <SDL3/SDL.h>
#include <cassert>
#include <algorithm>
#include <cmath>
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
#include "../src/host/cabinet_display.h"
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

static void split_screen_checks()
{
	for (const auto size : {SDL_Point{120, 80}, SDL_Point{101, 81}, SDL_Point{1, 1}}) {
		auto *surface = SDL_CreateSurface(size.x, size.y, SDL_PIXELFORMAT_XRGB8888);
		assert(surface);
		auto *renderer = SDL_CreateSoftwareRenderer(surface);
		assert(renderer);
		for (auto layout : {cabinet_layout::side_by_side, cabinet_layout::top_bottom, cabinet_layout::windows}) {
			const auto first = cabinet_viewport(layout, size.x, size.y, 0, false);
			const auto second = cabinet_viewport(layout, size.x, size.y, 1, false);
			assert(first.x == 0 && first.y == 0 && first.w == size.x && first.h == size.y);
			assert(layout == cabinet_layout::windows ? second.w == size.x && second.h == size.y : second.w == 0 && second.h == 0);
		}
		SDL_Texture *textures[2]{};
		uint32_t colours[2] = {0x00ff0000, 0x000000ff};
		for (int p = 0; p < 2; p++) {
			textures[p] = create_cabinet_texture(renderer, 1, 1);
			assert(textures[p]);
			assert(SDL_SetTextureScaleMode(textures[p], SDL_SCALEMODE_NEAREST));
			assert(SDL_UpdateTexture(textures[p], nullptr, &colours[p], 4));
		}
		for (auto layout : {cabinet_layout::side_by_side, cabinet_layout::top_bottom}) {
			assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
			assert(SDL_RenderClear(renderer));
			assert(draw_cabinet(renderer, textures[0], cabinet_viewport(layout, size.x, size.y, 0, false), float(size.x) / size.y));
			assert(draw_cabinet(renderer, nullptr, cabinet_viewport(layout, size.x, size.y, 1, false), 4.0f / 3));
			auto *pixels = SDL_RenderReadPixels(renderer, nullptr);
			assert(pixels);
			for (int y = 0; y < size.y; y++)
				for (int x = 0; x < size.x; x++) {
					Uint8 r, g, b, a;
					assert(SDL_ReadSurfacePixel(pixels, x, y, &r, &g, &b, &a));
					assert(r == 255 && g == 0 && b == 0);
				}
			SDL_DestroySurface(pixels);
		}
		// Redraw both cached textures when either cabinet updates, including after a layout change.
		for (auto layout : {cabinet_layout::side_by_side, cabinet_layout::top_bottom, cabinet_layout::side_by_side}) {
			assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
			assert(SDL_RenderClear(renderer));
			for (int p = 0; p < 2; p++) {
				const auto viewport = cabinet_viewport(layout, size.x, size.y, p);
				const float aspect = viewport.w && viewport.h ? float(viewport.w) / viewport.h : 4.0f / 3;
				assert(draw_cabinet(renderer, textures[p], viewport, aspect));
			}
			auto *pixels = SDL_RenderReadPixels(renderer, nullptr);
			assert(pixels);
			for (int y = 0; y < size.y; y++)
				for (int x = 0; x < size.x; x++) {
					const int p = layout == cabinet_layout::side_by_side ? x >= size.x / 2 : y >= size.y / 2;
					Uint8 r, g, b, a;
					assert(SDL_ReadSurfacePixel(pixels, x, y, &r, &g, &b, &a));
					assert(r == ((colours[p] >> 16) & 255) && g == ((colours[p] >> 8) & 255) && b == (colours[p] & 255));
				}
			SDL_DestroySurface(pixels);
			colours[0] = 0x0000ff00;
			assert(SDL_UpdateTexture(textures[0], nullptr, &colours[0], 4));
		}
		if (size.x == 120) {
			for (auto layout : {cabinet_layout::side_by_side, cabinet_layout::top_bottom, cabinet_layout::windows}) {
				assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
				assert(SDL_RenderClear(renderer));
				const int views = layout == cabinet_layout::windows ? 1 : 2;
				for (int p = 0; p < views; p++)
					assert(draw_cabinet(renderer, textures[p], cabinet_viewport(layout, size.x, size.y, p), p ? 16.0f / 9 : 4.0f / 3));
				auto *pixels = SDL_RenderReadPixels(renderer, nullptr);
				assert(pixels);
				for (int p = 0; p < views; p++) {
					const auto view = cabinet_viewport(layout, size.x, size.y, p);
					Uint8 r, g, b, a;
					assert(SDL_ReadSurfacePixel(pixels, view.x + view.w / 2, view.y + view.h / 2, &r, &g, &b, &a));
					assert(p ? b == 255 : g == 255);
					assert(SDL_ReadSurfacePixel(pixels, view.x, view.y, &r, &g, &b, &a));
					assert(r == 0 && g == 0 && b == 0);
					int min_x = view.w, min_y = view.h, max_x = -1, max_y = -1;
					for (int y = 0; y < view.h; y++)
						for (int x = 0; x < view.w; x++) {
							assert(SDL_ReadSurfacePixel(pixels, view.x + x, view.y + y, &r, &g, &b, &a));
							assert(r == 0 && (p ? g == 0 && (b == 0 || b == 255) : b == 0 && (g == 0 || g == 255)));
							if (!g && !b) continue;
							min_x = std::min(min_x, x); max_x = std::max(max_x, x);
							min_y = std::min(min_y, y); max_y = std::max(max_y, y);
						}
					const float aspect = p ? 16.0f / 9 : 4.0f / 3;
					const float width = std::min(float(view.w), view.h * aspect);
					const float height = width / aspect;
					assert(std::fabs((max_x - min_x + 1) - width) <= 1);
					assert(std::fabs((max_y - min_y + 1) - height) <= 1);
					assert(std::fabs(min_x - (view.w - width) / 2) <= 1);
					assert(std::fabs(min_y - (view.h - height) / 2) <= 1);
				}
				SDL_DestroySurface(pixels);
			}
		}
		for (auto *texture : textures) SDL_DestroyTexture(texture);
		SDL_DestroyRenderer(renderer);
		SDL_DestroySurface(surface);
	}
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

	auto *notice = overlay->GetElementById("notice");
	assert(notice);
	for (const char *text : {
		"Player 2: press 2 or your controller's Start binding to join",
		"Player 2 queued - waiting for player 1 to return to attract mode",
		"Starting player 2 - linking cabinets, please wait...",
		"Player 2: press < & > to join",
	}) {
		assert(menu.set_notice(text));
		assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
		assert(SDL_RenderClear(renderer));
		menu.render();
		assert(overlay->IsVisible());
		assert(notice->GetNumChildren() == 1);
		assert(notice->GetFirstChild()->GetNumChildren() == 1);
		int width = 0, height = 0;
		assert(SDL_GetRenderOutputSize(renderer, &width, &height));
		const auto bounds = notice->GetBox().GetSize();
		assert(bounds.x > width * 0.8f && bounds.x <= width);
		assert(bounds.y < height * 0.2f);
		auto *pixels = SDL_RenderReadPixels(renderer, nullptr);
		assert(pixels);
		int gold = 0;
		for (int y = height * 3 / 4; y < height; y++)
			for (int x = 0; x < width; x++) {
				Uint8 r, g, b, a;
				assert(SDL_ReadSurfacePixel(pixels, x, y, &r, &g, &b, &a));
				if (r > 180 && g > 120 && b < 100) ++gold;
			}
		assert(gold > 100);
		SDL_DestroySurface(pixels);
		menu.toast("Temporary notice", 0.001);
		menu.render();
		SDL_Delay(5);
		menu.render();
		assert(overlay->IsVisible() && notice->GetNumChildren() == 1);
		menu.open({"Pause", "", {}});
		menu.render();
		assert(!overlay->IsVisible());
		menu.close();
		menu.render();
		assert(overlay->IsVisible());
	}
	for (const auto size : {SDL_Point{480, 720}, SDL_Point{1920, 540}}) {
		assert(SDL_SetWindowSize(window, size.x, size.y));
		assert(SDL_SyncWindow(window));
		assert(menu.set_notice("Player 2 queued - waiting for player 1 to return to attract mode"));
		menu.render();
		int width = 0, height = 0;
		assert(SDL_GetRenderOutputSize(renderer, &width, &height));
		const auto bounds = notice->GetBox().GetSize();
		assert(bounds.x > width * 0.8f && bounds.x <= width);
		assert(bounds.y < height * 0.2f);
	}
	assert(menu.set_notice(""));
	menu.render();
	assert(!overlay->IsVisible() && notice->GetNumChildren() == 0);
}

int main()
{
	SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
	assert(SDL_Init(SDL_INIT_VIDEO));
	split_screen_checks();
	auto *window = SDL_CreateWindow("Graphics regression tests", 1280, 720, SDL_WINDOW_HIDDEN);
	assert(window);
	auto *renderer = SDL_CreateRenderer(window, nullptr);
	assert(renderer);
	compose_checks(renderer);
	menu_checks(window, renderer);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	printf("Host graphics: split-screen pixels/layouts/letterboxing, persistent join notices, compose rounding boundaries, notice timing, menu pause and FPS coexistence passed\n");
}
