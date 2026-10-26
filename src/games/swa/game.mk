# Native recompilation from assets/games/swa/swa_map.txt (strict lockstep, all scenarios). Scripted play: src/games/swa/script.cpp.
GAMES += swa
swa_HOST_REC = native
# the scripted runs model1cov records as hints for the recompile (make swa-coverage)
swa_COV_SCENARIOS = default gunner training continue progression recording complete
swa_COV_FRAMES ?= 38400
swa_COV_FRAMES_complete ?= 57600   # the recorded run that finishes the game, to its ending
swa_LOCK_SCENARIOS = default gunner training continue progression recording complete
swa_LOCK_FRAMES ?= 38400
swa_LOCK_FRAMES_complete ?= 57600

$(OBJ)/tests/games/swa/profile_test.o $(OBJ)/tests/games/swa/controls_test.o $(OBJ)/tests/games/swa/dsb_test.o: CXXFLAGS += $(SDL_CFLAGS)
$(BIN)/swa_profile_test: $(OBJ)/tests/games/swa/profile_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-swa-profile: $(BIN)/swa_profile_test
	$(BIN)/swa_profile_test

test-swa-romset: $(BIN)/romset_test swa-regions
	$(BIN)/romset_test swa roms/swa.zip $(IOBOARD_ZIP) build/swa

$(BIN)/swa_boot_test: $(OBJ)/tests/games/swa/boot_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-swa-boot: $(BIN)/swa_boot_test swa-regions
	$(BIN)/swa_boot_test build/swa

$(BIN)/swa_controls_test: $(OBJ)/tests/games/swa/controls_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-swa-controls: $(BIN)/swa_controls_test swa-regions
	$(BIN)/swa_controls_test build/swa

$(BIN)/swa_dsb_test: $(OBJ)/tests/games/swa/dsb_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-swa-dsb: $(BIN)/swa_dsb_test swa-regions
	$(BIN)/swa_dsb_test build/swa

$(BIN)/swa_gameplay_test: $(OBJ)/tests/games/swa/gameplay_test.o $(GAMES_LINK) $(call objs,$(RT) $(NATIVE)) $(REC_NONE) $(SLJIT_OBJ) $(CORO_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-swa-gameplay: $(BIN)/swa_gameplay_test swa-regions
	$(BIN)/swa_gameplay_test build/swa

# a recorded human run, replayed by its cabinet inputs (tests/games/swa/recordings)
$(BIN)/swa_recording_test: $(OBJ)/tests/games/swa/recording_test.o $(GAMES_LINK) $(call objs,$(RT) $(NATIVE)) $(REC_NONE) $(SLJIT_OBJ) $(CORO_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-swa-recording: $(BIN)/swa_recording_test swa-regions
	$(BIN)/swa_recording_test build/swa

test-swa-video: $(BIN)/model1_video_test swa-regions
	$(BIN)/model1_video_test build/swa --swa-priority
	$(BIN)/model1_video_test build/swa --swa-object-bounds

test-swa-wrap: $(BIN)/model1_video_test swa-regions
	$(BIN)/model1_video_test build/swa --swa-wrap

.PHONY: test-swa-profile test-swa-romset test-swa-boot test-swa-controls test-swa-dsb test-swa-gameplay test-swa-recording test-swa-video test-swa-wrap
