#include "includes.hpp"

#include <limits>

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>

typedef void (*wine_get_host_version)(const char **sysname, const char **release);

constexpr double SMALLEST_FLOAT = std::numeric_limits<float>::min();

constexpr InputEvent EMPTY_INPUT = InputEvent{ 0, 0, PlayerButton::Jump, 0, 0 };
constexpr Step EMPTY_STEP = Step{ EMPTY_INPUT, 1.0, true };

std::queue<struct InputEvent> inputQueueCopy;
std::queue<struct Step> stepQueue;

std::atomic<bool> softToggle;

InputEvent nextInput = EMPTY_INPUT;

LARGE_INTEGER lastFrameTime;
LARGE_INTEGER currentFrameTime;

HANDLE hSharedMem = NULL;
HANDLE hMutex = NULL;

bool firstFrame = true; // necessary to prevent accidental inputs at the start of the level or when unpausing
bool skipUpdate = true; // true -> dont split steps during PlayerObject::update()
bool enableInput = false;
bool linuxNative = false;
bool lateCutoff; // false -> ignore inputs that happen after the start of the frame; true -> check for inputs at the latest possible moment

/*
this function copies over the inputQueue from the input thread and uses it to build a queue of physics steps
based on when each input happened relative to the start of the frame
(and also calculates the associated deltaTime multipliers for each step)
*/
void buildStepQueue(int stepCount) {
	PlayLayer* playLayer = PlayLayer::get();
	nextInput = EMPTY_INPUT;
	stepQueue = {}; // shouldnt be necessary, but just in case

	if (linuxNative) {
		GetSystemTimePreciseAsFileTime((FILETIME*)&currentFrameTime); // used instead of QPC to make it possible to convert between linux and windows timestamps
		linuxCheckInputs();
	}
	else {
		std::lock_guard lock(inputQueueLock);

		if (lateCutoff) { // copy all inputs in queue, use current time as the frame boundary
			QueryPerformanceCounter(&currentFrameTime);
			inputQueueCopy = inputQueue;
			inputQueue = {};
		}
		else { // only copy inputs that happened before the start of the frame
			while (!inputQueue.empty() && inputQueue.front().time.QuadPart <= currentFrameTime.QuadPart) {
				inputQueueCopy.push(inputQueue.front());
				inputQueue.pop();
			}
		}
	}

	if (!firstFrame) skipUpdate = false;
	else {
		skipUpdate = true;
		firstFrame = false;
		lastFrameTime = currentFrameTime;
		if (!lateCutoff) inputQueueCopy = {};
		return;
	}

	LARGE_INTEGER deltaTime;
	LARGE_INTEGER stepDelta;
	deltaTime.QuadPart = currentFrameTime.QuadPart - lastFrameTime.QuadPart;
	stepDelta.QuadPart = (deltaTime.QuadPart / stepCount) + 1; // the +1 is to prevent dropped inputs caused by integer division

	for (int i = 0; i < stepCount; i++) { // for each physics step of the frame
		double elapsedTime = 0.0;
		while (true) { // while loop to account for multiple inputs on the same step
			InputEvent front;
			bool empty = inputQueueCopy.empty();
			if (!empty) front = inputQueueCopy.front();
			else break; // no more inputs this frame

			if (front.time.QuadPart - lastFrameTime.QuadPart < stepDelta.QuadPart * (i + 1)) { // if the first input in the queue happened on the current step
				double inputTime = static_cast<double>((front.time.QuadPart - lastFrameTime.QuadPart) % stepDelta.QuadPart) / stepDelta.QuadPart; // proportion of step elapsed at the time the input was made
				stepQueue.emplace(Step{ front, std::clamp(inputTime - elapsedTime, SMALLEST_FLOAT, 1.0), false });
				inputQueueCopy.pop();
				elapsedTime = inputTime;
			}
			else break; // no more inputs this step, more later in the frame
		}

		stepQueue.emplace(Step{ EMPTY_INPUT, std::max(SMALLEST_FLOAT, 1.0 - elapsedTime), true });
	}

	lastFrameTime = currentFrameTime;
}

/*
return the first step in the queue,
also check if an input happened on the previous step, if so run handleButton.
tbh this doesnt need to be a separate function from the PlayerObject::update() hook
*/
Step popStepQueue() {
	if (stepQueue.empty()) return EMPTY_STEP;

	Step front = stepQueue.front();
	double deltaFactor = front.deltaFactor;

	if (nextInput.time.QuadPart != 0) {
		PlayLayer* playLayer = PlayLayer::get();

		enableInput = true;
		playLayer->handleButton(nextInput.inputState, (int)nextInput.inputType, nextInput.isPlayer1);
		enableInput = false;
	}

	nextInput = front.input;
	stepQueue.pop();

	return front;
}

/*
send list of keybinds to the input thread
*/
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

/*
"decompiled" version of PlayerObject::resetCollisionLog() since it's inlined in GD 2.2074 on Windows
*/
void decomp_resetCollisionLog(PlayerObject* p) {
	(*(CCDictionary**)((char*)p + 0x5b0))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5b8))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5c0))->removeAllObjects();
	(*(CCDictionary**)((char*)p + 0x5c8))->removeAllObjects();
	*(unsigned long*)((char*)p + 0x5e0) = *(unsigned long*)((char*)p + 0x5d0);
	*(long long*)((char*)p + 0x5d0) = -1;
}

double averageDelta = 0.0;

bool legacyBypass;
bool actualDelta;

/*
determine the number of physics steps that happen on each frame, 
need to rewrite the vanilla formula bc otherwise you'd have to use inline assembly to get the step count
*/
int calculateStepCount(float delta, float timewarp, bool forceVanilla) {
	if (!actualDelta || forceVanilla) { // vanilla 2.2
		return std::round(std::max(1.0, ((delta * 60.0) / std::min(1.0f, timewarp)) * 4.0)); // not sure if this is different from `(delta * 240) / timewarp` bc of float precision
	}
	else if (legacyBypass) { // 2.1 physics bypass (same as vanilla 2.1)
		return std::round(std::max(4.0, delta * 240.0) / std::min(1.0f, timewarp));
	}
	else { // sorta just 2.2 + physics bypass but it doesnt allow below 240 steps/sec, also it smooths things out a bit when lagging
		double animationInterval = CCDirector::sharedDirector()->getAnimationInterval();
		averageDelta = (0.05 * delta) + (0.95 * averageDelta); // exponential moving average to detect lag/external fps caps
		if (averageDelta > animationInterval * 10) averageDelta = animationInterval * 10; // dont let averageDelta get too high
		
		bool laggingOneFrame = animationInterval < delta - (1.0 / 240.0); // more than 1 step of lag on a single frame
		bool laggingManyFrames = averageDelta - animationInterval > 0.0005; // average lag is >0.5ms
		
		if (!laggingOneFrame && !laggingManyFrames) { // no stepcount variance when not lagging
			return std::round(std::ceil((animationInterval * 240.0) - 0.0001) / std::min(1.0f, timewarp));
		} 
		else if (!laggingOneFrame) { // consistently low fps
			return std::round(std::ceil(averageDelta * 240.0) / std::min(1.0f, timewarp));
		}
		else { // need to catch up badly
			return std::round(std::ceil(delta * 240.0) / std::min(1.0f, timewarp));
		}
	}
}

bool safeMode;

class $modify(PlayLayer) {
	// update keybinds when you enter a level
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		updateKeybinds();
		return PlayLayer::init(level, useReplay, dontCreateObjects);
	}

	// disable progress in safe mode
	void levelComplete() {
		bool testMode = this->m_isTestMode;
		if (safeMode && !softToggle.load()) this->m_isTestMode = true;

		PlayLayer::levelComplete();

		this->m_isTestMode = testMode;
	}

	// disable new best popup in safe mode
	void showNewBest(bool p0, int p1, int p2, bool p3, bool p4, bool p5) {
		if (!safeMode || softToggle.load()) PlayLayer::showNewBest(p0, p1, p2, p3, p4, p5);
	}
};

bool mouseFix;

class $modify(CCEGLView) {
	void pollEvents() {
		PlayLayer* playLayer = PlayLayer::get();
		CCNode* par;

		if (!lateCutoff && !linuxNative) QueryPerformanceCounter(&currentFrameTime);

		if (softToggle.load() // CBF disabled
			|| !GetFocus() // GD is minimized
			|| !playLayer // not in level
			|| !(par = playLayer->getParent()) // must be a real playLayer with a parent (for compatibility with mods that use a fake playLayer)
			|| (par->getChildByType<PauseLayer>(0)) // if paused
			|| (playLayer->getChildByType<EndLevelLayer>(0))) // if on endscreen
		{
			firstFrame = true;
			skipUpdate = true;
			enableInput = true;

			inputQueueCopy = {};

			if (!linuxNative) { // clearing the queue isnt necessary on Linux since its fixed size anyway, but on windows memory leaks are possible
				std::lock_guard lock(inputQueueLock);
				inputQueue = {};
			}
		}
		if (mouseFix && !skipUpdate) { // reduce lag with high polling rate mice by limiting the number of mouse movements per frame to 1
			MSG msg;
			int index = 1;
			while (PeekMessage(&msg, NULL, WM_MOUSEFIRST + index, WM_MOUSELAST, PM_NOREMOVE)) { // check for mouse inputs in the queue
				if (msg.message == WM_MOUSEMOVE || msg.message == WM_NCMOUSEMOVE) {
					PeekMessage(&msg, NULL, WM_MOUSEFIRST + index, WM_MOUSELAST, PM_REMOVE); // remove mouse movements from queue
				}
				else index++;
			}
		}

		CCEGLView::pollEvents();
	}
};

UINT32 stepCount;

class $modify(GJBaseGameLayer) {
	static void onModify(auto& self) {
		self.setHookPriority("GJBaseGameLayer::handleButton", Priority::VeryEarly);
		self.setHookPriority("GJBaseGameLayer::getModifiedDelta", Priority::VeryEarly);
	}

	// disable regular inputs while CBF is active
	void handleButton(bool down, int button, bool isPlayer1) {
		if (enableInput) GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}

	// either use the modified delta to calculate the step count, or use the actual delta if physics bypass is enabled
	float getModifiedDelta(float delta) {
		float modifiedDelta = GJBaseGameLayer::getModifiedDelta(delta);

		PlayLayer* pl = PlayLayer::get();
		if (pl) {
			const float timewarp = pl->m_gameState.m_timeWarp;
			if (actualDelta) modifiedDelta = CCDirector::sharedDirector()->getActualDeltaTime() * timewarp;
			
			stepCount = calculateStepCount(modifiedDelta, timewarp, false);

			if (pl->m_player1->m_isDead || GameManager::sharedState()->getEditorLayer()) {
				enableInput = true;
				skipUpdate = true;
				firstFrame = true;
			}
			else if (modifiedDelta > 0.0) buildStepQueue(stepCount);
			else skipUpdate = true;
		}
		else if (actualDelta) stepCount = calculateStepCount(modifiedDelta, this->m_gameState.m_timeWarp, true); // disable physics bypass outside levels
		
		return modifiedDelta;
	}
};

CCPoint p1Pos = { NULL, NULL };
CCPoint p2Pos = { NULL, NULL };

float rotationDelta;
bool midStep = false;

class $modify(PlayerObject) {
	// split a single step based on the entries in stepQueue
	void update(float stepDelta) {
		PlayLayer* pl = PlayLayer::get();

		if (skipUpdate 
			|| !pl 
			|| !(this == pl->m_player1 || this == pl->m_player2)) // for compatibility with mods like Globed
		{
			PlayerObject::update(stepDelta);
			return;
		}

		PlayerObject* p2 = pl->m_player2;
		if (this == p2) return; // do all of the logic during the P1 update for simplicity

		enableInput = false;

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

		p1Pos = PlayerObject::getPosition(); // save for later to prevent desync with move triggers & some other issues
		p2Pos = p2->getPosition();

		Step step;
		bool firstLoop = true;
		midStep = true;

		do {
			step = popStepQueue();

			const float substepDelta = stepDelta * step.deltaFactor;
			rotationDelta = substepDelta;

			if (p1NotBuffering) {
				PlayerObject::update(substepDelta);
				if (!step.endStep) {
					if (firstLoop && ((this->m_yVelocity < 0) ^ this->m_isUpsideDown)) this->m_isOnGround = p1StartedOnGround; // this fixes delayed inputs on platforms moving down for some reason
					if (!this->m_isOnSlope || this->m_isDart) pl->checkCollisions(this, 0.0f, true); // moving platforms will launch u really high if this is anything other than 0.0, idk why
					else pl->checkCollisions(this, stepDelta, true); // slopes will launch you really high if the 2nd argument is lower than like 0.01, idk why
					PlayerObject::updateRotation(substepDelta);
					decomp_resetCollisionLog(this); // presumably this function clears the list of objects that the icon is touching, necessary for wave
				}
			}
			else if (step.endStep) { // revert to click-on-steps mode when buffering to reduce bugs
				PlayerObject::update(stepDelta);
			}

			if (isDual) {
				if (p2NotBuffering) {
					p2->update(substepDelta);
					if (!step.endStep) {
						if (firstLoop && ((p2->m_yVelocity < 0) ^ p2->m_isUpsideDown)) p2->m_isOnGround = p2StartedOnGround;
						if (!p2->m_isOnSlope || p2->m_isDart) pl->checkCollisions(p2, 0.0f, true);
						else pl->checkCollisions(p2, stepDelta, true);
						p2->updateRotation(substepDelta);
						decomp_resetCollisionLog(p2);
					}
				}
				else if (step.endStep) {
					p2->update(stepDelta);
				}
			}

			firstLoop = false;
		} while (!step.endStep);

		midStep = false;
	}

	// this function was chosen to update m_lastPosition in just because it's called right at the end of the vanilla physics step loop
	void updateRotation(float t) {
		PlayLayer* pl = PlayLayer::get();
		if (!skipUpdate && pl && this == pl->m_player1) {
			PlayerObject::updateRotation(rotationDelta); // perform the remaining rotation that was left incomplete in the PlayerObject::update() hook

			if (p1Pos.x && !midStep) { // ==true only at the end of a step that an input happened on
				this->m_lastPosition = p1Pos; // move triggers & spider get confused without this (iirc)
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

/*
CBF/PB endscreen watermark
*/
class $modify(EndLevelLayer) {
	void customSetup() {
		EndLevelLayer::customSetup();

		if (!softToggle.load() || actualDelta) {
			std::string text;

			if (softToggle.load() && actualDelta) text = "PB";
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

/*
notify the player if theres an issue with input on Linux
*/
class $modify(CreatorLayer) {
	bool init() {
		if (!CreatorLayer::init()) return false;

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
			log::error("CreatorLayer WaitForSingleObject failed: {}", GetLastError());
		}
		return true;
	} 
};

/*
dont submit to leaderboards for rated levels
*/
class $modify(GJGameLevel) {
	void savePercentage(int percent, bool p1, int clicks, int attempts, bool valid) {
		valid = (
			Mod::get()->getSettingValue<bool>("soft-toggle")
			&& !Mod::get()->getSettingValue<bool>("actual-delta")
			|| this->m_stars == 0
		);

		if (!safeMode || softToggle.load()) GJGameLevel::savePercentage(percent, p1, clicks, attempts, valid);
	}
};

Patch* pbPatch;

void togglePhysicsBypass(bool enable) {
	void* addr = reinterpret_cast<void*>(geode::base::get() + 0x2322ca);
	DWORD oldProtect;
	DWORD newProtect = 0x40;
	
	VirtualProtect(addr, 4, newProtect, &oldProtect);

	if (!pbPatch) {
		geode::ByteVector bytes = { 0x48, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0, 0x44, 0x8b, 0x19 }; // could be 1 instruction if i was less lazy
		UINT32* stepAddr = &stepCount;
		for (int i = 0; i < 8; i++) { // for each byte in stepAddr
			bytes[i + 2] = ((char*)&stepAddr)[i]; // replace the zeroes with the address of stepCount
		}
		log::info("Physics bypass patch: {} at {}", bytes, addr);
		pbPatch = Mod::get()->patch(addr, bytes).unwrap();
	}

	if (enable) pbPatch->enable();
	else pbPatch->disable();
	
	VirtualProtect(addr, 4, oldProtect, &newProtect);

	actualDelta = enable;
}

Patch* modPatch;

void toggleMod(bool disable) {
	void* addr = reinterpret_cast<void*>(geode::base::get() + 0x5ec8e8);
	DWORD oldProtect;
	DWORD newProtect = 0x40;
	
	VirtualProtect(addr, 4, newProtect, &oldProtect);

	if (!modPatch) modPatch = Mod::get()->patch(addr, { 0x29, 0x5c, 0x4f, 0x3f }).unwrap();

	if (disable) modPatch->disable();
	else modPatch->enable();
	
	VirtualProtect(addr, 4, oldProtect, &newProtect);

	softToggle.store(disable);
}

HANDLE gdMutex;

$on_mod(Loaded) {
	Mod::get()->setSavedValue<bool>("is-linux", false);

	toggleMod(Mod::get()->getSettingValue<bool>("soft-toggle"));
	listenForSettingChanges("soft-toggle", toggleMod);

	togglePhysicsBypass(Mod::get()->getSettingValue<bool>("actual-delta"));
	listenForSettingChanges("actual-delta", togglePhysicsBypass);

	legacyBypass = Mod::get()->getSettingValue<std::string>("bypass-mode") == "2.1";
	listenForSettingChanges("bypass-mode", +[](std::string mode) {
		legacyBypass = mode == "2.1";
	});

	safeMode = Mod::get()->getSettingValue<bool>("safe-mode");
	listenForSettingChanges("safe-mode", +[](bool enable) {
		safeMode = enable;
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

	if (!linuxNative) {
		std::thread(inputThread).detach();
	}
}