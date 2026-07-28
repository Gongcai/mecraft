#include "UIRadioButton.h"

#include <algorithm>
#include <glm/vec4.hpp>

#include "../core/UITheme.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"

#include <cstdlib>

UIRadioButtonGroup::UIRadioButtonGroup() {
    interactive = true;
    focusable = false; // The group itself is not focusable; individual hit-testing handles interaction.
    width = 200.0f;
    height = 100.0f;
}

UIRadioButtonGroup::~UIRadioButtonGroup() {
    shutdown();
}

void UIRadioButtonGroup::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiRadioButton.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiRadioButton.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "UiRadioButton.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiRadioButton.Pipeline";
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
    UIWidget::init(resourceMgr);
}

void UIRadioButtonGroup::shutdown() {
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

int UIRadioButtonGroup::addOption(const std::string& text) {
    Option opt;
    opt.text = text;
    opt.selectTween.start(0.0f, 0.0f, 0.15f, EasingType::EaseOut);
    m_options.push_back(std::move(opt));

    // Recalculate height.
    const float rowHeight = 24.0f;
    height = static_cast<float>(m_options.size()) * (rowHeight + m_spacing) - m_spacing;
    return static_cast<int>(m_options.size()) - 1;
}

void UIRadioButtonGroup::setSelectedIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_options.size())) return;
    if (m_selectedIndex == index) return;

    // Animate old selection out.
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size())) {
        m_options[m_selectedIndex].selectTween.start(
            m_options[m_selectedIndex].selectTween.value(), 0.0f, 0.15f, EasingType::EaseOut);
    }

    m_selectedIndex = index;

    // Animate new selection in.
    m_options[m_selectedIndex].selectTween.start(
        m_options[m_selectedIndex].selectTween.value(), 1.0f, 0.15f, EasingType::EaseOut);

    if (onSelectionChanged) onSelectionChanged(m_selectedIndex);
}

void UIRadioButtonGroup::setStyle(const UIRadioButtonStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIRadioButtonGroup::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UIRadioButtonGroup::updateAnimations(float dt) {
    for (auto& opt : m_options) {
        opt.selectTween.tick(dt);
    }
    UIWidget::updateAnimations(dt);
}

void UIRadioButtonGroup::initMesh() {
    constexpr float vertices[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
    };
    RhiBufferDesc desc;
    desc.debugName = "UiRadioButton.VertexBuffer";
    desc.size = sizeof(vertices);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    desc.memoryCategory = RhiMemoryCategory::Geometry;
    m_vertexBuffer = m_rhiDevice->createBuffer(desc, vertices, sizeof(vertices));
    if (!m_vertexBuffer.isValid()) std::abort();
}

void UIRadioButtonGroup::cleanupMesh() {
    if (m_rhiDevice != nullptr && m_vertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_vertexBuffer);
    }
    m_vertexBuffer = {};
}

int UIRadioButtonGroup::hitTestOption(float px, float py, const UIRenderContext& ctx) const {
    const float flippedY = static_cast<float>(ctx.screenHeight) - py;
    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float radioSz = resolveStyle(ctx, false).radioSize;
    const float rowHeight = 24.0f;

    for (int i = 0; i < static_cast<int>(m_options.size()); ++i) {
        const float rowY = ay + static_cast<float>(i) * (rowHeight + m_spacing);
        const float cy = rowY + rowHeight * 0.5f;
        // Hit test the radio circle area (with padding).
        const float dx = px - (ax + radioSz * 0.5f);
        const float dy = flippedY - cy;
        if (dx * dx + dy * dy <= (radioSz * 0.5f + 4.0f) * (radioSz * 0.5f + 4.0f)) {
            return i;
        }
        // Also allow clicking on the label text area.
        if (px >= ax && px < ax + width * scaleX &&
            flippedY >= rowY && flippedY < rowY + rowHeight) {
            return i;
        }
    }
    return -1;
}

void UIRadioButtonGroup::renderSelf(const UIRenderContext& ctx) const {
    if (m_options.empty()) return;
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record && (ctx.commandList == nullptr || !m_pipeline.isValid() ||
                   !m_vertexBuffer.isValid())) return;

    const UIResolvedRadioButtonStyle baseResolved = resolveStyle(ctx, false);
    const float radioSz = baseResolved.radioSize;
    const float rowHeight = 24.0f;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);

    if (record) {
        ctx.commandList->setGraphicsPipeline(m_pipeline);
        ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);

        for (int i = 0; i < static_cast<int>(m_options.size()); ++i) {
            const Option& opt = m_options[i];
            const UIResolvedRadioButtonStyle resolved = resolveStyle(ctx, opt.hovered);
            const float rowY = ay + static_cast<float>(i) * (rowHeight + m_spacing);
            const float cy = rowY + rowHeight * 0.5f;
            const float cx = ax + radioSz * 0.5f;
            const float outerR = radioSz * 0.5f;
            const float innerR = outerR * 0.48f * opt.selectTween.value();

            auto drawCircle = [&](const float radius, Color circleColor) {
                const float diameter = radius * 2.0f;
                circleColor[3] *= alpha;
                struct PushConstants { glm::vec4 screenRect; glm::vec4 rectRadius; glm::vec4 color; };
                const PushConstants pushConstants{
                    glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight),
                              cx - radius, cy - radius),
                    glm::vec4(diameter, diameter, radius, 0.0f),
                    glm::vec4(circleColor[0], circleColor[1], circleColor[2], circleColor[3])
                };
                ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                               rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
                ctx.commandList->draw(6u, 1u, 0u, 0u);
            };

            const Color oc = resolved.outer;
            drawCircle(outerR, oc);

            Color wellCol {
                std::clamp(oc[0] * 0.34f, 0.0f, 1.0f),
                std::clamp(oc[1] * 0.34f, 0.0f, 1.0f),
                std::clamp(oc[2] * 0.34f, 0.0f, 1.0f),
                opt.hovered ? 0.82f : 0.70f,
            };
            drawCircle(outerR * 0.66f, wellCol);

            if (innerR > 0.5f) {
                drawCircle(innerR, resolved.inner);
            }
        }
    }

    // Render label text.
    if (ctx.textRenderer) {
        const float textScale = 1.0f;
        const float textX = ax + radioSz + 8.0f;
        for (int i = 0; i < static_cast<int>(m_options.size()); ++i) {
            const Option& opt = m_options[i];
            const UIResolvedRadioButtonStyle resolved = resolveStyle(ctx, opt.hovered);
            const Color txtCol = resolved.text;
            const float rowY = ay + static_cast<float>(i) * (rowHeight + m_spacing);
            const auto metrics = ctx.textRenderer->measureText(opt.text, textScale);
            const float textY = rowY + (rowHeight - metrics.height) * 0.5f;
            ctx.textRenderer->draw(
                ctx,
                opt.text,
                textX,
                textY,
                textScale,
                {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * alpha});
        }
    }
}

UIEventResult UIRadioButtonGroup::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    switch (event.type) {
    case UIInputEventType::PointerMove: {
        const int idx = hitTestOption(event.x, event.y, ctx);
        bool changed = false;
        for (int i = 0; i < static_cast<int>(m_options.size()); ++i) {
            const bool wasHovered = m_options[i].hovered;
            m_options[i].hovered = (i == idx);
            if (m_options[i].hovered != wasHovered) changed = true;
        }
        return (idx >= 0 || changed) ? UIEventResult::Handled : UIEventResult::Ignored;
    }

    case UIInputEventType::PointerUp:
        if (event.button == UIPointerButton::Primary) {
            const int idx = hitTestOption(event.x, event.y, ctx);
            if (idx >= 0) {
                setSelectedIndex(idx);
                return UIEventResult::Consumed;
            }
        }
        break;

    default:
        break;
    }

    return UIEventResult::Ignored;
}

UIRadioButtonStyle UIRadioButtonGroup::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UIRadioButtonStyle style = UIStyleResolver::radioButtonStyleFromTheme(ctx.theme);
    if (m_hasLocalColors || !ctx.theme) {
        style.outerNormal = m_outerColor;
        style.outerHover = m_outerHoverColor;
        style.outerDisabled = m_outerColor;
        style.innerNormal = m_innerColor;
        style.innerDisabled = m_innerColor;
        style.textNormal = m_textColor;
        style.textDisabled = m_textColor;
        style.radioSize = 18.0f;
    }
    return style;
}

UIResolvedRadioButtonStyle UIRadioButtonGroup::resolveStyle(const UIRenderContext& ctx, bool hovered) const {
    int state = interactive ? static_cast<int>(UIStyleState_Normal) : static_cast<int>(UIStyleState_Disabled);
    if (hovered) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    return UIStyleResolver::resolveRadioButton(resolveBaseStyle(ctx), state);
}
