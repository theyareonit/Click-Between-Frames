#include <queue>
#include <algorithm>
#include <limits>
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
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

struct inputEvent {
    LARGE_INTEGER time;
    PlayerButton inputType;
    bool inputState;
    bool player;
};

struct step {
    inputEvent input;
    double deltaFactor;
};

std::queue<struct inputEvent> inputQueue;
std::set<size_t> inputBinds[6];
std::set<USHORT> heldInputs;

CRITICAL_SECTION inputQueueLock;
CRITICAL_SECTION keybindsLock;
bool enableRightClick;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LARGE_INTEGER time;
    PlayerButton inputType;
    bool inputState;
    bool player;

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

                    if (!shouldEmplace) return 0;
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

    QueryPerformanceCounter(&time);

    EnterCriticalSection(&inputQueueLock);
    inputQueue.emplace(inputEvent{ time, inputType, inputState, player });
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
    dev[0].usUsagePage = 0x01;
    dev[0].usUsage = 0x06;
    dev[0].dwFlags = RIDEV_INPUTSINK;
    dev[0].hwndTarget = hwnd;

    dev[1].usUsagePage = 0x01;
    dev[1].usUsage = 0x02;
    dev[1].dwFlags = RIDEV_INPUTSINK;
    dev[1].hwndTarget = hwnd;

    if (!RegisterRawInputDevices(dev, 2, sizeof(dev[0]))) {
        log::error("Failed to register raw input devices");
        return;
    }

    MSG msg;
    while (GetMessage(&msg, hwnd, 0, 0)) {
        DispatchMessage(&msg);
    }
}

std::queue<struct inputEvent> inputQueueCopy;
std::queue<struct step> stepQueue;
inputEvent nextInput = { 0, 0, PlayerButton::Jump, 0 };
LARGE_INTEGER lastFrameTime;
LARGE_INTEGER lastPhysicsFrameTime;
LARGE_INTEGER currentFrameTime;
int inputQueueSize;
int stepCount;
bool newFrame = true;
bool firstFrame = true;
bool skipUpdate = true;
bool enableInput = false;
bool lateCutoff;

void updateInputQueueAndTime() {
    PlayLayer* playLayer = PlayLayer::get();
    if (!playLayer || GameManager::sharedState()->getEditorLayer() || stepCount == 0 || playLayer->m_player1->m_isDead) {
        enableInput = true;
        firstFrame = true;
        skipUpdate = true;
        return;
    } else {
        nextInput = { 0, 0, PlayerButton::Jump, 0, 0 };
        std::queue<struct step>().swap(stepQueue);
        lastFrameTime = lastPhysicsFrameTime;
        newFrame = true;

        EnterCriticalSection(&inputQueueLock);
        if (lateCutoff) {
            QueryPerformanceCounter(&currentFrameTime);
            inputQueueCopy = inputQueue;
            std::queue<struct inputEvent>().swap(inputQueue);
        } else {
            while (!inputQueue.empty() && inputQueue.front().time.QuadPart <= currentFrameTime.QuadPart) {
                inputQueueCopy.push(inputQueue.front());
                inputQueue.pop();
            }
        }
        LeaveCriticalSection(&inputQueueLock);
        lastPhysicsFrameTime = currentFrameTime;

        if (!firstFrame) skipUpdate = false;
        else {
            inputQueueSize = 0;
            skipUpdate = true;
            firstFrame = false;
            if (!lateCutoff) std::queue<struct inputEvent>().swap(inputQueueCopy);
            return;
        }

        LARGE_INTEGER deltaTime;
        LARGE_INTEGER stepDelta;
        deltaTime.QuadPart = currentFrameTime.QuadPart - lastFrameTime.QuadPart;
        stepDelta.QuadPart = (deltaTime.QuadPart / stepCount) + 1;
        inputQueueSize = inputQueueCopy.size();

        constexpr double smallestFloat = std::numeric_limits<float>::min();
        for (int i = 0; i < stepCount; i++) {
            double lastDFactor = 0.0;
            while (true) {
                inputEvent front;
                if (!inputQueueCopy.empty()) {
                    front = inputQueueCopy.front();
                    if (front.time.QuadPart - lastFrameTime.QuadPart < stepDelta.QuadPart * (i + 1)) nextInput = front;
                    else {
                        double deltaFactor = std::max(smallestFloat, (double)(front.time.QuadPart - lastFrameTime.QuadPart - (stepDelta.QuadPart * i)) / stepDelta.QuadPart);
                        stepQueue.emplace(step{ front, deltaFactor });
                        break;
                    }
                } else if (lastDFactor != 0.0) {
                    stepQueue.emplace(step{ nextInput, lastDFactor });
                    break;
                }
                inputQueueCopy.pop();
                lastDFactor = smallestFloat;
            }
        }
    }
}

void updateStep(int step) {
    if (stepQueue.empty()) return;
    while (!stepQueue.empty() && stepQueue.front().deltaFactor <= step) {
        inputEvent nextInput = stepQueue.front().input;
        stepQueue.pop();

        PlayLayer* playLayer = PlayLayer::get();
        if (!playLayer) return;
        GameManager* gm = GameManager::sharedState();
        PlayerObject* player = nextInput.player == Player1 ? playLayer->m_player1 : playLayer->m_player2;
        if (nextInput.inputState == Press) {
            switch (nextInput.inputType) {
                case PlayerButton::Jump:
                    if (player->m_isHolding) break;
                    player->m_isHolding = true;
                    player->m_jumpHeld = true;
                    player->pushButton(0, false);
                    if (player->m_isShip) player->playJumpSound();
                    break;
                case PlayerButton::Left:
                    if (!player->m_isBird) break;
                    player->m_isHolding = true;
                    player->m_vehicleSize = PlayerObject::VehicleSize::Mini;
                    break;
                case PlayerButton::Right:
                    if (!player->m_isBird) break;
                    player->m_isHolding = true;
                    player->m_vehicleSize = PlayerObject::VehicleSize::Normal;
                    break;
            }
        } else {
            switch (nextInput.inputType) {
                case PlayerButton::Jump:
                    player->m_isHolding = false;
                    player->m_jumpHeld = false;
                    break;
                case PlayerButton::Left:
                    player->m_isHolding = false;
                    break;
                case PlayerButton::Right:
                    player->m_isHolding = false;
                    break;
            }
        }
    }
}

class $modify(PlayLayer) {
    void update(float delta) {
        PlayLayer::update(delta);
        LARGE_INTEGER frameTime;
        if (skipUpdate) return;

        for (int i = 0; i < stepCount; i++) {
            updateStep(i);
        }
        if (newFrame) {
            newFrame = false;
            LARGE_INTEGER time;
            QueryPerformanceCounter(&time);
            currentFrameTime = time;
        }
    }
};

class $modify(CCEGLView) {
    void swapBuffers() {
        LARGE_INTEGER time;
        QueryPerformanceCounter(&time);
        currentFrameTime = time;
        CCEGLView::swapBuffers();
    }
};

class $modify(GJBaseGameLayer) {
    void draw() {
        GJBaseGameLayer::draw();
        LARGE_INTEGER time;
        QueryPerformanceCounter(&time);
        currentFrameTime = time;
    }
};

class $modify(GameManager) {
    void pauseGame(bool toggle) {
        LARGE_INTEGER time;
        QueryPerformanceCounter(&time);
        currentFrameTime = time;
        GameManager::pauseGame(toggle);
    }
};

void onLoadMod() {
    InitializeCriticalSection(&inputQueueLock);
    InitializeCriticalSection(&keybindsLock);

    std::thread input(inputThread);
    input.detach();

    log::info("Click Between Frames loaded!");
}

void onUnloadMod() {
    DeleteCriticalSection(&inputQueueLock);
    DeleteCriticalSection(&keybindsLock);

    log::info("Click Between Frames unloaded!");
}

$execute { onLoadMod(); }
