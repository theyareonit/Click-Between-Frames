#include "includes.hpp"

#include <limits>

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingEvent.hpp>

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>

#include <geode.custom-keybinds/include/Keybinds.hpp>

constexpr double smallestFloat = std::numeric_limits<float>::min();

const InputEvent emptyInput = InputEvent{ 0, 0, PlayerButton::Jump, 0, 0 };
const Step emptyStep = Step{ emptyInput, 1.0, true };

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
		stepQueue = {}; // just in case

		{
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

			inputQueueCopy = {};

			{
				std::lock_guard lock(inputQueueLock);
				inputQueue = {};
			}
		}

		CCDirector::setDeltaTime(dTime);
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
			|| (this->m_isDart || this->m_isBird || this->m_isShip || this->m_isSwing);

		bool p2NotBuffering = p2StartedOnGround
			|| p2->m_touchingRings->count()
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
					if (firstLoop && (this->m_yVelocity < 0 ^ this->m_isUpsideDown)) this->m_isOnGround = p1StartedOnGround; // this fixes delayed inputs on platforms moving down for some reason
					if (!this->m_isOnSlope || this->m_isDart) pl->checkCollisions(this, 0.0f, true);
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
						if (firstLoop && (p2->m_yVelocity < 0 ^ p2->m_isUpsideDown)) p2->m_isOnGround = p2StartedOnGround;
						if (!p2->m_isOnSlope || p2->m_isDart) pl->checkCollisions(p2, 0.0f, true);
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

	threadPriority = Mod::get()->getSettingValue<bool>("thread-priority");

	std::thread(inputThread).detach();
}

class $modify(GJGameLevel) {
	void savePercentage(int percent, bool p1, int clicks, int attempts, bool valid) {
		valid = (
			 Mod::get()->getSettingValue<bool>("soft-toggle") &&
			!Mod::get()->getSettingValue<bool>("actual-delta")
		);

		GJGameLevel::savePercentage(percent, p1, clicks, attempts, valid);
	}
};
