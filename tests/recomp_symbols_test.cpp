#include "../src/rt/analyse.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
	const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto root = std::filesystem::temp_directory_path();
	const auto input = root / ("model1-recomp-symbols-" + suffix + ".txt");
	const auto map_output = root / ("model1-recomp-map-" + suffix + ".txt");
	const auto listing_output = root / ("model1-recomp-listing-" + suffix + ".txt");
	{
		std::ofstream out(input);
		assert(out);
		out << "# optional names follow entry addresses\n"
			"entry 00abcd CoinageSetup\n"
			"entry 00abce\n"
			"edge 00abcd 00abce\n";
	}

	rec::hints hints;
	assert(hints.read(input.string()));
	assert(hints.entries.count(0x00abcd));
	assert(hints.entries.count(0x00abce));
	assert(hints.symbols.at(0x00abcd) == "CoinageSetup");
	assert(!hints.symbols.count(0x00abce));

	rec::hints symbols_only;
	assert(symbols_only.read_symbols(input.string()));
	assert(symbols_only.entries.empty());
	assert(symbols_only.symbols.at(0x00abcd) == "CoinageSetup");

	rec::program p;
	p.functions.push_back({ 0x00abcd, {} });
	p.edges = hints.edges;
	p.symbols = hints.symbols;
	rec::write_map(p, map_output.string().c_str());
	rec::hints round_trip;
	assert(round_trip.read(map_output.string()));
	assert(round_trip.symbols.at(0x00abcd) == "CoinageSetup");

	rec::write_listing(p, listing_output.string().c_str());
	std::ifstream listing(listing_output);
	assert(listing);
	const std::string contents{std::istreambuf_iterator<char>(listing), std::istreambuf_iterator<char>()};
	assert(contents.find("F 00abcd CoinageSetup") != std::string::npos);

	for (const char *path : {
		"assets/games/swa/swa_map.txt",
		"assets/games/vf/vf_map.txt",
		"assets/games/vr/vr_map.txt",
		"assets/games/wingwar/wingwar_map.txt"
	}) {
		rec::hints game_map;
		assert(game_map.read(path));
	}
	rec::hints swa_map;
	assert(swa_map.read("assets/games/swa/swa_map.txt"));
	assert(swa_map.symbols.at(0x0dbedc) == "DestroyerManagerHandler");
	rec::hints vf_map;
	assert(vf_map.read("assets/games/vf/vf_map.txt"));
	assert(vf_map.symbols.at(0xff49a1) == "SkyScroll");
	rec::hints vr_map;
	assert(vr_map.read("assets/games/vr/vr_map.txt"));
	assert(vr_map.symbols.at(0xfc68ea) == "CoinageSetup");
	rec::hints vr_symbols;
	assert(vr_symbols.read_symbols("assets/games/vr/vr_map.txt"));
	assert(vr_symbols.entries.empty());
	assert(vr_symbols.symbols.at(0xfc68ea) == "CoinageSetup");

	std::filesystem::remove(input);
	std::filesystem::remove(map_output);
	std::filesystem::remove(listing_output);
}
