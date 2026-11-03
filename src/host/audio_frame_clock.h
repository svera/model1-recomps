#pragma once
#include <algorithm>
#include <cmath>

class audio_frame_clock {
	static constexpr double QUEUED = 3;
	double hz;
	double pending = 0, queue_avg = QUEUED, lead = 0;
	bool prime = true;

public:
	explicit audio_frame_clock(double frame_hz) : hz(frame_hz) {}

	void reset()
	{
		pending = 0;
		queue_avg = QUEUED;
		lead = 0;
		prime = true;
	}

	// queued is measured in game frames; negative means playback is unavailable.
	void update(double elapsed, double queued = -1)
	{
		double rate = hz;
		if (queued >= 0) {
			if (prime) {
				pending += std::max(0.0, QUEUED - queued);
				lead = QUEUED;
				prime = false;
			}
			queue_avg += (queued - queue_avg) * std::min(1.0, elapsed / 0.5);
			rate *= 1 - std::clamp((queue_avg - QUEUED) * 0.005, -0.01, 0.01);
			// A stalled or slow device must not accumulate unbounded audio latency.
			if (queued > hz * 0.3) {
				pending = std::min(pending, 0.999);
				return;
			}
		} else prime = true;
		pending = std::min(4.0, pending + elapsed * rate);
		lead = std::max(0.0, lead - elapsed * hz * 0.01);
		if (queued >= 0 && queued < QUEUED - 0.5) {
			// Refill a nearly empty queue, but bound the lead if a device gulps samples after wake.
			const double extra = std::min(std::max(0.0, std::ceil(QUEUED - queued) - std::floor(pending)),
				std::max(0.0, std::floor(QUEUED - lead)));
			pending = std::min(4.0, pending + extra);
			lead += extra;
		}
	}

	bool due() const { return pending >= 1; }
	void advance() { pending -= 1; }
	double fraction() const { return std::clamp(pending, 0.0, 1.0); }
	double until_next() const { return std::max(0.0, 1 - pending) / hz; }
};
