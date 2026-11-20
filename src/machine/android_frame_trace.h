#pragma once

#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
#include <android/trace.h>
#endif

class android_frame_trace
{
public:
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	explicit android_frame_trace(const char *name) : active(name && ATrace_isEnabled())
	{
		if (active) ATrace_beginSection(name);
	}
	~android_frame_trace() { if (active) ATrace_endSection(); }
#else
	explicit android_frame_trace(const char *) {}
#endif
	android_frame_trace(const android_frame_trace &) = delete;
	android_frame_trace &operator=(const android_frame_trace &) = delete;

private:
#if defined(__ANDROID__) && defined(M1_ALWAYS_FRAME_TIMINGS)
	bool active;
#endif
};
