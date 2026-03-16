#pragma once

#include <Geode/Geode.hpp>
#include "linuxeventcodes.hpp"

enum DeviceType : int8_t {
    MOUSE,
    TOUCHPAD,
    KEYBOARD,
    TOUCHSCREEN,
    CONTROLLER,
    UNKNOWN
};

struct __attribute__((packed)) LinuxInputEvent {
    LARGE_INTEGER time;
    USHORT type;
    USHORT code;
    int value;
    DeviceType deviceType;
};

extern LARGE_INTEGER freq;

extern HANDLE hSharedMem;
extern HANDLE hMutex;
extern LPVOID pBuf;

extern bool linuxNative;

constexpr size_t BUFFER_SIZE = 20;

void windowsSetup();
void linuxCheckInputs();