#include <queue>
#include <algorithm>
#include <limits>
#include <Windows.h>
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <geode.custom-keybinds/include/Keybinds.hpp>

using namespace geode::prelude;

enum GameAction : int {
	p1Jump = 0,
	p1Left = 1,
	p1Right = 2,
	p2Jump = 3,
	p2Left = 4,
	p2Right = 5
};

enum Player : bool {
	Player1 = 0,
	Player2 = 1
};

enum State : bool {
	Press = 0,
	Release = 1
};

struct inputEvent {
	LARGE_INTEGER time;
	PlayerButton inputType;
	bool inputState;
	bool player;
};

struct step {
	inputEvent input;
	double deltaFactor;
};

std::queue<struct inputEvent> inputQueue;
std::set<size_t> inputBinds[6];
std::set<USHORT> heldInputs;

CRITICAL_SECTION inputQueueLock;
CRITICAL_SECTION keybindsLock;
bool enableRightClick;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	LARGE_INTEGER time;
	PlayerButton inputType;
	bool inputState;
	bool player;
	
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
			if (heldInputs.contains(vkey)) {
				if (!inputState) return 0;
				else heldInputs.erase(vkey);
			}
			
			// cocos2d::enumKeyCodes corresponds directly to vkeys
			bool shouldEmplace = true;
			player = Player1;
			EnterCriticalSection(&keybindsLock);
			if (inputBinds[p1Jump].contains(vkey)) inputType = PlayerButton::Jump;
			else if (inputBinds[p1Left].contains(vkey)) inputType = PlayerButton::Left;
			else if (inputBinds[p1Right].contains(vkey)) inputType = PlayerButton::Right;
			else {
				player = Player2;
				if (inputBinds[p2Jump].contains(vkey)) inputType = PlayerButton::Jump;
				else if (inputBinds[p2Left].contains(vkey)) inputType = PlayerButton::Left;
				else if (inputBinds[p2Right].contains(vkey)) inputType = PlayerButton::Right;
				else shouldEmplace = false;
			}
			if (!inputState) heldInputs.emplace(vkey);
			LeaveCriticalSection(&keybindsLock);

			if (!shouldEmplace) return 0; // has to be done outside of the critical section
			break;
		}
		case RIM_TYPEMOUSE: {
			USHORT flags = raw->data.mouse.usButtonFlags;
			bool shouldEmplace = true;
			player = Player1;
			inputType = PlayerButton::Jump;

			EnterCriticalSection(&keybindsLock);
			bool rc = enableRightClick;
			LeaveCriticalSection(&keybindsLock);
			if (flags & RI_MOUSE_BUTTON_1_DOWN) inputState = Press;
			else if (flags & RI_MOUSE_BUTTON_1_UP) inputState = Release;
			else {
				player = Player2;
				if (!rc) return 0;
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

	QueryPerformanceCounter(&time);

	EnterCriticalSection(&inputQueueLock);
	inputQueue.emplace(inputEvent{ time, inputType, inputState, player });
	LeaveCriticalSection(&inputQueueLock);
	return 0;
}

void inputThread() {
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "Click Between Frames";

	RegisterClass(&wc);
	HWND hwnd = CreateWindow("Click Between Frames", "Raw Input Window", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, wc.hInstance, 0);
	if (!hwnd) {
		const DWORD err = GetLastError();
		log::error("Failed to create raw input window: {}", err);
		return;
	}

	RAWINPUTDEVICE dev[2];
	dev[0].usUsagePage = 0x01;        // generic desktop controls
	dev[0].usUsage = 0x06;            // keyboard
	dev[0].dwFlags = RIDEV_INPUTSINK; // allow inputs without being in the foreground
	dev[0].hwndTarget = hwnd;         // raw input window

	dev[1].usUsagePage = 0x01;
	dev[1].usUsage = 0x02;            // mouse
	dev[1].dwFlags = RIDEV_INPUTSINK;
	dev[1].hwndTarget = hwnd;

	if (!RegisterRawInputDevices(dev, 2, sizeof(dev[0]))) {
		log::error("Failed to register raw input devices");
		return;
	}

	MSG msg;
	while (GetMessage(&msg, hwnd, 0, 0)) {
		DispatchMessage(&msg);
	}
}

std::queue<struct inputEvent> inputQueueCopy;
std::queue<struct step> stepQueue;
inputEvent nextInput = { 0, 0, PlayerButton::Jump, 0 };
LARGE_INTEGER lastFrameTime;
LARGE_INTEGER lastPhysicsFrameTime;
LARGE_INTEGER currentFrameTime;
int inputQueueSize;
int stepCount;
bool newFrame = true;
bool firstFrame = true;
bool skipUpdate = true;
bool enableInput = false;
bool lateCutoff;
void updateInputQueueAndTime() {
	PlayLayer* playLayer = PlayLayer::get();
	if (!playLayer || GameManager::sharedState()->getEditorLayer() || stepCount == 0 || playLayer->m_player1->m_isDead) {
		enableInput = true;
		firstFrame = true;
		skipUpdate = true;
		return;
	}
	else {
		nextInput = { 0, 0, PlayerButton::Jump, 0, 0 };
		std::queue<struct step>().swap(stepQueue); // shouldnt do anything but just in case
		lastFrameTime = lastPhysicsFrameTime;
		newFrame = true;

		// queryperformancecounter is done within the critical section to prevent a race condition which could cause dropped inputs
		EnterCriticalSection(&inputQueueLock);
		if (lateCutoff) {
			QueryPerformanceCounter(&currentFrameTime);
			inputQueueCopy = inputQueue;
			std::queue<struct inputEvent>().swap(inputQueue);
		}
		else {
			while (!inputQueue.empty() && inputQueue.front().time.QuadPart <= currentFrameTime.QuadPart) {
				inputQueueCopy.push(inputQueue.front());
				inputQueue.pop();
			}
		}
		LeaveCriticalSection(&inputQueueLock);
		lastPhysicsFrameTime = currentFrameTime;

		// on the first frame after entering a level, stepDelta is 0. if you do PlayerObject::update(0) at any point, the player will permanently freeze
		if (!firstFrame) skipUpdate = false;
		else {
			inputQueueSize = 0;
			skipUpdate = true;
			firstFrame = false;
			if (!lateCutoff) std::queue<struct inputEvent>().swap(inputQueueCopy);
			return;
		}

		LARGE_INTEGER deltaTime;
		LARGE_INTEGER stepDelta;
		deltaTime.QuadPart = currentFrameTime.QuadPart - lastFrameTime.QuadPart;
		stepDelta.QuadPart = (deltaTime.QuadPart / stepCount) + 1; // the +1 is to prevent dropped inputs caused by integer division
		inputQueueSize = inputQueueCopy.size();

		constexpr double smallestFloat = std::numeric_limits<float>::min(); // ensures deltaFactor can never be 0, even after being converted to float
		for (int i = 0; i < stepCount; i++) {
			double lastDFactor = 0.0;
			while (true) {
				inputEvent front;
				if (!inputQueueCopy.empty()) {
					front = inputQueueCopy.front();
					if (front.time.QuadPart - lastFrameTime.QuadPart < stepDelta.QuadPart * (i + 1)) {
						double dFactor = static_cast<double>((front.time.QuadPart - lastFrameTime.QuadPart) % stepDelta.QuadPart) / stepDelta.QuadPart;
						stepQueue.emplace(step{ front, std::clamp(dFactor - lastDFactor, smallestFloat, 1.0) });
						lastDFactor = dFactor;
						inputQueueCopy.pop();
						continue;
					}
				}
				front = nextInput;
				stepQueue.emplace(step{ front, std::max(smallestFloat, 1.0 - lastDFactor)});
				break;
			}
		}
	}
}

// this can be done without a midhook if you feel like rewriting the step calculation algorithm
intptr_t prePhysicsReturn;
intptr_t inputQueueSizePtr;

void prePhysicsMidhook() {
    __asm__ __volatile__ (
        "pushfq\n\t"
        "pushq %%rax\n\t"
        "pushq %%rbx\n\t"
        "pushq %%rcx\n\t"
        "pushq %%rdx\n\t"
        "pushq %%rsi\n\t"
        "pushq %%rdi\n\t"
        "pushq %%r8\n\t"
        "pushq %%r9\n\t"
        "pushq %%r10\n\t"
        "pushq %%r11\n\t"
        "callq *%[updateInputQueueAndTime]\n\t" // Use callq for 64-bit, call for 32-bit
        "popq %%r11\n\t"
        "popq %%r10\n\t"
        "popq %%r9\n\t"
        "popq %%r8\n\t"
        "popq %%rdi\n\t"
        "popq %%rsi\n\t"
        "popq %%rdx\n\t"
        "popq %%rcx\n\t"
        "popq %%rbx\n\t"
        "popq %%rax\n\t"
        "movq %[inputQueueSizePtr], %%rsi\n\t"  // Use movq for 64-bit, movl for 32-bit
        "popfq\n\t"
        "movq $0x0, %[returnValue]\n\t"         // Use movq for 64-bit, movl for 32-bit
        "jmpq *%[prePhysicsReturn]\n\t"         // Use jmpq for 64-bit, jmp for 32-bit
        :
        : [returnValue] "m" (prePhysicsReturn), [inputQueueSizePtr] "m" (inputQueueSizePtr),
          [updateInputQueueAndTime] "r" (updateInputQueueAndTime), [prePhysicsReturn] "r" (prePhysicsReturn)
        : "memory", "cc", "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"
    );
}

double deltaFactor = 1.0;
void updateDeltaFactorAndInput() {
	enableInput = false;
	step front = stepQueue.front();
	deltaFactor = front.deltaFactor;
	if (nextInput.time.QuadPart != 0) {
		PlayLayer *playLayer = PlayLayer::get();
		enableInput = true;
		playLayer->handleButton(!nextInput.inputState, (int)nextInput.inputType, !nextInput.player);
		enableInput = false;
	}
	nextInput = front.input;
	stepQueue.pop();
}

// probably possible without a midhook if youre cleverer about it
// i made sure to hook a place in the code where xmm registers dont need to be preserved
intptr_t physicsReturn;
double esp0x3c;
double esp0x4c;
double esp0x44;
float esp0x14;

void physicsMidhook() {
    char edi;  // Assuming edi is declared somewhere else

    long long esp0x3c_, esp0x4c_, esp0x44_;  // Using native long long for intptr_t
    float esp0x14_;

    __asm__ __volatile__ (
        "pushfq\n\t"
        "cmpb $0, %[skipUpdate]\n\t"
        "jnz skipUpdateEnd\n\t"
        "pushq %%rax\n\t"
        "pushq %%rbx\n\t"
        "pushq %%rcx\n\t"
        "pushq %%rdx\n\t"
        "pushq %%rsi\n\t"
        "pushq %%rdi\n\t"
        "pushq %%r8\n\t"
        "pushq %%r9\n\t"
        "pushq %%r10\n\t"
        "pushq %%r11\n\t"
        "callq *%[updateDeltaFactorAndInput]\n\t"
        "popq %%r11\n\t"
        "popq %%r10\n\t"
        "popq %%r9\n\t"
        "popq %%r8\n\t"
        "popq %%rdi\n\t"
        "popq %%rsi\n\t"
        "popq %%rdx\n\t"
        "popq %%rcx\n\t"
        "popq %%rbx\n\t"
        "popq %%rax\n\t"
        "cmpb $0, %[newFrame]\n\t"
        "movb $0, %[newFrame]\n\t"
        "jz multiply\n\t"

        // Copy the value of every stack variable dependent on stepDelta
        "movq %[esp0x3c_], %%rax\n\t"
        "movq %%rax, %[esp0x3c]\n\t"

        "movq %[esp0x4c_], %%rax\n\t"
        "movq %%rax, %[esp0x4c]\n\t"

        "movq %[esp0x44_], %%rax\n\t"
        "movq %%rax, %[esp0x44]\n\t"

        "movss %[esp0x14_], %%xmm0\n\t"
        "movss %%xmm0, %[esp0x14]\n\t"

    "multiply:\n\t"
        "movq %[esp0x3c], %%rax\n\t"
        "mulsd %[deltaFactor], %%xmm0\n\t"
        "movsd %%xmm0, %[esp0x3c]\n\t"

        "movq %[esp0x4c], %%rax\n\t"
        "mulsd %[deltaFactor], %%xmm0\n\t"
        "movsd %%xmm0, %[esp0x4c]\n\t"

        "movq %[esp0x44], %%rax\n\t"
        "mulsd %[deltaFactor], %%xmm0\n\t"
        "movsd %%xmm0, %[esp0x44]\n\t"

        "movss %[esp0x14], %%xmm0\n\t"
        "mulss %%xmm1, %%xmm0\n\t"
        "movss %%xmm0, %[esp0x14]\n\t"

    "skipUpdateEnd:\n\t"
        "popfq\n\t"
        "cmpb $0, %[edi_0x2c28]\n\t"
        "jmp *%[physicsReturn]\n\t"
        :
        : [skipUpdate] "m" (skipUpdate),
          [updateDeltaFactorAndInput] "r" (updateDeltaFactorAndInput),
          [newFrame] "m" (newFrame),
          [esp0x3c_] "m" (esp0x3c_),
          [esp0x4c_] "m" (esp0x4c_),
          [esp0x44_] "m" (esp0x44_),
          [esp0x14_] "m" (esp0x14_),
          [deltaFactor] "m" (deltaFactor),
          [edi_0x2c28] "m" (edi),  // Replace with proper declaration of edi
          [esp0x3c] "m" (esp0x3c),
          [esp0x4c] "m" (esp0x4c),
          [esp0x44] "m" (esp0x44),
          [esp0x14] "m" (esp0x14),
          [physicsReturn] "r" (physicsReturn)
        : "memory", "cc", "rax", "rbx", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "xmm0", "xmm1"
    );
}

void updateKeybinds() {
    // Use a temporary variable to handle the ignored return value warning
    std::vector<geode::Ref<keybinds::Bind>> v;
    
    // Lock the critical section to ensure thread safety
    EnterCriticalSection(&keybindsLock);

    // Update keybinds for player 1
    enableRightClick = Mod::get()->getSettingValue<bool>("right-click");
    inputBinds[p1Jump].clear();
    v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/jump-p1");
    for (size_t i = 0; i < v.size(); ++i) {
        inputBinds[p1Jump].emplace(v[i]->getHash());
    }

    inputBinds[p1Left].clear();
    v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-left-p1");
    for (size_t i = 0; i < v.size(); ++i) {
        inputBinds[p1Left].emplace(v[i]->getHash());
    }

    inputBinds[p1Right].clear();
    v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-right-p1");
    for (size_t i = 0; i < v.size(); ++i) {
        inputBinds[p1Right].emplace(v[i]->getHash());
    }

    // Update keybinds for player 2
    inputBinds[p2Jump].clear();
    v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/jump-p2");
    for (size_t i = 0; i < v.size(); ++i) {
        inputBinds[p2Jump].emplace(v[i]->getHash());
    }

    inputBinds[p2Left].clear();
    v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-left-p2");
    for (size_t i = 0; i < v.size(); ++i) {
        inputBinds[p2Left].emplace(v[i]->getHash());
    }

    inputBinds[p2Right].clear();
    v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-right-p2");
    for (size_t i = 0; i < v.size(); ++i) {
        inputBinds[p2Right].emplace(v[i]->getHash());
    }

    // Release the lock on the critical section
    LeaveCriticalSection(&keybindsLock);
}

class $modify(PlayLayer) {
	bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects) {
		updateKeybinds();
		lateCutoff = Mod::get()->getSettingValue<bool>("late-cutoff");
		return PlayLayer::init(level, useReplay, dontCreateObjects);
	}
};

class $modify(CCEGLView) {
	void pollEvents() {
		if (!lateCutoff) QueryPerformanceCounter(&currentFrameTime);
		PlayLayer* playLayer = PlayLayer::get();
		CCNode* par;
		if (!playLayer || !(par = playLayer->getParent()) || (getChildOfType<PauseLayer>(par, 0) != nullptr)) {
			firstFrame = true;
			enableInput = true;
			std::queue<struct inputEvent>().swap(inputQueueCopy);
			EnterCriticalSection(&inputQueueLock);
			std::queue<struct inputEvent>().swap(inputQueue);
			LeaveCriticalSection(&inputQueueLock);
		}
		CCEGLView::pollEvents();
	}
};

class $modify(GJBaseGameLayer) {
	static void onModify(auto & self) {
		self.setHookPriority("GJBaseGameLayer::handleButton", INT_MIN);
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		if (enableInput) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
		}
	}
};

$on_mod(Loaded) {
	if (!InitializeCriticalSectionAndSpinCount(&inputQueueLock, 0x00004000)) {
		log::error("Failed to initialize input queue lock");
		return;
	}
	if (!InitializeCriticalSectionAndSpinCount(&keybindsLock, 0x00004000)) {
		log::error("Failed to initialize keybind lock");
		return;
	}

	// surely there is a more elegant way to do midhooks. if only i knew what it was...
	const uintptr_t prePhysicsPatch = geode::base::get() + 0x1bbaf6;
	uint8_t prePhysicsJmp[] = "\xe9\x00\x00\x00\x00\x90\x90\x90";
	*reinterpret_cast<DWORD *>(prePhysicsJmp + 1) = (reinterpret_cast<uintptr_t>(prePhysicsMidhook) - prePhysicsPatch) - 0x5;
	ByteVector prePhysicsJmpVector;
	for (int i = 0; i < 8; i++) prePhysicsJmpVector.push_back(prePhysicsJmp[i]);
	prePhysicsReturn = (intptr_t)(prePhysicsPatch + 0x8);

	const uintptr_t physicsPatch = geode::base::get() + 0x1bbb20;
	uint8_t physicsJmp[] = "\xe9\x00\x00\x00\x00\x90\x90";
	*reinterpret_cast<DWORD*>(physicsJmp + 1) = (reinterpret_cast<uintptr_t>(physicsMidhook) - physicsPatch) - 0x5;
	ByteVector physicsJmpVector;
	for (int i = 0; i < 7; i++) physicsJmpVector.push_back(physicsJmp[i]);
	physicsReturn = (intptr_t)(physicsPatch + 0x7);
	
	Mod::get()->patch(reinterpret_cast<void*>(geode::base::get() + 0x2de530), {0x3d, 0x0a, 0x57, 0x3f});
	Mod::get()->patch(reinterpret_cast<void*>(prePhysicsPatch), prePhysicsJmpVector);
	Mod::get()->patch(reinterpret_cast<void*>(physicsPatch), physicsJmpVector);
	std::thread *inputT = new std::thread(inputThread);
}
