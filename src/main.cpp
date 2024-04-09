#include <queue>
#include <algorithm>
#include <limits>

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayerObject.hpp>
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

enum Player : int {
	Player1 = 0,
	Player2 = 1,
	OtherPlayer = 2,
	BothPlayers = 3,
	NoPlayer = 4
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
			player = Player1;
			inputType = PlayerButton::Jump;
			if (flags & RI_MOUSE_BUTTON_1_DOWN) inputState = Press;
			else if (flags & RI_MOUSE_BUTTON_1_UP) inputState = Release;
			else {
				player = Player2;
				if (flags & RI_MOUSE_BUTTON_2_DOWN) {
					inputState = Press;
					keybinds::InvokeBindEvent("robtop.geometry-dash/jump-p2", true).post();
				}
				else if (flags & RI_MOUSE_BUTTON_2_UP) {
					inputState = Release;
					keybinds::InvokeBindEvent("robtop.geometry-dash/jump-p2", false).post();
				}
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

bool isDead = false;
bool inDual = false;
bool twoPlayer = false;
bool lock2p = false;
void updateGameState() {
	PlayLayer *playLayer = PlayLayer::get();
	if (!playLayer) return;
	isDead = playLayer->m_player1->m_isDead;
	inDual = playLayer->m_gameState.m_isDualMode;
	twoPlayer = playLayer->m_level->m_twoPlayerMode;
}

std::queue<struct inputEvent> inputQueueCopy;
std::queue<struct step> stepQueue;
inputEvent nextInput = { 0, 0, PlayerButton::Jump, 0 };
LARGE_INTEGER lastFrameTime;
LARGE_INTEGER currentFrameTime;
int inputQueueSize;
int stepCount;
bool newFrame = true;
bool firstFrame = true;
bool skipUpdate = true;
bool enableInput = false;
void updateInputQueueAndTime() {
	if (GameManager::sharedState()->getEditorLayer() || stepCount == 0) {
		skipUpdate = true;
		return;
	}
	else {
		nextInput = { 0, 0, PlayerButton::Jump, 0, 0 };
		std::queue<struct step>().swap(stepQueue); // shouldnt do anything but just in case
		lastFrameTime = currentFrameTime;
		newFrame = true;

		// queryperformancecounter is done within the critical section to prevent a race condition which could cause dropped inputs
		EnterCriticalSection(&inputQueueLock);
		inputQueueCopy = inputQueue;
		std::queue<struct inputEvent>().swap(inputQueue);
		QueryPerformanceCounter(&currentFrameTime);
		LeaveCriticalSection(&inputQueueLock);

		// on the first frame after entering a level, stepDelta is 0. if you do PlayerObject::update(0) at any point, the player will permanently freeze
		if (!firstFrame) skipUpdate = false;
		if (firstFrame || isDead) {
			inputQueueSize = 0;
			enableInput = true;
			skipUpdate = true;
			firstFrame = false;
			return;
		}

		LARGE_INTEGER deltaTime;
		LARGE_INTEGER stepDelta;
		deltaTime.QuadPart = currentFrameTime.QuadPart - lastFrameTime.QuadPart;
		stepDelta.QuadPart = (deltaTime.QuadPart / stepCount) + 1; // the +1 is to prevent dropped inputs caused by integer division
		inputQueueSize = inputQueueCopy.size();

		constexpr double smallestFloat = std::numeric_limits<float>::min(); // ensures deltaFactor can never be 0, even after being converted to float
		for (int i = 0; i < stepCount; i++) {
			double deltaTotal = 0.0;
			while (true) {
				inputEvent front;
				if (!inputQueueCopy.empty()) {
					front = inputQueueCopy.front();
					if (front.time.QuadPart - lastFrameTime.QuadPart < stepDelta.QuadPart * (i + 1)) {
						double dFactor = static_cast<double>((front.time.QuadPart - lastFrameTime.QuadPart) % stepDelta.QuadPart) / stepDelta.QuadPart;
						stepQueue.emplace(step{ front, std::clamp(dFactor - deltaTotal, smallestFloat, 1.0) });
						deltaTotal += dFactor;
						inputQueueCopy.pop();
						continue;
					}
				}
				front = { 0, 0, PlayerButton::Jump, 0, 0 };
				stepQueue.emplace(step{ front, std::max(smallestFloat, 1.0 - deltaTotal)});
				break;
			}
		}
	}
}

// this can be done without a midhook if you feel like rewriting the step calculation algorithm
intptr_t prePhysicsReturn;
__declspec(naked) void prePhysicsMidhook() {
	__asm {
		mov stepCount, esi
		pushfd
		pushad 
		call updateInputQueueAndTime
		popad
		add esi, inputQueueSize
		mov dword ptr [esp + 0x1c + 0x4], esi // +0x4 bc pushfd
		popfd
		mov dword ptr [esp + 0x18], 0x0 // instruction replaced by the jmp
		jmp prePhysicsReturn
	}
}

double deltaFactor = 1.0;
void updateDeltaFactorAndInput() {
	updateGameState();
	if (skipUpdate) return;
	enableInput = false;
	step front = stepQueue.front();
	deltaFactor = front.deltaFactor;
	if (nextInput.time.QuadPart != 0) {
		PlayLayer *playLayer = PlayLayer::get();
		int player;

		if (!inDual && !twoPlayer) player = Player1;
		else if (!inDual && !lock2p) player = Player1;
		else if (!inDual && front.input.player == Player1) player = Player1;
		else if (!inDual) player = NoPlayer;
		else if (!twoPlayer) player = BothPlayers;
		else if (nextInput.player == Player1) player = Player1;
		else player = Player2;

		enableInput = true;
		if (player == Player1) {
			if (nextInput.inputState == Press) playLayer->m_player1->pushButton(nextInput.inputType);
			else playLayer->m_player1->releaseButton(nextInput.inputType);
		}
		else if (player == Player2) {
			if (nextInput.inputState == Press) playLayer->m_player2->pushButton(nextInput.inputType);
			else playLayer->m_player2->releaseButton(nextInput.inputType);
		}
		else if (player == BothPlayers) {
			if (nextInput.inputState == Press) {
				playLayer->m_player1->pushButton(nextInput.inputType);
				playLayer->m_player2->pushButton(nextInput.inputType);
			}
			else {
				playLayer->m_player1->releaseButton(nextInput.inputType);
				playLayer->m_player2->releaseButton(nextInput.inputType);
			}
		}
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
__declspec(naked) void physicsMidhook() {
	__asm {
		pushfd
		pushad
		call updateDeltaFactorAndInput
		popad
		cmp skipUpdate, 0
		jnz end
		cmp newFrame, 0
		mov newFrame, 0
		jz multiply

		// copy the value of every stack variable dependent on stepDelta
		// idk if all of these are necessary/what all of them do

		movsd xmm0, qword ptr[esp + 0x3c + 0x4] // +0x4 bc of pushfd once again
		movsd esp0x3c, xmm0

		movsd xmm0, qword ptr[esp + 0x4c + 0x4]
		movsd esp0x4c, xmm0

		movsd xmm0, qword ptr[esp + 0x44 + 0x4]
		movsd esp0x44, xmm0

		movss xmm0, dword ptr[esp + 0x14 + 0x4]
		movss esp0x14, xmm0

	multiply:

		movsd xmm0, esp0x3c
		mulsd xmm0, deltaFactor
		movsd qword ptr[esp + 0x3c + 0x4], xmm0
		
		movsd xmm0, esp0x4c
		mulsd xmm0, deltaFactor
		movsd qword ptr[esp + 0x4c + 0x4], xmm0
		
		movsd xmm0, esp0x44
		mulsd xmm0, deltaFactor
		movsd qword ptr[esp + 0x44 + 0x4], xmm0
		
		movss xmm0, esp0x14
		cvtsd2ss xmm1, deltaFactor
		mulss xmm0, xmm1
		movss dword ptr[esp + 0x14 + 0x4], xmm0

	end:

		popfd
		cmp byte ptr[edi + 0x2c28], 0x0 // the instruction replaced by the jmp
		jmp physicsReturn
	}
}

void updateKeybinds() {
	std::vector<geode::Ref<keybinds::Bind>> v;
	EnterCriticalSection(&keybindsLock);
	inputBinds->clear();
	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/jump-p1");
	for (int i = 0; i < v.size(); i++) inputBinds[p1Jump].emplace(v[i]->getHash());
	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-left-p1");
	for (int i = 0; i < v.size(); i++) inputBinds[p1Left].emplace(v[i]->getHash());
	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-right-p1");
	for (int i = 0; i < v.size(); i++) inputBinds[p1Right].emplace(v[i]->getHash());
	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/jump-p2");
	for (int i = 0; i < v.size(); i++) inputBinds[p2Jump].emplace(v[i]->getHash());
	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-left-p2");
	for (int i = 0; i < v.size(); i++) inputBinds[p2Left].emplace(v[i]->getHash());
	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-right-p2");
	for (int i = 0; i < v.size(); i++) inputBinds[p2Right].emplace(v[i]->getHash());
	LeaveCriticalSection(&keybindsLock);
}

class $modify(PlayLayer) {
	bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects) {
		updateKeybinds();
		return PlayLayer::init(level, useReplay, dontCreateObjects);
	}
};

class $modify(CCScheduler) {
	void update(float dTime) {
		// clear queue if the player is not in a level or is paused
		PlayLayer* playLayer = PlayLayer::get();
		CCNode* par;
		if (!playLayer || !(par = playLayer->getParent()) || (getChildOfType<PauseLayer>(par, 0) != nullptr)) {
			EnterCriticalSection(&inputQueueLock);
			std::queue<struct inputEvent>().swap(inputQueue);
			LeaveCriticalSection(&inputQueueLock);
			firstFrame = true;
			enableInput = true;
		}
		CCScheduler::update(dTime);
	}
};

class $modify(PlayerObject) {
	static void onModify(auto & self) {
		self.setHookPriority("PlayerObject::pushButton", INT_MIN);
		self.setHookPriority("PlayerObject::releaseButton", INT_MIN);
	}

	void pushButton(PlayerButton button) {
		if (this->playerType() == OtherPlayer || enableInput) {
			PlayerObject::pushButton(button);
		}
	}

	void releaseButton(PlayerButton button) {
		if (this->playerType() == OtherPlayer || enableInput) {
			PlayerObject::releaseButton(button);
		}
	}

	int playerType() {
		PlayLayer *playLayer;
		if (!(playLayer = PlayLayer::get())) return OtherPlayer;
		if (this == playLayer->m_player1) return Player1;
		else if (this == playLayer->m_player2) return Player2;
		else return OtherPlayer;
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
