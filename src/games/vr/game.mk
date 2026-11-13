# Virtua Racing's build settings (the Makefile's GAME_RULES make its targets from these)
GAMES += vr
# the desktop build uses the same startup recompilation as the downloads
vr_HOST_REC = native
# the scripted runs model1cov records for the recompile (make vr-coverage, vr-static)
vr_COV_SCENARIOS = default alternate intermediate views service qualifying
vr_COV_FRAMES ?= 26000
vr_LOCK_SCENARIOS = default alternate intermediate views service qualifying
vr_LOCK_FRAMES ?= 26000

# its tests (tests/games/vr)
$(OBJ)/tests/games/vr/profile_test.o: CXXFLAGS += $(SDL_CFLAGS)
$(BIN)/vr_profile_test: $(OBJ)/tests/games/vr/profile_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS) $(SDL_LIBS)
test-vr-profile: $(BIN)/vr_profile_test
	$(BIN)/vr_profile_test

$(BIN)/vr_gameplay_test: $(OBJ)/tests/games/vr/gameplay_test.o $(GAMES_LINK) $(call objs,$(RT) $(NATIVE)) $(REC_NONE) $(SLJIT_OBJ) $(CORO_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-vr-gameplay: $(BIN)/vr_gameplay_test
	$(BIN)/vr_gameplay_test

$(BIN)/vr_tgp_track_test: tests/games/vr/tgp_track_test.cpp src/games/vr/tgp.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< -o $@
test-vr-tgp-track: $(BIN)/vr_tgp_track_test
	$(BIN)/vr_tgp_track_test

$(BIN)/vr_link_test: $(OBJ)/tests/games/vr/link_test.o $(GAMES_LINK) $(call objs,$(RT) $(NATIVE)) $(REC_NONE) $(SLJIT_OBJ) $(CORO_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-vr-link: $(BIN)/vr_link_test
	$(BIN)/vr_link_test
$(BIN)/vr_lan_test: $(OBJ)/tests/games/vr/lan_test.o $(GAMES_LINK) $(call objs,$(RT) $(NATIVE)) $(REC_NONE) $(SLJIT_OBJ) $(CORO_OBJ) $(LAN_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS) $(LAN_LIBS)
test-vr-lan: $(BIN)/vr_lan_test
	$(BIN)/vr_lan_test
test-vr-lan-desktop: $(BIN)/vr-native vr-regions
	python3 tests/games/vr/lan_desktop_test.py $(BIN)/vr-native build/vr
test-vr-lan-desktop-four: $(BIN)/vr-native vr-regions
	python3 tests/games/vr/lan_desktop_test.py $(BIN)/vr-native build/vr 4
.PHONY: test-vr-lan test-vr-lan-desktop test-vr-lan-desktop-four
test-vr-link-rom: $(BIN)/vr_link_test vr-regions
	$(BIN)/vr_link_test --roms build/vr
test-vr-two-cabinets: $(BIN)/vr_link_test vr-regions
	$(BIN)/vr_link_test --in-process build/vr
test-vr-split-screen: $(BIN)/vr-native vr-regions
	python3 tests/games/vr/split_screen_test.py $(BIN)/vr-native build/vr
test-vr-menu-backdrops: $(BIN)/model1_video_test vr-regions
	$(BIN)/model1_video_test build/vr --vr-menu-backdrops

$(BIN)/vr_live_monitor_test: $(OBJ)/tests/games/vr/live_monitor_test.o $(GAMES_LINK) $(call objs,$(RT) $(NATIVE)) $(REC_NONE) $(SLJIT_OBJ) $(CORO_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-vr-live-monitor: $(BIN)/vr_live_monitor_test vr-regions
	$(BIN)/vr_live_monitor_test build/vr build/vr/live-monitor
test-vr-live-monitor-desktop: $(BIN)/vr-native vr-regions
	python3 tests/games/vr/live_monitor_desktop_test.py $(BIN)/vr-native build/vr
.PHONY: test-vr-profile test-vr-gameplay test-vr-tgp-track test-vr-link test-vr-link-rom test-vr-two-cabinets test-vr-split-screen test-vr-menu-backdrops test-vr-live-monitor test-vr-live-monitor-desktop
