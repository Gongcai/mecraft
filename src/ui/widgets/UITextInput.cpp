#include "UITextInput.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <glm/vec4.hpp>

#include "../core/UITheme.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"

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
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiTextInput.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiTextInput.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "UiTextInput.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiTextInput.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, sizeof(float) * 2u, RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes.push_back({0u, 0u, RhiVertexFormat::Float2, 0u});
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(m_rhiDevice->swapchainColorFormat());
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    m_pipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    initMesh();
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid() ||
        !m_pipelineLayout.isValid() || !m_pipeline.isValid() || !m_vertexBuffer.isValid()) {
        std::abort();
    }
    UIWidget::init(resourceMgr);
}

void UITextInput::shutdown() {
    cleanupMesh();
    if (m_rhiDevice != nullptr) {
        if (m_pipeline.isValid()) m_rhiDevice->destroyPipeline(m_pipeline);
        if (m_pipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_fragmentShader.isValid()) m_rhiDevice->destroyShader(m_fragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
    }
    m_pipeline = {};
    m_pipelineLayout = {};
    m_fragmentShader = {};
    m_vertexShader = {};
    m_rhiDevice = nullptr;
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

void UITextInput::setStyle(const UITextInputStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UITextInput::clearLocalStyle() {
    m_hasLocalStyle = false;
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
    constexpr float vertices[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
    };
    RhiBufferDesc desc;
    desc.debugName = "UiTextInput.VertexBuffer";
    desc.size = sizeof(vertices);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    m_vertexBuffer = m_rhiDevice->createBuffer(desc, vertices, sizeof(vertices));
}

void UITextInput::cleanupMesh() {
    if (m_rhiDevice != nullptr && m_vertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_vertexBuffer);
    }
    m_vertexBuffer = {};
}

void UITextInput::renderSelf(const UIRenderContext& ctx) const {
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record &&
        (ctx.commandList == nullptr || !m_pipeline.isValid() || !m_vertexBuffer.isValid())) return;

    const UIResolvedTextInputStyle resolved =
        UIStyleResolver::resolveTextInput(resolveBaseStyle(ctx), currentStyleState());
    const Color bgCol = resolved.frame.background;
    const Color brdCol = resolved.frame.border;
    const Color txtCol = resolved.frame.text;
    const Color phCol = resolved.placeholder;
    const Color selCol = resolved.selection;
    const Color curCol = resolved.cursor;
    const float brdWidth = resolved.frame.borderWidth;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;

    const float uiScale = ctx.pixelScale();
    RhiRect2D contentScissor{
        static_cast<int32_t>(std::floor((ax + 2.0f) * uiScale)),
        static_cast<int32_t>(std::floor((ay + 2.0f) * uiScale)),
        static_cast<uint32_t>(std::max(1.0f, std::ceil((aw - 4.0f) * uiScale))),
        static_cast<uint32_t>(std::max(1.0f, std::ceil((ah - 4.0f) * uiScale)))
    };
    if (ctx.hasScissor) {
        const int32_t x0 = std::max(contentScissor.x, ctx.scissor.x);
        const int32_t y0 = std::max(contentScissor.y, ctx.scissor.y);
        const int32_t x1 = std::min(contentScissor.x + static_cast<int32_t>(contentScissor.width),
                                    ctx.scissor.x + static_cast<int32_t>(ctx.scissor.width));
        const int32_t y1 = std::min(contentScissor.y + static_cast<int32_t>(contentScissor.height),
                                    ctx.scissor.y + static_cast<int32_t>(ctx.scissor.height));
        contentScissor = {x0, y0, static_cast<uint32_t>(std::max(0, x1 - x0)),
                          static_cast<uint32_t>(std::max(0, y1 - y0))};
    }

    if (record) {
        ctx.commandList->setGraphicsPipeline(m_pipeline);
        ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
        auto drawRect = [&](float x, float y, float rectWidth, float rectHeight, Color color) {
            color[3] *= alpha;
            struct PushConstants {
                glm::vec4 screenRect;
                glm::vec4 rectRadius;
                glm::vec4 color;
            };
            const PushConstants pushConstants{
                glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), x, y),
                glm::vec4(rectWidth, rectHeight, 0.0f, 0.0f),
                glm::vec4(color[0], color[1], color[2], color[3])
            };
            ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                           rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
            ctx.commandList->draw(6u, 1u, 0u, 0u);
        };

        drawRect(ax, ay, aw, ah, bgCol);
        drawRect(ax, ay + ah - brdWidth, aw, brdWidth, brdCol);
        drawRect(ax, ay, aw, brdWidth, brdCol);
        drawRect(ax, ay, brdWidth, ah, brdCol);
        drawRect(ax + aw - brdWidth, ay, brdWidth, ah, brdCol);

        ctx.commandList->setScissor(contentScissor);

        const int selLo = std::min(m_selStart, m_selEnd);
        const int selHi = std::max(m_selStart, m_selEnd);
        if (hasSelection()) {
            const float selLeftPx = measureTextUpTo(utf8CharCountUpTo(m_text, selLo), ctx);
            const float selRightPx = measureTextUpTo(utf8CharCountUpTo(m_text, selHi), ctx);
            const float sx0 = ax + kTextPadX + selLeftPx - m_scrollOffset;
            const float sx1 = ax + kTextPadX + selRightPx - m_scrollOffset;
            drawRect(sx0, ay + 2.0f, sx1 - sx0, ah - 4.0f, selCol);
        }
        if (m_cursorVisible && isFocused()) {
            const float cursorX = ax + kTextPadX + measureTextUpTo(utf8CharCountUpTo(m_text, m_cursorPos), ctx) - m_scrollOffset;
            drawRect(cursorX, ay + 3.0f, 1.5f, ah - 6.0f, curCol);
        }
    }

    if (ctx.textRenderer) {
        UIRenderContext textContext = ctx;
        textContext.hasScissor = true;
        textContext.scissor = contentScissor;
        const float textScale = 1.0f;
        if (m_text.empty() && !m_placeholder.empty() && !isFocused()) {
            ctx.textRenderer->draw(
                textContext,
                m_placeholder,
                ax + kTextPadX,
                ay + (ah - ctx.textRenderer->measureText("A", textScale).height) * 0.5f,
                textScale,
                {phCol[0], phCol[1], phCol[2], phCol[3] * alpha});
        } else if (!m_text.empty()) {
            const auto metrics = ctx.textRenderer->measureText(m_text, textScale);
            ctx.textRenderer->draw(
                textContext,
                m_text,
                ax + kTextPadX - m_scrollOffset,
                ay + (ah - metrics.height) * 0.5f,
                textScale,
                {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * alpha});
        }

    }

    if (record) {
        const RhiRect2D parentScissor = ctx.hasScissor
            ? ctx.scissor
            : RhiRect2D{0, 0, static_cast<uint32_t>(ctx.screenWidth * uiScale),
                        static_cast<uint32_t>(ctx.screenHeight * uiScale)};
        ctx.commandList->setScissor(parentScissor);
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
        const bool ctrl = hasInputModifier(event.modifiers, UIInputModifier::Control);

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

UITextInputStyle UITextInput::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UITextInputStyle style = UIStyleResolver::textInputStyleFromTheme(ctx.theme);
    if (m_hasLocalColors) {
        style.frame.backgroundNormal = m_bgColor;
        style.frame.backgroundHover = m_bgColor;
        style.frame.backgroundPressed = m_bgColor;
        style.frame.backgroundDisabled = m_bgColor;
        style.frame.borderNormal = m_borderColor;
        style.frame.borderHover = m_borderColor;
        style.frame.borderFocused = m_borderFocusedColor;
        style.frame.borderPressed = m_borderFocusedColor;
        style.frame.borderDisabled = m_borderColor;
        style.frame.textNormal = m_textColor;
        style.frame.textDisabled = m_textColor;
        style.placeholder = m_placeholderColor;
        style.selection = m_selectionColor;
        style.cursor = m_cursorColor;
    }
    return style;
}

int UITextInput::currentStyleState() const {
    if (!interactive) {
        return static_cast<int>(UIStyleState_Disabled);
    }

    int state = static_cast<int>(UIStyleState_Normal);
    if (m_hovered) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    if (isFocused()) {
        state |= static_cast<int>(UIStyleState_Focused);
    }
    return state;
}
