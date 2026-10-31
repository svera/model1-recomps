#include "../src/host/audio_recovery.h"
#include <array>
#include <cassert>

int main()
{
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
}
