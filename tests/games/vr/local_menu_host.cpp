#include "../../../src/host/game_host.h"
#include <cstdio>

std::unique_ptr<game_host> make_vr_game_host();

namespace {
class test_link final : public local_cabinet_link
{
	std::unique_ptr<local_cabinet_link> inner;
public:
	explicit test_link(std::unique_ptr<local_cabinet_link> link) : inner(std::move(link))
	{
		fprintf(stderr, "test host: custom link created\n");
	}
	void run_frame() override { inner->run_frame(); }
	local_link_status status(unsigned cabinet) const override { return inner->status(cabinet); }
};

class test_host final : public game_host
{
	std::unique_ptr<game_host> base = make_vr_game_host();
	host_context *ctx = nullptr;
	std::string sidecar = "default";
public:
	const romset::definition &romset() const override { return base->romset(); }
	const control_profile &controls() const override { return base->controls(); }
	const local_multiplayer_info *local_multiplayer() const override { return base->local_multiplayer(); }
	std::unique_ptr<local_cabinet_link> make_local_link(model1 &a, model1 *b, model1 *monitor) override
	{
		auto link = base->make_local_link(a, b, monitor);
		return link ? std::make_unique<test_link>(std::move(link)) : nullptr;
	}
	local_multiplayer_status local_status(model1 &m) const override { return base->local_status(m); }
	bool can_join_local_cabinet() const override { return base->can_join_local_cabinet(); }
	bool has_play_style() const override { return base->has_play_style(); }
	void start(host_context &context) override { ctx = &context; base->start(context); }
	void before_boot() override { base->before_boot(); }
	bool booted() override { return base->booted(); }
	void before_frame() override { base->before_frame(); }
	void keep_arcade_settings() override { base->keep_arcade_settings(); }
	void play_style_changed() override { base->play_style_changed(); }
	void load_settings(const settings_map &s) override { base->load_settings(s); }
	void save_settings(FILE *f) const override { base->save_settings(f); }
	settings_map shared_display_settings() const override { return base->shared_display_settings(); }
	bool sync_shared_settings(model1 &m) override { return base->sync_shared_settings(m); }
	const char *save_sidecar_name() const override { return "local-menu-test.txt"; }
	std::string save_sidecar() const override { return sidecar; }
	void load_sidecar(const std::string &text) override
	{
		sidecar = text;
		fprintf(stderr, "test host: loaded %s for cabinet %d\n", text.c_str(), ctx->link_player);
	}
	std::vector<menu_item> sound_menu_items() override
	{
		return {{"Test sound", "", {}, {}, [this] {
			fprintf(stderr, "test host: sound cabinet %d\n", ctx->link_player);
		}}};
	}
	std::vector<menu_item> menu_items_after_controls() override
	{
		return {{"Test options", "", {}, {}, [this] {
			menu_page page{"Test options", "", {}};
			page.items.push_back({"Test action", "", {}, {}, [this] {
				sidecar = "option" + std::to_string(ctx->link_player);
				fprintf(stderr, "test host: option cabinet %d\n", ctx->link_player);
			}});
			page.items.push_back({"Test capture", "", {}, {}, [this] {
				ctx->refresh_menu();
				menu_page replacement{"Capture test", "", {ctx->back_item()}};
				ctx->replace_page(std::move(replacement), 0);
				ctx->capture("Press T", [this](const SDL_Event &event) {
					if (event.type != SDL_EVENT_KEY_DOWN || event.key.scancode != SDL_SCANCODE_T) return false;
					fprintf(stderr, "test host: captured cabinet %d\n", ctx->link_player);
					return true;
				}, [] {});
			}});
			page.items.push_back(ctx->back_item());
			ctx->push_page(std::move(page));
		}}};
	}
};
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<test_host>(); }
