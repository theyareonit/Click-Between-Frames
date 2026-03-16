#pragma once

#include <Geode/platform/cplatform.h>

using TimestampType = double;

#ifdef GEODE_IS_WINDOWS

#include "windows.hpp"

inline TimestampType getCurrentTimestamp() {
	LARGE_INTEGER t;
	if (linuxNative) {
		// used instead of QPC to make it possible to convert between Linux and Windows timestamps
		GetSystemTimePreciseAsFileTime((FILETIME*)&t);
	} else {
		QueryPerformanceCounter(&t);
	}
	return static_cast<TimestampType>(t.QuadPart) / static_cast<TimestampType>(freq.QuadPart);
}

#elif defined(GEODE_IS_ANDROID)

#include <time.h>

inline TimestampType getCurrentTimestamp() {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	// time as seconds
	return static_cast<TimestampType>(now.tv_sec) + (static_cast<TimestampType>(now.tv_nsec) / 1'000'000'000);
}

#elif defined(GEODE_IS_MAC) || defined(GEODE_IS_IOS)

#include <time.h>

inline TimestampType getCurrentTimestamp() {
	// convert ns to seconds
	return static_cast<TimestampType>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW)) / 1'000'000'000;
}

#endif
