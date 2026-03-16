#pragma once

#include <queue>
#include <mutex>

#include <Geode/Geode.hpp>

using namespace geode::prelude;

#include "timestamp.hpp"

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

struct Step {
	PlayerButtonCommand input;
	double deltaFactor;
	bool endStep;
};

extern std::vector<struct PlayerButtonCommand> inputVector;

extern std::array<std::unordered_set<size_t>, 6> inputBinds;
extern std::unordered_set<uint16_t> heldInputs;

extern bool enableRightClick;
extern bool softToggle; // true -> cbf disabled