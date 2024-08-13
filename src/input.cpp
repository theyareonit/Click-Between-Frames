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
	bool player;

	QueryPerformanceCounter(&time);
	
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
			USHORT vkey = raw->data.keyboard.VKey;
			inputState = raw->data.keyboard.Flags & RI_KEY_BREAK;

			if (vkey >= VK_NUMPAD0 && vkey <= VK_NUMPAD9) vkey -= 0x30; // make numpad numbers work with customkeybinds

			// cocos2d::enumKeyCodes corresponds directly to vkeys
			if (heldInputs.contains(vkey)) {
				if (!inputState) return 0;
				else heldInputs.erase(vkey);
			}
			
			bool shouldEmplace = true;
			player = Player1;

			std::array<std::unordered_set<size_t>, 6> binds;
			{
				std::lock_guard lock(keybindsLock);
				binds = inputBinds;
			}

			if (binds[p1Jump].contains(vkey)) inputType = PlayerButton::Jump;
			else if (binds[p1Left].contains(vkey)) inputType = PlayerButton::Left;
			else if (binds[p1Right].contains(vkey)) inputType = PlayerButton::Right;
			else {
				player = Player2;
				if (binds[p2Jump].contains(vkey)) inputType = PlayerButton::Jump;
				else if (binds[p2Left].contains(vkey)) inputType = PlayerButton::Left;
				else if (binds[p2Right].contains(vkey)) inputType = PlayerButton::Right;
				else shouldEmplace = false;
			}

			if (!inputState) heldInputs.emplace(vkey);
			if (!shouldEmplace) return 0;

			break;
		}
		case RIM_TYPEMOUSE: {
			USHORT flags = raw->data.mouse.usButtonFlags;
			bool shouldEmplace = true;

			player = Player1;
			inputType = PlayerButton::Jump;

			if (flags & RI_MOUSE_BUTTON_1_DOWN) inputState = Press;
			else if (flags & RI_MOUSE_BUTTON_1_UP) inputState = Release;
			else {
				player = Player2;
				if (!enableRightClick.load()) return 0;
				if (flags & RI_MOUSE_BUTTON_2_DOWN) inputState = Press;
				else if (flags & RI_MOUSE_BUTTON_2_UP) inputState = Release;
				else return 0;
			}
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
		inputQueue.emplace(InputEvent{ time, inputType, inputState, player });
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
	}
}