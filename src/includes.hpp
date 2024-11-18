#pragma once

#include <algorithm>
#include <queue>
#include <mutex>

#include <Geode/Geode.hpp>
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

enum State : bool {
	Press = 0,
	Release = 1
};

struct __attribute__((packed)) LinuxInputEvent {
	LARGE_INTEGER time;
	USHORT type;
	USHORT code;
	int value;
};

struct InputEvent {
	LARGE_INTEGER time;
	PlayerButton inputType;
	bool inputState;
	bool isPlayer1;
};

struct Step {
	InputEvent input;
	double deltaFactor;
	bool endStep;
};

extern HANDLE hSharedMem;
extern HANDLE hMutex;
extern LPVOID pBuf;

extern std::queue<struct InputEvent> inputQueue;
extern std::queue<struct InputEvent> inputQueueCopy;

extern std::array<std::unordered_set<size_t>, 6> inputBinds;
extern std::unordered_set<USHORT> heldInputs;

extern std::mutex inputQueueLock;
extern std::mutex keybindsLock;

extern std::atomic<bool> enableRightClick;
extern std::atomic<bool> softToggle;

extern bool threadPriority;

constexpr size_t BUFFER_SIZE = 20;

void linuxCheckInputs();
void inputThread();