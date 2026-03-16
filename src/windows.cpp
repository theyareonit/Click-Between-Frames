#include "includes.hpp"

LARGE_INTEGER freq;

LPVOID pBuf;
HANDLE hSharedMem = NULL;
HANDLE hMutex = NULL;

// notify the player if theres an issue with input on Linux
#include <Geode/modify/CreatorLayer.hpp>
class $modify(CreatorLayer) {
	bool init() {
		if (!CreatorLayer::init()) return false;

		if (linuxNative) {
			DWORD waitResult = WaitForSingleObject(hMutex, 5);
			if (waitResult == WAIT_OBJECT_0) {
				if (static_cast<LinuxInputEvent*>(pBuf)[0].type == 3 && !softToggle) {
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
	static std::unordered_map<int, enumKeyCodes> linuxToCCKey = {
		{ BTN_A, CONTROLLER_A },
		{ BTN_B, CONTROLLER_B },
		{ BTN_X, CONTROLLER_X },
		{ BTN_Y, CONTROLLER_Y },
		{ BTN_TL, CONTROLLER_LB },
		{ BTN_TR, CONTROLLER_RB },
		{ BTN_SELECT, CONTROLLER_Back },
		{ BTN_START, CONTROLLER_Start },
	};

	DWORD waitResult = WaitForSingleObject(hMutex, 1);
	if (waitResult == WAIT_OBJECT_0) {
		LinuxInputEvent* events = static_cast<LinuxInputEvent*>(pBuf);
		for (int i = 0; i < BUFFER_SIZE; i++) {
			if (events[i].type == 0) break; // if there are no more events

			PlayerButtonCommand input;
			bool player1 = true;
			USHORT scanCode = events[i].code;
			int value = events[i].value;

			switch (events[i].deviceType) {
			case MOUSE:
			case TOUCHPAD:
				if (scanCode == BUTTON_LEFT) {
					input.m_button = PlayerButton::Jump;
				}
				else if (scanCode == BUTTON_RIGHT) {
					if (!enableRightClick) continue;
					input.m_button = PlayerButton::Jump;
					player1 = false;
				}
				break;
			case KEYBOARD: {
				USHORT keyCode = MapVirtualKeyExA(scanCode, MAPVK_VSC_TO_VK, GetKeyboardLayout(0));
				if (inputBinds[p1Jump].contains(keyCode)) input.m_button = PlayerButton::Jump;
				else if (inputBinds[p1Left].contains(keyCode)) input.m_button = PlayerButton::Left;
				else if (inputBinds[p1Right].contains(keyCode)) input.m_button = PlayerButton::Right;
				else {
					player1 = false;
					if (inputBinds[p2Jump].contains(keyCode)) input.m_button = PlayerButton::Jump;
					else if (inputBinds[p2Left].contains(keyCode)) input.m_button = PlayerButton::Left;
					else if (inputBinds[p2Right].contains(keyCode)) input.m_button = PlayerButton::Right;
					else continue;
				}
				break;
			}
			case TOUCHSCREEN:
				if (scanCode == BTN_TOUCH) { // touching screen
					input.m_button = PlayerButton::Jump;
				}
				break;
			case CONTROLLER: {
				int keyCode = -1;
				if (events[i].type == EV_KEY) {
					keyCode = linuxToCCKey[scanCode];
				} else if (events[i].type == EV_ABS) {
					bool continueLoop = false;
					auto analyze4Directions = [&] (int deadzone, enumKeyCodes negative, enumKeyCodes positive) {
						if (events[i].value < -deadzone) {
							keyCode = negative;
							if (heldInputs.contains(negative)) {
								continueLoop = true; // already held, ignore
							}
							value = Press;
						} else if (events[i].value > deadzone) {
							keyCode = positive;
							if (heldInputs.contains(positive)) {
								continueLoop = true; // already held, ignore
							}
							value = Press;
						} else {
							value = Release;
							if (heldInputs.contains(negative)) {
								keyCode = negative;
							} else if (heldInputs.contains(positive)) {
								keyCode = positive;
							} else {
								continueLoop = true; // continue cuz button was already released
							}
						}
					};

					switch (events[i].code) {
					case ABS_X: // left thumbstick x
						analyze4Directions(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, CONTROLLER_LTHUMBSTICK_LEFT, CONTROLLER_LTHUMBSTICK_RIGHT);
						break;
					case ABS_Y: // left thumbstick y
						analyze4Directions(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, CONTROLLER_LTHUMBSTICK_UP, CONTROLLER_LTHUMBSTICK_DOWN);
						break;
					case ABS_RX: // right thumbstick x
						analyze4Directions(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, CONTROLLER_RTHUMBSTICK_LEFT, CONTROLLER_RTHUMBSTICK_RIGHT);
						break;
					case ABS_RY: // right thumbstick y
						analyze4Directions(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, CONTROLLER_RTHUMBSTICK_UP, CONTROLLER_RTHUMBSTICK_DOWN);
						break;
					case ABS_HAT0X: // dpadx
						analyze4Directions(10, CONTROLLER_Left, CONTROLLER_Right);
						break;
					case ABS_HAT0Y: // dpady
						analyze4Directions(10, CONTROLLER_Up, CONTROLLER_Down);
						break;
					case ABS_Z:
						keyCode = CONTROLLER_LT;
						if (events[i].value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
							value = Press;
						} else {
							value = Release;
						}
						break;
					case ABS_RZ:
						keyCode = CONTROLLER_RT;
						if (events[i].value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
							value = Press;
						} else {
							value = Release;
						}
						break;
					}
					if (continueLoop) continue;
				}
				if (inputBinds[p1Jump].contains(keyCode)) input.m_button = PlayerButton::Jump;
				else if (inputBinds[p1Left].contains(keyCode)) input.m_button = PlayerButton::Left;
				else if (inputBinds[p1Right].contains(keyCode)) input.m_button = PlayerButton::Right;
				else {
					player1 = false;
					if (inputBinds[p2Jump].contains(keyCode)) input.m_button = PlayerButton::Jump;
					else if (inputBinds[p2Left].contains(keyCode)) input.m_button = PlayerButton::Left;
					else if (inputBinds[p2Right].contains(keyCode)) input.m_button = PlayerButton::Right;
					else continue;
				}
				if (value == Press) {
					if (heldInputs.contains(keyCode)) {
						continue; // already held, ignore
					} else {
						heldInputs.emplace(keyCode);
					}
				} else {
					if (!heldInputs.contains(keyCode)) {
						continue; // already released, ignore
					} else {
						heldInputs.erase(keyCode);
					}
				}
				break;
			}
			default:
				continue;
			}

			input.m_isPush = value;
			input.m_timestamp = (double)events[i].time.QuadPart / (double)freq.QuadPart;
			input.m_isPlayer2 = !player1;

			inputVector.emplace_back(input);
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
	QueryPerformanceFrequency(&freq);

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
}
