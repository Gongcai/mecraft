#include "UIProgressBar.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

UIProgressBar::UIProgressBar() {
    interactive = false;
    focusable = false;
    width = 200.0f;
    height = 20.0f;
}

UIProgressBar::~UIProgressBar() {
    shutdown();
}

void UIProgressBar::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource)
        std::abort();

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiCapsule.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiCapsule.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "UiCapsule.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiCapsule.Pipeline";
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
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid() || !m_pipelineLayout.isValid() ||
        !m_pipeline.isValid())
        std::abort();
    initMesh();
    m_progressTween.start(0.0f, m_progress, 0.3f, EasingType::EaseOut);
    UIWidget::init(resourceMgr);
}

void UIProgressBar::shutdown() {
    cleanupMesh();
    if (m_rhiDevice != nullptr) {
        if (m_pipeline.isValid())
            m_rhiDevice->destroyPipeline(m_pipeline);
        if (m_pipelineLayout.isValid())
            m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_fragmentShader.isValid())
            m_rhiDevice->destroyShader(m_fragmentShader);
        if (m_vertexShader.isValid())
            m_rhiDevice->destroyShader(m_vertexShader);
    }
    m_pipeline = {};
    m_pipelineLayout = {};
    m_fragmentShader = {};
    m_vertexShader = {};
    m_rhiDevice = nullptr;
    UIWidget::shutdown();
}

void UIProgressBar::setProgress(float progress) {
    m_progress = std::clamp(progress, 0.0f, 1.0f);
    m_progressTween.start(m_progressTween.value(), m_progress, 0.3f, EasingType::EaseOut);
}

void UIProgressBar::setLabel(const std::string& label) {
    m_label = label;
}

void UIProgressBar::setTone(UIProgressBarTone tone) {
    m_tone = tone;
    m_hasLocalColors = false;
    m_hasLocalStyle = false;
}

void UIProgressBar::setStyle(const UIProgressBarStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIProgressBar::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UIProgressBar::updateAnimations(float dt) {
    m_progressTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UIProgressBar::initMesh() {
    constexpr float vertices[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    RhiBufferDesc desc;
    desc.debugName = "UiCapsule.VertexBuffer";
    desc.size = sizeof(vertices);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    desc.memoryCategory = RhiMemoryCategory::Geometry;
    m_vertexBuffer = m_rhiDevice->createBuffer(desc, vertices, sizeof(vertices));
    if (!m_vertexBuffer.isValid())
        std::abort();
}

void UIProgressBar::cleanupMesh() {
    if (m_rhiDevice != nullptr && m_vertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_vertexBuffer);
    }
    m_vertexBuffer = {};
}

void UIProgressBar::renderSelf(const UIRenderContext& ctx) const {
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record && (ctx.commandList == nullptr || !m_pipeline.isValid() || !m_vertexBuffer.isValid()))
        return;

    const UIResolvedProgressBarStyle resolved = resolveStyle(ctx);
    const Color trackCol = resolved.track;
    const Color fillCol = resolved.fill;
    const Color textCol = resolved.text;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;

    const float progressVal = std::clamp(m_progressTween.value(), 0.0f, 1.0f);
    const float fillWidth = aw * progressVal;

    if (record) {
        ctx.commandList->setGraphicsPipeline(m_pipeline);
        ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
        auto drawShape = [&](const float x0, const float y0, const float x1, const float y1, Color shapeColor) {
            shapeColor[3] *= alpha;
            struct PushConstants {
                glm::vec4 screenRect;
                glm::vec4 rectRadius;
                glm::vec4 color;
            };
            const PushConstants pushConstants{
                glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), x0, y0),
                glm::vec4(x1 - x0, y1 - y0, std::min((x1 - x0) * 0.5f, (y1 - y0) * 0.5f), 0.0f),
                glm::vec4(shapeColor[0], shapeColor[1], shapeColor[2], shapeColor[3])};
            ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                           rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
            ctx.commandList->draw(6u, 1u, 0u, 0u);
        };
        drawShape(ax, ay, ax + aw, ay + ah, trackCol);

        if (fillWidth > 0.5f) {
            drawShape(ax, ay, ax + fillWidth, ay + ah, fillCol);
        }
    }

    // Render text overlay.
    if (ctx.textRenderer) {
        std::string overlayText;
        if (!m_label.empty()) {
            overlayText = m_label;
        } else if (m_showPercent) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f%%", progressVal * 100.0f);
            overlayText = buf;
        }

        if (!overlayText.empty()) {
            if (resolved.fontPixelHeight <= 0.0f) {
                std::abort();
            }
            const float fontPixelHeight = resolved.fontPixelHeight;
            const float textScale = (ah * resolved.textHeightRatio) / fontPixelHeight;
            const auto metrics = ctx.textRenderer->measureText(overlayText, textScale);
            const float textX = ax + (aw - metrics.width) * 0.5f;
            const float textY = ay + (ah - metrics.height) * 0.5f;
            ctx.textRenderer->draw(ctx, overlayText, textX, textY, textScale,
                                   {textCol[0], textCol[1], textCol[2], textCol[3] * alpha});
        }
    }
}

UIProgressBarStyle UIProgressBar::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UIProgressBarStyle style = UIStyleResolver::progressBarStyleFromTheme(ctx.theme, m_tone);
    if (m_hasLocalColors) {
        style.track = m_trackColor;
        style.fill = m_fillColor;
        style.text = m_textColor;
    }
    return style;
}

UIResolvedProgressBarStyle UIProgressBar::resolveStyle(const UIRenderContext& ctx) const {
    return UIStyleResolver::resolveProgressBar(resolveBaseStyle(ctx));
}
