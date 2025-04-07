#include "includes.hpp"

TimestampType pendingInputTimestamp = 0;

#include <Geode/modify/GJBaseGameLayer.hpp>
class $modify(GJBaseGameLayer) {
	void queueButton(int button, bool push, bool isPlayer2) {
		if (!softToggle.load() && pendingInputTimestamp) {
			std::lock_guard lock(inputQueueLock);
			inputQueue.emplace_back(InputEvent {
				.time = pendingInputTimestamp,
				.inputType = PlayerButton(button),
				.inputState = push ? State::Press : State::Release,
				.isPlayer1 = !isPlayer2
			});
		}

		GJBaseGameLayer::queueButton(button, push, isPlayer2);
	}
};