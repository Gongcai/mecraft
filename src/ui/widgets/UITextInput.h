#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "renderer/rhi/RhiHandles.h"
#include "../core/UIStyle.h"
#include "../core/UIWidget.h"

class RhiDevice;

// General-purpose text input widget with cursor, selection, and keyboard navigation.
// Supports: typing, backspace, delete, left/right arrows, home/end, shift+arrow selection.
class UITextInput : public UIWidget {
public:
    UITextInput();
    ~UITextInput() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setText(const std::string& text);
    [[nodiscard]] const std::string& getText() const { return m_text; }

    void setPlaceholder(const std::string& placeholder) { m_placeholder = placeholder; }
    [[nodiscard]] const std::string& getPlaceholder() const { return m_placeholder; }

    void setMaxLength(std::size_t maxLength);
    [[nodiscard]] std::size_t getMaxLength() const { return m_maxLength; }

    // Select all text.
    void selectAll();

    // Local color overrides.
    void setBackgroundColor(const Color& c) {
        m_bgColor = c;
        m_hasLocalColors = true;
    }
    void setBorderColor(const Color& c) {
        m_borderColor = c;
        m_hasLocalColors = true;
    }
    void setBorderFocusedColor(const Color& c) {
        m_borderFocusedColor = c;
        m_hasLocalColors = true;
    }
    void setTextColor(const Color& c) {
        m_textColor = c;
        m_hasLocalColors = true;
    }
    void setPlaceholderColor(const Color& c) {
        m_placeholderColor = c;
        m_hasLocalColors = true;
    }
    void setSelectionColor(const Color& c) {
        m_selectionColor = c;
        m_hasLocalColors = true;
    }
    void setCursorColor(const Color& c) {
        m_cursorColor = c;
        m_hasLocalColors = true;
    }
    void setStyle(const UITextInputStyle& style);
    void clearLocalStyle();

    // Callbacks.
    std::function<void(const std::string&)> onTextChanged;
    std::function<void(const std::string&)> onSubmit;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    void onUpdate(float dt) override;

private:
    void initMesh();
    void cleanupMesh();

    // Cursor and selection helpers.
    int charIndexFromX(float localX, const UIRenderContext& ctx) const;
    [[nodiscard]] float measureTextUpTo(int index, const UIRenderContext& ctx) const;
    void deleteSelection();
    void clampCursor();
    [[nodiscard]] bool hasSelection() const { return m_selStart != m_selEnd; }

    // Insert a UTF-8 string at the cursor position.
    void insertText(const std::string& text);
    [[nodiscard]] UITextInputStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] int currentStyleState() const;

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_vertexBuffer;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;

    std::string m_text;
    std::string m_placeholder;
    std::size_t m_maxLength = 256;

    int m_cursorPos = 0; // Byte offset into m_text.
    int m_selStart = 0; // Byte offset, selection start (== m_selEnd when no selection).
    int m_selEnd = 0; // Byte offset, selection end.

    float m_scrollOffset = 0.0f; // Horizontal scroll offset in pixels.
    float m_cursorBlinkTimer = 0.0f;
    bool m_cursorVisible = true;
    bool m_hovered = false;
    static constexpr float kCursorBlinkRate = 1.0f; // Full cycle in seconds.

    bool m_hasLocalColors = false;
    Color m_bgColor{0.15f, 0.15f, 0.15f, 0.9f};
    Color m_borderColor{0.40f, 0.40f, 0.40f, 0.7f};
    Color m_borderFocusedColor{0.2f, 0.8f, 1.0f, 1.0f};
    Color m_textColor{1.0f, 1.0f, 1.0f, 1.0f};
    Color m_placeholderColor{0.5f, 0.5f, 0.5f, 0.8f};
    Color m_selectionColor{0.2f, 0.5f, 0.9f, 0.4f};
    Color m_cursorColor{1.0f, 1.0f, 1.0f, 0.9f};
    bool m_hasLocalStyle = false;
    UITextInputStyle m_localStyle;
};
