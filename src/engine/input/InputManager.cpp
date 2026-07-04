//
// Created by seawon on 2026/3/18.
//

#include "InputManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

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
bool indicesToBoolArray(const json& indices, bool (&values)[Count]) {
    std::fill(std::begin(values), std::end(values), false);
    if (!indices.is_array()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Index list must be an array\n");
        return false;
    }
    for (const json& item : indices) {
        if (!item.is_number_integer()) {
            MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Index list contains a non-integer value\n");
            return false;
        }
        const auto index = item.get<int64_t>();
        if (index < 0 || index >= static_cast<int64_t>(Count)) {
            MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Index is out of range\n");
            return false;
        }
        values[static_cast<size_t>(index)] = true;
    }
    return true;
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

const json* findField(const json& object, const char* fieldName) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(fieldName);
    return it != object.end() ? &(*it) : nullptr;
}

bool readBoolField(const json& object, const char* fieldName, bool& out) {
    const json* value = findField(object, fieldName);
    if (value == nullptr || !value->is_boolean()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Field must be a boolean: %s\n", fieldName);
        return false;
    }
    out = value->get<bool>();
    return true;
}

bool readIntField(const json& object, const char* fieldName, int& out) {
    const json* value = findField(object, fieldName);
    if (value == nullptr || !value->is_number_integer()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Field must be an integer: %s\n", fieldName);
        return false;
    }
    const auto raw = value->get<int64_t>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Integer field is out of range: %s\n", fieldName);
        return false;
    }
    out = static_cast<int>(raw);
    return true;
}

bool readUint32Value(const json& value, const char* fieldName, uint32_t& out) {
    if (!value.is_number_integer()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Field must be an unsigned integer: %s\n", fieldName);
        return false;
    }
    const auto raw = value.get<int64_t>();
    if (raw < 0 || raw > std::numeric_limits<uint32_t>::max()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Unsigned integer field is out of range: %s\n", fieldName);
        return false;
    }
    out = static_cast<uint32_t>(raw);
    return true;
}

bool readDoubleField(const json& object, const char* fieldName, double& out) {
    const json* value = findField(object, fieldName);
    if (value == nullptr || !value->is_number()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Field must be numeric: %s\n", fieldName);
        return false;
    }
    out = value->get<double>();
    return true;
}

bool readFloatValue(const json& value, const char* fieldName, float& out) {
    if (!value.is_number()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Field must be numeric: %s\n", fieldName);
        return false;
    }
    out = value.get<float>();
    return true;
}

bool readStringField(const json& object, const char* fieldName, std::string& out) {
    const json* value = findField(object, fieldName);
    if (value == nullptr || !value->is_string()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Field must be a string: %s\n", fieldName);
        return false;
    }
    out = value->get<std::string>();
    return true;
}

bool readVec2(const json& value, const char* fieldName, glm::vec2& out) {
    if (!value.is_array() || value.size() != 2) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Field must be a vec2: %s\n", fieldName);
        return false;
    }
    float x = 0.0f;
    float y = 0.0f;
    if (!readFloatValue(value[0], fieldName, x) || !readFloatValue(value[1], fieldName, y)) {
        return false;
    }
    out = {x, y};
    return true;
}

template <size_t Count>
bool readIndexArrayField(const json& object, const char* fieldName, bool (&values)[Count]) {
    const json* field = findField(object, fieldName);
    if (field == nullptr) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Missing index array field: %s\n", fieldName);
        return false;
    }
    return indicesToBoolArray(*field, values);
}

bool snapshotFromJson(const json& frame, InputSnapshot& snapshot) {
    if (!frame.is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Frame must be an object\n");
        return false;
    }

    const json* mousePosition = findField(frame, "mousePosition");
    const json* mouseDelta = findField(frame, "mouseDelta");
    const json* typedChars = findField(frame, "typedChars");
    const json* drag = findField(frame, "draggedItem");
    const json* gamepad = findField(frame, "gamepad");

    if (mousePosition == nullptr || mouseDelta == nullptr || typedChars == nullptr ||
        drag == nullptr || gamepad == nullptr) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Frame is missing required fields\n");
        return false;
    }

    if (!readIndexArrayField(frame, "keys", snapshot.keys) ||
        !readIndexArrayField(frame, "keysJustPressed", snapshot.keysJustPressed) ||
        !readIndexArrayField(frame, "keysJustReleased", snapshot.keysJustReleased) ||
        !readIndexArrayField(frame, "keysDoubleTapped", snapshot.keysDoubleTapped) ||
        !readIndexArrayField(frame, "mouseButtons", snapshot.mouseButtons) ||
        !readIndexArrayField(frame, "mouseButtonsJustPressed", snapshot.mouseButtonsJustPressed) ||
        !readIndexArrayField(frame, "mouseButtonsJustReleased", snapshot.mouseButtonsJustReleased) ||
        !readIndexArrayField(frame, "mouseButtonsDoubleTapped", snapshot.mouseButtonsDoubleTapped) ||
        !readVec2(*mousePosition, "mousePosition", snapshot.mousePosition) ||
        !readVec2(*mouseDelta, "mouseDelta", snapshot.mouseDelta) ||
        !readDoubleField(frame, "scrollDelta", snapshot.scrollDelta)) {
        return false;
    }

    if (!typedChars->is_array() || typedChars->size() > InputSnapshot::kMaxTypedCharsPerFrame) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Typed character list is invalid\n");
        return false;
    }
    snapshot.typedCharCount = typedChars->size();
    for (size_t index = 0; index < snapshot.typedCharCount; ++index) {
        if (!readUint32Value((*typedChars)[index], "typedChars", snapshot.typedChars[index])) {
            return false;
        }
    }

    if (!drag->is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Dragged item field must be an object\n");
        return false;
    }
    const json* pointerPosition = findField(*drag, "pointerPosition");
    if (pointerPosition == nullptr ||
        !readBoolField(*drag, "active", snapshot.draggedItem.active) ||
        !readIntField(*drag, "itemId", snapshot.draggedItem.itemId) ||
        !readIntField(*drag, "count", snapshot.draggedItem.count) ||
        !readIntField(*drag, "sourceSlot", snapshot.draggedItem.sourceSlot) ||
        !readVec2(*pointerPosition, "draggedItem.pointerPosition", snapshot.draggedItem.pointerPosition)) {
        return false;
    }

    if (!gamepad->is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Gamepad field must be an object\n");
        return false;
    }
    const json* axes = findField(*gamepad, "axes");
    if (axes == nullptr || !axes->is_array() || axes->size() != std::size(snapshot.gamepad.axes)) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Gamepad axis list is invalid\n");
        return false;
    }
    if (!readBoolField(*gamepad, "connected", snapshot.gamepad.connected) ||
        !readIndexArrayField(*gamepad, "buttons", snapshot.gamepad.buttons) ||
        !readIndexArrayField(*gamepad, "buttonsJustPressed", snapshot.gamepad.buttonsJustPressed) ||
        !readIndexArrayField(*gamepad, "buttonsJustReleased", snapshot.gamepad.buttonsJustReleased) ||
        !readIndexArrayField(*gamepad, "buttonsDoubleTapped", snapshot.gamepad.buttonsDoubleTapped)) {
        return false;
    }

    for (size_t index = 0; index < std::size(snapshot.gamepad.axes); ++index) {
        if (!readFloatValue((*axes)[index], "gamepad.axes", snapshot.gamepad.axes[index])) {
            return false;
        }
    }
    return true;
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

bool framesFromReplayFile(const std::filesystem::path& path, std::vector<InputSnapshot>& outFrames) {
    outFrames.clear();
    std::ifstream input(path);
    if (!input) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Failed to open input replay file: %s\n", path.string().c_str());
        return false;
    }

    json root = json::parse(input, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Invalid input replay JSON: %s\n", path.string().c_str());
        return false;
    }

    std::string kind;
    int version = 0;
    if (!readStringField(root, "kind", kind) ||
        !readIntField(root, "version", version) ||
        kind != "mecraft.input_replay" ||
        version != 1) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Unsupported input replay file: %s\n", path.string().c_str());
        return false;
    }

    const json* framesJson = findField(root, "frames");
    if (framesJson == nullptr || !framesJson->is_array()) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Frames field must be an array\n");
        return false;
    }

    std::vector<InputSnapshot> frames;
    frames.reserve(framesJson->size());
    for (const json& frameJson : *framesJson) {
        InputSnapshot snapshot;
        if (!snapshotFromJson(frameJson, snapshot)) {
            return false;
        }
        frames.push_back(snapshot);
    }
    outFrames = std::move(frames);
    return true;
}

bool writeReplayFile(const std::filesystem::path& path, const std::vector<InputSnapshot>& frames) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code createError;
        std::filesystem::create_directories(parent, createError);
        if (createError) {
            MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Failed to create replay directory %s: %s\n",
                                parent.string().c_str(), createError.message().c_str());
            return false;
        }
    }
    std::ofstream output(path);
    if (!output) {
        MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Failed to write input replay file: %s\n", path.string().c_str());
        return false;
    }
    output << replayFileFromFrames(frames).dump(2);
    return static_cast<bool>(output);
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

bool InputManager::configureInputRecording(const std::filesystem::path& path) {
    shutdownInputReplay();
    m_replayPath = path;
    m_recordedFrames.clear();
    m_playbackFrames.clear();
    m_playbackFrameIndex = 0;
    m_playbackFinished = false;
    m_recordingDirty = false;
    m_replayActive = false;
    m_replayMode = ReplayMode::Recording;
    return true;
}

bool InputManager::configureInputPlayback(const std::filesystem::path& path) {
    shutdownInputReplay();
    m_replayPath = path;
    m_recordedFrames.clear();
    if (!framesFromReplayFile(path, m_playbackFrames)) {
        m_replayPath.clear();
        m_playbackFrames.clear();
        m_replayMode = ReplayMode::None;
        return false;
    }
    m_playbackFrameIndex = 0;
    m_playbackFinished = m_playbackFrames.empty();
    m_recordingDirty = false;
    m_replayActive = false;
    m_replayMode = ReplayMode::Playback;
    return true;
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
        if (!writeReplayFile(m_replayPath, m_recordedFrames)) {
            MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Failed to flush input recording\n");
        }
        m_recordingDirty = false;
    }
}

void InputManager::shutdownInputReplay() {
    if (m_replayMode == ReplayMode::Recording && m_recordingDirty) {
        if (!writeReplayFile(m_replayPath, m_recordedFrames)) {
            MECRAFT_LOG_FPRINTF(stderr, "[InputReplay] Failed to flush input recording during shutdown\n");
        }
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
