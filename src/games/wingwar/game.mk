# Wing War (World): ROM-backed runtime and controls.
GAMES += wingwar
wingwar_COV_SCENARIOS = boot attract
wingwar_COV_FRAMES_boot ?= 700
wingwar_COV_FRAMES_attract ?= 7200
wingwar_EXTRA_HINTS = src/games/wingwar/native_entries.txt
wingwar_EXTRA_HINTS += $(wildcard build/cov/wingwar/coverage-human-*.txt)

build/wingwar/boot.inputs: $(BIN)/wingwar_script_test build/wingwar/maincpu.bin
	$(BIN)/wingwar_script_test build/wingwar $@.tmp
	mv $@.tmp $@
wingwar-recording-seed: build/wingwar/boot.inputs
test-wingwar-recording: build/wingwar/boot.inputs $(BIN)/model1cov $(BIN)/wingwar_native_overlay_test build/gen/wingwar_listing.txt
	$(BIN)/model1cov --game wingwar --replay-inputs build/wingwar/boot.inputs build/wingwar 700 build/cov/wingwar/coverage-recording-seed.txt
	$(BIN)/wingwar_native_overlay_test build/wingwar build/gen/wingwar_map.txt --replay-inputs build/wingwar/boot.inputs

$(BIN)/wingwar_script_test: $(OBJ)/tests/games/wingwar/script_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-wingwar-script: $(BIN)/wingwar_script_test wingwar-regions
	$(BIN)/wingwar_script_test build/wingwar

$(BIN)/wingwar_ram_code_test: $(OBJ)/tests/games/wingwar/ram_code_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-wingwar-ram-code: $(BIN)/wingwar_ram_code_test wingwar-regions
	$(BIN)/wingwar_ram_code_test build/wingwar

$(BIN)/wingwar_native_overlay_test: $(OBJ)/tests/games/wingwar/native_overlay_test.o $(GAMES_LINK) $(call objs,$(RT) $(NATIVE)) $(REC_NONE) $(SLJIT_OBJ) $(CORO_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-wingwar-native-overlays: $(BIN)/wingwar_native_overlay_test build/gen/wingwar_listing.txt wingwar-regions
	$(BIN)/wingwar_native_overlay_test build/wingwar build/gen/wingwar_map.txt
test-wingwar-native-shared: $(BIN)/wingwar_native_overlay_test vr-regions swa-regions
	$(BIN)/wingwar_native_overlay_test --shared build/vr build/swa

$(OBJ)/tests/games/wingwar/profile_test.o: CXXFLAGS += $(SDL_CFLAGS)
$(BIN)/wingwar_profile_test: $(OBJ)/tests/games/wingwar/profile_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS) $(SDL_LIBS)
test-wingwar-profile: $(BIN)/wingwar_profile_test
	$(BIN)/wingwar_profile_test

$(BIN)/wingwar_lod_test: $(OBJ)/tests/games/wingwar/lod_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-wingwar-lod: $(BIN)/wingwar_lod_test wingwar-regions
	$(BIN)/wingwar_lod_test build/wingwar

$(BIN)/wingwar_draw_distance_test: $(OBJ)/tests/games/wingwar/draw_distance_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-wingwar-draw-distance: $(BIN)/wingwar_draw_distance_test wingwar-regions
	$(BIN)/wingwar_draw_distance_test build/wingwar

$(BIN)/wingwar_interpolation_test: $(OBJ)/tests/games/wingwar/interpolation_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-wingwar-interpolation: $(BIN)/wingwar_interpolation_test wingwar-regions
	$(BIN)/wingwar_interpolation_test build/wingwar

$(BIN)/wingwar_boot_test: $(OBJ)/tests/games/wingwar/boot_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-wingwar-boot: $(BIN)/wingwar_boot_test wingwar-regions
	$(BIN)/wingwar_boot_test build/wingwar

test-wingwar-romset: $(BIN)/romset_test wingwar-regions
	$(BIN)/romset_test wingwar roms/wingwar.zip $(IOBOARD2_ZIP) build/wingwar

.PHONY: test-wingwar-profile test-wingwar-lod test-wingwar-draw-distance test-wingwar-interpolation test-wingwar-boot test-wingwar-romset test-wingwar-script test-wingwar-ram-code test-wingwar-native-overlays test-wingwar-native-shared wingwar-recording-seed test-wingwar-recording
