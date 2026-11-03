#pragma once
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Own snapshots, never live machine state. Coalesce pending writes to the same file.
class bg_saver {
	using writer = std::function<bool(const std::string &, const uint8_t *, size_t)>;
	writer write;
	std::mutex mx;
	std::condition_variable cv;
	std::map<std::string, std::vector<uint8_t>> jobs;
	bool stop = false, writing = false;
	std::thread th;

	void run()
	{
		std::unique_lock<std::mutex> l(mx);
		for (;;) {
			cv.wait(l, [&] { return stop || !jobs.empty(); });
			if (jobs.empty()) return;
			auto todo = std::move(jobs);
			jobs.clear();
			writing = true;
			l.unlock();
			for (auto &[path, bytes] : todo)
				if (!write(path, bytes.data(), bytes.size())) fprintf(stderr, "couldn't save %s\n", path.c_str());
			l.lock();
			writing = false;
			cv.notify_all();
		}
	}

public:
	explicit bg_saver(writer write) : write(std::move(write)), th([this] { run(); }) {}

	void put(const std::string &path, const void *src, size_t n)
	{
		{
			std::lock_guard<std::mutex> l(mx);
			if (stop) {
				fprintf(stderr, "couldn't queue save %s: writer has stopped\n", path.c_str());
				return;
			}
			jobs[path].assign(static_cast<const uint8_t *>(src), static_cast<const uint8_t *>(src) + n);
		}
		cv.notify_all();
	}

	// Drain before a synchronous save so an older snapshot cannot overwrite it.
	void wait()
	{
		std::unique_lock<std::mutex> l(mx);
		cv.wait(l, [&] { return jobs.empty() && !writing; });
	}

	void finish()
	{
		{ std::lock_guard<std::mutex> l(mx); stop = true; }
		cv.notify_all();
		if (th.joinable()) th.join();
	}

	~bg_saver() { finish(); }
};
