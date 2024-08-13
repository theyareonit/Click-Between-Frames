#pragma once

#include <algorithm>
#include <queue>
#include <mutex>

#include <Geode/Geode.hpp>

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

extern std::queue<struct InputEvent> inputQueue;

extern std::array<std::unordered_set<size_t>, 6> inputBinds;
extern std::unordered_set<USHORT> heldInputs;

extern std::mutex inputQueueLock;
extern std::mutex keybindsLock;

extern std::atomic<bool> enableRightClick;
extern bool threadPriority;

void inputThread();