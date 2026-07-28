#include "ScreenTransition.h"

#include <glm/vec4.hpp>

#include "../../resource/ResourceMgr.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"

#include <cstdlib>

ScreenTransition::~ScreenTransition() {
    shutdown();
}

void ScreenTransition::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "ScreenTransition.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "ScreenTransition.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "ScreenTransition.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "ScreenTransition.Pipeline";
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
}

void ScreenTransition::shutdown() {
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

void ScreenTransition::initMesh() {
    constexpr float vertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };
    RhiBufferDesc desc;
    desc.debugName = "ScreenTransition.VertexBuffer";
    desc.size = sizeof(vertices);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    desc.memoryCategory = RhiMemoryCategory::Geometry;
    m_vertexBuffer = m_rhiDevice->createBuffer(desc, vertices, sizeof(vertices));
    if (!m_vertexBuffer.isValid()) std::abort();
}

void ScreenTransition::cleanupMesh() {
    if (m_rhiDevice != nullptr && m_vertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_vertexBuffer);
    }
    m_vertexBuffer = {};
}

void ScreenTransition::startFadeOut(float duration) {
    m_alphaTween.start(0.0f, 1.0f, duration, EasingType::Linear);
}

void ScreenTransition::startFadeIn(float duration) {
    m_alphaTween.start(1.0f, 0.0f, duration, EasingType::Linear);
}

void ScreenTransition::tick(float dt) {
    m_alphaTween.tick(dt);
}

void ScreenTransition::render(const int screenW,
                              const int screenH,
                              RhiCommandList& commandList) const {
    if (!m_pipeline.isValid() || !m_vertexBuffer.isValid() || m_alphaTween.isDone()) return;

    float a = m_alphaTween.value();
    if (a <= 0.0f) return;

    struct PushConstants { glm::vec4 screenRect; glm::vec4 rectRadius; glm::vec4 color; };
    const PushConstants pushConstants{
        glm::vec4(static_cast<float>(screenW), static_cast<float>(screenH), 0.0f, 0.0f),
        glm::vec4(static_cast<float>(screenW), static_cast<float>(screenH), 0.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, a)
    };
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setVertexBuffer(0u, m_vertexBuffer, 0u);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(6u, 1u, 0u, 0u);
}
