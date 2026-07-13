#include "UIToggle.h"

#include <algorithm>

#include "../../resource/ResourceMgr.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"

#include <glm/vec4.hpp>
#include <cstdlib>

UIToggle::UIToggle() {
    interactive = true;
    focusable = true;
    width = 44.0f;
    height = 22.0f;
}

UIToggle::~UIToggle() {
    shutdown();
}

void UIToggle::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiToggle.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiToggle.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);
    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "UiToggle.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiToggle.Pipeline";
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
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid() ||
        !m_pipelineLayout.isValid() || !m_pipeline.isValid()) std::abort();
    initMesh();
    m_knobTween.start(m_checked ? 1.0f : 0.0f, m_checked ? 1.0f : 0.0f, 0.15f, EasingType::EaseOut);
    m_label.anchor = Anchor::BottomLeft;
    m_label.init(resourceMgr);
    UIWidget::init(resourceMgr);
}

void UIToggle::shutdown() {
    m_label.shutdown();
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

void UIToggle::setChecked(bool checked) {
    if (m_checked == checked) return;
    m_checked = checked;
    m_knobTween.start(m_knobTween.value(), m_checked ? 1.0f : 0.0f, 0.15f, EasingType::EaseOut);
}

void UIToggle::setLabel(const std::string& text) {
    m_label.setText(text);
}

void UIToggle::setLabelTextScale(float scale) {
    m_label.setTextScale(scale);
}

void UIToggle::setStyle(const UIToggleStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIToggle::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UIToggle::updateAnimations(float dt) {
    m_knobTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UIToggle::setFocused(bool focused) {
    UIWidget::setFocused(focused);
}

void UIToggle::toggle() {
    m_checked = !m_checked;
    m_knobTween.start(m_knobTween.value(), m_checked ? 1.0f : 0.0f, 0.15f, EasingType::EaseOut);
    if (onChanged) onChanged(m_checked);
}

void UIToggle::initMesh() {
    constexpr float vertices[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
    };
    RhiBufferDesc desc;
    desc.debugName = "UiToggle.VertexBuffer";
    desc.size = sizeof(vertices);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    m_vertexBuffer = m_rhiDevice->createBuffer(desc, vertices, sizeof(vertices));
    if (!m_vertexBuffer.isValid()) std::abort();
}

void UIToggle::cleanupMesh() {
    if (m_rhiDevice != nullptr && m_vertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_vertexBuffer);
    m_vertexBuffer = {};
}

void UIToggle::renderSelf(const UIRenderContext& ctx) const {
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record &&
        (ctx.commandList == nullptr || !m_pipeline.isValid() || !m_vertexBuffer.isValid())) return;

    const UIResolvedToggleStyle resolved =
        UIStyleResolver::resolveToggle(resolveBaseStyle(ctx), currentStyleState());
    const Color trackOff = resolved.trackOff;
    const Color trackOn = resolved.trackOn;
    const Color knobCol = resolved.knob;
    const float toggleW = resolved.width;
    const float toggleH = resolved.height;

    const float ax = getAbsoluteX(ctx);
    const float widgetH = height * scaleY;
    const float ay = getAbsoluteY(ctx) + std::max(0.0f, (widgetH - toggleH) * 0.5f);

    const float trackRadius = toggleH * 0.5f;
    const float knobRadius = toggleH * 0.38f;
    const float t = m_knobTween.value();
    const float knobCx = ax + trackRadius + t * (toggleW - toggleH);
    const float knobCy = ay + trackRadius;

    // Interpolate track color.
    Color trackCol = {
        trackOff[0] + (trackOn[0] - trackOff[0]) * t,
        trackOff[1] + (trackOn[1] - trackOff[1]) * t,
        trackOff[2] + (trackOn[2] - trackOff[2]) * t,
        trackOff[3] + (trackOn[3] - trackOff[3]) * t,
    };
    const bool active = m_hovered || isFocused();
    if (active) {
        trackCol[0] = std::clamp(trackCol[0] * 0.82f, 0.0f, 1.0f);
        trackCol[1] = std::clamp(trackCol[1] * 0.82f, 0.0f, 1.0f);
        trackCol[2] = std::clamp(trackCol[2] * 0.82f, 0.0f, 1.0f);
        trackCol[3] = std::clamp(trackCol[3] * 1.08f, 0.0f, 1.0f);
    }

    if (record) {
        ctx.commandList->setGraphicsPipeline(m_pipeline);
        ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
        auto drawShape = [&](const float x0, const float y0, const float shapeW,
                             const float shapeH, const float radius, Color shapeColor) {
            shapeColor[3] *= alpha;
            struct PushConstants { glm::vec4 screenRect; glm::vec4 rectRadius; glm::vec4 color; };
            const PushConstants pushConstants{
                glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), x0, y0),
                glm::vec4(shapeW, shapeH, radius, 0.0f),
                glm::vec4(shapeColor[0], shapeColor[1], shapeColor[2], shapeColor[3])
            };
            ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                           rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
            ctx.commandList->draw(6u, 1u, 0u, 0u);
        };
        drawShape(ax, ay, toggleW, toggleH, trackRadius, trackCol);
        Color knobShadow {0.0f, 0.0f, 0.0f, active ? 0.30f : 0.20f};
        const float shadowRadius = knobRadius + 1.5f;
        drawShape(knobCx - shadowRadius, knobCy - 1.0f - shadowRadius,
                  shadowRadius * 2.0f, shadowRadius * 2.0f, shadowRadius, knobShadow);
        drawShape(knobCx - knobRadius, knobCy - knobRadius,
                  knobRadius * 2.0f, knobRadius * 2.0f, knobRadius, knobCol);
    }

    // Render label to the right.
    const float labelGap = 8.0f;
    const Color textCol = resolved.text;
    const float th = ctx.textRenderer ? m_label.measureTextHeight(*ctx.textRenderer) : 0.0f;
    const_cast<UIText&>(m_label).anchorOffsetX = ax + toggleW + labelGap;
    const_cast<UIText&>(m_label).anchorOffsetY = ay + (toggleH - th) * 0.5f;
    const_cast<UIText&>(m_label).setTextColor(textCol);
    const_cast<UIText&>(m_label).alpha = alpha;
    m_label.render(ctx);
}

UIEventResult UIToggle::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    // Forward to children first.
    const UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    const bool inside = hitTest(event.x, event.y, ctx);

    switch (event.type) {
    case UIInputEventType::PointerMove:
        m_hovered = inside;
        return inside ? UIEventResult::Handled : UIEventResult::Ignored;

    case UIInputEventType::PointerDown:
        if (event.button == UIPointerButton::Primary && inside) {
            return UIEventResult::Handled;
        }
        break;

    case UIInputEventType::PointerUp:
        if (event.button == UIPointerButton::Primary && inside) {
            toggle();
            return UIEventResult::Consumed;
        }
        break;

    case UIInputEventType::Command:
        if (isFocused() && event.command == UICommand::Activate) {
            toggle();
            return UIEventResult::Consumed;
        }
        break;

    default:
        break;
    }

    return childResult;
}

UIToggleStyle UIToggle::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UIToggleStyle style = UIStyleResolver::toggleStyleFromTheme(ctx.theme);
    if (m_hasLocalColors || !ctx.theme) {
        style.trackOff = m_trackOffColor;
        style.trackOn = m_trackOnColor;
        style.trackDisabled = m_trackOffColor;
        style.knobNormal = m_knobColor;
        style.knobHover = m_knobHoverColor;
        style.knobDisabled = m_knobColor;
        style.width = width;
        style.height = height;
    }
    return style;
}

int UIToggle::currentStyleState() const {
    if (!interactive) {
        return static_cast<int>(UIStyleState_Disabled);
    }

    int state = static_cast<int>(UIStyleState_Normal);
    if (m_hovered || isFocused()) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    return state;
}
