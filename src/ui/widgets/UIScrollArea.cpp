#include "UIScrollArea.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"

namespace {

RhiRect2D makeScaledScissorBox(float x, float y, float width, float height, const UIRenderContext& ctx) {
    const float uiScale = ctx.pixelScale();
    RhiRect2D rect{static_cast<int32_t>(std::floor(x * uiScale)),
                   static_cast<int32_t>(std::floor(y * uiScale)),
                   static_cast<uint32_t>(std::max(1.0f, std::ceil(width * uiScale))),
                   static_cast<uint32_t>(std::max(1.0f, std::ceil(height * uiScale)))};
    if (!ctx.hasScissor) return rect;
    const int32_t x0 = std::max(rect.x, ctx.scissor.x);
    const int32_t y0 = std::max(rect.y, ctx.scissor.y);
    const int32_t x1 = std::min(rect.x + static_cast<int32_t>(rect.width),
                                ctx.scissor.x + static_cast<int32_t>(ctx.scissor.width));
    const int32_t y1 = std::min(rect.y + static_cast<int32_t>(rect.height),
                                ctx.scissor.y + static_cast<int32_t>(ctx.scissor.height));
    return {x0, y0, static_cast<uint32_t>(std::max(0, x1 - x0)),
            static_cast<uint32_t>(std::max(0, y1 - y0))};
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
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiScrollArea.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiScrollArea.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "UiScrollArea.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiScrollArea.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, sizeof(float) * 2u, RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes.push_back({0u, 0u, RhiVertexFormat::Float2, 0u});
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(m_rhiDevice->swapchainColorFormat());
    pipelineDesc.depthFormat = m_rhiDevice->swapchainDepthStencilFormat();
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

void UIScrollArea::shutdown() {
    UIWidget::shutdown();
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
    constexpr float vertices[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
    };
    RhiBufferDesc desc;
    desc.debugName = "UiScrollArea.VertexBuffer";
    desc.size = sizeof(vertices);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    desc.memoryCategory = RhiMemoryCategory::Geometry;
    m_vertexBuffer = m_rhiDevice->createBuffer(desc, vertices, sizeof(vertices));
}

void UIScrollArea::cleanupMesh() {
    if (m_rhiDevice != nullptr && m_vertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_vertexBuffer);
    }
    m_vertexBuffer = {};
}

void UIScrollArea::render(const UIRenderContext& ctx) const {
    if (!visible) return;
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record && ctx.commandList == nullptr) return;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    UIRenderContext clippedContext = ctx;
    clippedContext.hasScissor = true;
    clippedContext.scissor = makeScaledScissorBox(ax, ay, aw, ah, ctx);
    if (record) {
        ctx.commandList->setScissor(clippedContext.scissor);
    }
    renderSelf(clippedContext);

    // Render children with Y offset applied. Content is laid out in bottom-left
    // widget coordinates; increasing the scroll offset moves content upward.
    for (const auto& child : getChildren()) {
        const_cast<UIWidget*>(child.get())->anchorOffsetY += m_scrollOffset;
        child->render(clippedContext);
        const_cast<UIWidget*>(child.get())->anchorOffsetY -= m_scrollOffset;
    }

    const RhiRect2D parentScissor = ctx.hasScissor
        ? ctx.scissor
        : RhiRect2D{0, 0, static_cast<uint32_t>(ctx.screenWidth * ctx.pixelScale()),
                    static_cast<uint32_t>(ctx.screenHeight * ctx.pixelScale())};
    if (record) {
        ctx.commandList->setScissor(parentScissor);
    }

    // Render scrollbar outside scissor (on top)
    if (record && m_scrollbarVisible && maxScroll() > 0.0f) {
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
    if (ctx.commandList == nullptr || !m_pipeline.isValid() ||
        !m_vertexBuffer.isValid() || m_contentHeight <= 0.0f) return;

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

    ctx.commandList->setGraphicsPipeline(m_pipeline);
    ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
    auto drawShape = [&](float x, float y, float shapeWidth, float shapeHeight, Color shapeColor) {
        shapeColor[3] *= alpha;
        struct PushConstants {
            glm::vec4 screenRect;
            glm::vec4 rectRadius;
            glm::vec4 color;
        };
        const PushConstants pushConstants{
            glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), x, y),
            glm::vec4(shapeWidth, shapeHeight, shapeWidth * 0.5f, 0.0f),
            glm::vec4(shapeColor[0], shapeColor[1], shapeColor[2], shapeColor[3])
        };
        ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                       rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        ctx.commandList->draw(6u, 1u, 0u, 0u);
    };
    drawShape(visualTrackX, trackY, visualTrackW, trackH, trackCol);
    drawShape(visualThumbX, thumbY, visualThumbW, thumbH, thumbCol);
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
