#pragma once

#include <Geode/Geode.hpp>

struct __attribute__((packed)) LinuxInputEvent {
	LARGE_INTEGER time;
	USHORT type;
	USHORT code;
	int value;
};

extern HANDLE hSharedMem;
extern HANDLE hMutex;
extern LPVOID pBuf;

extern bool linuxNative;
extern bool useCustomKeybinds;

inline LARGE_INTEGER largeFromTimestamp(TimestampType t) {
	LARGE_INTEGER res;
	res.QuadPart = t;
	return res;
}

inline TimestampType timestampFromLarge(LARGE_INTEGER l) {
	return l.QuadPart;
}

constexpr size_t BUFFER_SIZE = 20;

void windowsSetup();
void linuxCheckInputs();
void inputThread();