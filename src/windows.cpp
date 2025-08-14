#include "includes.hpp"
#include <geode.custom-keybinds/include/Keybinds.hpp>
// #include "Xinput.h"

TimestampType getCurrentTimestamp() {
	LARGE_INTEGER t;
	if (linuxNative) {
		// used instead of QPC to make it possible to convert between Linux and Windows timestamps
		GetSystemTimePreciseAsFileTime((FILETIME*)&t);
	} else {
		QueryPerformanceCounter(&t);
	}
	return t.QuadPart;
}

LPVOID pBuf;
HANDLE hSharedMem = NULL;
HANDLE hMutex = NULL;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	LARGE_INTEGER time;
	PlayerButton inputType;
	bool inputState;
	bool player1;

	LPVOID pData;
	switch (uMsg) {
	case WM_INPUT: {

		UINT dwSize;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));

		auto lpb = std::unique_ptr<BYTE[]>(new BYTE[dwSize]);
		if (!lpb) {
			return 0;
		}
		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.get(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
			log::debug("GetRawInputData does not return correct size");
		}

		RAWINPUT* raw = (RAWINPUT*)lpb.get();
		switch (raw->header.dwType) {
		case RIM_TYPEKEYBOARD: {
			QueryPerformanceCounter(&time);

			USHORT vkey = raw->data.keyboard.VKey;
			inputState = raw->data.keyboard.Flags & RI_KEY_BREAK ? Release : Press;

			if (vkey >= VK_NUMPAD0 && vkey <= VK_NUMPAD9) vkey -= 0x30; // make numpad numbers work with customkeybinds

			// cocos2d::enumKeyCodes corresponds directly to vkeys
			if (heldInputs.contains(vkey)) {
				if (inputState) return 0;
				else heldInputs.erase(vkey);
			}

			bool shouldEmplace = true;
			player1 = true;

			{
				std::lock_guard lock(keybindsLock);

				if (inputBinds[p1Jump].contains(vkey)) inputType = PlayerButton::Jump;
				else if (inputBinds[p1Left].contains(vkey)) inputType = PlayerButton::Left;
				else if (inputBinds[p1Right].contains(vkey)) inputType = PlayerButton::Right;
				else {
					player1 = false;
					if (inputBinds[p2Jump].contains(vkey)) inputType = PlayerButton::Jump;
					else if (inputBinds[p2Left].contains(vkey)) inputType = PlayerButton::Left;
					else if (inputBinds[p2Right].contains(vkey)) inputType = PlayerButton::Right;
					else shouldEmplace = false;
				}
			}

			if (inputState) heldInputs.emplace(vkey);
			if (!shouldEmplace) return 0;

			break;
		}
		case RIM_TYPEMOUSE: {
			USHORT flags = raw->data.mouse.usButtonFlags;
			bool shouldEmplace = true;

			player1 = true;
			inputType = PlayerButton::Jump;

			if (flags & RI_MOUSE_BUTTON_1_DOWN) inputState = Press;
			else if (flags & RI_MOUSE_BUTTON_1_UP) inputState = Release;
			else {
				player1 = false;
				if (!enableRightClick.load()) return 0;
				if (flags & RI_MOUSE_BUTTON_2_DOWN) inputState = Press;
				else if (flags & RI_MOUSE_BUTTON_2_UP) inputState = Release;
				else return 0;

				queueInMainThread([inputState]() {keybinds::InvokeBindEvent("robtop.geometry-dash/jump-p2", inputState).post();});
			}

			QueryPerformanceCounter(&time); // dont call on mouse move events
			break;
		}
		default:
			return 0;
		}
		break;
	}
	default:
		return DefWindowProcA(hwnd, uMsg, wParam, lParam);
	}

	{
		std::lock_guard lock(inputQueueLock);
		inputQueue.emplace_back(InputEvent{ timestampFromLarge(time), inputType, inputState, player1 });
	}

	return 0;
}

void rawInputThread() {
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "CBF";

	RegisterClass(&wc);
	HWND hwnd = CreateWindow("CBF", "Raw Input Window", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, wc.hInstance, 0);
	if (!hwnd) {
		const DWORD err = GetLastError();
		log::error("Failed to create raw input window: {}", err);
		return;
	}

	RAWINPUTDEVICE dev[2];
	dev[0].usUsagePage = 0x01;        // generic desktop controls
	dev[0].usUsage = 0x02;            // mouse
	dev[0].dwFlags = RIDEV_INPUTSINK; // allow inputs without being in the foreground
	dev[0].hwndTarget = hwnd;         // raw input window

	dev[1].usUsagePage = 0x01;
	dev[1].usUsage = 0x06;            // keyboard
	dev[1].dwFlags = RIDEV_INPUTSINK;
	dev[1].hwndTarget = hwnd;

	if (!RegisterRawInputDevices(dev, 2, sizeof(dev[0]))) {
		log::error("Failed to register raw input devices");
		return;
	}

	if (threadPriority) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	MSG msg;
    while (GetMessage(&msg, hwnd, 0, 0)) {
        DispatchMessage(&msg);
        while (softToggle.load()) { // reduce lag while mod is disabled
            Sleep(2000);
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)); // clear all pending messages
        }
    }
}

void xinputThread() {
    const HMODULE xinputLib = LoadLibrary("Xinput1_4.dll");
    if (xinputLib == NULL) {
        log::error("Failed to load Xinput1_4.dll");
        return;
    }
    typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
    const auto XInputGetState = (XInputGetState_t)GetProcAddress(xinputLib, "XInputGetState");

    // cocos2d doesn't match with Xinput
    std::unordered_map<int, enumKeyCodes> xinputToCCKey = {
        { XINPUT_GAMEPAD_DPAD_UP, CONTROLLER_Up },
        { XINPUT_GAMEPAD_DPAD_DOWN, CONTROLLER_Down },
        { XINPUT_GAMEPAD_DPAD_LEFT, CONTROLLER_Left },
        { XINPUT_GAMEPAD_DPAD_RIGHT, CONTROLLER_Right },
        { XINPUT_GAMEPAD_START, CONTROLLER_Start },
        { XINPUT_GAMEPAD_BACK, CONTROLLER_Back },
        { XINPUT_GAMEPAD_LEFT_THUMB, CONTROLLER_LT },
        { XINPUT_GAMEPAD_RIGHT_THUMB, CONTROLLER_RT },
        { XINPUT_GAMEPAD_LEFT_SHOULDER, CONTROLLER_LB },
        { XINPUT_GAMEPAD_RIGHT_SHOULDER, CONTROLLER_RB },
        { XINPUT_GAMEPAD_A, CONTROLLER_A },
        { XINPUT_GAMEPAD_B, CONTROLLER_B },
        { XINPUT_GAMEPAD_X, CONTROLLER_X },
        { XINPUT_GAMEPAD_Y, CONTROLLER_Y }
    };

    if (threadPriority) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (true) {
        DWORD dwResult;    
        for (DWORD i = 0; i < XUSER_MAX_COUNT; i++ ) {
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(XINPUT_STATE));
    
            dwResult = XInputGetState(i, &state);
            
            // controller not connected
            if (dwResult != ERROR_SUCCESS) {
                continue;
            }

            for (auto& [xinputButton, ccButton] : xinputToCCKey) {
                bool inputState;
                // button held
                if (state.Gamepad.wButtons & xinputButton) {
                    if (heldInputs.contains(ccButton)) continue; // skip if already held
                    heldInputs.emplace(ccButton);
                    inputState = Press;
                } else {
                    if (!heldInputs.contains(ccButton)) continue; // skip if not held
                    heldInputs.erase(ccButton);
                    inputState = Release;
                }
                LARGE_INTEGER time;
                QueryPerformanceCounter(&time);
                PlayerButton inputType;
                bool player1 = true;

                {
                    std::lock_guard lock(keybindsLock);

                    if (inputBinds[p1Jump].contains(ccButton)) inputType = PlayerButton::Jump;
                    else if (inputBinds[p1Left].contains(ccButton)) inputType = PlayerButton::Left;
                    else if (inputBinds[p1Right].contains(ccButton)) inputType = PlayerButton::Right;
                    else {
                        player1 = false;
                        if (inputBinds[p2Jump].contains(ccButton)) inputType = PlayerButton::Jump;
                        else if (inputBinds[p2Left].contains(ccButton)) inputType = PlayerButton::Left;
                        else if (inputBinds[p2Right].contains(ccButton)) inputType = PlayerButton::Right;
                        else continue;
                    }
                }
                {
                    std::lock_guard lock(inputQueueLock);
                    inputQueue.emplace_back(InputEvent{ timestampFromLarge(time), inputType, inputState, player1 });
                }
            }
        }
        while (softToggle.load()) { // reduce lag while mod is disabled
            Sleep(2000);
        }
    }
}

// notify the player if theres an issue with input on Linux
#include <Geode/modify/CreatorLayer.hpp>
class $modify(CreatorLayer) {
	bool init() {
		if (!CreatorLayer::init()) return false;

		if (linuxNative) {
			DWORD waitResult = WaitForSingleObject(hMutex, 5);
			if (waitResult == WAIT_OBJECT_0) {
				if (static_cast<LinuxInputEvent*>(pBuf)[0].type == 3 && !softToggle.load()) {
					log::error("Linux input failed");
					FLAlertLayer* popup = FLAlertLayer::create(
						"CBF Linux",
						"Failed to read input devices.\nOn most distributions, this can be resolved with the following command: <cr>sudo usermod -aG input $USER</c> (reboot afterward; this will make your system slightly less secure).\nIf the issue persists, please contact the mod developer.",
						"OK"
					);
					popup->m_scene = this;
					popup->show();
				}
				ReleaseMutex(hMutex);
			}
			else if (waitResult == WAIT_TIMEOUT) {
				log::error("Mutex stalling");
			}
			else {
				// log::error("CreatorLayer WaitForSingleObject failed: {}", GetLastError());
			}
		}
		return true;
	}
};

void linuxCheckInputs() {
	DWORD waitResult = WaitForSingleObject(hMutex, 1);
	if (waitResult == WAIT_OBJECT_0) {
		LinuxInputEvent* events = static_cast<LinuxInputEvent*>(pBuf);
		for (int i = 0; i < BUFFER_SIZE; i++) {
			if (events[i].type == 0) break; // if there are no more events

			InputEvent input;
			bool player1 = true;

			USHORT scanCode = events[i].code;
			if (scanCode == 0x3110) { // left click
				input.inputType = PlayerButton::Jump;
			}
			else if (scanCode == 0x3111) { // right click
				if (!enableRightClick.load()) continue;
				input.inputType = PlayerButton::Jump;
				player1 = false;
			}
			else {
				USHORT keyCode = MapVirtualKeyExA(scanCode, MAPVK_VSC_TO_VK, GetKeyboardLayout(0));
				if (inputBinds[p1Jump].contains(keyCode)) input.inputType = PlayerButton::Jump;
				else if (inputBinds[p1Left].contains(keyCode)) input.inputType = PlayerButton::Left;
				else if (inputBinds[p1Right].contains(keyCode)) input.inputType = PlayerButton::Right;
				else {
					player1 = false;
					if (inputBinds[p2Jump].contains(keyCode)) input.inputType = PlayerButton::Jump;
					else if (inputBinds[p2Left].contains(keyCode)) input.inputType = PlayerButton::Left;
					else if (inputBinds[p2Right].contains(keyCode)) input.inputType = PlayerButton::Right;
					else continue;
				}
			}

			input.inputState = events[i].value;
			input.time = timestampFromLarge(events[i].time);
			input.isPlayer1 = player1;

			inputQueue.emplace_back(input);
		}
		ZeroMemory(events, sizeof(LinuxInputEvent[BUFFER_SIZE]));
		ReleaseMutex(hMutex);
	}
	else if (waitResult != WAIT_TIMEOUT) {
		log::error("WaitForSingleObject failed: {}", GetLastError());
	}
}

void windowsSetup() {
	HANDLE gdMutex;

	HMODULE ntdll = GetModuleHandle("ntdll.dll");
	typedef void (*wine_get_host_version)(const char **sysname, const char **release);
	wine_get_host_version wghv = (wine_get_host_version)GetProcAddress(ntdll, "wine_get_host_version");
	if (wghv) { // if this function exists, the user is on Wine
		const char* sysname;
		const char* release;
		wghv(&sysname, &release);

		std::string sys = sysname;
		log::info("Wine {}", sys);

		if (sys == "Linux") Mod::get()->setSavedValue<bool>("you-must-be-on-linux-to-change-this", true);
		if (sys == "Linux" && Mod::get()->getSettingValue<bool>("wine-workaround")) { // background raw keyboard input doesn't work in Wine
			linuxNative = true;
			log::info("Linux native");

			hSharedMem = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(LinuxInputEvent[BUFFER_SIZE]), "LinuxSharedMemory");
			if (hSharedMem == NULL) {
				log::error("Failed to create file mapping: {}", GetLastError());
				return;
			}

			pBuf = MapViewOfFile(hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinuxInputEvent[BUFFER_SIZE]));
			if (pBuf == NULL) {
				log::error("Failed to map view of file: {}", GetLastError());
				CloseHandle(hSharedMem);
				return;
			}

			hMutex = CreateMutex(NULL, FALSE, "CBFLinuxMutex"); // used to gate access to the shared memory buffer for inputs
			if (hMutex == NULL) {
				log::error("Failed to create shared memory mutex: {}", GetLastError());
				CloseHandle(hSharedMem);
				return;
			}

			gdMutex = CreateMutex(NULL, TRUE, "CBFWatchdogMutex"); // will be released when gd closes
			if (gdMutex == NULL) {
				log::error("Failed to create watchdog mutex: {}", GetLastError());
				CloseHandle(hMutex);
				CloseHandle(hSharedMem);
				return;
			}

			SECURITY_ATTRIBUTES sa;
			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.bInheritHandle = TRUE;
			sa.lpSecurityDescriptor = NULL;

			STARTUPINFO si;
			PROCESS_INFORMATION pi;
			ZeroMemory(&si, sizeof(si));
			si.cb = sizeof(si);
			ZeroMemory(&pi, sizeof(pi));

			std::string path = CCFileUtils::get()->fullPathForFilename("linux-input.so"_spr, true);

			if (!CreateProcess(path.c_str(), NULL, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
				log::error("Failed to launch Linux input program: {}", GetLastError());
				CloseHandle(hMutex);
				CloseHandle(gdMutex);
				CloseHandle(hSharedMem);
				return;
			}
		}
	}

	if (!linuxNative) {
		std::thread(rawInputThread).detach();
        if (PlatformToolbox::isControllerConnected()) {
            std::thread(xinputThread).detach();
        }
	}
}
