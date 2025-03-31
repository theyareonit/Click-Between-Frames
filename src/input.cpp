#include "includes.hpp"

std::queue<struct InputEvent> inputQueue;

std::array<std::unordered_set<size_t>, 6> inputBinds;
std::unordered_set<USHORT> heldInputs;

std::mutex inputQueueLock;
std::mutex keybindsLock;

std::atomic<bool> enableRightClick;
bool threadPriority;

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
		inputQueue.emplace(InputEvent{ time, inputType, inputState, player1 });
	}

	return 0;
}

void inputThread() {
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
			input.time = events[i].time;
            input.isPlayer1 = player1;
                
            inputQueue.emplace(input);
        }
		ZeroMemory(events, sizeof(LinuxInputEvent[BUFFER_SIZE]));
        ReleaseMutex(hMutex);
    }
    else if (waitResult != WAIT_TIMEOUT) {
        log::error("WaitForSingleObject failed: {}", GetLastError());
    }
}