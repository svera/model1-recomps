#include "../src/host/background_saver.h"
#include <cassert>
#include <chrono>
#include <future>

using namespace std::chrono_literals;

int main()
{
	std::mutex mx;
	std::condition_variable cv;
	bool writing = false, release = false;
	std::vector<std::vector<uint8_t>> written;
	bg_saver saver([&](const std::string &path, const uint8_t *bytes, size_t n) {
		assert(path == "nvram.sav");
		std::unique_lock<std::mutex> l(mx);
		writing = true;
		cv.notify_all();
		cv.wait(l, [&] { return release; });
		written.emplace_back(bytes, bytes + n);
		return true;
	});
	std::vector<uint8_t> ram{1, 2, 3};
	saver.put("nvram.sav", ram.data(), ram.size());
	{
		std::unique_lock<std::mutex> l(mx);
		assert(cv.wait_for(l, 5s, [&] { return writing; }));
	}
	// A blocked disk must not block gameplay or retain pointers to mutable machine RAM.
	ram[0] = 4;
	saver.put("nvram.sav", ram.data(), ram.size());
	ram[0] = 5;
	saver.put("nvram.sav", ram.data(), ram.size());
	ram[0] = 6;

	std::promise<void> waiting;
	auto drained = std::async(std::launch::async, [&] {
		waiting.set_value();
		saver.wait();
	});
	waiting.get_future().wait();
	assert(drained.wait_for(20ms) == std::future_status::timeout);
	{
		std::lock_guard<std::mutex> l(mx);
		release = true;
	}
	cv.notify_all();
	assert(drained.wait_for(5s) == std::future_status::ready);
	drained.get();
	assert((written == std::vector<std::vector<uint8_t>>{{1, 2, 3}, {5, 2, 3}}));

	// Draining for a manual save must not disable later autosaves.
	saver.put("nvram.sav", ram.data(), ram.size());
	saver.finish();
	assert((written.back() == std::vector<uint8_t>{6, 2, 3}));
	assert(written.size() == 3);

	bool finished = false;
	{
		bg_saver on_exit([&](const std::string &, const uint8_t *, size_t) {
			finished = true;
			return true;
		});
		on_exit.put("eeprom.sav", ram.data(), ram.size());
	}
	assert(finished);
	printf("Background saves: nonblocking snapshots, coalescing, drain, reuse and shutdown passed\n");
}
