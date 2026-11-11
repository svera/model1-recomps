#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#ifndef _WIN32
#include <signal.h>
#endif

class frame_recorder
{
public:
	~frame_recorder() { finish(); }

	bool start(const std::string &directory, int width, int height, int fps)
	{
		if (m_video || m_wav) return fail("recording already started");
		std::error_code ec;
		std::filesystem::create_directories(directory, ec);
		if (ec) return fail("couldn't create " + directory + ": " + ec.message());
		m_width = width & ~1;
		m_height = height & ~1;
		m_fps = fps;
		if (m_width < 2 || m_height < 2) return fail("renderer output is too small");
		const std::string output = directory + "/video.mp4";
		const std::string quoted = quote(output);
		if (quoted.empty()) return fail("recording path contains unsupported shell characters");
		const std::string command = "ffmpeg -loglevel error -y -f rawvideo -pixel_format rgba -video_size " +
			std::to_string(m_width) + "x" + std::to_string(m_height) + " -framerate " + std::to_string(m_fps) +
			" -i - -c:v libx264 -preset slow -crf 12 -pix_fmt yuv420p -movflags +faststart " + quoted;
#ifdef _WIN32
		m_video = _popen(command.c_str(), "w");
#else
		m_video = popen(command.c_str(), "w");
#endif
		if (!m_video) return fail("couldn't start ffmpeg");
#ifndef _WIN32
		struct sigaction ignore{};
		ignore.sa_handler = SIG_IGN;
		sigemptyset(&ignore.sa_mask);
		if (sigaction(SIGPIPE, &ignore, &m_old_sigpipe) != 0) {
			const std::string message = std::string("couldn't ignore SIGPIPE: ") + std::strerror(errno);
			finish();
			return fail(message);
		}
		m_sigpipe_ignored = true;
#endif
		m_wav = fopen((directory + "/sound.wav").c_str(), "wb");
		if (!m_wav) {
			finish();
			return fail("couldn't create " + directory + "/sound.wav");
		}
		if (!write_wav_header()) return fail("couldn't write the WAV header");
		return true;
	}

	bool write_frame(SDL_Renderer *renderer)
	{
		if (!m_video || !m_wav) return fail("recording is not active");
		SDL_Surface *raw = SDL_RenderReadPixels(renderer, nullptr);
		if (!raw) return fail(std::string("couldn't read the rendered frame: ") + SDL_GetError());
		SDL_Surface *rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(raw);
		if (!rgba) return fail(std::string("couldn't convert the rendered frame: ") + SDL_GetError());
		if (rgba->w < m_width || rgba->h < m_height) {
			SDL_DestroySurface(rgba);
			return fail("renderer output changed size during recording");
		}
		bool ok = true;
		for (int y = 0; y < m_height && ok; y++)
			ok = fwrite(static_cast<const uint8_t *>(rgba->pixels) + size_t(y) * rgba->pitch, 4, size_t(m_width), m_video) == size_t(m_width);
		SDL_DestroySurface(rgba);
		return ok || fail("ffmpeg stopped accepting video frames");
	}

	bool write_audio(const std::vector<int16_t> &samples)
	{
		if (!m_wav || samples.empty()) return true;
		if (!reserve_audio(samples.size() * 2)) return false;
		std::vector<uint8_t> bytes(samples.size() * 2);
		for (size_t i = 0; i < samples.size(); i++) {
			const uint16_t v = uint16_t(samples[i]);
			bytes[i * 2] = uint8_t(v);
			bytes[i * 2 + 1] = uint8_t(v >> 8);
		}
		if (fwrite(bytes.data(), 1, bytes.size(), m_wav) != bytes.size()) return fail("couldn't write WAV audio");
		m_wav_bytes += uint32_t(bytes.size());
		return true;
	}

	bool write_silence(uint64_t sample_frames)
	{
		static const uint8_t silence[4096] = {};
		if (sample_frames > UINT64_MAX / 4) return fail("silent WAV audio size overflow");
		const uint64_t bytes = sample_frames * 4;
		if (!reserve_audio(bytes)) return false;
		for (uint64_t left = bytes; left;) {
			const size_t n = size_t(std::min<uint64_t>(left, sizeof silence));
			if (fwrite(silence, 1, n, m_wav) != n) return fail("couldn't write silent WAV audio");
			left -= n;
		}
		m_wav_bytes += uint32_t(bytes);
		return true;
	}

	bool finish()
	{
		bool ok = m_error.empty();
		if (m_wav) {
			ok = write_wav_header() && ok;
			if (fclose(m_wav) != 0) ok = fail("couldn't finish WAV audio") && ok;
			m_wav = nullptr;
		}
		if (m_video) {
#ifdef _WIN32
			const int status = _pclose(m_video);
#else
			const int status = pclose(m_video);
#endif
			if (status != 0) ok = fail("ffmpeg did not finish successfully") && ok;
			m_video = nullptr;
		}
#ifndef _WIN32
		if (m_sigpipe_ignored) {
			if (sigaction(SIGPIPE, &m_old_sigpipe, nullptr) != 0) ok = fail("couldn't restore SIGPIPE handling") && ok;
			m_sigpipe_ignored = false;
		}
#endif
		return ok;
	}

	const std::string &error() const { return m_error; }
	bool active() const { return m_video && m_wav && m_error.empty(); }

private:
	FILE *m_video = nullptr, *m_wav = nullptr;
	int m_width = 0, m_height = 0, m_fps = 60;
	uint32_t m_wav_bytes = 0;
	std::string m_error;
#ifndef _WIN32
	struct sigaction m_old_sigpipe{};
	bool m_sigpipe_ignored = false;
#endif

	bool fail(std::string message)
	{
		if (m_error.empty()) m_error = std::move(message);
		return false;
	}

	bool reserve_audio(uint64_t bytes)
	{
		if (!m_wav) return fail("WAV output is not open");
		if (bytes > UINT32_MAX - 36u - m_wav_bytes) return fail("WAV recording exceeds the 4 GiB RIFF limit");
		return true;
	}

	bool write_wav_header()
	{
		if (!m_wav) return false;
		const uint32_t rate = 44100, byte_rate = rate * 4, riff_size = 36 + m_wav_bytes;
		const uint8_t header[44] = {
			'R','I','F','F', uint8_t(riff_size), uint8_t(riff_size >> 8), uint8_t(riff_size >> 16), uint8_t(riff_size >> 24),
			'W','A','V','E','f','m','t',' ', 16,0,0,0, 1,0, 2,0,
			uint8_t(rate), uint8_t(rate >> 8), uint8_t(rate >> 16), uint8_t(rate >> 24),
			uint8_t(byte_rate), uint8_t(byte_rate >> 8), uint8_t(byte_rate >> 16), uint8_t(byte_rate >> 24),
			4,0, 16,0, 'd','a','t','a', uint8_t(m_wav_bytes), uint8_t(m_wav_bytes >> 8),
			uint8_t(m_wav_bytes >> 16), uint8_t(m_wav_bytes >> 24)
		};
		if (fseek(m_wav, 0, SEEK_SET) != 0 || fwrite(header, 1, sizeof header, m_wav) != sizeof header ||
			fseek(m_wav, 0, SEEK_END) != 0) return fail("couldn't update the WAV header");
		return true;
	}

	static std::string quote(const std::string &value)
	{
#ifdef _WIN32
		for (unsigned char c : value)
			if (!(std::isalnum(c) || c == ' ' || c == '.' || c == '_' || c == '-' || c == ':' || c == '\\' || c == '/'))
				return {};
		return "\"" + value + "\"";
#else
		std::string result = "'";
		for (char c : value) {
			if (c == '\'') result += "'\\''";
			else result += c;
		}
		return result + "'";
#endif
	}
};
