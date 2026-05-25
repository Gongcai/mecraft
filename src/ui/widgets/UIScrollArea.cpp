#include "UIScrollArea.h"

#include <algorithm>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../core/UIRenderUtils.h"

UIScrollArea::UIScrollArea() {
    interactive = true;
    width = 300.0f;
    height = 400.0f;
}

UIScrollArea::~UIScrollArea() { shutdown(); }

void UIScrollArea::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
}

void UIScrollArea::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
}

void UIScrollArea::setContentHeight(float height) {
    m_contentHeight = height;
    m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, maxScroll());
}

float UIScrollArea::getContentHeight() const {
    return m_contentHeight;
}

float UIScrollArea::getScrollOffset() const {
    return m_scrollOffset;
}

void UIScrollArea::setScrollOffset(float offset) {
    m_scrollOffset = std::clamp(offset, 0.0f, maxScroll());
}

void UIScrollArea::scrollToBottom() {
    m_scrollOffset = maxScroll();
}

void UIScrollArea::setScrollbarVisible(bool visible) {
    m_scrollbarVisible = visible;
}

void UIScrollArea::updateAnimations(float dt) {
    m_scrollTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

float UIScrollArea::maxScroll() const {
    return std::max(0.0f, m_contentHeight - height * scaleY);
}

void UIScrollArea::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Track (6) + thumb (6) = 12 verts * 2 floats
    glBufferData(GL_ARRAY_BUFFER, 12 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIScrollArea::cleanupMesh() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void UIScrollArea::render(const UIRenderContext& ctx) const {
    if (!visible) return;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    GLint scissorX = static_cast<GLint>(ax);
    GLint scissorY = static_cast<GLint>(ay);
    GLint scissorW = static_cast<GLint>(aw);
    GLint scissorH = static_cast<GLint>(ah);

    // Save current scissor state
    GLboolean wasScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint prevScissor[4];
    glGetIntegerv(GL_SCISSOR_BOX, prevScissor);

    glScissor(scissorX, scissorY, scissorW, scissorH);
    glEnable(GL_SCISSOR_TEST);

    renderSelf(ctx);

    // Render children with Y offset applied
    for (const auto& child : getChildren()) {
        const_cast<UIWidget*>(child.get())->anchorOffsetY -= m_scrollOffset;
        child->render(ctx);
        const_cast<UIWidget*>(child.get())->anchorOffsetY += m_scrollOffset;
    }

    // Restore scissor
    if (wasScissorEnabled) {
        glScissor(prevScissor[0], prevScissor[1], prevScissor[2], prevScissor[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    // Render scrollbar outside scissor (on top)
    if (m_scrollbarVisible && maxScroll() > 0.0f) {
        renderScrollbar(ctx);
    }
}

void UIScrollArea::renderSelf(const UIRenderContext& ctx) const {
    // ScrollArea itself is transparent -- children render inside it
    (void)ctx;
}

void UIScrollArea::renderScrollbar(const UIRenderContext& ctx) const {
    if (!m_shader || m_vao == 0) return;

    const UITheme* theme = ctx.theme;
    float sbWidth = theme ? theme->scrollbarWidth : m_scrollbarWidth;
    const auto& trackCol = theme ? theme->scrollbarTrack : m_scrollbarTrackColor;
    const auto& thumbCol = m_thumbHovered
        ? (theme ? theme->scrollbarThumbHover : m_scrollbarThumbHoverColor)
        : (theme ? theme->scrollbarThumb : m_scrollbarThumbColor);

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    // Scrollbar track: right edge
    float trackX = ax + aw - sbWidth;
    float trackY = ay;
    float trackH = ah;

    // Thumb proportional sizing
    float viewRatio = ah / m_contentHeight;
    float thumbH = std::max(20.0f, viewRatio * trackH);
    float scrollMax = maxScroll();
    float thumbY = trackY;
    if (scrollMax > 0.0f) {
        float scrollRatio = m_scrollOffset / scrollMax;
        thumbY = trackY + scrollRatio * (trackH - thumbH);
    }

    const UIRenderUtils::GLStateGuard glState;
    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(static_cast<float>(ctx.screenWidth),
                                                static_cast<float>(ctx.screenHeight)));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Track
    {
        std::array<float, 4> c = trackCol;
        c[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float verts[] = {
            trackX, trackY,  trackX + sbWidth, trackY,  trackX + sbWidth, trackY + trackH,
            trackX, trackY,  trackX + sbWidth, trackY + trackH,  trackX, trackY + trackH,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // Thumb
    {
        std::array<float, 4> c = thumbCol;
        c[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float verts[] = {
            trackX, thumbY,  trackX + sbWidth, thumbY,  trackX + sbWidth, thumbY + thumbH,
            trackX, thumbY,  trackX + sbWidth, thumbY + thumbH,  trackX, thumbY + thumbH,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 6, 6);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool UIScrollArea::hitTestScrollbarThumb(float px, float py, const UIRenderContext& ctx) const {
    const UITheme* theme = ctx.theme;
    float sbWidth = theme ? theme->scrollbarWidth : m_scrollbarWidth;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;
    float flippedY = static_cast<float>(ctx.screenHeight) - py;

    float trackX = ax + aw - sbWidth;
    float trackY = ay;

    float viewRatio = ah / m_contentHeight;
    float thumbH = std::max(20.0f, viewRatio * ah);
    float scrollMax = maxScroll();
    float thumbY = trackY;
    if (scrollMax > 0.0f) {
        float scrollRatio = m_scrollOffset / scrollMax;
        thumbY = trackY + scrollRatio * (ah - thumbH);
    }

    return px >= trackX && px <= trackX + sbWidth
        && flippedY >= thumbY && flippedY <= thumbY + thumbH;
}

UIEventResult UIScrollArea::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    bool inside = hitTest(event.x, event.y, ctx);

    // Handle scrollbar dragging
    if (m_draggingScrollbar) {
        if (event.type == UIInputEventType::PointerMove) {
            float flippedY = static_cast<float>(ctx.screenHeight) - event.y;
            float delta = flippedY - m_dragStartY;
            float ah = height * scaleY;
            float viewRatio = ah / m_contentHeight;
            float thumbH = std::max(20.0f, viewRatio * ah);
            float scrollRange = ah - thumbH;
            if (scrollRange > 0.0f) {
                float scrollDelta = (delta / scrollRange) * maxScroll();
                m_scrollOffset = std::clamp(m_dragStartOffset + scrollDelta, 0.0f, maxScroll());
            }
            return UIEventResult::Consumed;
        }
        if (event.type == UIInputEventType::PointerUp) {
            m_draggingScrollbar = false;
            return UIEventResult::Consumed;
        }
        return UIEventResult::Consumed;
    }

    // Scroll events
    if (event.type == UIInputEventType::Scroll && inside) {
        float scrollAmount = 40.0f;
        m_scrollOffset = std::clamp(m_scrollOffset - event.scrollY * scrollAmount, 0.0f, maxScroll());
        return UIEventResult::Handled;
    }

    // Scrollbar thumb click
    if (event.type == UIInputEventType::PointerDown && event.button == UIPointerButton::Primary) {
        if (hitTestScrollbarThumb(event.x, event.y, ctx)) {
            m_draggingScrollbar = true;
            m_dragStartY = static_cast<float>(ctx.screenHeight) - event.y;
            m_dragStartOffset = m_scrollOffset;
            return UIEventResult::Consumed;
        }
    }

    // Scrollbar hover
    if (event.type == UIInputEventType::PointerMove && m_scrollbarVisible && maxScroll() > 0.0f) {
        m_thumbHovered = hitTestScrollbarThumb(event.x, event.y, ctx);
    }

    // Forward input to children with adjusted hit test
    // Children have their anchorOffsetY shifted by -m_scrollOffset during render,
    // so the hit test in UIWidget::onInput already accounts for the visual offset.
    // We just need to forward normally.
    UIEventResult result = UIWidget::onInput(event, ctx);

    // If pointer is inside scroll area but not on any child, still consume scroll
    if (result == UIEventResult::Ignored && inside && event.type == UIInputEventType::Scroll) {
        float scrollAmount = 40.0f;
        m_scrollOffset = std::clamp(m_scrollOffset - event.scrollY * scrollAmount, 0.0f, maxScroll());
        return UIEventResult::Handled;
    }

    return result;
}
