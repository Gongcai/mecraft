//
// Created by seawon on 2026/3/18.
//

#include "InputManager.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#ifdef MECRAFT_DEBUG
#include <chrono>
#endif

#include "Diagnostics.h"
#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

template <size_t Count>
json boolArrayToIndices(const bool (&values)[Count]) {
    json indices = json::array();
    for (size_t index = 0; index < Count; ++index) {
        if (values[index]) {
            indices.push_back(index);
        }
    }
    return indices;
}

template <size_t Count>
void indicesToBoolArray(const json& indices, bool (&values)[Count]) {
    std::fill(std::begin(values), std::end(values), false);
    if (!indices.is_array()) {
        throw std::runtime_error("Input replay index list must be an array");
    }
    for (const json& item : indices) {
        const int index = item.get<int>();
        if (index < 0 || index >= static_cast<int>(Count)) {
            throw std::runtime_error("Input replay index is out of range");
        }
        values[static_cast<size_t>(index)] = true;
    }
}

json snapshotToJson(const InputSnapshot& snapshot) {
    json frame;
    frame["keys"] = boolArrayToIndices(snapshot.keys);
    frame["keysJustPressed"] = boolArrayToIndices(snapshot.keysJustPressed);
    frame["keysJustReleased"] = boolArrayToIndices(snapshot.keysJustReleased);
    frame["keysDoubleTapped"] = boolArrayToIndices(snapshot.keysDoubleTapped);
    frame["mouseButtons"] = boolArrayToIndices(snapshot.mouseButtons);
    frame["mouseButtonsJustPressed"] = boolArrayToIndices(snapshot.mouseButtonsJustPressed);
    frame["mouseButtonsJustReleased"] = boolArrayToIndices(snapshot.mouseButtonsJustReleased);
    frame["mouseButtonsDoubleTapped"] = boolArrayToIndices(snapshot.mouseButtonsDoubleTapped);
    frame["mousePosition"] = {snapshot.mousePosition.x, snapshot.mousePosition.y};
    frame["mouseDelta"] = {snapshot.mouseDelta.x, snapshot.mouseDelta.y};
    frame["scrollDelta"] = snapshot.scrollDelta;

    json typedChars = json::array();
    for (size_t index = 0; index < snapshot.typedCharCount; ++index) {
        typedChars.push_back(snapshot.typedChars[index]);
    }
    frame["typedChars"] = std::move(typedChars);

    const auto& drag = snapshot.draggedItem;
    frame["draggedItem"] = {
        {"active", drag.active},
        {"itemId", drag.itemId},
        {"count", drag.count},
        {"sourceSlot", drag.sourceSlot},
        {"pointerPosition", {drag.pointerPosition.x, drag.pointerPosition.y}}
    };

    json gamepad;
    gamepad["connected"] = snapshot.gamepad.connected;
    gamepad["buttons"] = boolArrayToIndices(snapshot.gamepad.buttons);
    gamepad["buttonsJustPressed"] = boolArrayToIndices(snapshot.gamepad.buttonsJustPressed);
    gamepad["buttonsJustReleased"] = boolArrayToIndices(snapshot.gamepad.buttonsJustReleased);
    gamepad["buttonsDoubleTapped"] = boolArrayToIndices(snapshot.gamepad.buttonsDoubleTapped);
    json axes = json::array();
    for (float axis : snapshot.gamepad.axes) {
        axes.push_back(axis);
    }
    gamepad["axes"] = std::move(axes);
    frame["gamepad"] = std::move(gamepad);
    return frame;
}

glm::vec2 readVec2(const json& value, const char* fieldName) {
    if (!value.is_array() || value.size() != 2) {
        throw std::runtime_error(std::string("Input replay field must be a vec2: ") + fieldName);
    }
    return {value[0].get<float>(), value[1].get<float>()};
}

InputSnapshot snapshotFromJson(const json& frame) {
    InputSnapshot snapshot;
    indicesToBoolArray(frame.at("keys"), snapshot.keys);
    indicesToBoolArray(frame.at("keysJustPressed"), snapshot.keysJustPressed);
    indicesToBoolArray(frame.at("keysJustReleased"), snapshot.keysJustReleased);
    indicesToBoolArray(frame.at("keysDoubleTapped"), snapshot.keysDoubleTapped);
    indicesToBoolArray(frame.at("mouseButtons"), snapshot.mouseButtons);
    indicesToBoolArray(frame.at("mouseButtonsJustPressed"), snapshot.mouseButtonsJustPressed);
    indicesToBoolArray(frame.at("mouseButtonsJustReleased"), snapshot.mouseButtonsJustReleased);
    indicesToBoolArray(frame.at("mouseButtonsDoubleTapped"), snapshot.mouseButtonsDoubleTapped);
    snapshot.mousePosition = readVec2(frame.at("mousePosition"), "mousePosition");
    snapshot.mouseDelta = readVec2(frame.at("mouseDelta"), "mouseDelta");
    snapshot.scrollDelta = frame.at("scrollDelta").get<double>();

    const json& typedChars = frame.at("typedChars");
    if (!typedChars.is_array() || typedChars.size() > InputSnapshot::kMaxTypedCharsPerFrame) {
        throw std::runtime_error("Input replay typed character list is invalid");
    }
    snapshot.typedCharCount = typedChars.size();
    for (size_t index = 0; index < snapshot.typedCharCount; ++index) {
        snapshot.typedChars[index] = typedChars[index].get<uint32_t>();
    }

    const json& drag = frame.at("draggedItem");
    snapshot.draggedItem.active = drag.at("active").get<bool>();
    snapshot.draggedItem.itemId = drag.at("itemId").get<int>();
    snapshot.draggedItem.count = drag.at("count").get<int>();
    snapshot.draggedItem.sourceSlot = drag.at("sourceSlot").get<int>();
    snapshot.draggedItem.pointerPosition = readVec2(drag.at("pointerPosition"), "draggedItem.pointerPosition");

    const json& gamepad = frame.at("gamepad");
    snapshot.gamepad.connected = gamepad.at("connected").get<bool>();
    indicesToBoolArray(gamepad.at("buttons"), snapshot.gamepad.buttons);
    indicesToBoolArray(gamepad.at("buttonsJustPressed"), snapshot.gamepad.buttonsJustPressed);
    indicesToBoolArray(gamepad.at("buttonsJustReleased"), snapshot.gamepad.buttonsJustReleased);
    indicesToBoolArray(gamepad.at("buttonsDoubleTapped"), snapshot.gamepad.buttonsDoubleTapped);
    const json& axes = gamepad.at("axes");
    if (!axes.is_array() || axes.size() != std::size(snapshot.gamepad.axes)) {
        throw std::runtime_error("Input replay gamepad axis list is invalid");
    }
    for (size_t index = 0; index < std::size(snapshot.gamepad.axes); ++index) {
        snapshot.gamepad.axes[index] = axes[index].get<float>();
    }
    return snapshot;
}

json replayFileFromFrames(const std::vector<InputSnapshot>& frames) {
    json root;
    root["version"] = 1;
    root["kind"] = "mecraft.input_replay";
    root["frames"] = json::array();
    for (const InputSnapshot& frame : frames) {
        root["frames"].push_back(snapshotToJson(frame));
    }
    return root;
}

std::vector<InputSnapshot> framesFromReplayFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open input replay file: " + path.string());
    }
    json root = json::parse(input);
    if (root.at("kind").get<std::string>() != "mecraft.input_replay" ||
        root.at("version").get<int>() != 1) {
        throw std::runtime_error("Unsupported input replay file: " + path.string());
    }

    const json& framesJson = root.at("frames");
    if (!framesJson.is_array()) {
        throw std::runtime_error("Input replay frames field must be an array");
    }

    std::vector<InputSnapshot> frames;
    frames.reserve(framesJson.size());
    for (const json& frameJson : framesJson) {
        frames.push_back(snapshotFromJson(frameJson));
    }
    return frames;
}

void writeReplayFile(const std::filesystem::path& path, const std::vector<InputSnapshot>& frames) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to write input replay file: " + path.string());
    }
    output << replayFileFromFrames(frames).dump(2);
}

#ifdef MECRAFT_DEBUG
double debugElapsedMs(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
#endif
} // namespace

bool InputSnapshot::isKeyHeld(int key) const {
    return key >= 0 && key <= GLFW_KEY_LAST && keys[key];
}

bool InputSnapshot::isKeyJustPressed(int key) const {
    return key >= 0 && key <= GLFW_KEY_LAST && keysJustPressed[key];
}

bool InputSnapshot::isKeyJustReleased(int key) const {
    return key >= 0 && key <= GLFW_KEY_LAST && keysJustReleased[key];
}

bool InputSnapshot::isMouseButtonHeld(int button) const {
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && mouseButtons[button];
}

bool InputSnapshot::isMouseButtonJustPressed(int button) const {
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && mouseButtonsJustPressed[button];
}

bool InputSnapshot::isMouseButtonJustReleased(int button) const {
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && mouseButtonsJustReleased[button];
}

bool InputSnapshot::isKeyDoubleTapped(int key) const {
    return key >= 0 && key <= GLFW_KEY_LAST && keysDoubleTapped[key];
}

bool InputSnapshot::isMouseButtonDoubleTapped(int button) const {
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && mouseButtonsDoubleTapped[button];
}

bool InputSnapshot::isGamepadConnected() const {
    return gamepad.connected;
}

bool InputSnapshot::isGamepadButtonHeld(int button) const {
    return button >= 0 && button <= GLFW_GAMEPAD_BUTTON_LAST && gamepad.buttons[button];
}

bool InputSnapshot::isGamepadButtonJustPressed(int button) const {
    return button >= 0 && button <= GLFW_GAMEPAD_BUTTON_LAST && gamepad.buttonsJustPressed[button];
}

bool InputSnapshot::isGamepadButtonJustReleased(int button) const {
    return button >= 0 && button <= GLFW_GAMEPAD_BUTTON_LAST && gamepad.buttonsJustReleased[button];
}

bool InputSnapshot::isGamepadButtonDoubleTapped(int button) const {
    return button >= 0 && button <= GLFW_GAMEPAD_BUTTON_LAST && gamepad.buttonsDoubleTapped[button];
}

float InputSnapshot::getGamepadAxis(int axis) const {
    if (axis < 0 || axis > GLFW_GAMEPAD_AXIS_LAST) {
        return 0.0f;
    }
    return gamepad.axes[axis];
}

void InputManager::init(GLFWwindow* windowHandle) {
    m_handle = windowHandle;
    if (m_handle == nullptr) {
        return;
    }

    glfwSetWindowUserPointer(m_handle, this);

    glfwSetKeyCallback(m_handle, keyCallback);
    glfwSetMouseButtonCallback(m_handle, mouseButtonCallback);
    glfwSetCursorPosCallback(m_handle, cursorPosCallback);
    glfwSetScrollCallback(m_handle, scrollCallback);
    glfwSetCharCallback(m_handle, charCallback);

    // Initialize baseline cursor position to avoid a large first delta.
    resetMouseDelta();
}

void InputManager::update() {
    if (m_replayActive && m_replayMode == ReplayMode::Playback) {
        if (m_playbackFrameIndex < m_playbackFrames.size()) {
            m_snapshot = m_playbackFrames[m_playbackFrameIndex++];
            m_draggedItem = m_snapshot.draggedItem;
            m_mouseX = static_cast<double>(m_snapshot.mousePosition.x);
            m_mouseY = static_cast<double>(m_snapshot.mousePosition.y);
            m_mouseDeltaX = static_cast<double>(m_snapshot.mouseDelta.x);
            m_mouseDeltaY = static_cast<double>(m_snapshot.mouseDelta.y);
            m_playbackFinished = m_playbackFrameIndex >= m_playbackFrames.size();
            return;
        }
        m_snapshot = {};
        m_draggedItem = {};
        m_playbackFinished = true;
        return;
    }

    double now = glfwGetTime();

    for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
        m_keysJustPressed[key] = m_keys[key] && !m_keysPrev[key];
        m_keysJustReleased[key] = !m_keys[key] && m_keysPrev[key];

        // 双击检测：本次刚按下 且 距上次按下时间在超时内
        m_snapshot.keysDoubleTapped[key] = m_keysJustPressed[key]
            && m_keyLastPressTime[key] > 0.0
            && (now - m_keyLastPressTime[key]) <= m_doubleTapTimeout;
        if (m_keysJustPressed[key]) {
            m_keyLastPressTime[key] = now;
        }
        // 松开后重置上次按下时间，避免松开再按被误判为双击
        // 不重置：允许双击的第二次按下可以跨松开事件
        // 如果想要"快速连按"而非"双击"语义，可取消下面的注释
        // if (m_keysJustReleased[key]) { m_keyLastPressTime[key] = 0.0; }

        m_keysPrev[key] = m_keys[key];
    }

    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        m_mouseButtonsJustPressed[button] = m_mouseButtons[button] && !m_mouseButtonsPrev[button];
        m_mouseButtonsJustReleased[button] = !m_mouseButtons[button] && m_mouseButtonsPrev[button];

        // 鼠标双击检测
        m_snapshot.mouseButtonsDoubleTapped[button] = m_mouseButtonsJustPressed[button]
            && m_mouseButtonLastPressTime[button] > 0.0
            && (now - m_mouseButtonLastPressTime[button]) <= m_doubleTapTimeout;
        if (m_mouseButtonsJustPressed[button]) {
            m_mouseButtonLastPressTime[button] = now;
        }

        m_mouseButtonsPrev[button] = m_mouseButtons[button];
    }

    // Update gamepad state
    m_gamepadConnected = glfwJoystickPresent(kGamepadJoystickId) && glfwJoystickIsGamepad(kGamepadJoystickId);

    if (m_gamepadConnected) {
        GLFWgamepadstate state;
        if (glfwGetGamepadState(kGamepadJoystickId, &state)) {
            // Update button states
            for (int btn = 0; btn <= GLFW_GAMEPAD_BUTTON_LAST; ++btn) {
                bool pressed = (state.buttons[btn] == GLFW_PRESS);
                m_gamepadButtonsJustPressed[btn] = pressed && !m_gamepadButtonsPrev[btn];
                m_gamepadButtonsJustReleased[btn] = !pressed && m_gamepadButtonsPrev[btn];

                // Gamepad button double-tap detection
                m_snapshot.gamepad.buttonsDoubleTapped[btn] = m_gamepadButtonsJustPressed[btn]
                    && m_gamepadButtonLastPressTime[btn] > 0.0
                    && (now - m_gamepadButtonLastPressTime[btn]) <= m_doubleTapTimeout;
                if (m_gamepadButtonsJustPressed[btn]) {
                    m_gamepadButtonLastPressTime[btn] = now;
                }

                m_gamepadButtons[btn] = pressed;
                m_gamepadButtonsPrev[btn] = pressed;
            }

            // Update axes with dead zone applied
            for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
                float rawValue = state.axes[axis];

                // Apply dead zone for stick axes (left/right stick X/Y)
                if (axis == GLFW_GAMEPAD_AXIS_LEFT_X || axis == GLFW_GAMEPAD_AXIS_LEFT_Y ||
                    axis == GLFW_GAMEPAD_AXIS_RIGHT_X || axis == GLFW_GAMEPAD_AXIS_RIGHT_Y) {
                    rawValue = applyDeadZone(rawValue, InputSnapshot::GamepadState::kStickDeadZone);
                }
                // Triggers (LT/RT) are mapped to [-1, 1] by GLFW, normalize to [0, 1]
                else if (axis == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER || axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    rawValue = (rawValue + 1.0f) * 0.5f; // Map [-1, 1] to [0, 1]
                }

                m_gamepadAxes[axis] = rawValue;
            }
        }
    } else {
        // Clear gamepad state if disconnected
        for (int btn = 0; btn <= GLFW_GAMEPAD_BUTTON_LAST; ++btn) {
            m_gamepadButtons[btn] = false;
            m_gamepadButtonsPrev[btn] = false;
            m_gamepadButtonsJustPressed[btn] = false;
            m_gamepadButtonsJustReleased[btn] = false;
        }
        for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
            m_gamepadAxes[axis] = 0.0f;
        }
    }

    if (m_handle == nullptr) {
        m_mouseDeltaX = 0.0;
        m_mouseDeltaY = 0.0;
        if (m_draggedItem.active) {
            m_draggedItem.pointerPosition = m_snapshot.mousePosition;
        }
        m_snapshot.draggedItem = m_draggedItem;
        return;
    }

    // Use accumulated delta from callbacks
    m_mouseDeltaX = m_accumDeltaX;
    m_mouseDeltaY = m_accumDeltaY;
    m_accumDeltaX = 0.0;
    m_accumDeltaY = 0.0;

    // Filter out tiny spurious deltas (likely from floating-point noise or cursor drift)
    // Only keep deltas with magnitude >= 1.5 pixels
    constexpr double kMouseDeltaThreshold = 1.5;
    if (std::abs(m_mouseDeltaX) < kMouseDeltaThreshold) {
        m_mouseDeltaX = 0.0;
    }
    if (std::abs(m_mouseDeltaY) < kMouseDeltaThreshold) {
        m_mouseDeltaY = 0.0;
    }

    for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
        m_snapshot.keys[key] = m_keys[key];
        m_snapshot.keysJustPressed[key] = m_keysJustPressed[key];
        m_snapshot.keysJustReleased[key] = m_keysJustReleased[key];
    }
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        m_snapshot.mouseButtons[button] = m_mouseButtons[button];
        m_snapshot.mouseButtonsJustPressed[button] = m_mouseButtonsJustPressed[button];
        m_snapshot.mouseButtonsJustReleased[button] = m_mouseButtonsJustReleased[button];
    }

    // Copy gamepad state to snapshot
    m_snapshot.gamepad.connected = m_gamepadConnected;
    for (int btn = 0; btn <= GLFW_GAMEPAD_BUTTON_LAST; ++btn) {
        m_snapshot.gamepad.buttons[btn] = m_gamepadButtons[btn];
        m_snapshot.gamepad.buttonsJustPressed[btn] = m_gamepadButtonsJustPressed[btn];
        m_snapshot.gamepad.buttonsJustReleased[btn] = m_gamepadButtonsJustReleased[btn];
    }
    for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
        m_snapshot.gamepad.axes[axis] = m_gamepadAxes[axis];
    }

    m_snapshot.mousePosition = {
        static_cast<float>(m_mouseX),
        static_cast<float>(m_mouseY)
    };
    m_snapshot.mouseDelta = {
        static_cast<float>(m_mouseDeltaX),
        static_cast<float>(m_mouseDeltaY)
    };

    if (m_draggedItem.active) {
        m_draggedItem.pointerPosition = m_snapshot.mousePosition;
    }
    m_snapshot.draggedItem = m_draggedItem;
    m_snapshot.scrollDelta = m_accumScrollY;
    m_accumScrollY = 0.0;

    m_snapshot.typedCharCount = m_typedCharCount;
    for (size_t i = 0; i < m_typedCharCount; ++i) {
        m_snapshot.typedChars[i] = m_typedChars[i];
    }
    m_typedCharCount = 0;

    if (m_replayActive && m_replayMode == ReplayMode::Recording) {
        m_recordedFrames.push_back(m_snapshot);
        m_recordingDirty = true;
    }
}

const InputSnapshot& InputManager::snapshot() const {
    return m_snapshot;
}

void InputManager::configureInputRecording(const std::filesystem::path& path) {
    shutdownInputReplay();
    m_replayPath = path;
    m_recordedFrames.clear();
    m_playbackFrames.clear();
    m_playbackFrameIndex = 0;
    m_playbackFinished = false;
    m_recordingDirty = false;
    m_replayActive = false;
    m_replayMode = ReplayMode::Recording;
}

void InputManager::configureInputPlayback(const std::filesystem::path& path) {
    shutdownInputReplay();
    m_replayPath = path;
    m_recordedFrames.clear();
    m_playbackFrames = framesFromReplayFile(path);
    m_playbackFrameIndex = 0;
    m_playbackFinished = m_playbackFrames.empty();
    m_recordingDirty = false;
    m_replayActive = false;
    m_replayMode = ReplayMode::Playback;
}

void InputManager::setInputReplayActive(const bool active) {
    if (m_replayMode == ReplayMode::None) {
        return;
    }
    if (m_replayActive == active) {
        return;
    }
    m_replayActive = active;
    if (active) {
        m_replayActiveStartTime = glfwGetTime();
        resetMouseDelta();
        return;
    }
    if (m_replayMode == ReplayMode::Recording && m_recordingDirty) {
        writeReplayFile(m_replayPath, m_recordedFrames);
        m_recordingDirty = false;
    }
}

void InputManager::shutdownInputReplay() {
    if (m_replayMode == ReplayMode::Recording && m_recordingDirty) {
        writeReplayFile(m_replayPath, m_recordedFrames);
    }
    m_replayMode = ReplayMode::None;
    m_replayActive = false;
    m_playbackFinished = false;
    m_recordingDirty = false;
    m_replayActiveStartTime = 0.0;
    m_replayPath.clear();
    m_recordedFrames.clear();
    m_playbackFrames.clear();
    m_playbackFrameIndex = 0;
}

double InputManager::inputReplayActiveSeconds() const {
    if (!m_replayActive) {
        return 0.0;
    }
    return std::max(0.0, glfwGetTime() - m_replayActiveStartTime);
}

InputManager* InputManager::fromWindow(GLFWwindow* w) {
    if (w == nullptr) {
        return nullptr;
    }
    return static_cast<InputManager*>(glfwGetWindowUserPointer(w));
}

void InputManager::captureMouse(bool capture) {
    if (m_handle == nullptr) {
        return;
    }

    glfwSetInputMode(
        m_handle,
        GLFW_CURSOR,
        capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
    );

    // --- 新增：尝试开启/关闭原始鼠标运动 ---
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(
            m_handle,
            GLFW_RAW_MOUSE_MOTION,
            capture ? GLFW_TRUE : GLFW_FALSE
        );
    }
    // ----------------------------------------

    // Avoid stale delta spikes after cursor mode switches.
    resetMouseDelta();
}

void InputManager::resetMouseDelta() {
    if (m_handle != nullptr) {
        glfwGetCursorPos(m_handle, &m_mouseX, &m_mouseY);
    }
    m_lastMouseX = m_mouseX;
    m_lastMouseY = m_mouseY;
    m_mouseDeltaX = 0.0;
    m_mouseDeltaY = 0.0;
    m_accumDeltaX = 0.0;
    m_accumDeltaY = 0.0;
    m_accumScrollY = 0.0;
    // Ignore the first sample after reset/capture toggle to avoid spikes.
    m_firstMouse = true;
}

void InputManager::beginUIDragItem(const int itemId, const int count, const int sourceSlot) {
    if (itemId <= 0) {
        clearUIDragItem();
        return;
    }

    m_draggedItem.active = true;
    m_draggedItem.itemId = itemId;
    m_draggedItem.count = count;
    m_draggedItem.sourceSlot = sourceSlot;
    m_draggedItem.pointerPosition = {static_cast<float>(m_mouseX), static_cast<float>(m_mouseY)};
    m_snapshot.draggedItem = m_draggedItem;
}

void InputManager::clearUIDragItem() {
    m_draggedItem = {};
    m_snapshot.draggedItem = m_draggedItem;
}

const InputSnapshot::UIDragPayload& InputManager::getUIDragItem() const {
    return m_draggedItem;
}

void InputManager::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    auto* self = fromWindow(w);
    if (self == nullptr || key < 0 || key > GLFW_KEY_LAST) {
        return;
    }
#ifdef MECRAFT_DEBUG
    const auto debugStart = std::chrono::steady_clock::now();
    ++self->m_debugEventStats.keyEvents;
#endif

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        self->m_keys[key] = true;
    } else if (action == GLFW_RELEASE) {
        self->m_keys[key] = false;
    }
#ifdef MECRAFT_DEBUG
    self->m_debugEventStats.keyCallbackMs += debugElapsedMs(debugStart);
#endif
}

void InputManager::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* self = fromWindow(w);
    if (self == nullptr || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) {
        return;
    }
#ifdef MECRAFT_DEBUG
    const auto debugStart = std::chrono::steady_clock::now();
    ++self->m_debugEventStats.mouseButtonEvents;
#endif

    if (action == GLFW_PRESS) {
        self->m_mouseButtons[button] = true;
    } else if (action == GLFW_RELEASE) {
        self->m_mouseButtons[button] = false;
    }
#ifdef MECRAFT_DEBUG
    self->m_debugEventStats.mouseButtonCallbackMs += debugElapsedMs(debugStart);
#endif
}

void InputManager::cursorPosCallback(GLFWwindow* w, double xpos, double ypos) {
    auto* self = fromWindow(w);
    if (self == nullptr) {
        return;
    }
#ifdef MECRAFT_DEBUG
    const auto debugStart = std::chrono::steady_clock::now();
    ++self->m_debugEventStats.cursorPosEvents;
#endif
    if (self->m_firstMouse) {
        self->m_lastMouseX = xpos;
        self->m_lastMouseY = ypos;
        self->m_firstMouse = false;
    }

    // Keep absolute cursor position updated; frame delta baseline lives in m_lastMouseX/m_lastMouseY.
    self->m_accumDeltaX += xpos - self->m_lastMouseX;
    self->m_accumDeltaY += ypos - self->m_lastMouseY;

    self->m_lastMouseX = xpos;
    self->m_lastMouseY = ypos;
    self->m_mouseX = xpos;
    self->m_mouseY = ypos;
#ifdef MECRAFT_DEBUG
    self->m_debugEventStats.cursorPosCallbackMs += debugElapsedMs(debugStart);
#endif
}

void InputManager::scrollCallback(GLFWwindow* w, double /*xoffset*/, double yoffset) {
    auto* self = fromWindow(w);
    if (self == nullptr) {
        return;
    }
#ifdef MECRAFT_DEBUG
    const auto debugStart = std::chrono::steady_clock::now();
    ++self->m_debugEventStats.scrollEvents;
#endif
    self->m_accumScrollY += yoffset;
#ifdef MECRAFT_DEBUG
    self->m_debugEventStats.scrollCallbackMs += debugElapsedMs(debugStart);
#endif
}

void InputManager::charCallback(GLFWwindow* w, unsigned int codepoint) {
    auto* self = fromWindow(w);
    if (self == nullptr) {
        return;
    }
#ifdef MECRAFT_DEBUG
    const auto debugStart = std::chrono::steady_clock::now();
    ++self->m_debugEventStats.charEvents;
#endif
    if (self->m_typedCharCount >= InputSnapshot::kMaxTypedCharsPerFrame) {
#ifdef MECRAFT_DEBUG
        self->m_debugEventStats.charCallbackMs += debugElapsedMs(debugStart);
#endif
        return;
    }

    self->m_typedChars[self->m_typedCharCount++] = codepoint;
#ifdef MECRAFT_DEBUG
    self->m_debugEventStats.charCallbackMs += debugElapsedMs(debugStart);
#endif
}

float InputManager::applyDeadZone(float value, float deadZone) {
    if (value > deadZone) {
        return (value - deadZone) / (1.0f - deadZone);
    }
    if (value < -deadZone) {
        return (value + deadZone) / (1.0f - deadZone);
    }
    return 0.0f;
}

