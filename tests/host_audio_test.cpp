#include "../src/host/audio_recovery.h"
#include "../src/host/audio_frame_clock.h"
#include <array>
#include <cassert>

static void test_frame_clock()
{
	constexpr double hz = 60;
	audio_frame_clock clock(hz);
	auto run_due = [&] {
		int frames = 0;
		while (clock.due()) { clock.advance(); ++frames; }
		assert(clock.fraction() >= 0 && clock.fraction() < 1);
		return frames;
	};
	clock.update(0, 0);
	assert(run_due() == 3);
	clock.update(0, 3);
	assert(run_due() == 0);
	clock.reset();
	clock.update(0);
	assert(run_due() == 0);
	clock.update(1.0 / hz);
	assert(run_due() == 1);
	clock.update(10);
	assert(run_due() == 4);
	clock.reset();
	clock.update(0, 20);
	assert(run_due() == 0);
	clock.update(1, 20);
	assert(run_due() == 0);
	clock.reset();
	clock.update(0, 0);
	assert(run_due() == 3);
	int gulped_frames = 0;
	for (int pass = 0; pass < 240; ++pass) {
		clock.update(1.0 / 120, 0);
		gulped_frames += run_due();
	}
	assert(gulped_frames <= 123);

	// Slightly different playback clocks and occasional long frames must not drain the queue.
	for (const int display_hz : {30, 60, 120})
	for (const double playback_rate : {0.995, 1.0, 1.005}) {
		clock.reset();
		double queued = 0, elapsed = 0;
		int frames = 0;
		clock.update(0, queued);
		queued += run_due();
		for (int pass = 1; pass <= display_hz * 180; ++pass) {
			const double dt = pass % (display_hz * 30) == 0 ? 0.035 : 1.0 / display_hz;
			elapsed += dt;
			queued -= dt * hz * playback_rate;
			if (queued <= 0)
				fprintf(stderr, "Audio underrun: playback=%g pass=%d queued=%g\n", playback_rate, pass, queued);
			assert(queued > 0);
			clock.update(dt, queued);
			const int due = run_due();
			queued += due;
			frames += due;
			assert(queued < 7);
		}
		assert(std::fabs(frames / hz - elapsed) <= elapsed * 0.01 + 4 / hz);
	}
}

int main()
{
	test_frame_clock();
	assert(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
	assert(SDL_Init(SDL_INIT_AUDIO));
	const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
	std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> audio(
		SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr), SDL_DestroyAudioStream);
	assert(audio);
	// Sleep advances the wake clock, but need not advance SDL's game clock or stall its queue.
	audio_wake_clock wake_clock(1000000000ull);
	assert(!wake_clock.update(1010000000ull));
	assert(!wake_clock.update(1510000000ull));
	assert(wake_clock.update(61510000000ull));
	assert(!wake_clock.update(61520000000ull));
	const auto before_wake = SDL_GetAudioStreamDevice(audio.get());
	audio_recovery recovery;
	assert(recovery.update(audio, spec, 0.4f, true, 10000, true));
	assert(audio && SDL_GetAudioStreamDevice(audio.get()) != before_wake);
	assert(!SDL_AudioDevicePaused(SDL_GetAudioStreamDevice(audio.get())));
	assert(SDL_PauseAudioStreamDevice(audio.get()));
	assert(!recovery.update(audio, spec, 0.4f, true, 10000));
	std::array<Sint16, 4800> samples{};
	auto queue = [&] {
		assert(SDL_PutAudioStreamData(audio.get(), samples.data(), sizeof samples));
		recovery.submitted(sizeof samples);
	};
	queue();
	assert(!recovery.update(audio, spec, 0.4f, true, 11999));
	// Consuming queued samples resets the watchdog, even while more samples arrive.
	assert(SDL_GetAudioStreamData(audio.get(), samples.data(), sizeof samples) > 0);
	queue();
	assert(!recovery.update(audio, spec, 0.4f, true, 12000));
	assert(!recovery.update(audio, spec, 0.4f, true, 13999));
	const auto stalled_device = SDL_GetAudioStreamDevice(audio.get());
	assert(recovery.update(audio, spec, 0.4f, true, 14000));
	assert(audio && SDL_GetAudioStreamDevice(audio.get()) != stalled_device);
	assert(SDL_GetAudioStreamQueued(audio.get()) == 0);
	assert(SDL_GetAudioStreamGain(audio.get()) == 0.4f);
	assert(!SDL_AudioDevicePaused(SDL_GetAudioStreamDevice(audio.get())));

	assert(recovery.update(audio, spec, 0.0f, false, 15000, true, true));
	assert(SDL_AudioDevicePaused(SDL_GetAudioStreamDevice(audio.get())));
	assert(SDL_GetAudioStreamGain(audio.get()) == 0.0f);
	queue();
	assert(!recovery.update(audio, spec, 0.0f, false, 20000, false, true));
	assert(!recovery.update(audio, spec, 0.0f, true, 21999));
	assert(recovery.update(audio, spec, 0.0f, true, 22000));
	assert(!SDL_AudioDevicePaused(SDL_GetAudioStreamDevice(audio.get())));
	// An unavailable device is retried rather than leaving playback disabled forever.
	audio.reset();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	assert(recovery.update(audio, spec, 0.4f, true, 24000));
	assert(!audio);
	assert(SDL_InitSubSystem(SDL_INIT_AUDIO));
	assert(!recovery.update(audio, spec, 0.4f, true, 25999));
	assert(recovery.update(audio, spec, 0.4f, true, 26000));
	assert(audio && !SDL_AudioDevicePaused(SDL_GetAudioStreamDevice(audio.get())));
	audio.reset();
	SDL_Quit();
	printf("Audio recovery: sleep-inclusive wake detection, healthy-queue restart, stalled playback, progress, volume, menu pause and device retry passed\n");
	printf("Audio pacing: prebuffer, reset, no-device timing, bounded catch-up, stalled queue and three-minute clock-drift/hitch simulations passed\n");
}
