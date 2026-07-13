#include "UIToast.h"

#include <algorithm>

#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"

#include <glm/vec4.hpp>
#include <cstdlib>

namespace {

UIToastTone toToastTone(UIToast::Type type) {
    switch (type) {
    case UIToast::Type::Info:    return UIToastTone::Info;
    case UIToast::Type::Success: return UIToastTone::Success;
    case UIToast::Type::Warning: return UIToastTone::Warning;
    case UIToast::Type::Error:   return UIToastTone::Error;
    }
    return UIToastTone::Info;
}

} // namespace

UIToast::UIToast() {
    interactive = false;
    focusable = false;
    visible = true;
}

UIToast::~UIToast() {
    shutdown();
}

void UIToast::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiToast.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiToast.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);
    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "UiToast.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiToast.Pipeline";
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
    constexpr float vertices[] = {0,0, 1,0, 1,1, 0,0, 1,1, 0,1};
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "UiToast.VertexBuffer";
    bufferDesc.size = sizeof(vertices);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    m_vertexBuffer = m_rhiDevice->createBuffer(bufferDesc, vertices, sizeof(vertices));
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid() ||
        !m_pipelineLayout.isValid() || !m_pipeline.isValid() || !m_vertexBuffer.isValid()) std::abort();

    UIWidget::init(resourceMgr);
}

void UIToast::shutdown() {
    cleanupMesh();
    if (m_rhiDevice) {
        if (m_pipeline.isValid()) m_rhiDevice->destroyPipeline(m_pipeline);
        if (m_pipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_fragmentShader.isValid()) m_rhiDevice->destroyShader(m_fragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
    }
    m_pipeline = {}; m_pipelineLayout = {}; m_fragmentShader = {}; m_vertexShader = {};
    m_rhiDevice = nullptr;
    m_toasts.clear();
    UIWidget::shutdown();
}

void UIToast::showToast(const std::string& text, Type type, float duration) {
    ToastEntry entry;
    entry.text = text;
    entry.type = type;
    entry.elapsed = 0.0f;
    entry.duration = duration;
    entry.alphaTween.start(0.0f, 1.0f, 0.2f, EasingType::EaseOut);
    m_toasts.insert(m_toasts.begin(), std::move(entry));

    // Remove excess toasts.
    while (static_cast<int>(m_toasts.size()) > m_maxVisible) {
        m_toasts.pop_back();
    }
}

void UIToast::setStyle(const UIToastStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIToast::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UIToast::onUpdate(float dt) {
    // Update toast timers and animations.
    for (auto it = m_toasts.begin(); it != m_toasts.end();) {
        it->elapsed += dt;
        it->alphaTween.tick(dt);

        // Start fade-out when nearing the end.
        const float fadeStart = it->duration - 0.3f;
        if (it->elapsed >= fadeStart && it->alphaTween.value() > 0.0f) {
            // Only start the fade-out tween once.
            if (it->alphaTween.value() > 0.9f) {
                it->alphaTween.start(1.0f, 0.0f, 0.3f, EasingType::EaseIn);
            }
        }

        if (it->elapsed >= it->duration) {
            it = m_toasts.erase(it);
        } else {
            ++it;
        }
    }
}

void UIToast::cleanupMesh() {
    if (m_rhiDevice && m_vertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_vertexBuffer);
    m_vertexBuffer = {};
}

void UIToast::renderSelf(const UIRenderContext& ctx) const {
    if (m_toasts.empty()) return;
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record && (!ctx.commandList || !m_pipeline.isValid() || !m_vertexBuffer.isValid())) return;
    const UIToastStyle baseStyle = resolveBaseStyle(ctx);

    const float screenW = static_cast<float>(ctx.screenWidth);
    const float centerX = screenW * 0.5f;
    float currentY = baseStyle.bottomMargin;

    for (const auto& toast : m_toasts) {
        const float toastAlpha = toast.alphaTween.value();
        if (toastAlpha < 0.01f) continue;

        const UIResolvedToastStyle resolved = UIStyleResolver::resolveToast(baseStyle, toToastTone(toast.type));
        const float x0 = centerX - resolved.width * 0.5f;
        const float y0 = currentY;
        const float x1 = x0 + resolved.width;
        const float y1 = y0 + resolved.height;

        if (record) {
            const float bw = resolved.borderWidth;
            ctx.commandList->setGraphicsPipeline(m_pipeline);
            ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
            auto drawRect = [&](float rx, float ry, float rw, float rh, Color color) {
                color[3] *= toastAlpha;
                struct Push { glm::vec4 screenRect; glm::vec4 rectRadius; glm::vec4 color; };
                const Push push{glm::vec4(screenW, static_cast<float>(ctx.screenHeight), rx, ry),
                                glm::vec4(rw, rh, 0.0f, 0.0f),
                                glm::vec4(color[0], color[1], color[2], color[3])};
                ctx.commandList->pushConstants(&push, sizeof(push),
                    rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
                ctx.commandList->draw(6u, 1u, 0u, 0u);
            };
            drawRect(x0, y0, resolved.width, resolved.height, resolved.background);
            drawRect(x0, y1 - bw, resolved.width, bw, resolved.border);
            drawRect(x0, y0, resolved.width, bw, resolved.border);
            drawRect(x0, y0, bw, resolved.height, resolved.border);
            drawRect(x1 - bw, y0, bw, resolved.height, resolved.border);
            drawRect(x0, y0, resolved.accentWidth, resolved.height, resolved.accent);
        }

        // Render text.
        if (ctx.textRenderer) {
            const float textScale = 1.0f;
            const auto metrics = ctx.textRenderer->measureText(toast.text, textScale);
            const float textX = x0 + resolved.textPadding;
            const float textY = y0 + (resolved.height - metrics.height) * 0.5f;
            ctx.textRenderer->draw(
                ctx,
                toast.text,
                textX,
                textY,
                textScale,
                {resolved.text[0], resolved.text[1], resolved.text[2],
                 resolved.text[3] * toastAlpha});
        }

        currentY += resolved.height + resolved.spacing;
    }
}

UIToastStyle UIToast::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }
    return UIStyleResolver::toastStyleFromTheme(ctx.theme);
}

UIResolvedToastStyle UIToast::resolveStyle(const UIRenderContext& ctx, Type type) const {
    return UIStyleResolver::resolveToast(resolveBaseStyle(ctx), toToastTone(type));
}
