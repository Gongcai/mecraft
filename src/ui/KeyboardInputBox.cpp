#include "KeyboardInputBox.h"

#include "../core/InputContextManager.h"

#include "player/ActionMap.h"

namespace {
constexpr float kBackspaceInitialDelaySeconds = 0.28f;
constexpr float kBackspaceRepeatIntervalSeconds = 0.05f;
}

KeyboardInputBox::KeyboardInputBox(size_t maxLength)
    : m_maxLength(maxLength) {
}

void KeyboardInputBox::open(const std::string& initialText) {
    m_open = true;
    m_submitRequested = false;
    m_cancelRequested = false;
    m_backspaceHoldElapsed = 0.0f;
    m_backspaceRepeatAccumulator = 0.0f;
    m_backspaceActionActiveLastFrame = false;
    setText(initialText);
}

void KeyboardInputBox::close() {
    m_open = false;
    m_backspaceHoldElapsed = 0.0f;
    m_backspaceRepeatAccumulator = 0.0f;
    m_backspaceActionActiveLastFrame = false;
}

bool KeyboardInputBox::isOpen() const {
    return m_open;
}

void KeyboardInputBox::update(const InputSnapshot& snapshot,
                              const float dtSeconds,
                              const InputContextManager* inputContext) {
    if (!m_open) {
        return;
    }

    if (inputContext == nullptr) {
        return;
    }

    const bool cancelTriggered = inputContext->isActionTriggered(Action::Cancel);
    if (cancelTriggered) {
        m_cancelRequested = true;
        m_open = false;
        return;
    }

    const bool submitTriggered = inputContext->isActionTriggered(Action::Confirm);
    if (submitTriggered) {
        m_submitRequested = true;
        m_open = false;
        return;
    }

    const bool backspaceActive = inputContext->isActionTriggered(Action::Backspace);
    const bool backspacePressed = backspaceActive && !m_backspaceActionActiveLastFrame;

    if (backspacePressed && !m_text.empty()) {
        m_text.pop_back();
        m_backspaceHoldElapsed = 0.0f;
        m_backspaceRepeatAccumulator = 0.0f;
    } else if (backspaceActive) {
        m_backspaceHoldElapsed += dtSeconds;
        if (m_backspaceHoldElapsed > kBackspaceInitialDelaySeconds) {
            m_backspaceRepeatAccumulator += dtSeconds;
            while (m_backspaceRepeatAccumulator >= kBackspaceRepeatIntervalSeconds && !m_text.empty()) {
                m_text.pop_back();
                m_backspaceRepeatAccumulator -= kBackspaceRepeatIntervalSeconds;
            }
        }
    } else {
        m_backspaceHoldElapsed = 0.0f;
        m_backspaceRepeatAccumulator = 0.0f;
    }
    m_backspaceActionActiveLastFrame = backspaceActive;

    for (size_t i = 0; i < snapshot.typedCharCount; ++i) {
        const uint32_t codepoint = snapshot.typedChars[i];
        if (codepoint < 32 || codepoint > 126) {
            continue;
        }
        if (m_text.size() >= m_maxLength) {
            break;
        }

        const char character = static_cast<char>(codepoint);
        m_text.push_back(character);
    }
}

const std::string& KeyboardInputBox::getText() const {
    return m_text;
}

void KeyboardInputBox::setText(const std::string& text) {
    m_text = text.substr(0, m_maxLength);
}

void KeyboardInputBox::clear() {
    m_text.clear();
}

size_t KeyboardInputBox::getMaxLength() const {
    return m_maxLength;
}

void KeyboardInputBox::setMaxLength(size_t maxLength) {
    m_maxLength = maxLength;
    if (m_text.size() > m_maxLength) {
        m_text.resize(m_maxLength);
    }
}

bool KeyboardInputBox::consumeSubmit(std::string* submittedText) {
    if (!m_submitRequested) {
        return false;
    }

    if (submittedText != nullptr) {
        *submittedText = m_text;
    }
    m_submitRequested = false;
    return true;
}

bool KeyboardInputBox::consumeCancel() {
    if (!m_cancelRequested) {
        return false;
    }

    m_cancelRequested = false;
    return true;
}

