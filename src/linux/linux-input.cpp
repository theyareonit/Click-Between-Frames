#include <libevdev-1.0/libevdev/libevdev.h> // thankfully this can be put before windows.h, it bugs otherwise
#include <windows.h> // winelib lets you use both windows and linux apis

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>

#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <array>

struct __attribute__((packed)) LinuxInputEvent {
	LARGE_INTEGER time;
	USHORT type;
	USHORT code;
	int value;
};

constexpr size_t BUFFER_SIZE = 20;
constexpr int MAX_EVENTS = 10;

std::atomic<bool> should_quit{false};

void stop(int i) {
    should_quit.store(true);
}

LARGE_INTEGER convert_time(timeval t) { // convert timeval to windows file time
    LARGE_INTEGER wft;
    wft.QuadPart = ((static_cast<ULONGLONG>(t.tv_sec) + 11644473600) * 10000000) + (t.tv_usec * 10); // hopefully wine doesnt change how this is calculated
    return wft;
}

USHORT convert_scan_code(USHORT code) {
    static const std::array<uint16_t, 116 - 96> special_codes = []() {
        std::array<uint16_t, 116 - 96> map{};
        map[96 - 96] = 0xE01C;  // KPENTER
        map[97 - 96] = 0xE01D;  // RIGHTCTRL
        map[98 - 96] = 0xE035;  // KPSLASH
        map[100 - 96] = 0xE038; // RIGHTALT
        map[102 - 96] = 0xE047; // HOME
        map[103 - 96] = 0xE048; // UP
        map[104 - 96] = 0xE049; // PAGEUP
        map[105 - 96] = 0xE04B; // LEFT
        map[106 - 96] = 0xE04D; // RIGHT
        map[107 - 96] = 0xE04F; // END
        map[108 - 96] = 0xE050; // DOWN
        map[109 - 96] = 0xE051; // PAGEDOWN
        map[110 - 96] = 0xE052; // INSERT
        map[111 - 96] = 0xE053; // DELETE
        map[113 - 96] = 0xE020; // MUTE
        map[114 - 96] = 0xE02E; // VOLUMEDOWN
        map[115 - 96] = 0xE030; // VOLUMEUP
        return map;
    }();

    return (code > 96) && (code < 116) ? special_codes[code - 96] : code;
}

DWORD WINAPI gd_watchdog(LPVOID) { // CreateProcess doesn't return a handle for linux-input.exe, so a job object to auto-close linux-input.exe isn't an option
    HANDLE gdMutex = OpenMutex(SYNCHRONIZE, FALSE, "CBFWatchdogMutex");
    if (gdMutex == NULL) {
        std::cerr << "[CBF] Failed to open mutex: " << GetLastError() << std::endl;
        should_quit.store(true);
        return 1;
    }
    WaitForSingleObject(gdMutex, INFINITE);
    ReleaseMutex(gdMutex);
    should_quit.store(true);
    return 0;
}

int main() {
    std::cerr << "[CBF] Linux input program started" << std::endl;
    std::vector<struct libevdev*> devices;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "[CBF] Failed to create epoll instance: " << strerror(errno) << std::endl;
        return 1;
    }

    for (int i = 0;; i++) {
        std::string path = "/dev/input/event" + std::to_string(i);
        int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) break;

        libevdev* dev = nullptr;
        int rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) {
            std::cerr << "[CBF] Failed to create evdev device: " << strerror(-rc) << std::endl;
            close(fd);
            continue;
        }


        int bus = libevdev_get_id_bustype(dev);
        if (bus == BUS_USB || bus == BUS_BLUETOOTH || bus == BUS_I8042) {
            devices.push_back(dev);
            
            epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.ptr = dev;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                std::cerr << "[CBF] Failed to add fd to epoll: " << strerror(errno) << std::endl;
                libevdev_free(dev);
                close(fd);
                continue;
            }
        } else {
            libevdev_free(dev);
            close(fd);
        }
    }

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    HANDLE hSharedMem = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, "LinuxSharedMemory");
    if (hSharedMem == NULL) {
        std::cerr << "[CBF] Failed to open file mapping: " << GetLastError() << std::endl;
        return 1;
    }

    LPVOID pBuf = MapViewOfFile(hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinuxInputEvent) * BUFFER_SIZE);
    if (pBuf == NULL) {
        std::cerr << "[CBF] Failed to map view of file: " << GetLastError() << std::endl;
        CloseHandle(hSharedMem);
        return 1;
    }

    LinuxInputEvent* shared_events = static_cast<LinuxInputEvent*>(pBuf);

    HANDLE hMutex = OpenMutex(SYNCHRONIZE, FALSE, "CBFLinuxMutex");
    if (hMutex == NULL) {
        std::cerr << "[CBF] Failed to open mutex: " << GetLastError() << std::endl;
        UnmapViewOfFile(pBuf);
        CloseHandle(hSharedMem);
        return 1;
    }

    if (devices.empty()) {
        std::cerr << "[CBF] No input devices" << std::endl;
        close(epoll_fd);

        DWORD waitResult = WaitForSingleObject(hMutex, 1000);
        if (waitResult == WAIT_OBJECT_0) {
            shared_events[0].type = 3; // cant access input devices
            ReleaseMutex(hMutex);
        }
        else if (waitResult != WAIT_TIMEOUT) {
            std::cerr << "[CBF] Failed to acquire mutex: " << GetLastError() << std::endl;
        }

        return 1;
    }

    std::cerr << "[CBF] Waiting for input events" << std::endl;
    CreateThread(NULL, 0, gd_watchdog, NULL, 0, NULL);

    epoll_event events[MAX_EVENTS];

    while (!should_quit.load()) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
        if (nfds == -1) {
            if (errno == EINTR) continue; // timeout
            std::cerr << "[CBF] Failed to epoll_wait: " << strerror(errno) << std::endl;
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            struct libevdev* dev = static_cast<struct libevdev*>(events[n].data.ptr);
            struct input_event ev;
            
            while (libevdev_has_event_pending(dev)) {
                int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                if (rc != -EAGAIN && rc != 0) {
                    std::cerr << "[CBF] Error reading event: " << strerror(-rc) << std::endl;
                    break;
                }
                if (ev.type == EV_KEY && ev.value != 2) { // Exclude autorepeat
                    LARGE_INTEGER time = convert_time(ev.time);
                    USHORT code;

                    if (libevdev_has_event_type(dev, EV_REL)) code = ev.code + 0x3000; // if mouse
                    else code = convert_scan_code(ev.code);

                    DWORD waitResult = WaitForSingleObject(hMutex, 1000);
                    if (waitResult == WAIT_OBJECT_0) {
                        for (int i = 0; i < BUFFER_SIZE; i++) {
                            if (shared_events[i].type == 0) { // if there is room in the buffer
                                shared_events[i].time = time;
                                shared_events[i].type = ev.type;
                                shared_events[i].code = code;
                                shared_events[i].value = ev.value;
                                break;
                            }
                        }
                        ReleaseMutex(hMutex);
                    }
                    else if (waitResult != WAIT_TIMEOUT) {
                        std::cerr << "[CBF] Failed to acquire mutex: " << GetLastError() << std::endl;
                    }
                }
            }
        }
    }

    for (auto dev : devices) {
        int fd = libevdev_get_fd(dev);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        libevdev_free(dev);
        close(fd);
    }

    close(epoll_fd);

    UnmapViewOfFile(pBuf);
    CloseHandle(hSharedMem);
    CloseHandle(hMutex);

    std::cerr << "[CBF] Linux input program exiting" << std::endl;
    return 0;
}
