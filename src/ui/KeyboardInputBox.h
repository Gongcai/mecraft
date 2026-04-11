#ifndef MECRAFT_KEYBOARDINPUTBOX_H
#define MECRAFT_KEYBOARDINPUTBOX_H

#include <cstddef>
#include <string>

#include "../core/InputManager.h"

class InputContextManager;

// Lightweight keyboard-only text input used by command-like UI.
class KeyboardInputBox {
public:
    explicit KeyboardInputBox(size_t maxLength = 128);

    void open(const std::string& initialText = "");
    void close();
    [[nodiscard]] bool isOpen() const;

    void update(const InputSnapshot& snapshot,
                float dtSeconds = 0.0f,
                const InputContextManager* inputContext = nullptr);

    [[nodiscard]] const std::string& getText() const;
    void setText(const std::string& text);
    void clear();

    [[nodiscard]] size_t getMaxLength() const;
    void setMaxLength(size_t maxLength);

    // Returns true only once after Enter/Escape was pressed while open.
    bool consumeSubmit(std::string* submittedText = nullptr);
    bool consumeCancel();

private:

    bool m_open = false;
    size_t m_maxLength = 128;
    std::string m_text;
    bool m_submitRequested = false;
    bool m_cancelRequested = false;
    float m_backspaceHoldElapsed = 0.0f;
    float m_backspaceRepeatAccumulator = 0.0f;
    bool m_backspaceActionActiveLastFrame = false;
};

#endif // MECRAFT_KEYBOARDINPUTBOX_H

