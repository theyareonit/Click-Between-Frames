#include <queue>
#include <algorithm>
#include <limits>

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingEvent.hpp>

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

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

struct InputEvent {
	LARGE_INTEGER time;
	PlayerButton inputType;
	bool inputState;
	bool player;
};

struct Step {
	InputEvent input;
	double deltaFactor;
	bool endStep;
};

const InputEvent emptyInput = InputEvent{ 0, 0, PlayerButton::Jump, 0, 0 };
const Step emptyStep = Step{ emptyInput, 1.0, true };

std::queue<struct InputEvent> inputQueue;

std::unordered_set<size_t> inputBinds[6];
std::unordered_set<USHORT> heldInputs;

CRITICAL_SECTION inputQueueLock;
CRITICAL_SECTION keybindsLock;

bool enableRightClick;

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

	EnterCriticalSection(&inputQueueLock);
	inputQueue.emplace(InputEvent{ time, inputType, inputState, player });
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

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	MSG msg;
	while (GetMessage(&msg, hwnd, 0, 0)) {
		DispatchMessage(&msg);
	}
}

std::queue<struct InputEvent> inputQueueCopy;
std::queue<struct Step> stepQueue;

InputEvent nextInput = { 0, 0, PlayerButton::Jump, 0 };

LARGE_INTEGER lastFrameTime;
LARGE_INTEGER lastPhysicsFrameTime;
LARGE_INTEGER currentFrameTime;

bool firstFrame = true;
bool skipUpdate = true;
bool enableInput = false;
bool lateCutoff;

void updateInputQueueAndTime(int stepCount) {
	PlayLayer* playLayer = PlayLayer::get();
	if (!playLayer 
		|| GameManager::sharedState()->getEditorLayer() 
		|| playLayer->m_player1->m_isDead) 
	{
		enableInput = true;
		firstFrame = true;
		skipUpdate = true;
		return;
	}
	else {
		nextInput = emptyInput;
		lastFrameTime = lastPhysicsFrameTime;
		std::queue<struct Step>().swap(stepQueue); // just in case

		EnterCriticalSection(&inputQueueLock);

		if (lateCutoff) {
			QueryPerformanceCounter(&currentFrameTime); // done within the critical section to prevent a race condition which could cause dropped inputs
			inputQueueCopy = inputQueue;
			std::queue<struct InputEvent>().swap(inputQueue);
		}
		else {
			while (!inputQueue.empty() && inputQueue.front().time.QuadPart <= currentFrameTime.QuadPart) {
				inputQueueCopy.push(inputQueue.front());
				inputQueue.pop();
			}
		}

		LeaveCriticalSection(&inputQueueLock);

		lastPhysicsFrameTime = currentFrameTime;

		if (!firstFrame) skipUpdate = false;
		else {
			skipUpdate = true;
			firstFrame = false;
			if (!lateCutoff) std::queue<struct InputEvent>().swap(inputQueueCopy);
			return;
		}

		LARGE_INTEGER deltaTime;
		LARGE_INTEGER stepDelta;
		deltaTime.QuadPart = currentFrameTime.QuadPart - lastFrameTime.QuadPart;
		stepDelta.QuadPart = (deltaTime.QuadPart / stepCount) + 1; // the +1 is to prevent dropped inputs caused by integer division

		constexpr double smallestFloat = std::numeric_limits<float>::min(); // ensures deltaFactor can never be 0, even after being converted to float
		for (int i = 0; i < stepCount; i++) {
			double lastDFactor = 0.0;
			while (true) {
				InputEvent front;
				if (!inputQueueCopy.empty()) {
					front = inputQueueCopy.front();
					if (front.time.QuadPart - lastFrameTime.QuadPart < stepDelta.QuadPart * (i + 1)) {
						double dFactor = static_cast<double>((front.time.QuadPart - lastFrameTime.QuadPart) % stepDelta.QuadPart) / stepDelta.QuadPart;
						stepQueue.emplace(Step{ front, std::clamp(dFactor - lastDFactor, smallestFloat, 1.0), false });
						lastDFactor = dFactor;
						inputQueueCopy.pop();
						continue;
					}
				}
				front = nextInput;
				stepQueue.emplace(Step{ front, std::max(smallestFloat, 1.0 - lastDFactor), true });
				break;
			}
		}
	}
}

Step updateDeltaFactorAndInput() {
	enableInput = false;

	if (stepQueue.empty()) return emptyStep;

	Step front = stepQueue.front();
	double deltaFactor = front.deltaFactor;

	if (nextInput.time.QuadPart != 0) {
		PlayLayer* playLayer = PlayLayer::get();

		enableInput = true;
		playLayer->handleButton(!nextInput.inputState, (int)nextInput.inputType, !nextInput.player);
		enableInput = false;
	}

	nextInput = front.input;
	stepQueue.pop();

	return front;
}

void updateKeybinds() {
	std::vector<geode::Ref<keybinds::Bind>> v;

	EnterCriticalSection(&keybindsLock);

	enableRightClick = Mod::get()->getSettingValue<bool>("right-click");
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

void newResetCollisionLog(PlayerObject* p) { // inlined in 2.206...
	(*(CCDictionary**)((char*)p + 0x5b0))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5b8))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5c0))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5c8))->removeAllObjects();
	*(unsigned long*)((char*)p + 0x5e0) = *(unsigned long*)((char*)p + 0x5d0);
	*(long long*)((char*)p + 0x5d0) = -1;
}

class $modify(PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		updateKeybinds();
		return PlayLayer::init(level, useReplay, dontCreateObjects);
	}
};

bool softToggle; // cant just disable all hooks bc thatll cause a memory leak with inputQueue, may improve this in the future

class $modify(CCDirector) {
	void setDeltaTime(float dTime) {
		PlayLayer* playLayer = PlayLayer::get();
		CCNode* par;

		if (!lateCutoff) QueryPerformanceCounter(&currentFrameTime);

		if (softToggle 
			|| !playLayer 
			|| !(par = playLayer->getParent()) 
			|| (getChildOfType<PauseLayer>(par, 0) != nullptr)) 
		{
			firstFrame = true;
			skipUpdate = true;
			enableInput = true;

			std::queue<struct InputEvent>().swap(inputQueueCopy);

			EnterCriticalSection(&inputQueueLock);
			std::queue<struct InputEvent>().swap(inputQueue);
			LeaveCriticalSection(&inputQueueLock);
		}

		CCDirector::setDeltaTime(dTime);
	}
};

float p1CollisionDelta;
float p2CollisionDelta;
bool actualDelta;

class $modify(GJBaseGameLayer) {
	static void onModify(auto& self) {
		self.setHookPriority("GJBaseGameLayer::handleButton", INT_MIN);
		self.setHookPriority("GJBaseGameLayer::getModifiedDelta", INT_MIN);
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		if (enableInput) GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}

	float getModifiedDelta(float delta) {
		float modifiedDelta = GJBaseGameLayer::getModifiedDelta(delta);

		PlayLayer* pl = PlayLayer::get();
		if (pl) {
			const float timewarp = pl->m_gameState.m_timeWarp;
			if (actualDelta) modifiedDelta = CCDirector::sharedDirector()->getActualDeltaTime() * timewarp;
			
			const int stepCount = std::round(std::max(1.0, ((modifiedDelta * 60.0) / std::min(1.0f, timewarp)) * 4)); // not sure if this is different from (delta * 240) / timewarp

			if (modifiedDelta > 0.0) updateInputQueueAndTime(stepCount);
			else skipUpdate = true;
		}
		
		return modifiedDelta;
	}
};

CCPoint p1Pos = { NULL, NULL };
CCPoint p2Pos = { NULL, NULL };
float rotationDelta;
bool midStep = false;

class $modify(PlayerObject) {
	void update(float timeFactor) {

		PlayLayer* pl = PlayLayer::get();

		if (skipUpdate 
			|| !pl 
			|| !(this == pl->m_player1 || this == pl->m_player2))
		{
			PlayerObject::update(timeFactor);
			return;
		}

		PlayerObject* p2 = pl->m_player2;
		if (this == p2) return;

		bool p1NotBuffering = this->m_isOnGround
			|| this->m_touchingRings->count()
			|| (this->m_isDart || this->m_isBird || this->m_isShip || this->m_isSwing);

		bool p2NotBuffering = p2->m_isOnGround
			|| p2->m_touchingRings->count()
			|| (p2->m_isDart || p2->m_isBird || p2->m_isShip || p2->m_isSwing);

		bool isDual = pl->m_gameState.m_isDualMode;

		p1Pos = PlayerObject::getPosition();
		p2Pos = p2->getPosition();

		Step step;
		midStep = true;

		do {
			step = updateDeltaFactorAndInput();

			const float newTimeFactor = timeFactor * step.deltaFactor;
			rotationDelta = newTimeFactor;

			if (p1NotBuffering) {
				PlayerObject::update(newTimeFactor);
				if (!step.endStep) {
					p1CollisionDelta = newTimeFactor;
					pl->checkCollisions(this, 0.0f, true);
					PlayerObject::updateRotation(newTimeFactor);
					newResetCollisionLog(this);
				}
			}
			else if (step.endStep) { // disable cbf for buffers, revert to click-on-steps mode 
				PlayerObject::update(timeFactor);
			}

			if (isDual) {
				if (p2NotBuffering) {
					p2->update(newTimeFactor);
					if (!step.endStep) {
						p2CollisionDelta = newTimeFactor;
						pl->checkCollisions(p2, 0.0f, true);
						p2->updateRotation(newTimeFactor);
						newResetCollisionLog(p2);
					}
				}
				else if (step.endStep) {
					p2->update(timeFactor);
				}
			}

		} while (!step.endStep);

		midStep = false;
	}

	void updateRotation(float t) {
		PlayLayer* pl = PlayLayer::get();
		if (!skipUpdate && pl && this == pl->m_player1) {
			PlayerObject::updateRotation(rotationDelta);

			if (p1Pos.x && !midStep) { // to happen only when GJBGL::update() calls updateRotation after an input
				this->m_lastPosition = p1Pos;
				p1Pos.setPoint(NULL, NULL);
			}
		}
		else if (!skipUpdate && pl && this == pl->m_player2) {
			PlayerObject::updateRotation(rotationDelta);

			if (p2Pos.x && !midStep) {
				pl->m_player2->m_lastPosition = p2Pos;
				p2Pos.setPoint(NULL, NULL);
			}
		}
		else PlayerObject::updateRotation(t);
	}
};

class $modify(EndLevelLayer) {
	void customSetup() {
		EndLevelLayer::customSetup();

		if (!softToggle || actualDelta) {
			std::string text;

			if (softToggle && actualDelta) text = "PB";
			else if (actualDelta) text = "CBF+PB";
			else text = "CBF";

			cocos2d::CCSize size = cocos2d::CCDirector::sharedDirector()->getWinSize();
			CCLabelBMFont* indicator = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");

			indicator->setPosition({ size.width, size.height });
			indicator->setAnchorPoint({ 1.0f, 1.0f });
			indicator->setOpacity(30);
			indicator->setScale(0.2f);

			this->addChild(indicator);
		}
	}
};

Patch* patch;

void toggleMod(bool disable) {
	void* addr = reinterpret_cast<void*>(geode::base::get() + 0x5ec8e8);
	DWORD oldProtect;
	DWORD newProtect = 0x40;
	
	VirtualProtect(addr, 4, newProtect, &oldProtect);

	if (!patch) patch = Mod::get()->patch(addr, { 0x29, 0x5c, 0x4f, 0x3f }).unwrap();

	if (disable) patch->disable();
	else patch->enable();
	
	VirtualProtect(addr, 4, oldProtect, &newProtect);

	softToggle = disable;
}

$on_mod(Loaded) {
	if (!InitializeCriticalSectionAndSpinCount(&inputQueueLock, 0x00040000)) {
		log::error("Failed to initialize input queue lock");
		return;
	}

	if (!InitializeCriticalSectionAndSpinCount(&keybindsLock, 0x00040000)) {
		log::error("Failed to initialize keybind lock");
		return;
	}

	toggleMod(Mod::get()->getSettingValue<bool>("soft-toggle"));
	listenForSettingChanges("soft-toggle", toggleMod);

	lateCutoff = Mod::get()->getSettingValue<bool>("late-cutoff");
	listenForSettingChanges("late-cutoff", +[](bool enable) {
		lateCutoff = enable;
	});

	actualDelta = Mod::get()->getSettingValue<bool>("actual-delta");
	listenForSettingChanges("actual-delta", +[](bool enable) {
		actualDelta = enable;
	});

	std::thread(inputThread).detach();
}
