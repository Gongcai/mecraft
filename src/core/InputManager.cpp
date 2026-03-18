//
// Created by seawon on 2026/3/18.
//

#include "InputManager.h"

#include <iostream>
#include <ostream>

void InputManager::init(GLFWwindow* windowHandle) {
    m_handle = windowHandle;
    if (m_handle == nullptr) {
        return;
    }

    // Bind this instance to GLFW so static callbacks can forward state updates.
    glfwSetWindowUserPointer(m_handle, this);
    glfwSetKeyCallback(m_handle, keyCallback);
    glfwSetMouseButtonCallback(m_handle, mouseButtonCallback);
    glfwSetCursorPosCallback(m_handle, cursorPosCallback);
    glfwSetScrollCallback(m_handle, scrollCallback);

    // Initialize baseline cursor position to avoid a large first delta.
    resetMouseDelta();
}

void InputManager::update() {
    for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
        m_keysJustPressed[key] = m_keys[key] && !m_keysPrev[key];
        m_keysJustReleased[key] = !m_keys[key] && m_keysPrev[key];
        m_keysPrev[key] = m_keys[key];
    }

    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        m_mouseButtonsJustPressed[button] = m_mouseButtons[button] && !m_mouseButtonsPrev[button];
        m_mouseButtonsPrev[button] = m_mouseButtons[button];
    }

    if (m_handle == nullptr) {
        m_mouseDeltaX = 0.0;
        m_mouseDeltaY = 0.0;
        return;
    }

    // Use accumulated delta from callbacks
    m_mouseDeltaX = m_accumDeltaX;
    m_mouseDeltaY = m_accumDeltaY;
    m_accumDeltaX = 0.0;
    m_accumDeltaY = 0.0;
}

bool InputManager::isKeyHeld(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) {
        return false;
    }
    return m_keys[key];
}

bool InputManager::isKeyJustPressed(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) {
        return false;
    }
    return m_keysJustPressed[key];
}

bool InputManager::isKeyJustReleased(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) {
        return false;
    }
    return m_keysJustReleased[key];
}

bool InputManager::isMouseButtonHeld(int button) const {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) {
        return false;
    }
    return m_mouseButtons[button];
}

bool InputManager::isMouseButtonJustPressed(int button) const {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) {
        return false;
    }
    return m_mouseButtonsJustPressed[button];
}

glm::vec2 InputManager::getMousePosition() const {
    return {static_cast<float>(m_mouseX), static_cast<float>(m_mouseY)};
}

glm::vec2 InputManager::getMouseDelta() const {
    return {
        static_cast<float>(m_mouseDeltaX),
        static_cast<float>(m_mouseDeltaY)
    };
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
    // Ignore the first sample after reset/capture toggle to avoid spikes.
    m_firstMouse = true;
}

void InputManager::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(w));
    if (self == nullptr || key < 0 || key > GLFW_KEY_LAST) {
        return;
    }

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        self->m_keys[key] = true;
    } else if (action == GLFW_RELEASE) {
        self->m_keys[key] = false;
    }
}

void InputManager::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(w));
    if (self == nullptr || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) {
        return;
    }

    if (action == GLFW_PRESS) {
        self->m_mouseButtons[button] = true;
    } else if (action == GLFW_RELEASE) {
        self->m_mouseButtons[button] = false;
    }
}

void InputManager::cursorPosCallback(GLFWwindow* w, double xpos, double ypos) {
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(w));
    if (self == nullptr) {
        return;
    }
    std::cout << xpos << ", " << ypos << std::endl;
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
}

void InputManager::scrollCallback(GLFWwindow* w, double /*xoffset*/, double /*yoffset*/) {
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(w));
    if (self == nullptr) {
        return;
    }

    // Scroll is currently not exposed by the public API.
}
