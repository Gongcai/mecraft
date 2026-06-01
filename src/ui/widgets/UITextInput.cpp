#include "UITextInput.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../core/UIRenderUtils.h"
#include "../core/UITheme.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"

namespace {

// Count the number of UTF-8 characters in a string.
int utf8CharCount(const std::string& s) {
    int count = 0;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int len = 1;
        if ((c & 0x80u) == 0) len = 1;
        else if ((c & 0xE0u) == 0xC0u) len = 2;
        else if ((c & 0xF0u) == 0xE0u) len = 3;
        else if ((c & 0xF8u) == 0xF0u) len = 4;
        i += static_cast<size_t>(len);
        ++count;
    }
    return count;
}

// Get byte offset of the n-th UTF-8 character.
int utf8ByteOffset(const std::string& s, int charIndex) {
    int ci = 0;
    for (size_t i = 0; i < s.size();) {
        if (ci == charIndex) return static_cast<int>(i);
        const auto c = static_cast<unsigned char>(s[i]);
        int len = 1;
        if ((c & 0x80u) == 0) len = 1;
        else if ((c & 0xE0u) == 0xC0u) len = 2;
        else if ((c & 0xF0u) == 0xE0u) len = 3;
        else if ((c & 0xF8u) == 0xF0u) len = 4;
        i += static_cast<size_t>(len);
        ++ci;
    }
    return static_cast<int>(s.size());
}

// Count UTF-8 characters up to a byte offset.
int utf8CharCountUpTo(const std::string& s, int byteOffset) {
    int count = 0;
    for (int i = 0; i < byteOffset && i < static_cast<int>(s.size());) {
        const auto c = static_cast<unsigned char>(s[i]);
        int len = 1;
        if ((c & 0x80u) == 0) len = 1;
        else if ((c & 0xE0u) == 0xC0u) len = 2;
        else if ((c & 0xF0u) == 0xE0u) len = 3;
        else if ((c & 0xF8u) == 0xF0u) len = 4;
        i += len;
        ++count;
    }
    return count;
}

// Encode a Unicode codepoint to UTF-8 string.
std::string codepointToUtf8(std::uint32_t cp) {
    std::string result;
    if (cp < 0x80u) {
        result += static_cast<char>(cp);
    } else if (cp < 0x800u) {
        result += static_cast<char>(0xC0u | (cp >> 6));
        result += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        result += static_cast<char>(0xE0u | (cp >> 12));
        result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        result += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else {
        result += static_cast<char>(0xF0u | (cp >> 18));
        result += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
        result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        result += static_cast<char>(0x80u | (cp & 0x3Fu));
    }
    return result;
}

constexpr float kTextPadX = 6.0f;

} // namespace

UITextInput::UITextInput() {
    interactive = true;
    focusable = true;
    width = 200.0f;
    height = 28.0f;
}

UITextInput::~UITextInput() {
    shutdown();
}

void UITextInput::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    UIWidget::init(resourceMgr);
}

void UITextInput::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
    UIWidget::shutdown();
}

void UITextInput::setText(const std::string& text) {
    m_text = text.substr(0, m_maxLength);
    m_cursorPos = static_cast<int>(m_text.size());
    m_selStart = m_selEnd = m_cursorPos;
    m_scrollOffset = 0.0f;
}

void UITextInput::setMaxLength(std::size_t maxLength) {
    m_maxLength = maxLength;
    if (m_text.size() > m_maxLength) {
        m_text.resize(m_maxLength);
        clampCursor();
    }
}

void UITextInput::selectAll() {
    m_selStart = 0;
    m_selEnd = static_cast<int>(m_text.size());
    m_cursorPos = m_selEnd;
}

void UITextInput::deleteSelection() {
    if (!hasSelection()) return;
    const int lo = std::min(m_selStart, m_selEnd);
    const int hi = std::max(m_selStart, m_selEnd);
    m_text.erase(static_cast<size_t>(lo), static_cast<size_t>(hi - lo));
    m_cursorPos = lo;
    m_selStart = m_selEnd = m_cursorPos;
}

void UITextInput::clampCursor() {
    m_cursorPos = std::clamp(m_cursorPos, 0, static_cast<int>(m_text.size()));
}

void UITextInput::insertText(const std::string& text) {
    deleteSelection();
    const size_t available = m_maxLength - m_text.size();
    const std::string toInsert = text.substr(0, available);
    m_text.insert(static_cast<size_t>(m_cursorPos), toInsert);
    m_cursorPos += static_cast<int>(toInsert.size());
    m_selStart = m_selEnd = m_cursorPos;
}

int UITextInput::charIndexFromX(float localX, const UIRenderContext& ctx) const {
    if (!ctx.textRenderer) return 0;
    const float textScale = 1.0f;
    const int charCount = utf8CharCount(m_text);
    for (int i = 0; i <= charCount; ++i) {
        const int byteOff = utf8ByteOffset(m_text, i);
        const std::string sub = m_text.substr(0, static_cast<size_t>(byteOff));
        const float w = ctx.textRenderer->measureText(sub, textScale).width;
        if (localX < w + kTextPadX - m_scrollOffset + 4.0f) {
            return i;
        }
    }
    return charCount;
}

float UITextInput::measureTextUpTo(int index, const UIRenderContext& ctx) const {
    if (!ctx.textRenderer || index <= 0) return 0.0f;
    const int byteOff = utf8ByteOffset(m_text, index);
    const std::string sub = m_text.substr(0, static_cast<size_t>(byteOff));
    return ctx.textRenderer->measureText(sub, 1.0f).width;
}

void UITextInput::onUpdate(float dt) {
    if (isFocused()) {
        m_cursorBlinkTimer += dt;
        if (m_cursorBlinkTimer >= kCursorBlinkRate) {
            m_cursorBlinkTimer -= kCursorBlinkRate;
        }
        m_cursorVisible = m_cursorBlinkTimer < (kCursorBlinkRate * 0.5f);
    } else {
        m_cursorBlinkTimer = 0.0f;
        m_cursorVisible = false;
    }
}

void UITextInput::initMesh() {
    // Background (6 verts) + border (24 verts) + selection highlight (6 verts) + cursor (6 verts)
    // = 42 verts * 2 floats = 84 floats.
    constexpr int totalFloats = 42 * 2;
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

void UITextInput::cleanupMesh() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
}

void UITextInput::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader) return;

    const UIRenderUtils::GLStateGuard guard;

    const Color bgCol    = (!m_hasLocalColors && ctx.theme) ? ctx.theme->inputBackground    : m_bgColor;
    const Color brdCol   = (!m_hasLocalColors && ctx.theme) ?
                           (isFocused() ? ctx.theme->inputBorderFocused : ctx.theme->inputBorder) :
                           (isFocused() ? m_borderFocusedColor : m_borderColor);
    const Color txtCol   = (!m_hasLocalColors && ctx.theme) ? ctx.theme->inputText          : m_textColor;
    const Color phCol    = (!m_hasLocalColors && ctx.theme) ? ctx.theme->inputPlaceholder    : m_placeholderColor;
    const Color selCol   = (!m_hasLocalColors && ctx.theme) ? ctx.theme->inputSelection     : m_selectionColor;
    const Color curCol   = (!m_hasLocalColors && ctx.theme) ? ctx.theme->inputCursor        : m_cursorColor;
    const float brdWidth = ctx.theme ? ctx.theme->panelBorderWidth : 1.0f;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;

    // --- Draw background + border ---
    std::vector<float> verts;
    verts.reserve(84);
    UIRenderUtils::pushColorQuad(verts, ax, ay, ax + aw, ay + ah); // bg at 0
    // Border quads: top, bottom, left, right (offset 6, 12, 18, 24)
    UIRenderUtils::pushColorQuad(verts, ax, ay + ah - brdWidth, ax + aw, ay + ah);           // top
    UIRenderUtils::pushColorQuad(verts, ax, ay, ax + aw, ay + brdWidth);                      // bottom
    UIRenderUtils::pushColorQuad(verts, ax, ay, ax + brdWidth, ay + ah);                      // left
    UIRenderUtils::pushColorQuad(verts, ax + aw - brdWidth, ay, ax + aw, ay + ah);            // right

    const int selLo = std::min(m_selStart, m_selEnd);
    const int selHi = std::max(m_selStart, m_selEnd);
    const bool showSel = hasSelection();
    if (showSel) {
        const float selLeftPx = measureTextUpTo(utf8CharCountUpTo(m_text, selLo), ctx);
        const float selRightPx = measureTextUpTo(utf8CharCountUpTo(m_text, selHi), ctx);
        const float sx0 = ax + kTextPadX + selLeftPx - m_scrollOffset;
        const float sx1 = ax + kTextPadX + selRightPx - m_scrollOffset;
        UIRenderUtils::pushColorQuad(verts, sx0, ay + 2.0f, sx1, ay + ah - 2.0f); // selection at 30
    }
    // Cursor (offset 30 or 36)
    if (m_cursorVisible && isFocused()) {
        const float cursorX = ax + kTextPadX + measureTextUpTo(utf8CharCountUpTo(m_text, m_cursorPos), ctx) - m_scrollOffset;
        UIRenderUtils::pushColorQuad(verts, cursorX, ay + 3.0f, cursorX + 1.5f, ay + ah - 3.0f);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());

    m_shader->use();
    m_shader->setVec2("uScreenSize",
                      glm::vec2(static_cast<float>(ctx.screenWidth),
                                static_cast<float>(ctx.screenHeight)));

    // Background.
    m_shader->setVec4("uColor", glm::vec4(bgCol[0], bgCol[1], bgCol[2], bgCol[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Border.
    m_shader->setVec4("uColor", glm::vec4(brdCol[0], brdCol[1], brdCol[2], brdCol[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 6, 24);

    int nextVert = 30;

    // Selection highlight.
    if (showSel) {
        m_shader->setVec4("uColor", glm::vec4(selCol[0], selCol[1], selCol[2], selCol[3] * alpha));
        glDrawArrays(GL_TRIANGLES, nextVert, 6);
        nextVert += 6;
    }

    // Cursor.
    if (m_cursorVisible && isFocused()) {
        m_shader->setVec4("uColor", glm::vec4(curCol[0], curCol[1], curCol[2], curCol[3] * alpha));
        glDrawArrays(GL_TRIANGLES, nextVert, 6);
    }

    glBindVertexArray(0);

    // --- Render text ---
    if (ctx.textRenderer) {
        // Set up scissor to clip text within the input box.
        GLint prevScissor[4] = {};
        glGetIntegerv(GL_SCISSOR_BOX, prevScissor);
        glEnable(GL_SCISSOR_TEST);
        // Scissor is in actual pixel coords (not reference space).
        const float uiScale = ctx.uiScale > 0.0f ? ctx.uiScale : 1.0f;
        const int sx = static_cast<int>((ax + 2.0f) * uiScale);
        const int sy = static_cast<int>(((ctx.screenHeight - ay - ah) + 2.0f) * uiScale);
        const int sw = static_cast<int>((aw - 4.0f) * uiScale);
        const int sh = static_cast<int>((ah - 4.0f) * uiScale);
        glScissor(sx, sy, std::max(0, sw), std::max(0, sh));

        const float textScale = 1.0f;
        if (m_text.empty() && !m_placeholder.empty() && !isFocused()) {
            ctx.textRenderer->render(m_placeholder,
                                     ax + kTextPadX,
                                     ay + (ah - ctx.textRenderer->measureText("A", textScale).height) * 0.5f,
                                     textScale,
                                     {phCol[0], phCol[1], phCol[2], phCol[3] * alpha},
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        } else if (!m_text.empty()) {
            const auto metrics = ctx.textRenderer->measureText(m_text, textScale);
            ctx.textRenderer->render(m_text,
                                     ax + kTextPadX - m_scrollOffset,
                                     ay + (ah - metrics.height) * 0.5f,
                                     textScale,
                                     {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * alpha},
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        }

        glScissor(prevScissor[0], prevScissor[1],
                  static_cast<GLsizei>(prevScissor[2]),
                  static_cast<GLsizei>(prevScissor[3]));
    }
}

UIEventResult UITextInput::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    const bool inside = hitTest(event.x, event.y, ctx);

    switch (event.type) {
    case UIInputEventType::PointerMove:
        m_hovered = inside;
        return inside ? UIEventResult::Handled : UIEventResult::Ignored;

    case UIInputEventType::PointerDown:
        if (event.button == UIPointerButton::Primary && inside) {
            requestFocus();
            // Position cursor at click location.
            const float flippedY = static_cast<float>(ctx.screenHeight) - event.y;
            const float localX = event.x - getAbsoluteX(ctx);
            const int charIdx = charIndexFromX(localX, ctx);
            const int byteOff = utf8ByteOffset(m_text, charIdx);
            m_cursorPos = byteOff;
            m_selStart = m_selEnd = m_cursorPos;
            m_cursorBlinkTimer = 0.0f;
            m_cursorVisible = true;
            return UIEventResult::Consumed;
        }
        break;

    case UIInputEventType::KeyDown: {
        if (!isFocused()) break;

        const int key = event.key;
        // Detect Ctrl via GLFW key state. Note: this works because the
        // InputManager holds key states and the GLFW window is accessible
        // through the currently bound context. We check both left and right Ctrl.
        // This is a pragmatic approach since UIInputEvent doesn't carry modifiers.
        const bool ctrl = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                          glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

        if (key == GLFW_KEY_LEFT) {
            if (hasSelection() && !ctrl) {
                // Collapse selection to left side.
                m_cursorPos = std::min(m_selStart, m_selEnd);
                m_selStart = m_selEnd = m_cursorPos;
            } else if (m_cursorPos > 0) {
                // Move one UTF-8 character back.
                int prev = m_cursorPos - 1;
                while (prev > 0 && (static_cast<unsigned char>(m_text[prev]) & 0xC0u) == 0x80u) --prev;
                m_cursorPos = prev;
                m_selStart = m_selEnd = m_cursorPos;
            }
            m_cursorBlinkTimer = 0.0f;
            m_cursorVisible = true;
            return UIEventResult::Consumed;
        }

        if (key == GLFW_KEY_RIGHT) {
            if (hasSelection() && !ctrl) {
                m_cursorPos = std::max(m_selStart, m_selEnd);
                m_selStart = m_selEnd = m_cursorPos;
            } else if (m_cursorPos < static_cast<int>(m_text.size())) {
                int next = m_cursorPos + 1;
                while (next < static_cast<int>(m_text.size()) &&
                       (static_cast<unsigned char>(m_text[next]) & 0xC0u) == 0x80u) ++next;
                m_cursorPos = next;
                m_selStart = m_selEnd = m_cursorPos;
            }
            m_cursorBlinkTimer = 0.0f;
            m_cursorVisible = true;
            return UIEventResult::Consumed;
        }

        if (key == GLFW_KEY_HOME) {
            m_cursorPos = 0;
            m_selStart = m_selEnd = m_cursorPos;
            m_cursorBlinkTimer = 0.0f;
            m_cursorVisible = true;
            return UIEventResult::Consumed;
        }

        if (key == GLFW_KEY_END) {
            m_cursorPos = static_cast<int>(m_text.size());
            m_selStart = m_selEnd = m_cursorPos;
            m_cursorBlinkTimer = 0.0f;
            m_cursorVisible = true;
            return UIEventResult::Consumed;
        }

        if (key == GLFW_KEY_BACKSPACE) {
            if (hasSelection()) {
                deleteSelection();
            } else if (m_cursorPos > 0) {
                int prev = m_cursorPos - 1;
                while (prev > 0 && (static_cast<unsigned char>(m_text[prev]) & 0xC0u) == 0x80u) --prev;
                m_text.erase(static_cast<size_t>(prev),
                             static_cast<size_t>(m_cursorPos - prev));
                m_cursorPos = prev;
                m_selStart = m_selEnd = m_cursorPos;
            }
            m_cursorBlinkTimer = 0.0f;
            m_cursorVisible = true;
            if (onTextChanged) onTextChanged(m_text);
            return UIEventResult::Consumed;
        }

        if (key == GLFW_KEY_DELETE) {
            if (hasSelection()) {
                deleteSelection();
            } else if (m_cursorPos < static_cast<int>(m_text.size())) {
                int next = m_cursorPos + 1;
                while (next < static_cast<int>(m_text.size()) &&
                       (static_cast<unsigned char>(m_text[next]) & 0xC0u) == 0x80u) ++next;
                m_text.erase(static_cast<size_t>(m_cursorPos),
                             static_cast<size_t>(next - m_cursorPos));
            }
            m_cursorBlinkTimer = 0.0f;
            m_cursorVisible = true;
            if (onTextChanged) onTextChanged(m_text);
            return UIEventResult::Consumed;
        }

        if (key == GLFW_KEY_A && ctrl) {
            selectAll();
            return UIEventResult::Consumed;
        }

        break;
    }

    case UIInputEventType::Command: {
        if (!isFocused()) break;
        if (event.command == UICommand::Activate) {
            if (onSubmit) onSubmit(m_text);
            return UIEventResult::Consumed;
        }
        break;
    }

    case UIInputEventType::TextInput: {
        if (!isFocused()) break;
        const std::uint32_t cp = event.codepoint;
        // Accept printable ASCII and common Unicode ranges.
        if (cp < 32) break;
        insertText(codepointToUtf8(cp));
        m_cursorBlinkTimer = 0.0f;
        m_cursorVisible = true;
        if (onTextChanged) onTextChanged(m_text);
        return UIEventResult::Consumed;
    }

    default:
        break;
    }

    return UIEventResult::Ignored;
}
