#include "CrosshairControl.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"
#include "../core/UITheme.h"

void CrosshairControl::init(ResourceMgr& resourceMgr)
{
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/crosshair_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/crosshair_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "Crosshair.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "Crosshair.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "Crosshair.PipelineLayout";
    layoutDesc.pushConstantBytes = 32u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Crosshair.Pipeline";
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
    blend.srcColor = RhiBlendFactor::OneMinusDstColor;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::OneMinusDstColor;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    m_pipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid() ||
        !m_pipelineLayout.isValid() || !m_pipeline.isValid()) std::abort();
    initMesh();
}

void CrosshairControl::shutdown()
{
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

void CrosshairControl::setSize(float size)
{
    const float clamped = std::clamp(size, 0.5f, 4.0f);
    if (m_size == clamped) {
        return;
    }

    m_size = clamped;
    rebuildMesh();
}

float CrosshairControl::getSize() const
{
    return m_size;
}

void CrosshairControl::setColor(const std::array<float, 4>& color)
{
    m_color = color;
}

const std::array<float, 4>& CrosshairControl::getColor() const
{
    return m_color;
}

void CrosshairControl::initMesh()
{
    constexpr int kBaseArmLen = 7;
    constexpr int kBaseThickness = 2;

    const int armLen = std::max(2, static_cast<int>(std::lround(kBaseArmLen * m_size)));
    const int thickness = std::max(2, static_cast<int>(std::lround(kBaseThickness * m_size)));
    const int halfT = thickness / 2;

    std::vector<float> vertices;
    auto addQuad = [&](int x0, int y0, int x1, int y1)
    {
        const auto fx0 = static_cast<float>(x0);
        const auto fy0 = static_cast<float>(y0);
        const auto fx1 = static_cast<float>(x1);
        const auto fy1 = static_cast<float>(y1);

        vertices.push_back(fx0); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy1);
        vertices.push_back(fx0); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy1);
        vertices.push_back(fx0); vertices.push_back(fy1);
    };

    addQuad(-halfT, -halfT, halfT, halfT);
    addQuad(-armLen - halfT, -halfT, -halfT, halfT);
    addQuad(halfT, -halfT, armLen + halfT, halfT);
    addQuad(-halfT, -armLen - halfT, halfT, -halfT);
    addQuad(-halfT, halfT, halfT, armLen + halfT);

    m_vertexCount = static_cast<int>(vertices.size() / 2);

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Crosshair.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(float);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    m_vertexBuffer = m_rhiDevice->createBuffer(bufferDesc, vertices.data(), bufferDesc.size);
    if (!m_vertexBuffer.isValid()) std::abort();
}

void CrosshairControl::rebuildMesh()
{
    if (!m_vertexBuffer.isValid()) {
        return;
    }
    cleanupMesh();
    initMesh();
}

void CrosshairControl::cleanupMesh()
{
    if (m_rhiDevice != nullptr && m_vertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_vertexBuffer);
    }
    m_vertexBuffer = {};
    m_vertexCount = 0;
}

void CrosshairControl::renderSelf(const UIRenderContext& ctx) const
{
    if (ctx.commandList == nullptr || !m_pipeline.isValid() ||
        !m_vertexBuffer.isValid() || m_vertexCount == 0) {
        return;
    }

    const float screenW = static_cast<float>(ctx.screenWidth);
    const float screenH = static_cast<float>(ctx.screenHeight);

    const UITheme* theme = ctx.theme;
    const auto& col = theme ? theme->crosshair : m_color;
    struct PushConstants {
        glm::vec4 screenAndOffset;
        glm::vec4 color;
    };
    const PushConstants pushConstants{
        glm::vec4(screenW, screenH, screenW * 0.5f, screenH * 0.5f),
        glm::vec4(col[0], col[1], col[2], col[3])
    };
    ctx.commandList->setGraphicsPipeline(m_pipeline);
    ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
    ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                   rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    ctx.commandList->draw(static_cast<uint32_t>(m_vertexCount), 1u, 0u, 0u);
}
