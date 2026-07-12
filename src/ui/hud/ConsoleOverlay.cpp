#include "ConsoleOverlay.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <glm/vec4.hpp>

#include "../font/TextRenderer.h"
#include "../core/UIStyle.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"

void ConsoleOverlay::init(ResourceMgr& resourceMgr)
{
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "ConsoleOverlay.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "ConsoleOverlay.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);
    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "ConsoleOverlay.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "ConsoleOverlay.Pipeline";
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
    constexpr float vertices[] = {0,0, 1,0, 1,1, 0,0, 1,1, 0,1};
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "ConsoleOverlay.VertexBuffer";
    bufferDesc.size = sizeof(vertices);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    m_vertexBuffer = m_rhiDevice->createBuffer(bufferDesc, vertices, sizeof(vertices));
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid() ||
        !m_pipelineLayout.isValid() || !m_pipeline.isValid() || !m_vertexBuffer.isValid()) std::abort();
}

void ConsoleOverlay::shutdown()
{
    if (m_rhiDevice != nullptr) {
        if (m_vertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_vertexBuffer);
        if (m_pipeline.isValid()) m_rhiDevice->destroyPipeline(m_pipeline);
        if (m_pipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_fragmentShader.isValid()) m_rhiDevice->destroyShader(m_fragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
    }
    m_vertexBuffer = {}; m_pipeline = {}; m_pipelineLayout = {};
    m_fragmentShader = {}; m_vertexShader = {}; m_rhiDevice = nullptr;
    m_display.clear();
}

void ConsoleOverlay::appendLine(const std::string& message,
                                double createdAtSec,
                                ConsoleDisplayBox::MessageType type)
{
    if (message.empty()) {
        return;
    }

    m_display.setMaxLines(m_maxLines);
    m_display.appendLine(message, createdAtSec, type);
}

void ConsoleOverlay::clear()
{
    m_display.clear();
}

bool ConsoleOverlay::empty() const
{
    return m_display.empty();
}

void ConsoleOverlay::setMaxLines(std::size_t maxLines)
{
    m_maxLines = maxLines;
    m_display.setMaxLines(maxLines);
}

void ConsoleOverlay::setTextRenderer(const TextRenderer* textRenderer)
{
    m_textRenderer = textRenderer;
}

void ConsoleOverlay::renderSelf(const UIRenderContext& context) const
{
    const TextRenderer* textRenderer = context.textRenderer ? context.textRenderer : m_textRenderer;
    if (!textRenderer) {
        return;
    }

    renderMessages(static_cast<double>(context.timeSeconds), *textRenderer, context);
}

void ConsoleOverlay::drawOverlayRect(const UIRenderContext& context,
                                     int rectX,
                                     int rectY,
                                     int rectW,
                                     int rectH,
                                     const std::array<float, 4>& rectColor) const
{
    if (context.commandList == nullptr || !m_pipeline.isValid() ||
        !m_vertexBuffer.isValid() || rectW <= 0 || rectH <= 0) {
        return;
    }
    context.commandList->setGraphicsPipeline(m_pipeline);
    context.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
    struct PushConstants { glm::vec4 screenRect; glm::vec4 rectRadius; glm::vec4 color; };
    const PushConstants push{
        glm::vec4(context.screenWidth, context.screenHeight, rectX, rectY),
        glm::vec4(rectW, rectH, 0.0f, 0.0f),
        glm::vec4(rectColor[0], rectColor[1], rectColor[2], rectColor[3])};
    context.commandList->pushConstants(&push, sizeof(push),
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    context.commandList->draw(6u, 1u, 0u, 0u);
}

void ConsoleOverlay::renderMessages(double nowSec, const TextRenderer& textRenderer,
                                    const UIRenderContext& context) const
{
    if (m_display.empty()) {
        return;
    }
    const bool record = context.phase == UIRenderPhase::Record;
    if (record && context.commandList == nullptr) {
        return;
    }

    const int screenW = context.screenWidth;
    const int screenH = context.screenHeight;
    if (screenW <= 0 || screenH <= 0) {
        return;
    }

    ConsoleDisplayBox::RenderParams params;
    params.screenW = screenW;
    params.screenH = screenH;
    params.visibleBoxes = m_visibleBoxes;
    params.holdSeconds = m_holdSeconds;
    params.fadeEndSeconds = m_fadeEndSeconds;
    const UIResolvedConsoleStyle style =
        UIStyleResolver::resolveConsole(UIStyleResolver::consoleStyleFromTheme(context.theme));
    params.x = style.x;
    params.inputY = style.inputY;
    params.inputBoxH = style.inputBoxHeight;
    params.inputToFirstBoxGap = style.inputToFirstBoxGap;
    params.boxH = style.boxHeight;
    params.boxGap = style.boxGap;
    params.horizontalMargin = style.horizontalMargin;
    params.minBoxW = style.minBoxWidth;
    params.boxWidthRatio = style.boxWidthRatio;
    params.textPadX = style.textPaddingX;
    params.textPadY = style.textPaddingY;
    params.textScale = style.textScale;
    params.boxColor = style.box;
    params.normalTextColor = style.textNormal;
    params.warningTextColor = style.textWarning;
    params.successTextColor = style.textSuccess;

    m_display.setMaxLines(m_maxLines);
    const float uiScale = context.pixelScale();
    RhiRect2D textScissor = context.hasScissor
        ? context.scissor
        : RhiRect2D{0, 0, static_cast<uint32_t>(screenW * uiScale),
                    static_cast<uint32_t>(screenH * uiScale)};
    m_display.render(
        nowSec,
        params,
        [this, &context](int rectX, int rectY, int rectW, int rectH, const std::array<float, 4>& rectColor) {
            drawOverlayRect(context, rectX, rectY, rectW, rectH, rectColor);
        },
        [&context, record, &textScissor, uiScale](int clipX, int clipY, int clipW, int clipH) {
            RhiRect2D clip{
                static_cast<int32_t>(std::floor(static_cast<float>(clipX) * uiScale)),
                static_cast<int32_t>(std::floor(static_cast<float>(clipY) * uiScale)),
                static_cast<uint32_t>(std::max(0.0f, std::ceil(static_cast<float>(clipW) * uiScale))),
                static_cast<uint32_t>(std::max(0.0f, std::ceil(static_cast<float>(clipH) * uiScale)))
            };
            if (context.hasScissor) {
                const int32_t x0 = std::max(clip.x, context.scissor.x);
                const int32_t y0 = std::max(clip.y, context.scissor.y);
                const int32_t x1 = std::min(clip.x + static_cast<int32_t>(clip.width),
                                            context.scissor.x + static_cast<int32_t>(context.scissor.width));
                const int32_t y1 = std::min(clip.y + static_cast<int32_t>(clip.height),
                                            context.scissor.y + static_cast<int32_t>(context.scissor.height));
                clip = {x0, y0, static_cast<uint32_t>(std::max(0, x1 - x0)),
                        static_cast<uint32_t>(std::max(0, y1 - y0))};
            }
            textScissor = clip;
            if (record) {
                context.commandList->setScissor(clip);
            }
        },
        [&textRenderer, &context, &textScissor](const std::string& line,
                                          float textX,
                                          float textY,
                                          float scale,
                                          const std::array<float, 4>& textColor,
                                          float,
                                          float) {
            UIRenderContext textContext = context;
            textContext.hasScissor = true;
            textContext.scissor = textScissor;
            textRenderer.draw(textContext, line, textX, textY, scale, textColor);
        },
        [&textRenderer](const std::string& text, float scale) -> ConsoleDisplayBox::TextMetricsResult {
            auto m = textRenderer.measureText(text, scale);
            return {m.width, m.height};
        });

    if (record) {
        const RhiRect2D parentScissor = context.hasScissor
            ? context.scissor
            : RhiRect2D{0, 0, static_cast<uint32_t>(screenW * uiScale),
                        static_cast<uint32_t>(screenH * uiScale)};
        context.commandList->setScissor(parentScissor);
    }
}
