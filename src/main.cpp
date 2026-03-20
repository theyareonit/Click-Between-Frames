#include "Geode/loader/Log.hpp"
#include "includes.hpp"

#include <limits>
#include <math.h>

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <tulip/TulipHook.hpp>

constexpr double SMALLEST_FLOAT = std::numeric_limits<float>::min();

constexpr PlayerButtonCommand EMPTY_INPUT = PlayerButtonCommand {
	.m_button = PlayerButton::Jump,
	.m_isPush = false,
	.m_isPlayer2 = false,
	.m_timestamp = 0
};
constexpr Step EMPTY_STEP = Step {
	.input = EMPTY_INPUT,
	.deltaFactor = 1.0,
	.endStep = true,
};

std::vector<struct PlayerButtonCommand> inputVector;
std::deque<struct Step> stepQueue;

bool softToggle;
bool enableRightClick;

PlayerButtonCommand nextInput = EMPTY_INPUT;

TimestampType lastFrameTime;
TimestampType currentFrameTime;

bool firstFrame = true; // necessary to prevent accidental inputs at the start of the level or when unpausing
bool skipUpdate = true; // true -> dont split steps during PlayerObject::update()
bool linuxNative = false;

std::array<std::unordered_set<size_t>, 6> inputBinds;
std::unordered_set<uint16_t> heldInputs;

/*
this function copies over the input data and uses it to build a queue of physics steps
based on when each input happened relative to the start of the frame
(and also calculates the associated stepDelta multipliers for each step)
*/
void buildStepQueue(int stepCount) {
	PlayLayer* playLayer = PlayLayer::get();
	nextInput = EMPTY_INPUT;
	stepQueue = {}; // shouldnt be necessary, but just in case

	#ifdef GEODE_IS_WINDOWS
	if (linuxNative) linuxCheckInputs();
	#endif
	
	#ifdef GEODE_IS_ANDROID
	static double androidFactor = []() {
		VersionInfo ver = geode::Loader::get()->getVersion();
		if (ver.getMajor() == 5 && ver.getMinor() <= 3) return 1000.0;
		else return 1.0;
	}();
	#endif

	skipUpdate = false;
	if (firstFrame) {
		skipUpdate = true;
		firstFrame = false;
		lastFrameTime = currentFrameTime;
		inputVector.clear();
		return;
	}

	if (!linuxNative) inputVector.insert(inputVector.end(), playLayer->m_queuedButtons.begin(), playLayer->m_queuedButtons.end());
	playLayer->m_queuedButtons.clear();

	TimestampType deltaTime = currentFrameTime - lastFrameTime;
	TimestampType stepDelta = deltaTime / stepCount;

	int inputIdx = 0;
	for (int i = 0; i < stepCount; i++) { // for each physics step of the frame
		double elapsedTime = 0.0;
		while (inputIdx < inputVector.size()) { // while loop to account for multiple inputs on the same step
			PlayerButtonCommand input = inputVector[inputIdx];
			GEODE_ANDROID(input.m_timestamp /= androidFactor;)

			if (input.m_timestamp - lastFrameTime < stepDelta * (i + 1)) { // if the next input in the vector happened on the current step, or if its the last step
				double inputTime = fmod((input.m_timestamp - lastFrameTime), stepDelta) / stepDelta; // proportion of step elapsed at the time the input was made
				stepQueue.emplace_back(Step{ input, std::clamp(inputTime - elapsedTime, SMALLEST_FLOAT, 1.0), false });
				elapsedTime = inputTime;
				inputIdx++;
				//log::info("i{} l{} c{} {}", input.m_timestamp, lastFrameTime, currentFrameTime, input.m_timestamp < lastFrameTime || input.m_timestamp > currentFrameTime);
			}
			else break; 
		}

		stepQueue.emplace_back(Step{ EMPTY_INPUT, std::max(SMALLEST_FLOAT, 1.0 - elapsedTime), true });
	}

	lastFrameTime = currentFrameTime;
	inputVector.erase(inputVector.begin(), inputVector.begin() + inputIdx); // keep inputs with timestamps later than currentFrameTime
}

/*
return the first step in the queue,
also check if an input happened on the previous step, if so run handleButton.
*/
Step popStepQueue() {
	if (stepQueue.empty()) return EMPTY_STEP;

	Step front = stepQueue.front();
	double deltaFactor = front.deltaFactor;

	if (nextInput.m_timestamp != 0) {
		PlayLayer* playLayer = PlayLayer::get();
		playLayer->handleButton(nextInput.m_isPush, (int)nextInput.m_button, !nextInput.m_isPlayer2);
	}

	nextInput = front.input;
	stepQueue.pop_front();

	return front;
}

#ifdef GEODE_IS_WINDOWS
/*
prepare list of keybinds for linux
*/
void updateKeybinds() {
	std::array<std::unordered_set<size_t>, 6> binds;
	std::vector<geode::Keybind> v;
	Mod* customKeybinds = Loader::get()->getLoadedMod("geode.custom-keybinds");
	if (!customKeybinds) {
		static bool checked = 0;
		if (!checked) log::info("Custom keybinds not loaded.");
		checked = 1;

		inputBinds[p1Jump] = { KEY_Space, KEY_W, CONTROLLER_A, CONTROLLER_Up, CONTROLLER_RB };
		inputBinds[p1Left] = { KEY_A, CONTROLLER_Left, CONTROLLER_LTHUMBSTICK_LEFT };
		inputBinds[p1Right] = { KEY_D, CONTROLLER_Right, CONTROLLER_LTHUMBSTICK_RIGHT };

		inputBinds[p2Jump] = { KEY_Up, CONTROLLER_LB };
		inputBinds[p2Left] = { KEY_Left, CONTROLLER_RTHUMBSTICK_LEFT };
		inputBinds[p2Right] = { KEY_Right, CONTROLLER_RTHUMBSTICK_RIGHT };
		return;
	}

	enableRightClick = Mod::get()->getSettingValue<bool>("right-click");

	v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>("jump-p1");
	for (int i = 0; i < v.size(); i++) binds[p1Jump].emplace(v[i].key);

	v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>("move-left-p1");
	for (int i = 0; i < v.size(); i++) binds[p1Left].emplace(v[i].key);

	v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>("move-right-p1");
	for (int i = 0; i < v.size(); i++) binds[p1Right].emplace(v[i].key);

	v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>("jump-p2");
	for (int i = 0; i < v.size(); i++) binds[p2Jump].emplace(v[i].key);

	v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>("move-left-p2");
	for (int i = 0; i < v.size(); i++) binds[p2Left].emplace(v[i].key);

	v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>("move-right-p2");
	for (int i = 0; i < v.size(); i++) binds[p2Right].emplace(v[i].key);

	inputBinds = binds;
}
#endif

/*
rewritten PlayerObject::resetCollisionLog() since it's inlined in GD 2.2074 on Windows
*/
void decomp_resetCollisionLog(PlayerObject* p) {
	p->m_collisionLogTop->removeAllObjects();
    p->m_collisionLogBottom->removeAllObjects();
    p->m_collisionLogLeft->removeAllObjects();
    p->m_collisionLogRight->removeAllObjects();
	p->m_lastCollisionLeft = -1;
	p->m_lastCollisionRight = -1;
	p->m_lastCollisionBottom = -1;
	p->m_lastCollisionTop = -1;
}

double averageDelta = 0.0;

bool physicsBypass;
bool legacyBypass;

/*
determine the number of physics steps that happen on each frame,
need to rewrite the vanilla formula bc otherwise you'd have to use inline assembly to get the step count
*/
int calculateStepCount(double delta, float timewarp, bool forceVanilla) {
	if (!physicsBypass || forceVanilla) { // vanilla 2.2
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
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		#ifdef GEODE_IS_WINDOWS
		if (linuxNative) updateKeybinds(); // update keybinds when you enter a level (for linux)
		#endif
		bool result = PlayLayer::init(level, useReplay, dontCreateObjects);
		if (!softToggle) {
			this->m_clickBetweenSteps = false;
			this->m_clickOnSteps = false;
		}
		return result;
	}

	// disable progress in safe mode
	void levelComplete() {
		bool testMode = this->m_isTestMode;
		if (safeMode && !softToggle) this->m_isTestMode = true;

		PlayLayer::levelComplete();

		this->m_isTestMode = testMode;
	}

	// disable new best popup in safe mode
	void showNewBest(bool p0, int p1, int p2, bool p3, bool p4, bool p5) {
		if (!safeMode || softToggle) PlayLayer::showNewBest(p0, p1, p2, p3, p4, p5);
	}
};

bool mouseFix;
bool precisionFix;

void onFrameStart() {
	PlayLayer* playLayer = PlayLayer::get();
	CCNode* par;

	if (softToggle // CBF disabled
	#ifdef GEODE_IS_WINDOWS
		|| !GetFocus() // GD is minimized
	#endif
		|| !playLayer // not in level
		|| !(par = playLayer->getParent()) // must be a real playLayer with a parent (for compatibility with mods that use a fake playLayer)
		|| (par->getChildByType<PauseLayer>(0)) // if paused
		|| (playLayer->getChildByType<EndLevelLayer>(0))) // if on endscreen
	{
		firstFrame = true;
		skipUpdate = true;
		inputVector.clear();
	}
	
	#ifdef GEODE_IS_WINDOWS
	if (linuxNative) linuxHeartbeat();
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
	#endif
}

#ifdef GEODE_IS_WINDOWS
#include <Geode/modify/CCEGLView.hpp>
class $modify(CCEGLView) {
	void pollEvents() {
		onFrameStart();
		CCEGLView::pollEvents();
	}
};
#endif

#include <Geode/modify/CCScheduler.hpp>
class $modify(CCScheduler) {
	void update(float dt) {
		#ifndef GEODE_IS_WINDOWS
		onFrameStart();
		#else
		if (precisionFix) {
			static LARGE_INTEGER* cur = reinterpret_cast<LARGE_INTEGER*>(geode::base::getCocos() + 0x1a84d8);
			currentFrameTime = (double)cur->QuadPart / (double)freq.QuadPart;
		}
		#endif

		if (!precisionFix) currentFrameTime = getCurrentTimestamp();
		
		CCScheduler::update(dt);
	}
};

int stepCount;
bool clickOnSteps = false;

class $modify(GJBaseGameLayer) {
	static void onModify(auto& self) {
		(void) self.setHookPriority("GJBaseGameLayer::handleButton", Priority::VeryEarly);
		(void) self.setHookPriority("GJBaseGameLayer::getModifiedDelta", Priority::VeryEarly);
	}

	// either use the modified delta to calculate the step count, or use the actual delta if physics bypass is enabled
	double calculateSteps(float modifiedDelta) {
		PlayLayer* pl = PlayLayer::get();
		if (pl) {
			const float timewarp = pl->m_gameState.m_timeWarp;
			if (physicsBypass) {
				if (softToggle) modifiedDelta = CCDirector::sharedDirector()->getActualDeltaTime() * timewarp;
				else if (!firstFrame) modifiedDelta = (currentFrameTime - lastFrameTime) * timewarp;
			}

			stepCount = calculateStepCount(modifiedDelta, timewarp, false);

			if (pl->m_playerDied || GameManager::sharedState()->getEditorLayer() || softToggle) {
				skipUpdate = true;
				firstFrame = true;
			}
			else if (modifiedDelta > 0.0) buildStepQueue(stepCount);
			else skipUpdate = true;
		}
		else if (physicsBypass) stepCount = calculateStepCount(modifiedDelta, this->m_gameState.m_timeWarp, true); // disable physics bypass outside levels

		return modifiedDelta;
	}

	void processCommands(float p0, bool p1, bool p2) {
		if (clickOnSteps && !stepQueue.empty()) {
			Step step;
			do step = popStepQueue(); while (!stepQueue.empty() && !step.endStep); // process 1 step (or more if theres an input)
		}
		GJBaseGameLayer::processCommands(p0, p1 ,p2);
	}

	double getModifiedDelta(float delta) {
		return calculateSteps(GJBaseGameLayer::getModifiedDelta(delta));
	}

#ifdef GEODE_IS_MACOS
	// getModifiedDelta is inlined, hook update directly instead
	void update(float delta) {
		if (this->m_started) {
			float timewarp = std::max(this->m_gameState.m_timeWarp, 1.0f) / 240.0f;
			calculateSteps(roundf((this->m_extraDelta + (m_resumeTimer <= 0 ? delta : 0.0)) / timewarp) * timewarp);
		}

		GJBaseGameLayer::update(delta);
	}
#endif
};

CCPoint p1Pos = { 0.f, 0.f };
CCPoint p2Pos = { 0.f, 0.f };

float rotationDelta;
float shipRotDelta = 0.0f;
bool inputThisStep = false;
bool p1Split = false;
bool p2Split = false;
bool midStep = false;

class $modify(PlayerObject) {
	// split a single step based on the entries in stepQueue
	void update(float stepDelta) {
		PlayLayer* pl = PlayLayer::get();
		
		if (pl && this != pl->m_player1 || midStep) { // do all of the logic during the P1 update for simplicity
			if (midStep || !inputThisStep || this != pl->m_player2) PlayerObject::update(stepDelta);
			return; 
		}

		inputThisStep = stepQueue.empty() ? false : !stepQueue.front().endStep;
		if (!stepQueue.empty() && !inputThisStep && !clickOnSteps) stepQueue.pop_front();
		
		if (skipUpdate
			|| !pl
			|| !inputThisStep
			|| clickOnSteps)
		{
			p1Split = false;
			p2Split = false;
			inputThisStep = false;
			PlayerObject::update(stepDelta);
			return;
		}

		PlayerObject* p2 = pl->m_player2;
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

		p1Split = p1NotBuffering;
		p2Split = p2NotBuffering && isDual;
		
		Step step;
		bool firstLoop = true;
		midStep = true;

		do {
			step = popStepQueue();
			const float substepDelta = stepDelta * step.deltaFactor;
			rotationDelta = substepDelta;
			
			if (p1Split) {
				PlayerObject::update(substepDelta);
				if (!step.endStep) {
					if (firstLoop && ((this->m_yVelocity < 0) ^ this->m_isUpsideDown)) this->m_isOnGround = p1StartedOnGround; // this fixes delayed inputs on platforms moving down for some reason
					if (!this->m_isOnSlope || this->m_isDart) pl->checkCollisions(this, 0.0f, true); // moving platforms will launch u really high if this is anything other than 0.0, idk why
					else pl->checkCollisions(this, stepDelta, true); // slopes will launch you really high if the 2nd argument is lower than like 0.01, idk why
					PlayerObject::updateRotation(substepDelta);
					decomp_resetCollisionLog(this); // necessary for wave
				}
			}
			else if (step.endStep) PlayerObject::update(stepDelta); // revert to click-on-steps mode when buffering to reduce bugs

			if (p2Split) {
				p2->update(substepDelta);
				if (!step.endStep) {
					if (firstLoop && ((p2->m_yVelocity < 0) ^ p2->m_isUpsideDown)) p2->m_isOnGround = p2StartedOnGround;
					if (!p2->m_isOnSlope || p2->m_isDart) pl->checkCollisions(p2, 0.0f, true);
					else pl->checkCollisions(p2, stepDelta, true);
					p2->updateRotation(substepDelta);
					decomp_resetCollisionLog(p2);
				}
			}
			else if (step.endStep) p2->update(stepDelta);

			firstLoop = false;
		} while (!step.endStep);

		midStep = false;
	}

	// this function was chosen to update m_lastPosition in just because it's called right at the end of the vanilla physics step loop
	void updateRotation(float t) {
		PlayLayer* pl = PlayLayer::get();
		
		if (pl && this == pl->m_player1 && p1Split && !midStep) {
			PlayerObject::updateRotation(rotationDelta); // perform the remaining rotation that was left incomplete in the PlayerObject::update() hook
			this->m_lastPosition = p1Pos; // move triggers & spider get confused without this (iirc)
		}
		else if (pl && this == pl->m_player2 && p2Split && !midStep) {
			PlayerObject::updateRotation(rotationDelta);
			this->m_lastPosition = p2Pos;
		}
		else PlayerObject::updateRotation(t);

		if (physicsBypass && pl && !midStep) { // fix percent calculation with physics bypass on 2.2 levels
			pl->m_gameState.m_currentProgress = static_cast<int>(pl->m_gameState.m_levelTime * 240.0);
		}
	}

	#ifdef GEODE_IS_WINDOWS
	void updateShipRotation(float t) {
		PlayLayer* pl = PlayLayer::get();

		if (pl && (this == pl->m_player1 || this == pl->m_player2) && (physicsBypass || inputThisStep)) {
			shipRotDelta = t;
			PlayerObject::updateShipRotation(1.0/1024); // necessary to use a really small deltatime to get around an oversight in rob's math
			shipRotDelta = 0.0f;
		}
		else PlayerObject::updateShipRotation(t);
	}
	#endif
};

/*
CBF/PB endscreen watermark
*/
class $modify(EndLevelLayer) {
	void customSetup() {
		EndLevelLayer::customSetup();

		if (!softToggle || physicsBypass) {
			std::string text;

			if ((softToggle || clickOnSteps) && physicsBypass) text = "PB";
			else if (physicsBypass) text = "CBF+PB";
			else if (!clickOnSteps && !softToggle) text = "CBF";
			else return;

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

/*
dont submit to leaderboards for rated levels
*/
class $modify(GJGameLevel) {
	void savePercentage(int percent, bool p1, int clicks, int attempts, bool valid) {
		valid = (
			softToggle && !physicsBypass	
			|| clickOnSteps && !physicsBypass
			|| this->m_stars == 0
		);

		if (!safeMode || softToggle) GJGameLevel::savePercentage(percent, p1, clicks, attempts, valid);
	}
};

float Slerp2D(float p0, float p1, float p2) {
	auto orig = reinterpret_cast<float (*)(float, float, float)>(geode::base::get() + 0x71ef0);
	if (shipRotDelta != 0.0f) { // only do anything if Slerp2D is called within PlayerObject::updateShipRotation()
		shipRotDelta *= p2 * 1024; // p2 is 1/1024 scaled by a constant factor, we need to multiply shipRotDelta by that factor
		return orig(p0, p1, shipRotDelta);
	}
	else return orig(p0, p1, p2);
}



void writeAddr(geode::ByteVector& vec, size_t offset, const void* addr) {
	memcpy(vec.data() + offset, &addr, sizeof(addr));
}

// ok some of the surrounding stuff was vibe coded but i did write all the asm myself
void togglePrecisionFix(bool enable) {
#ifdef GEODE_IS_WINDOWS
	static Patch* pfPatch = nullptr;
	static Patch* pfPatch2 = nullptr;

	if (!pfPatch) {
		uintptr_t base = geode::base::getCocos();
		void* patchAddr = reinterpret_cast<void*>(base + 0x736f6);
		void* patchAddr2 = reinterpret_cast<void*>(base + 0x73716);
		void* retAddr = reinterpret_cast<void*>(base + 0x73703);
		void* currentTimeAddr = reinterpret_cast<void*>(base + 0x1a84d8);

		// prevent frame timing from desyncing over time in ccapplication::run
		geode::ByteVector cave = {
			0x9C,                                     // pushfq
			0x0F, 0x57, 0xC0,                         // xorps xmm0, xmm0
			0x0F, 0x57, 0xC9,                         // xorps xmm1, xmm1

			// grid snap currentTime based on the float version of animation interval
			0x48, 0x83, 0xC0, 0x0a,                   // add rax,10                               this fixes rounding errors, dont question it
			0x0F, 0x5A, 0xC7,                         // cvtps2pd xmm0,xmm7
			0x49, 0xB9, 0,0,0,0,0,0,0,0,              // movabs r9, &freq
			0xF2, 0x49, 0x0F, 0x2A, 0x09,             // cvtsi2sd xmm1,qword ptr [r9]
			0xF2, 0x0F, 0x59, 0xC1,                   // mulsd  xmm0,xmm1
			0xF2, 0x48, 0x0F, 0x2A, 0xC8,             // cvtsi2sd xmm1,rax
			0xF2, 0x0F, 0x5E, 0xC8,                   // divsd  xmm1,xmm0
			0xF2, 0x48, 0x0F, 0x2C, 0xC1,             // cvttsd2si rax,xmm1
			0xF2, 0x48, 0x0F, 0x2A, 0xC8,             // cvtsi2sd xmm1,rax
			0xF2, 0x0F, 0x59, 0xC8,                   // mulsd  xmm1,xmm0
			0xF2, 0x48, 0x0F, 0x2D, 0xC1,             // cvtsd2si rax,xmm1

			// fix precision issue in deltaTime calculation (supplemented by pfPatch2)
			0x4D, 0x8B, 0x21,                         // mov r12,QWORD PTR [r9]

			// move corrected currentTime value into relevant locations
			0x9D,                                     // popfq
			0x48, 0x89, 0x44, 0x24, 0x58,             // mov [rsp+0x58], rax
			0x48, 0xA3, 0,0,0,0,0,0,0,0,              // mov [currentTimeAddr], rax

			// restore previous state
			0x0F, 0x57, 0xC0,                         // xorps xmm0, xmm0
			0x0F, 0x57, 0xC9,                         // xorps xmm1, xmm1
			0xF2, 0x48, 0x0F, 0x2A, 0x44, 0x24, 0x68, // cvtsi2sd xmm0, [rsp+0x68]
			0x49, 0xB9, 0,0,0,0,0,0,0,0,              // movabs r9, retAddr
			0x41, 0xFF, 0xE1                          // jmp r9
		};

		writeAddr(cave, 16, &freq);
		writeAddr(cave, 72, currentTimeAddr);
		writeAddr(cave, 95, retAddr);

		void* caveAddr = VirtualAlloc(nullptr, cave.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		memcpy(caveAddr, cave.data(), cave.size());

		geode::ByteVector entry = {
			0x49, 0xB9, 0,0,0,0,0,0,0,0,              // mov r9, caveAddr
			0x41, 0xFF, 0xE1                          // jmp r9
		};
		writeAddr(entry, 2, caveAddr);

		log::info("Precision fix patch: {} at {}", entry, patchAddr);
		pfPatch = Mod::get()->patch(patchAddr, entry).unwrap();

		geode::ByteVector deltaPatch = {
			0x90, 0x90, 0x90,
			0xF2, 0x0F, 0x5E, 0xD1,
			0x90, 0x90, 0x90, 0x90
		};

		log::info("Delta fix patch: {} at {}", deltaPatch, patchAddr2);
		pfPatch2 = Mod::get()->patch(patchAddr2, deltaPatch).unwrap();
	}

	if (enable) {
		(void) pfPatch->enable();
		(void) pfPatch2->enable();
	}
	else {
		(void) pfPatch->disable();
		(void) pfPatch2->disable();
	}

	precisionFix = enable;
#endif
}

void togglePhysicsBypass(bool enable) {
#ifdef GEODE_IS_WINDOWS
	void* addr = reinterpret_cast<void*>(geode::base::get() + 0x237a91);

	static Patch* pbPatch = nullptr;
	if (!pbPatch) {
		// mov rcx, &stepCount
		// mov r11d, dword ptr [rcx]
		geode::ByteVector bytes = { 0x48, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0, 0x44, 0x8b, 0x19 };
		writeAddr(bytes, 2, &stepCount);
		log::info("Physics bypass patch: {} at {}", bytes, addr);
		pbPatch = Mod::get()->patch(addr, bytes).unwrap();
	}

	if (enable) {
		(void) pbPatch->enable();
	} 
	else  {
		(void) pbPatch->disable();
	}

	physicsBypass = enable;
#endif
}

void toggleMod(bool disable) {
#if defined(GEODE_IS_WINDOWS)
	void* addr = reinterpret_cast<void*>(geode::base::get() + 0x622bd0);

	static Patch* modPatch = nullptr;
	if (!modPatch) modPatch = Mod::get()->patch(addr, { 0x29, 0x5c, 0x4f, 0x3f }).unwrap();

	if (disable) {
		(void) modPatch->disable();
	} else {
		(void) modPatch->enable();
	}
#endif

	softToggle = disable;

	// for mod menus that let you toggle cbf mid-attempt
	PlayLayer* pl = PlayLayer::get();
	if (pl) {
		if (!softToggle) {
			pl->m_clickBetweenSteps = false;
			pl->m_clickOnSteps = false;
		}
		else {
			GameManager* gm = GameManager::get();
			pl->m_clickBetweenSteps = gm->getGameVariable("0177");
			pl->m_clickOnSteps = gm->getGameVariable("0176");
		}
	}
}

$on_mod(Loaded) {
	Mod::get()->setSavedValue<bool>("is-linux", false);

	toggleMod(Mod::get()->getSettingValue<bool>("soft-toggle"));
	listenForSettingChanges<bool>("soft-toggle", toggleMod);

	togglePrecisionFix(Mod::get()->getSettingValue<bool>("precision-fix"));
	listenForSettingChanges<bool>("precision-fix", togglePrecisionFix);

	togglePhysicsBypass(Mod::get()->getSettingValue<bool>("physics-bypass"));
	listenForSettingChanges<bool>("physics-bypass", togglePhysicsBypass);

	legacyBypass = Mod::get()->getSettingValue<std::string>("bypass-mode") == "2.1";
	listenForSettingChanges<std::string>("bypass-mode", +[](std::string mode) {
		legacyBypass = mode == "2.1";
	});

	safeMode = Mod::get()->getSettingValue<bool>("safe-mode");
	listenForSettingChanges<bool>("safe-mode", +[](bool enable) {
		safeMode = enable;
	});

	clickOnSteps = Mod::get()->getSettingValue<bool>("click-on-steps");
	listenForSettingChanges<bool>("click-on-steps", +[](bool enable) {
		clickOnSteps = enable;
	});

	mouseFix = Mod::get()->getSettingValue<bool>("mouse-fix");
	listenForSettingChanges<bool>("mouse-fix", +[](bool enable) {
		mouseFix = enable;
	});

#ifdef GEODE_IS_WINDOWS
	(void) Mod::get()->hook(
		reinterpret_cast<void*>(geode::base::get() + 0x71ef0),
		Slerp2D,
		"Slerp2D",
		tulip::hook::TulipConvention::Default
	);

	windowsSetup();
#endif
}