# Wing War (World): ROM-backed runtime and controls.
GAMES += wingwar

$(OBJ)/tests/games/wingwar/profile_test.o: CXXFLAGS += $(SDL_CFLAGS)
$(BIN)/wingwar_profile_test: $(OBJ)/tests/games/wingwar/profile_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS) $(SDL_LIBS)
test-wingwar-profile: $(BIN)/wingwar_profile_test
	$(BIN)/wingwar_profile_test

$(BIN)/wingwar_boot_test: $(OBJ)/tests/games/wingwar/boot_test.o $(GAMES_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)
test-wingwar-boot: $(BIN)/wingwar_boot_test wingwar-regions
	$(BIN)/wingwar_boot_test build/wingwar

test-wingwar-romset: $(BIN)/romset_test wingwar-regions
	$(BIN)/romset_test wingwar roms/wingwar.zip $(IOBOARD2_ZIP) build/wingwar

.PHONY: test-wingwar-profile test-wingwar-boot test-wingwar-romset
