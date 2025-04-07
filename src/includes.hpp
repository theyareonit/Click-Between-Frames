#pragma once

#include <queue>
#include <mutex>

#include <Geode/Geode.hpp>

using namespace geode::prelude;

using TimestampType = int64_t;
TimestampType getCurrentTimestamp();

enum GameAction : int {
	p1Jump = 0,
	p1Left = 1,
	p1Right = 2,
	p2Jump = 3,
	p2Left = 4,
	p2Right = 5
};

enum State : bool {
	Release = 0,
	Press = 1
};

struct InputEvent {
	TimestampType time;
	PlayerButton inputType;
	bool inputState;
	bool isPlayer1;
};

struct Step {
	InputEvent input;
	double deltaFactor;
	bool endStep;
};

extern std::deque<struct InputEvent> inputQueue;
extern std::deque<struct InputEvent> inputQueueCopy;

extern std::array<std::unordered_set<size_t>, 6> inputBinds;
extern std::unordered_set<uint16_t> heldInputs;

extern std::mutex inputQueueLock;
extern std::mutex keybindsLock;

extern std::atomic<bool> enableRightClick;
// true -> cbf disabled, confusing i know
extern std::atomic<bool> softToggle;

extern bool threadPriority;

#if defined(GEODE_IS_WINDOWS)
// some windows only global variables
#include "windows.hpp"
#else
extern TimestampType pendingInputTimestamp;
#endif