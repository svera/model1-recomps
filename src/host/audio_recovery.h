#pragma once
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

class audio_wake_clock {
	Uint64 last;

public:
	static Uint64 ticks()
	{
#ifdef __APPLE__
		// SDL's mach_absolute_time clock excludes sleep; the continuous clock includes it.
		static const auto timebase = [] {
			mach_timebase_info_data_t info{};
			if (mach_timebase_info(&info) != KERN_SUCCESS || !info.denom) {
				SDL_LogCritical(SDL_LOG_CATEGORY_AUDIO, "Cannot read the sleep-inclusive clock's timebase");
				std::abort();
			}
			return info;
		}();
		return Uint64(static_cast<long double>(mach_continuous_time()) * timebase.numer / timebase.denom);
#else
		return SDL_GetTicksNS();
#endif
	}

	explicit audio_wake_clock(Uint64 now = ticks()) : last(now) {}

	bool update(Uint64 now = ticks())
	{
		const bool gap = now - last > 500000000ull;
		last = now;
		return gap;
	}
};

class audio_recovery {
	int expected = 0;
	Uint64 progress_at = 0, retry_at = 0;

public:
	void submitted(int bytes) { expected += bytes; }

	bool update(std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> &audio,
				const SDL_AudioSpec &spec, float gain, bool active, Uint64 now, bool restart = false, bool paused = false)
	{
		const int queued = audio ? SDL_GetAudioStreamQueued(audio.get()) : 0;
		if (!active || queued == 0 || queued < expected) progress_at = now;
		expected = queued;
		if (!restart && audio && (!active || now - progress_at < 2000)) return false;
		if (!restart && now < retry_at) return false;
		fprintf(stderr, "sound: %s; reopening the default playback device\n",
			restart ? "resuming after suspension" : audio ? "playback stalled" : "retrying playback");
		// Close first: opening another logical device can otherwise reuse the stalled physical device.
		audio.reset();
		audio.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
		if (audio && (!SDL_SetAudioStreamGain(audio.get(), gain) ||
					  (!paused && !SDL_ResumeAudioStreamDevice(audio.get())))) audio.reset();
		if (!audio) fprintf(stderr, "sound: cannot restart playback: %s (will retry)\n", SDL_GetError());
		expected = 0;
		progress_at = now;
		retry_at = now + 2000;
		return true;
	}
};
