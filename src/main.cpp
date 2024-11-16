#include "includes.hpp"

#include <limits>
#include <algorithm>

#include <Geode/Geode.hpp>

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>

#include <geode.custom-keybinds/include/Keybinds.hpp>

typedef void (*wine_get_host_version)(const char **sysname, const char **release);

constexpr double SMALLEST_FLOAT = std::numeric_limits<float>::min();

constexpr InputEvent EMPTY_INPUT = InputEvent{ 0, 0, PlayerButton::Jump, 0, 0 };
constexpr Step EMPTY_STEP = Step{ EMPTY_INPUT, 1.0, true };

std::queue<struct InputEvent> inputQueueCopy;
std::queue<struct Step> stepQueue;

InputEvent nextInput = { 0, 0, PlayerButton::Jump, 0 };

LARGE_INTEGER lastFrameTime;
LARGE_INTEGER lastPhysicsFrameTime;
LARGE_INTEGER currentFrameTime;

HANDLE hSharedMem = NULL;
HANDLE hMutex = NULL;

bool firstFrame = true;
bool skipUpdate = true;
bool enableInput = false;
bool isLinux = false;
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
		nextInput = EMPTY_INPUT;
		lastFrameTime = lastPhysicsFrameTime;
		stepQueue = {}; // just in case

		if (isLinux) {
			GetSystemTimePreciseAsFileTime((FILETIME*)&currentFrameTime);
			linuxCheckInputs();
		}
		else {
			std::lock_guard lock(inputQueueLock);

			if (lateCutoff) {
				QueryPerformanceCounter(&currentFrameTime);
				inputQueueCopy = inputQueue;
				inputQueue = {};
			}
			else {
				while (!inputQueue.empty() && inputQueue.front().time.QuadPart <= currentFrameTime.QuadPart) {
					inputQueueCopy.push(inputQueue.front());
					inputQueue.pop();
				}
			}
		}

		lastPhysicsFrameTime = currentFrameTime;

		if (!firstFrame) skipUpdate = false;
		else {
			skipUpdate = true;
			firstFrame = false;
			if (!lateCutoff) inputQueueCopy = {};
			return;
		}

		LARGE_INTEGER deltaTime;
		LARGE_INTEGER stepDelta;
		deltaTime.QuadPart = currentFrameTime.QuadPart - lastFrameTime.QuadPart;
		stepDelta.QuadPart = (deltaTime.QuadPart / stepCount) + 1; // the +1 is to prevent dropped inputs caused by integer division

		for (int i = 0; i < stepCount; i++) {
			double lastDFactor = 0.0;
			while (true) {
				InputEvent front;
				bool empty = inputQueueCopy.empty();
				if (!empty) front = inputQueueCopy.front();

				if (!empty && front.time.QuadPart - lastFrameTime.QuadPart < stepDelta.QuadPart * (i + 1)) {
					double dFactor = static_cast<double>((front.time.QuadPart - lastFrameTime.QuadPart) % stepDelta.QuadPart) / stepDelta.QuadPart;
					stepQueue.emplace(Step{ front, std::clamp(dFactor - lastDFactor, SMALLEST_FLOAT, 1.0), false });
					inputQueueCopy.pop();
					lastDFactor = dFactor;
					continue;
				}
				else {
					stepQueue.emplace(Step{ EMPTY_INPUT, std::max(SMALLEST_FLOAT, 1.0 - lastDFactor), true });
					break;
				}
			}
		}
	}
}

Step updateDeltaFactorAndInput() {
	enableInput = false;

	if (stepQueue.empty()) return EMPTY_STEP;

	Step front = stepQueue.front();
	double deltaFactor = front.deltaFactor;

	if (nextInput.time.QuadPart != 0) {
		PlayLayer* playLayer = PlayLayer::get();

		enableInput = true;
		playLayer->handleButton(!nextInput.inputState, (int)nextInput.inputType, nextInput.isPlayer1);
		enableInput = false;
	}

	nextInput = front.input;
	stepQueue.pop();

	return front;
}

void updateKeybinds() {
	std::array<std::unordered_set<size_t>, 6> binds;
	std::vector<geode::Ref<keybinds::Bind>> v;

	enableRightClick.store(Mod::get()->getSettingValue<bool>("right-click"));

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/jump-p1");
	for (int i = 0; i < v.size(); i++) binds[p1Jump].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-left-p1");
	for (int i = 0; i < v.size(); i++) binds[p1Left].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-right-p1");
	for (int i = 0; i < v.size(); i++) binds[p1Right].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/jump-p2");
	for (int i = 0; i < v.size(); i++) binds[p2Jump].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-left-p2");
	for (int i = 0; i < v.size(); i++) binds[p2Left].emplace(v[i]->getHash());

	v = keybinds::BindManager::get()->getBindsFor("robtop.geometry-dash/move-right-p2");
	for (int i = 0; i < v.size(); i++) binds[p2Right].emplace(v[i]->getHash());

	{
		std::lock_guard lock(keybindsLock);
		inputBinds = binds;
	}
}

void newResetCollisionLog(PlayerObject* p) { // inlined in 2.206...
	(*(CCDictionary**)((char*)p + 0x5b0))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5b8))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5c0))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5c8))->removeAllObjects();
	*(unsigned long*)((char*)p + 0x5e0) = *(unsigned long*)((char*)p + 0x5d0);
	*(long long*)((char*)p + 0x5d0) = -1;
}

bool softToggle; // cant just disable all hooks bc thatll cause a memory leak with inputQueue, may improve this in the future
bool safeMode;

class $modify(PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		updateKeybinds();
		return PlayLayer::init(level, useReplay, dontCreateObjects);
	}

	void levelComplete() {
		bool testMode = this->m_isTestMode;
		if (safeMode && !softToggle) this->m_isTestMode = true;

		PlayLayer::levelComplete();

		this->m_isTestMode = testMode;
	}

	void showNewBest(bool p0, int p1, int p2, bool p3, bool p4, bool p5) {
		if (!safeMode || softToggle) PlayLayer::showNewBest(p0, p1, p2, p3, p4, p5);
	}
};

bool mouseFix;

class $modify(CCEGLView) {
	void pollEvents() {
		PlayLayer* playLayer = PlayLayer::get();
		CCNode* par;

		if (!lateCutoff && !isLinux) QueryPerformanceCounter(&currentFrameTime);

		if (softToggle 
			|| !GetFocus() // not in foreground
			|| !playLayer 
			|| !(par = playLayer->getParent()) 
			|| (par->getChildByType<PauseLayer>(0))
			|| (playLayer->getChildByType<EndLevelLayer>(0)))
		{
			firstFrame = true;
			skipUpdate = true;
			enableInput = true;

			inputQueueCopy = {};

			if (!isLinux) {
				std::lock_guard lock(inputQueueLock);
				inputQueue = {};
			}
		}
		else if (mouseFix && !skipUpdate) {
			MSG msg;
			while (PeekMessage(&msg, NULL, WM_MOUSEFIRST + 1, WM_MOUSELAST, PM_REMOVE)); // clear mouse inputs from message queue
		}

		CCEGLView::pollEvents();
	}
};

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

		bool isDual = pl->m_gameState.m_isDualMode;

		bool p1StartedOnGround = this->m_isOnGround;
		bool p2StartedOnGround = p2->m_isOnGround;

		bool p1NotBuffering = p1StartedOnGround
			|| this->m_touchingRings->count()
			|| this->m_isDashing
			|| (this->m_isDart || this->m_isBird || this->m_isShip || this->m_isSwing);

		bool p2NotBuffering = p2StartedOnGround
			|| p2->m_touchingRings->count()
			|| p2->m_isDashing
			|| (p2->m_isDart || p2->m_isBird || p2->m_isShip || p2->m_isSwing);

		p1Pos = PlayerObject::getPosition();
		p2Pos = p2->getPosition();

		Step step;
		bool firstLoop = true;
		midStep = true;

		do {
			step = updateDeltaFactorAndInput();

			const float newTimeFactor = timeFactor * step.deltaFactor;
			rotationDelta = newTimeFactor;

			if (p1NotBuffering) {
				PlayerObject::update(newTimeFactor);
				if (!step.endStep) {
					if (firstLoop && ((this->m_yVelocity < 0) ^ this->m_isUpsideDown)) this->m_isOnGround = p1StartedOnGround; // this fixes delayed inputs on platforms moving down for some reason
					if (!this->m_isOnSlope || this->m_isDart) pl->checkCollisions(this, 0.0f, true);
					else pl->checkCollisions(this, 0.25f, true);
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
						if (firstLoop && ((p2->m_yVelocity < 0) ^ p2->m_isUpsideDown)) p2->m_isOnGround = p2StartedOnGround;
						if (!p2->m_isOnSlope || p2->m_isDart) pl->checkCollisions(p2, 0.0f, true);
						else pl->checkCollisions(p2, 0.25f, true);
						p2->updateRotation(newTimeFactor);
						newResetCollisionLog(p2);
					}
				}
				else if (step.endStep) {
					p2->update(timeFactor);
				}
			}

			firstLoop = false;
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

LPVOID pBuf;

class $modify(CreatorLayer) {
	bool init() {
		if (!CreatorLayer::init()) return false;

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
			log::error("CreatorLayer WaitForSingleObject failed: {}", GetLastError());
		}
		return true;
	} 
};

class $modify(GJGameLevel) {
	void savePercentage(int percent, bool p1, int clicks, int attempts, bool valid) {
		valid = (
			Mod::get()->getSettingValue<bool>("soft-toggle")
			&& !Mod::get()->getSettingValue<bool>("actual-delta")
			|| this->m_stars == 0
		);

		if (!safeMode || softToggle) GJGameLevel::savePercentage(percent, p1, clicks, attempts, valid);
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

HANDLE gdMutex;

$on_mod(Loaded) {
	toggleMod(Mod::get()->getSettingValue<bool>("soft-toggle"));
	listenForSettingChanges("soft-toggle", toggleMod);

	safeMode = Mod::get()->getSettingValue<bool>("safe-mode");
	listenForSettingChanges("safe-mode", +[](bool enable) {
		safeMode = enable;
	});

	actualDelta = Mod::get()->getSettingValue<bool>("actual-delta");
	listenForSettingChanges("actual-delta", +[](bool enable) {
		actualDelta = enable;
	});

	mouseFix = Mod::get()->getSettingValue<bool>("mouse-fix");
	listenForSettingChanges("mouse-fix", +[](bool enable) {
		mouseFix = enable;
	});

	lateCutoff = Mod::get()->getSettingValue<bool>("late-cutoff");
	listenForSettingChanges("late-cutoff", +[](bool enable) {
		lateCutoff = enable;
	});

	threadPriority = Mod::get()->getSettingValue<bool>("thread-priority");

	HMODULE ntdll = GetModuleHandle("ntdll.dll");
	wine_get_host_version wghv = (wine_get_host_version)GetProcAddress(ntdll, "wine_get_host_version");
	if (wghv) {
		const char* sysname;
		const char* release;
		wghv(&sysname, &release);

		std::string sys = sysname;
		log::info("Wine {}", sys);
		if (sys == "Linux") { // background raw keyboard input doesn't work in Wine
            isLinux = true;

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

			hMutex = CreateMutex(NULL, FALSE, "CBFLinuxMutex");
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

			std::string path = CCFileUtils::get()->fullPathForFilename("linux-input.exe.so"_spr, true);

			if (!CreateProcess(path.c_str(), NULL, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
				log::error("Failed to launch Linux input program: {}", GetLastError());
				CloseHandle(hMutex);
				CloseHandle(gdMutex);
				CloseHandle(hSharedMem);
				return;
			}
		}
	}

	if (!isLinux) std::thread(inputThread).detach();
}
