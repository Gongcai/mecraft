#include "UIScrollArea.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/core/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../core/UIRenderUtils.h"

namespace {

struct ScissorBox {
    GLint x = 0;
    GLint y = 0;
    GLint width = 1;
    GLint height = 1;
};

ScissorBox makeScaledScissorBox(float x, float y, float width, float height, const UIRenderContext& ctx) {
    GLint viewport[4] = {0, 0, ctx.screenWidth, ctx.screenHeight};
    glGetIntegerv(GL_VIEWPORT, viewport);

    const float uiScale = ctx.pixelScale();
    return {
        viewport[0] + static_cast<GLint>(std::floor(x * uiScale)),
        viewport[1] + static_cast<GLint>(std::floor(y * uiScale)),
        std::max<GLint>(1, static_cast<GLint>(std::ceil(width * uiScale))),
        std::max<GLint>(1, static_cast<GLint>(std::ceil(height * uiScale))),
    };
}

float scrollbarThumbHeight(float trackHeight, float contentHeight) {
    if (trackHeight <= 0.0f || contentHeight <= 0.0f) {
        return 0.0f;
    }

    const float viewRatio = std::clamp(trackHeight / contentHeight, 0.0f, 1.0f);
    const float minThumbHeight = std::min(20.0f, trackHeight);
    return std::clamp(viewRatio * trackHeight, minThumbHeight, trackHeight);
}

float scrollbarThumbY(float trackY,
                      float trackHeight,
                      float thumbHeight,
                      float scrollOffset,
                      float scrollMax) {
    const float travel = std::max(0.0f, trackHeight - thumbHeight);
    if (scrollMax <= 0.0f || travel <= 0.0f) {
        return trackY + travel;
    }

    const float scrollRatio = std::clamp(scrollOffset / scrollMax, 0.0f, 1.0f);
    return trackY + (1.0f - scrollRatio) * travel;
}

} // namespace

UIScrollArea::UIScrollArea() {
    interactive = true;
    width = 300.0f;
    height = 400.0f;
}

UIScrollArea::~UIScrollArea() { shutdown(); }

void UIScrollArea::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    UIWidget::init(resourceMgr);
}

void UIScrollArea::shutdown() {
    UIWidget::shutdown();
    cleanupMesh();
    m_shader = nullptr;
}

void UIScrollArea::setContentHeight(float contentHeight) {
    m_contentHeight = contentHeight;
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

void UIScrollArea::setScrollbarVisible(bool scrollbarVisible) {
    m_scrollbarVisible = scrollbarVisible;
}

void UIScrollArea::setStyle(const UIScrollAreaStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIScrollArea::clearLocalStyle() {
    m_hasLocalStyle = false;
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
    constexpr int kMaxShapeVerts = 160;
    glBufferData(GL_ARRAY_BUFFER, kMaxShapeVerts * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
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

    const ScissorBox scissor = makeScaledScissorBox(ax, ay, aw, ah, ctx);

    // Save current scissor state
    GLboolean wasScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint prevScissor[4];
    glGetIntegerv(GL_SCISSOR_BOX, prevScissor);

    glScissor(scissor.x, scissor.y, scissor.width, scissor.height);
    glEnable(GL_SCISSOR_TEST);

    renderSelf(ctx);

    // Render children with Y offset applied. Content is laid out in bottom-left
    // widget coordinates; increasing the scroll offset moves content upward.
    for (const auto& child : getChildren()) {
        const_cast<UIWidget*>(child.get())->anchorOffsetY += m_scrollOffset;
        child->render(ctx);
        const_cast<UIWidget*>(child.get())->anchorOffsetY -= m_scrollOffset;
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

void UIScrollArea::renderOverlay(const UIRenderContext& ctx) const {
    if (!visible) return;

    for (const auto& child : getChildren()) {
        const_cast<UIWidget*>(child.get())->anchorOffsetY += m_scrollOffset;
        child->renderOverlay(ctx);
        const_cast<UIWidget*>(child.get())->anchorOffsetY -= m_scrollOffset;
    }
}

UIEventResult UIScrollArea::onOverlayInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible) return UIEventResult::Ignored;

    for (auto& child : const_cast<std::vector<std::unique_ptr<UIWidget>>&>(getChildren())) {
        child->anchorOffsetY += m_scrollOffset;
    }
    UIEventResult result = UIWidget::onOverlayInput(event, ctx);
    for (auto& child : const_cast<std::vector<std::unique_ptr<UIWidget>>&>(getChildren())) {
        child->anchorOffsetY -= m_scrollOffset;
    }
    return result;
}

void UIScrollArea::renderSelf(const UIRenderContext& ctx) const {
    // ScrollArea itself is transparent -- children render inside it
    (void)ctx;
}

bool UIScrollArea::clipsDescendantInput() const {
    return true;
}

bool UIScrollArea::hitTestDescendantInputClip(float px, float py, const UIRenderContext& ctx) const {
    return hitTestSelf(px, py, ctx);
}

void UIScrollArea::renderScrollbar(const UIRenderContext& ctx) const {
    if (!m_shader || m_vao == 0 || m_contentHeight <= 0.0f) return;

    const UIResolvedScrollAreaStyle resolved = resolveStyle(ctx, m_thumbHovered || m_draggingScrollbar);
    float sbWidth = resolved.scrollbarWidth;
    const Color trackCol = resolved.track;
    const Color thumbCol = resolved.thumb;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    // Scrollbar track: right edge
    float trackX = ax + aw - sbWidth;
    float trackY = ay;
    float trackH = ah;
    const float visualTrackW = std::max(3.0f, sbWidth * 0.48f);
    const float visualThumbW = std::max(5.0f, sbWidth * 0.72f);
    const float visualTrackX = trackX + (sbWidth - visualTrackW) * 0.5f;
    const float visualThumbX = trackX + (sbWidth - visualThumbW) * 0.5f;

    const float scrollMax = maxScroll();
    const float thumbH = scrollbarThumbHeight(trackH, m_contentHeight);
    const float thumbY = scrollbarThumbY(trackY, trackH, thumbH, m_scrollOffset, scrollMax);

    const UIRenderUtils::GLStateGuard glState;
    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(static_cast<float>(ctx.screenWidth),
                                                static_cast<float>(ctx.screenHeight)));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    auto drawShape = [&](const std::vector<float>& verts, Color shapeColor) {
        if (verts.empty()) return;
        shapeColor[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(shapeColor[0], shapeColor[1], shapeColor[2], shapeColor[3]));
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                        verts.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 2));
    };

    std::vector<float> verts;
    verts.reserve(160);

    verts.clear();
    UIRenderUtils::pushCapsule(verts, visualTrackX, trackY, visualTrackX + visualTrackW, trackY + trackH);
    drawShape(verts, trackCol);

    verts.clear();
    UIRenderUtils::pushCapsule(verts, visualThumbX, thumbY, visualThumbX + visualThumbW, thumbY + thumbH);
    drawShape(verts, thumbCol);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool UIScrollArea::hitTestScrollbarThumb(float px, float py, const UIRenderContext& ctx) const {
    if (m_contentHeight <= 0.0f || maxScroll() <= 0.0f) {
        return false;
    }

    const float sbWidth = resolveStyle(ctx, false).scrollbarWidth;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;
    float flippedY = static_cast<float>(ctx.screenHeight) - py;

    float trackX = ax + aw - sbWidth;
    float trackY = ay;

    const float scrollMax = maxScroll();
    const float thumbH = scrollbarThumbHeight(ah, m_contentHeight);
    const float thumbY = scrollbarThumbY(trackY, ah, thumbH, m_scrollOffset, scrollMax);

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
            const float delta = flippedY - m_dragStartY;
            const float ah = height * scaleY;
            const float thumbH = scrollbarThumbHeight(ah, m_contentHeight);
            const float scrollRange = ah - thumbH;
            if (scrollRange > 0.0f) {
                const float scrollDelta = (-delta / scrollRange) * maxScroll();
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

    // Forward input to children with the same visual offset used during render.
    for (auto& child : const_cast<std::vector<std::unique_ptr<UIWidget>>&>(getChildren())) {
        child->anchorOffsetY += m_scrollOffset;
    }
    UIEventResult result = UIWidget::onInput(event, ctx);
    for (auto& child : const_cast<std::vector<std::unique_ptr<UIWidget>>&>(getChildren())) {
        child->anchorOffsetY -= m_scrollOffset;
    }

    // If pointer is inside scroll area but not on any child, still consume scroll
    if (result == UIEventResult::Ignored && inside && event.type == UIInputEventType::Scroll) {
        float scrollAmount = 40.0f;
        m_scrollOffset = std::clamp(m_scrollOffset - event.scrollY * scrollAmount, 0.0f, maxScroll());
        return UIEventResult::Handled;
    }

    return result;
}

UIScrollAreaStyle UIScrollArea::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UIScrollAreaStyle style = UIStyleResolver::scrollAreaStyleFromTheme(ctx.theme);
    if (!ctx.theme) {
        style.track = m_scrollbarTrackColor;
        style.thumbNormal = m_scrollbarThumbColor;
        style.thumbHover = m_scrollbarThumbHoverColor;
        style.thumbDisabled = m_scrollbarThumbColor;
        style.scrollbarWidth = m_scrollbarWidth;
    }
    return style;
}

UIResolvedScrollAreaStyle UIScrollArea::resolveStyle(const UIRenderContext& ctx, bool thumbHovered) const {
    int state = interactive ? static_cast<int>(UIStyleState_Normal) : static_cast<int>(UIStyleState_Disabled);
    if (thumbHovered) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    return UIStyleResolver::resolveScrollArea(resolveBaseStyle(ctx), state);
}
