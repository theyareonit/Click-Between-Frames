#include <libevdev-1.0/libevdev/libevdev.h>
#include <linux/input-event-codes.h>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/inotify.h>

#include <iostream>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <array>
#include <algorithm>

enum DeviceType : int8_t {
    MOUSE,
    TOUCHPAD,
    KEYBOARD,
    TOUCHSCREEN,
    CONTROLLER,
    UNKNOWN
};

struct __attribute__((packed)) LinuxInputEvent {
    int64_t time;
    uint16_t type;
    uint16_t code;
    int32_t value;
    DeviceType deviceType;
};

constexpr size_t RING_BUFFER_SIZE = 256;

struct __attribute__((packed)) SharedMemory {
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t error_flag;
    volatile uint32_t heartbeat;
    LinuxInputEvent events[RING_BUFFER_SIZE];
};

constexpr int MAX_EVENTS = 10;
constexpr int WATCHDOG_TIMEOUT_SECS = 5;

#define INOTIFY_EVENT_SIZE  ( sizeof (struct inotify_event) )
#define INOTIFY_BUF_LEN     ( 1024 * ( INOTIFY_EVENT_SIZE + 16 ) )

std::atomic<bool> should_quit{false};

void stop(int) {
    should_quit.store(true);
}

int64_t convert_time(timeval t) {
    return ((static_cast<int64_t>(t.tv_sec) + 11644473600LL) * 10000000LL) + (t.tv_usec * 10);
}

uint16_t convert_scan_code(uint16_t code) {
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

void add_input_device(std::string path, int epoll_fd, std::vector<struct libevdev*> &devices, std::vector<std::string> &devices_paths){
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        // We ignore errno if its 2 because when a device is disconnected, an IN_ATTRIB signal will still be sent,
        // causing it to try to add the now deleted device. And we ignore errno 13 because it means that the IN_ATTRIB
        // signal that we catched is not the right one and we can't access the device yet. More information below.
        if(errno == 2 || errno == 13) return;
        std::cerr << "[CBF] Failed to open " << path << ": " << strerror(errno) << std::endl;
        return;
    }

    libevdev* dev = nullptr;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        std::cerr << "[CBF] Failed to create evdev device for " << path << ": " << strerror(-rc) << std::endl;
        close(fd);
        return;
    }

    int bus = libevdev_get_id_bustype(dev);
    if (bus == BUS_USB || bus == BUS_BLUETOOTH || bus == BUS_I8042 || bus == BUS_VIRTUAL) {
        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = dev;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            std::cerr << "[CBF] Failed to add fd to epoll for " << path << ": " << strerror(errno) << std::endl;
            libevdev_free(dev);
            close(fd);
            return;
        }

        devices.push_back(dev);
        devices_paths.push_back(path);
        std::cerr << "[CBF] Added device: " << path << std::endl;
    } else {
        libevdev_free(dev);
        close(fd);
    }
}

void remove_input_device(std::string path, std::vector<struct libevdev*> &devices, std::vector<std::string> &devices_paths){
    auto finder = std::find(devices_paths.begin(), devices_paths.end(), path);
    int index = std::distance(devices_paths.begin(), finder);
    if(finder == devices_paths.end()){
        std::cerr << "[CBF] Input device scheduled to be removed was not found." << std::endl;
        return;
    }

    close(libevdev_get_fd(devices[index]));
    libevdev_free(devices[index]);
    devices.erase(devices.begin() + index);
    devices_paths.erase(devices_paths.begin() + index);

    std::cerr << "[CBF] Removed device: " << path << std::endl;
}

int32_t normalize_axis(struct libevdev* dev, int code, int val, int min, int max) {
    int abs_min = libevdev_get_abs_minimum(dev, code);
    int abs_max = libevdev_get_abs_maximum(dev, code);
    float normalized = static_cast<float>(val - abs_min) / static_cast<float>(abs_max - abs_min);
    int32_t scaled = static_cast<int32_t>(normalized * (max - min)) + min;
    return scaled;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "[CBF] Usage: linux-input <shm_path>" << std::endl;
        return 1;
    }

    std::string shm_path = argv[1];
    std::cerr << "[CBF] Linux input program started, shm: " << shm_path << std::endl;

    int shm_fd = open(shm_path.c_str(), O_RDWR);
    if (shm_fd == -1) {
        std::cerr << "[CBF] Failed to open shared memory: " << strerror(errno) << std::endl;
        return 1;
    }

    SharedMemory* shm = static_cast<SharedMemory*>(
        mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    close(shm_fd);

    if (shm == MAP_FAILED) {
        std::cerr << "[CBF] Failed to mmap shared memory: " << strerror(errno) << std::endl;
        return 1;
    }

    std::vector<struct libevdev*> devices;
    // To my knowledge, there is not a proper way to access a device's path using libevdev, so we need to store
    // their paths in a separate vector.
    std::vector<std::string> devices_paths;

    const char* input_dir = "/dev/input/";

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "[CBF] Failed to create epoll instance: " << strerror(errno) << std::endl;
        munmap(shm, sizeof(SharedMemory));
        return 1;
    }

    int inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0){
        std::cerr << "[CBF] Failed to create inotify instance: " << strerror(errno) << std::endl;
        munmap(shm, sizeof(SharedMemory));
        return 1;
    }

    int inotify_watch = inotify_add_watch(inotify_fd, input_dir, IN_DELETE | IN_ATTRIB);
    if(inotify_watch < 0){
        std::cerr << "[CBF] Failed to create an inotify watch: " << strerror(errno) << std::endl;
        munmap(shm, sizeof(SharedMemory));
        return 1;
    }
    char inotify_buffer[INOTIFY_BUF_LEN];

    DIR* dir = opendir(input_dir);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename(entry->d_name);
        if (filename.find("event") == 0) {
            std::string path = std::string(input_dir) + filename;
            add_input_device(path, epoll_fd, devices, devices_paths);
        }
    }
    closedir(dir);

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    if (devices.empty()) {
        std::cerr << "[CBF] No input devices" << std::endl;
        shm->error_flag = 3;
        close(epoll_fd);
        inotify_rm_watch(inotify_fd, inotify_watch);
        close(inotify_fd);
        munmap(shm, sizeof(SharedMemory));
        return 1;
    }

    std::cerr << "[CBF] Waiting for input events" << std::endl;

    uint32_t last_heartbeat = shm->heartbeat;
    bool heartbeat_started = false;
    struct timespec last_heartbeat_time;
    clock_gettime(CLOCK_MONOTONIC, &last_heartbeat_time);

    epoll_event events[MAX_EVENTS];

    while (!should_quit.load()) {

        // Watchdog: exit if GD stops updating the heartbeat.
        // Don't start counting until GD has incremented the heartbeat at least once,
        // since GD may take a long time to finish loading.
        uint32_t current_heartbeat = shm->heartbeat;
        if (current_heartbeat != last_heartbeat) {
            last_heartbeat = current_heartbeat;
            clock_gettime(CLOCK_MONOTONIC, &last_heartbeat_time);
            heartbeat_started = true;
        } else if (heartbeat_started) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_secs = now.tv_sec - last_heartbeat_time.tv_sec;
            if (elapsed_secs >= WATCHDOG_TIMEOUT_SECS) {
                std::cerr << "[CBF] GD heartbeat timeout, exiting" << std::endl;
                break;
            }
        }

        int inotify_len = read(inotify_fd, inotify_buffer, INOTIFY_BUF_LEN);
        if(inotify_len > 0){
            int i = 0;

            while(i < inotify_len){
                struct inotify_event *event = ( struct inotify_event * ) &inotify_buffer[ i ];

                if(event->len) {
                    i+= INOTIFY_EVENT_SIZE + event->len;

                    std::string device_name = std::string(event->name);
                    std::string path = std::string(input_dir) + device_name;
                    if(device_name.find("event") != 0) continue;

                    // This signal is sent whenever a file (in this case, device) attributes are modified.
                    // We call the add_input_device function in here and not in IN_CREATE because we
                    // cannot access the device inmediatly after creation, and we have to wait for the proper
                    // IN_ATTRIB signal (it doesn't neccesarily have to be the first one).
                    if(event->mask & IN_ATTRIB) {
                        add_input_device(path, epoll_fd, devices, devices_paths);
                    }
                    // This is called when a device is disconnected. Before IN_DELETE is called, IN_ATTRIB is also
                    // called, but we ignore that signal with the conditional found in add_input_device.
                    else if(event->mask & IN_DELETE){
                        remove_input_device(path, devices, devices_paths);
                    }
                }
            }
        }

        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::cerr << "[CBF] Failed to epoll_wait: " << strerror(errno) << std::endl;
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            struct libevdev* dev = static_cast<struct libevdev*>(events[n].data.ptr);
            struct input_event ev;

            while (libevdev_has_event_pending(dev)) {
                int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                if (rc != -EAGAIN && rc != 0) {
                    if (rc == -ENODEV) break;

                    std::cerr << "[CBF] Error reading event: " << strerror(-rc) << std::endl;
                    break;
                }

                int64_t time = convert_time(ev.time);
                uint16_t code = ev.code;
                int32_t value = ev.value;
                DeviceType device_type;

                if (ev.type != EV_ABS && (ev.type != EV_KEY || ev.value == 2)) {
                    continue;
                }

                if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT) {
                    device_type = MOUSE;
                }
                else if (libevdev_has_event_code(dev, EV_KEY, KEY_1)) {
                    device_type = KEYBOARD;
                    code = convert_scan_code(ev.code);
                }
                else if (libevdev_has_property(dev, INPUT_PROP_DIRECT)) {
                    device_type = TOUCHSCREEN;
                }
                else if (libevdev_has_property(dev, INPUT_PROP_BUTTONPAD)) {
                    device_type = TOUCHPAD;
                }
                else if (libevdev_has_event_code(dev, EV_KEY, BTN_GAMEPAD)) {
                    device_type = CONTROLLER;
                    if (ev.type == EV_ABS) {
                        if (ev.code == ABS_Z || ev.code == ABS_RZ) value = normalize_axis(dev, code, value, 0, 255);
                        else value = normalize_axis(dev, code, value, -32768, 32767);
                    }
                }
                else {
                    device_type = UNKNOWN;
                }

                uint32_t h = shm->head;
                uint32_t t = shm->tail;
                if (h - t >= RING_BUFFER_SIZE) continue; // buffer full, drop event

                LinuxInputEvent& slot = shm->events[h & (RING_BUFFER_SIZE - 1)];
                slot.time = time;
                slot.type = ev.type;
                slot.code = code;
                slot.value = value;
                slot.deviceType = device_type;
                std::atomic_thread_fence(std::memory_order_release);
                shm->head = h + 1;
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
    inotify_rm_watch(inotify_fd, inotify_watch);
    close(inotify_fd);
    munmap(shm, sizeof(SharedMemory));
    unlink(shm_path.c_str());

    std::cerr << "[CBF] Linux input program exiting" << std::endl;
    return 0;
}
