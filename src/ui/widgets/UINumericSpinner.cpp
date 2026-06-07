#include "UINumericSpinner.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../core/UIRenderUtils.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"

UINumericSpinner::UINumericSpinner() {
    interactive = true;
    focusable = true;
    width = 120.0f;
    height = 28.0f;
}

UINumericSpinner::~UINumericSpinner() {
    shutdown();
}

void UINumericSpinner::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    UIWidget::init(resourceMgr);
}

void UINumericSpinner::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
    UIWidget::shutdown();
}

void UINumericSpinner::setValue(float value) {
    m_value = std::clamp(value, m_min, m_max);
    if (m_decimals == 0) {
        m_value = std::round(m_value);
    }
}

void UINumericSpinner::setRange(float min, float max) {
    m_min = min;
    m_max = max;
    setValue(m_value);
}

void UINumericSpinner::setStyle(const UINumericSpinnerStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UINumericSpinner::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UINumericSpinner::applyStep(float delta) {
    setValue(m_value + delta);
    if (onValueChanged) onValueChanged(m_value);
}

void UINumericSpinner::commitEditText() {
    if (m_editText.empty()) {
        m_editing = false;
        return;
    }
    // Parse the edit text as a float.
    char* end = nullptr;
    const float parsed = std::strtof(m_editText.c_str(), &end);
    if (end != m_editText.c_str()) {
        setValue(parsed);
        if (onValueChanged) onValueChanged(m_value);
    }
    m_editing = false;
    m_editText.clear();
}

std::string UINumericSpinner::formatValue() const {
    char buf[64];
    if (m_decimals <= 0) {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::round(m_value)));
    } else {
        char fmt[32];
        std::snprintf(fmt, sizeof(fmt), "%%.%df", m_decimals);
        std::snprintf(buf, sizeof(buf), fmt, m_value);
    }
    return buf;
}

// Returns: -1 = minus, 0 = value, 1 = plus, -2 = outside.
int UINumericSpinner::hitTestZone(float px, float py, const UIRenderContext& ctx) const {
    const float flippedY = static_cast<float>(ctx.screenHeight) - py;
    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;
    const UIResolvedNumericSpinnerStyle resolved = resolveStyle(ctx);

    if (px < ax || px >= ax + aw || flippedY < ay || flippedY >= ay + ah) return -2;

    const float localX = px - ax;
    if (localX < resolved.buttonWidth) return -1;
    if (localX >= aw - resolved.buttonWidth) return 1;
    return 0;
}

void UINumericSpinner::onUpdate(float dt) {
    if (m_editing && isFocused()) {
        m_cursorBlinkTimer += dt;
        if (m_cursorBlinkTimer >= kCursorBlinkRate) {
            m_cursorBlinkTimer -= kCursorBlinkRate;
        }
        m_cursorVisible = m_cursorBlinkTimer < (kCursorBlinkRate * 0.5f);

        // Backspace auto-repeat.
        const bool backspaceActive = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_BACKSPACE) == GLFW_PRESS;
        const bool backspacePressed = backspaceActive && !m_backspaceActiveLastFrame;
        if (backspacePressed && !m_editText.empty()) {
            m_editText.pop_back();
            m_backspaceHoldElapsed = 0.0f;
            m_backspaceRepeatAccum = 0.0f;
        } else if (backspaceActive && !m_editText.empty()) {
            m_backspaceHoldElapsed += dt;
            if (m_backspaceHoldElapsed > kBackspaceInitialDelay) {
                m_backspaceRepeatAccum += dt;
                while (m_backspaceRepeatAccum >= kBackspaceRepeatInterval && !m_editText.empty()) {
                    m_editText.pop_back();
                    m_backspaceRepeatAccum -= kBackspaceRepeatInterval;
                }
            }
        } else {
            m_backspaceHoldElapsed = 0.0f;
            m_backspaceRepeatAccum = 0.0f;
        }
        m_backspaceActiveLastFrame = backspaceActive;
    } else {
        m_cursorBlinkTimer = 0.0f;
        m_cursorVisible = false;
    }
}

void UINumericSpinner::initMesh() {
    // 3 zones: minus btn bg + border, value bg + border, plus btn bg + border
    // Each zone: 6 (bg) + 24 (border) = 30 verts, 3 zones = 90 verts, + cursor 6 verts = 96 verts * 2 floats
    constexpr int totalFloats = 96 * 2;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(totalFloats * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void UINumericSpinner::cleanupMesh() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
}

void UINumericSpinner::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader) return;

    const UIRenderUtils::GLStateGuard guard;
    const UIResolvedNumericSpinnerStyle resolved = resolveStyle(ctx);

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;

    const float minusX = ax;
    const float valueX = ax + resolved.buttonWidth + resolved.gap;
    const float plusX  = ax + aw - resolved.buttonWidth;
    const float valueW = aw - 2.0f * resolved.buttonWidth - 2.0f * resolved.gap;
    const float brdW = resolved.borderWidth;

    // Build vertex data for all three zones.
    std::vector<float> verts;
    verts.reserve(96 * 2);
    // Zone 0: minus button (offset 0)
    UIRenderUtils::pushColorQuad(verts, minusX, ay, minusX + resolved.buttonWidth, ay + ah);
    // Zone 0 border (offset 6)
    UIRenderUtils::pushColorQuad(verts, minusX, ay + ah - brdW, minusX + resolved.buttonWidth, ay + ah);
    UIRenderUtils::pushColorQuad(verts, minusX, ay, minusX + resolved.buttonWidth, ay + brdW);
    UIRenderUtils::pushColorQuad(verts, minusX, ay, minusX + brdW, ay + ah);
    UIRenderUtils::pushColorQuad(verts, minusX + resolved.buttonWidth - brdW, ay,
                                 minusX + resolved.buttonWidth, ay + ah);
    // Zone 1: value area (offset 30)
    UIRenderUtils::pushColorQuad(verts, valueX, ay, valueX + valueW, ay + ah);
    UIRenderUtils::pushColorQuad(verts, valueX, ay + ah - brdW, valueX + valueW, ay + ah);
    UIRenderUtils::pushColorQuad(verts, valueX, ay, valueX + valueW, ay + brdW);
    UIRenderUtils::pushColorQuad(verts, valueX, ay, valueX + brdW, ay + ah);
    UIRenderUtils::pushColorQuad(verts, valueX + valueW - brdW, ay, valueX + valueW, ay + ah);
    // Zone 2: plus button (offset 54)
    UIRenderUtils::pushColorQuad(verts, plusX, ay, plusX + resolved.buttonWidth, ay + ah);
    UIRenderUtils::pushColorQuad(verts, plusX, ay + ah - brdW, plusX + resolved.buttonWidth, ay + ah);
    UIRenderUtils::pushColorQuad(verts, plusX, ay, plusX + resolved.buttonWidth, ay + brdW);
    UIRenderUtils::pushColorQuad(verts, plusX, ay, plusX + brdW, ay + ah);
    UIRenderUtils::pushColorQuad(verts, plusX + resolved.buttonWidth - brdW, ay,
                                 plusX + resolved.buttonWidth, ay + ah);

    // Cursor for editing mode (offset 78).
    if (m_editing && m_cursorVisible && isFocused()) {
        float cursorX = valueX + resolved.textPadding;
        if (ctx.textRenderer && !m_editText.empty()) {
            cursorX += ctx.textRenderer->measureText(m_editText, 1.0f).width;
        }
        UIRenderUtils::pushColorQuad(verts, cursorX, ay + resolved.cursorInset,
                                     cursorX + resolved.cursorWidth, ay + ah - resolved.cursorInset);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());

    m_shader->use();
    m_shader->setVec2("uScreenSize",
                      glm::vec2(static_cast<float>(ctx.screenWidth),
                                static_cast<float>(ctx.screenHeight)));

    // Minus button.
    m_shader->setVec4("uColor", glm::vec4(resolved.minusBackground[0], resolved.minusBackground[1],
                                          resolved.minusBackground[2], resolved.minusBackground[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_shader->setVec4("uColor", glm::vec4(resolved.minusBorder[0], resolved.minusBorder[1],
                                          resolved.minusBorder[2], resolved.minusBorder[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 6, 24);

    // Value area.
    m_shader->setVec4("uColor", glm::vec4(resolved.valueBackground[0], resolved.valueBackground[1],
                                          resolved.valueBackground[2], resolved.valueBackground[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 30, 6);
    m_shader->setVec4("uColor", glm::vec4(resolved.valueBorder[0], resolved.valueBorder[1],
                                          resolved.valueBorder[2], resolved.valueBorder[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 36, 24);

    // Plus button.
    m_shader->setVec4("uColor", glm::vec4(resolved.plusBackground[0], resolved.plusBackground[1],
                                          resolved.plusBackground[2], resolved.plusBackground[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 54, 6);
    m_shader->setVec4("uColor", glm::vec4(resolved.plusBorder[0], resolved.plusBorder[1],
                                          resolved.plusBorder[2], resolved.plusBorder[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 60, 24);

    // Cursor.
    if (m_editing && m_cursorVisible && isFocused()) {
        m_shader->setVec4("uColor", glm::vec4(resolved.cursor[0], resolved.cursor[1],
                                              resolved.cursor[2], resolved.cursor[3] * alpha));
        glDrawArrays(GL_TRIANGLES, 78, 6);
    }

    glBindVertexArray(0);

    // Render text.
    if (ctx.textRenderer) {
        const float textScale = 1.0f;

        // Minus sign.
        {
            const auto m = ctx.textRenderer->measureText("-", textScale);
            ctx.textRenderer->render("-",
                                     minusX + (resolved.buttonWidth - m.width) * 0.5f,
                                     ay + (ah - m.height) * 0.5f,
                                     textScale,
                                     {resolved.text[0], resolved.text[1], resolved.text[2],
                                      resolved.text[3] * alpha},
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        }
        // Plus sign.
        {
            const auto m = ctx.textRenderer->measureText("+", textScale);
            ctx.textRenderer->render("+",
                                     plusX + (resolved.buttonWidth - m.width) * 0.5f,
                                     ay + (ah - m.height) * 0.5f,
                                     textScale,
                                     {resolved.text[0], resolved.text[1], resolved.text[2],
                                      resolved.text[3] * alpha},
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        }
        // Value text.
        {
            const std::string valStr = m_editing ? m_editText : formatValue();
            const auto m = ctx.textRenderer->measureText(valStr.empty() ? " " : valStr, textScale);
            // Clip to value area.
            GLint prevScissor[4] = {};
            glGetIntegerv(GL_SCISSOR_BOX, prevScissor);
            glEnable(GL_SCISSOR_TEST);
            const float uiScale = ctx.uiScale > 0.0f ? ctx.uiScale : 1.0f;
            glScissor(static_cast<int>((valueX + 2.0f) * uiScale),
                      static_cast<int>(((ctx.screenHeight - ay - ah) + 2.0f) * uiScale),
                      static_cast<int>((valueW - 4.0f) * uiScale),
                      static_cast<int>((ah - 4.0f) * uiScale));

            ctx.textRenderer->render(valStr.empty() ? " " : valStr,
                                     valueX + resolved.textPadding,
                                     ay + (ah - m.height) * 0.5f,
                                     textScale,
                                     {resolved.text[0], resolved.text[1], resolved.text[2],
                                      resolved.text[3] * alpha},
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));

            glScissor(prevScissor[0], prevScissor[1],
                      static_cast<GLsizei>(prevScissor[2]),
                      static_cast<GLsizei>(prevScissor[3]));
        }
    }
}

UIEventResult UINumericSpinner::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    const bool inside = hitTest(event.x, event.y, ctx);

    switch (event.type) {
    case UIInputEventType::PointerMove: {
        const int zone = hitTestZone(event.x, event.y, ctx);
        m_minusHovered = (zone == -1);
        m_plusHovered = (zone == 1);
        return inside ? UIEventResult::Handled : UIEventResult::Ignored;
    }

    case UIInputEventType::PointerDown:
        if (event.button == UIPointerButton::Primary && inside) {
            const int zone = hitTestZone(event.x, event.y, ctx);
            if (zone == -1) {
                applyStep(-m_step);
            } else if (zone == 1) {
                applyStep(m_step);
            } else if (zone == 0) {
                // Enter editing mode.
                m_editing = true;
                m_editText = formatValue();
                m_cursorBlinkTimer = 0.0f;
                m_cursorVisible = true;
            }
            requestFocus();
            return UIEventResult::Consumed;
        }
        break;

    case UIInputEventType::PointerUp:
        if (event.button == UIPointerButton::Primary) {
            return UIEventResult::Handled;
        }
        break;

    case UIInputEventType::KeyDown: {
        if (!isFocused()) break;

        if (m_editing) {
            const int key = event.key;
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                commitEditText();
                return UIEventResult::Consumed;
            }
            if (key == GLFW_KEY_ESCAPE) {
                m_editing = false;
                m_editText.clear();
                return UIEventResult::Consumed;
            }
            // Handled by onUpdate for backspace auto-repeat.
            return UIEventResult::Consumed;
        }

        // Non-editing mode: arrow keys adjust value.
        if (event.command == UICommand::NavigateLeft || event.command == UICommand::NavigateDown) {
            applyStep(-m_step);
            return UIEventResult::Consumed;
        }
        if (event.command == UICommand::NavigateRight || event.command == UICommand::NavigateUp) {
            applyStep(m_step);
            return UIEventResult::Consumed;
        }
        if (event.command == UICommand::Home) {
            setValue(m_min);
            if (onValueChanged) onValueChanged(m_value);
            return UIEventResult::Consumed;
        }
        if (event.command == UICommand::End) {
            setValue(m_max);
            if (onValueChanged) onValueChanged(m_value);
            return UIEventResult::Consumed;
        }
        break;
    }

    case UIInputEventType::Command: {
        if (!isFocused()) break;
        if (m_editing) {
            if (event.command == UICommand::Activate) {
                commitEditText();
                return UIEventResult::Consumed;
            }
            if (event.command == UICommand::Cancel) {
                m_editing = false;
                m_editText.clear();
                return UIEventResult::Consumed;
            }
        }
        break;
    }

    case UIInputEventType::TextInput: {
        if (!isFocused() || !m_editing) break;
        const std::uint32_t cp = event.codepoint;
        // Accept digits, minus sign (at start), and decimal point.
        if (cp >= 32 && cp < 127) {
            const char c = static_cast<char>(cp);
            if (std::isdigit(c) || c == '-' || c == '.') {
                // Only allow '-' at the start.
                if (c == '-' && !m_editText.empty()) break;
                // Only one decimal point.
                if (c == '.' && m_editText.find('.') != std::string::npos) break;
                m_editText += c;
                m_cursorBlinkTimer = 0.0f;
                m_cursorVisible = true;
            }
        }
        return UIEventResult::Consumed;
    }

    case UIInputEventType::Scroll: {
        if (inside || isFocused()) {
            const float delta = (event.scrollY > 0.0f) ? m_step : -m_step;
            applyStep(delta);
            return UIEventResult::Consumed;
        }
        break;
    }

    default:
        break;
    }

    return UIEventResult::Ignored;
}

UINumericSpinnerStyle UINumericSpinner::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }
    return UIStyleResolver::numericSpinnerStyleFromTheme(ctx.theme);
}

UIResolvedNumericSpinnerStyle UINumericSpinner::resolveStyle(const UIRenderContext& ctx) const {
    int minusState = interactive ? static_cast<int>(UIStyleState_Normal) : static_cast<int>(UIStyleState_Disabled);
    int plusState = interactive ? static_cast<int>(UIStyleState_Normal) : static_cast<int>(UIStyleState_Disabled);
    int valueState = interactive ? static_cast<int>(UIStyleState_Normal) : static_cast<int>(UIStyleState_Disabled);

    if (m_minusHovered) {
        minusState |= static_cast<int>(UIStyleState_Hovered);
    }
    if (m_plusHovered) {
        plusState |= static_cast<int>(UIStyleState_Hovered);
    }
    if (isFocused()) {
        valueState |= static_cast<int>(UIStyleState_Focused);
    }

    return UIStyleResolver::resolveNumericSpinner(resolveBaseStyle(ctx), minusState, plusState, valueState);
}
