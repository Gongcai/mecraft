#include "BlockInteractionOverlayRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockSelection.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../world/redstone/WireFaceGeometry.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <array>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>

namespace {

bool isAxisNormal(const glm::ivec3& normal) {
    const int components =
        (normal.x == 0 ? 0 : 1) +
        (normal.y == 0 ? 0 : 1) +
        (normal.z == 0 ? 0 : 1);
    return components == 1 &&
           normal.x >= -1 && normal.x <= 1 &&
           normal.y >= -1 && normal.y <= 1 &&
           normal.z >= -1 && normal.z <= 1;
}

BlockSelectionBox selectionBoxForTarget(const BlockStateId targetState,
                                        const glm::ivec3& hitNormal) {
    const BlockID targetId = BlockStateRegistry::getBlockId(targetState);
    const BlockDef& targetDef = BlockRegistry::getFast(targetId);
    if (targetDef.isWireContainer && isAxisNormal(hitNormal)) {
        const uint16_t facing = WireFaceGeometry::facingFromSurfaceNormal(hitNormal);
        return BlockSelection::getFacePlaneBoxForFacing(facing);
    }
    return BlockSelection::getBox(targetState);
}

} // namespace

void BlockInteractionOverlayRenderer::init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice) {
    m_rhiDevice = &rhiDevice;
    static_cast<void>(resourceMgr);
    initOutlineMesh();
    initBreakOverlayMesh();
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/block_outline_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/block_outline_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "BlockOverlay.Outline.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_outlineVertexShader = rhiDevice.createShader(shaderDesc);
    shaderDesc.debugName = "BlockOverlay.Outline.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_outlineFragmentShader = rhiDevice.createShader(shaderDesc);
    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "BlockOverlay.Outline.PipelineLayout";
    layoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                    rhiFlag(RhiShaderStage::Fragment);
    m_outlinePipelineLayout = rhiDevice.createPipelineLayout(layoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "BlockOverlay.Outline.Pipeline";
    pipelineDesc.vertexShader = m_outlineVertexShader;
    pipelineDesc.fragmentShader = m_outlineFragmentShader;
    pipelineDesc.layout = m_outlinePipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(float) * 3u, RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, 0u}};
    pipelineDesc.topology = RhiPrimitiveTopology::LineList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(1u);
    m_outlinePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    const auto breakVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/block_break_rhi.vert");
    const auto breakFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/block_break_rhi.frag");
    if (!breakVertexSource || !breakFragmentSource) std::abort();
    RhiShaderDesc breakShaderDesc;
    breakShaderDesc.debugName = "BlockOverlay.Break.Vertex";
    breakShaderDesc.stage = RhiShaderStage::Vertex;
    breakShaderDesc.source = breakVertexSource->c_str();
    breakShaderDesc.sourceSize = breakVertexSource->size();
    m_breakVertexShader = rhiDevice.createShader(breakShaderDesc);
    breakShaderDesc.debugName = "BlockOverlay.Break.Fragment";
    breakShaderDesc.stage = RhiShaderStage::Fragment;
    breakShaderDesc.source = breakFragmentSource->c_str();
    breakShaderDesc.sourceSize = breakFragmentSource->size();
    m_breakFragmentShader = rhiDevice.createShader(breakShaderDesc);
    RhiPipelineLayoutDesc breakLayoutDesc;
    breakLayoutDesc.debugName = "BlockOverlay.Break.PipelineLayout";
    breakLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4) + sizeof(glm::ivec4);
    breakLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                         rhiFlag(RhiShaderStage::Fragment);
    m_breakPipelineLayout = rhiDevice.createPipelineLayout(breakLayoutDesc);
    RhiGraphicsPipelineDesc breakPipelineDesc;
    breakPipelineDesc.debugName = "BlockOverlay.Break.Pipeline";
    breakPipelineDesc.vertexShader = m_breakVertexShader;
    breakPipelineDesc.fragmentShader = m_breakFragmentShader;
    breakPipelineDesc.layout = m_breakPipelineLayout;
    breakPipelineDesc.vertexInput.bindings = {{0u, sizeof(float) * 5u, RhiVertexInputRate::Vertex}};
    breakPipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, 0u},
        {1u, 0u, RhiVertexFormat::Float2, sizeof(float) * 3u}
    };
    breakPipelineDesc.raster.cullMode = RhiCullMode::None;
    breakPipelineDesc.depthStencil.depthTestEnabled = true;
    breakPipelineDesc.depthStencil.depthWriteEnabled = false;
    breakPipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    breakPipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    RhiBlendAttachmentState breakBlend;
    breakBlend.blendEnabled = true;
    breakBlend.srcColor = RhiBlendFactor::SrcAlpha;
    breakBlend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    breakBlend.srcAlpha = RhiBlendFactor::One;
    breakBlend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    breakPipelineDesc.blend.attachments.push_back(breakBlend);
    m_breakPipeline = rhiDevice.createGraphicsPipeline(breakPipelineDesc);
    if (!m_outlineVertexShader.isValid() || !m_outlineFragmentShader.isValid() ||
        !m_outlinePipelineLayout.isValid() || !m_outlinePipeline.isValid() ||
        !m_breakVertexShader.isValid() || !m_breakFragmentShader.isValid() ||
        !m_breakPipelineLayout.isValid() || !m_breakPipeline.isValid()) std::abort();
}

void BlockInteractionOverlayRenderer::shutdown() {
    if (m_rhiDevice != nullptr) {
        if (m_outlinePipeline.isValid()) m_rhiDevice->destroyPipeline(m_outlinePipeline);
        if (m_outlinePipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_outlinePipelineLayout);
        if (m_outlineFragmentShader.isValid()) m_rhiDevice->destroyShader(m_outlineFragmentShader);
        if (m_outlineVertexShader.isValid()) m_rhiDevice->destroyShader(m_outlineVertexShader);
        if (m_breakPipeline.isValid()) m_rhiDevice->destroyPipeline(m_breakPipeline);
        if (m_breakPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_breakPipelineLayout);
        if (m_breakFragmentShader.isValid()) m_rhiDevice->destroyShader(m_breakFragmentShader);
        if (m_breakVertexShader.isValid()) m_rhiDevice->destroyShader(m_breakVertexShader);
    }
    m_outlinePipeline = {};
    m_outlinePipelineLayout = {};
    m_outlineFragmentShader = {};
    m_outlineVertexShader = {};
    m_breakPipeline = {};
    m_breakPipelineLayout = {};
    m_breakFragmentShader = {};
    m_breakVertexShader = {};
    if (m_rhiDevice != nullptr) {
        if (m_breakOverlayCrossVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_breakOverlayCrossVertexBuffer);
        if (m_breakOverlayVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_breakOverlayVertexBuffer);
        if (m_outlineVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_outlineVertexBuffer);
    }
    m_breakOverlayCrossVertexBuffer = {};
    m_breakOverlayVertexBuffer = {};
    m_outlineVertexBuffer = {};
    m_rhiDevice = nullptr;
}

void BlockInteractionOverlayRenderer::render(const IWorldView& worldView,
                                              const glm::mat4& viewProj,
                                              const BlockTargetRenderData& target,
                                              const BlockBreakRenderData& blockBreak,
                                              RhiCommandList& commandList) {
    renderBlockBreakOverlay(worldView, viewProj, blockBreak, commandList);
    renderBlockOutline(worldView, viewProj, target, commandList);
}

void BlockInteractionOverlayRenderer::initOutlineMesh() {
    if (m_outlineVertexBuffer.isValid()) {
        return;
    }

    constexpr std::array<float, 72> kOutlineVertices = {
        0,0,0, 1,0,0,  1,0,0, 1,1,0,  1,1,0, 0,1,0,  0,1,0, 0,0,0,
        0,0,1, 1,0,1,  1,0,1, 1,1,1,  1,1,1, 0,1,1,  0,1,1, 0,0,1,
        0,0,0, 0,0,1,  1,0,0, 1,0,1,  1,1,0, 1,1,1,  0,1,0, 0,1,1
    };

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "BlockOverlay.Outline.VertexBuffer";
    bufferDesc.size = kOutlineVertices.size() * sizeof(float);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    m_outlineVertexBuffer = m_rhiDevice->createBuffer(
        bufferDesc, kOutlineVertices.data(), bufferDesc.size);
    if (!m_outlineVertexBuffer.isValid()) std::abort();
}

void BlockInteractionOverlayRenderer::initBreakOverlayMesh() {
    if (m_breakOverlayVertexBuffer.isValid() && m_breakOverlayCrossVertexBuffer.isValid()) {
        return;
    }

    // position.xyz + uv.xy
    constexpr std::array<float, 180> kBreakOverlayVertices = {
        // front
        0,0,1, 0,0,  1,0,1, 1,0,  1,1,1, 1,1,
        0,0,1, 0,0,  1,1,1, 1,1,  0,1,1, 0,1,
        // back
        1,0,0, 0,0,  0,0,0, 1,0,  0,1,0, 1,1,
        1,0,0, 0,0,  0,1,0, 1,1,  1,1,0, 0,1,
        // left
        0,0,0, 0,0,  0,0,1, 1,0,  0,1,1, 1,1,
        0,0,0, 0,0,  0,1,1, 1,1,  0,1,0, 0,1,
        // right
        1,0,1, 0,0,  1,0,0, 1,0,  1,1,0, 1,1,
        1,0,1, 0,0,  1,1,0, 1,1,  1,1,1, 0,1,
        // top
        0,1,1, 0,0,  1,1,1, 1,0,  1,1,0, 1,1,
        0,1,1, 0,0,  1,1,0, 1,1,  0,1,0, 0,1,
        // bottom
        0,0,0, 0,0,  1,0,0, 1,0,  1,0,1, 1,1,
        0,0,0, 0,0,  1,0,1, 1,1,  0,0,1, 0,1
    };

    constexpr std::array<float, 60> kBreakOverlayCrossVertices = {
        // quad A
        0.1464f,0.0f,0.1464f, 0,0,  0.8536f,0.0f,0.8536f, 1,0,  0.8536f,1.0f,0.8536f, 1,1,
        0.1464f,0.0f,0.1464f, 0,0,  0.8536f,1.0f,0.8536f, 1,1,  0.1464f,1.0f,0.1464f, 0,1,
        // quad B
        0.8536f,0.0f,0.1464f, 0,0,  0.1464f,0.0f,0.8536f, 1,0,  0.1464f,1.0f,0.8536f, 1,1,
        0.8536f,0.0f,0.1464f, 0,0,  0.1464f,1.0f,0.8536f, 1,1,  0.8536f,1.0f,0.1464f, 0,1
    };

    m_breakOverlayVertexCount = static_cast<int32_t>(kBreakOverlayVertices.size() / 5);
    RhiBufferDesc overlayBufferDesc;
    overlayBufferDesc.debugName = "BlockOverlay.Break.VertexBuffer";
    overlayBufferDesc.size = kBreakOverlayVertices.size() * sizeof(float);
    overlayBufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    overlayBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    overlayBufferDesc.initialState = RhiResourceState::VertexBuffer;
    m_breakOverlayVertexBuffer = m_rhiDevice->createBuffer(
        overlayBufferDesc, kBreakOverlayVertices.data(), overlayBufferDesc.size);
    if (!m_breakOverlayVertexBuffer.isValid()) std::abort();

    m_breakOverlayCrossVertexCount = static_cast<int32_t>(kBreakOverlayCrossVertices.size() / 5);
    RhiBufferDesc crossBufferDesc;
    crossBufferDesc.debugName = "BlockOverlay.BreakCross.VertexBuffer";
    crossBufferDesc.size = kBreakOverlayCrossVertices.size() * sizeof(float);
    crossBufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                            rhiFlag(RhiBufferUsage::TransferDst);
    crossBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    crossBufferDesc.initialState = RhiResourceState::VertexBuffer;
    m_breakOverlayCrossVertexBuffer = m_rhiDevice->createBuffer(
        crossBufferDesc, kBreakOverlayCrossVertices.data(), crossBufferDesc.size);
    if (!m_breakOverlayCrossVertexBuffer.isValid()) std::abort();

}

void BlockInteractionOverlayRenderer::renderBlockOutline(const IWorldView& worldView,
                                                         const glm::mat4& viewProj,
                                                         const BlockTargetRenderData& target,
                                                         RhiCommandList& commandList) {
    if (!m_outlineVertexBuffer.isValid() || !target.hasTarget) {
        return;
    }

    const glm::ivec3 targetBlock = target.targetBlock;
    const BlockStateId targetState = worldView.getBlockState(targetBlock.x, targetBlock.y, targetBlock.z);
    const BlockSelectionBox selectionBox = selectionBoxForTarget(targetState, target.hitNormal);
    const glm::vec3 boxCenter = (selectionBox.min + selectionBox.max) * 0.5f;
    const glm::vec3 boxSize = selectionBox.max - selectionBox.min;

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(targetBlock) + boxCenter);
    model = glm::scale(model, boxSize + glm::vec3(0.002f));
    model = glm::translate(model, glm::vec3(-0.5f));

    struct PushConstants { glm::mat4 viewProj; glm::mat4 model; glm::vec4 color; };
    const PushConstants constants{viewProj, model, glm::vec4(0.05f, 0.05f, 0.05f, 1.0f)};
    commandList.setGraphicsPipeline(m_outlinePipeline);
    commandList.setVertexBuffer(0u, m_outlineVertexBuffer, 0u);
    commandList.pushConstants(&constants, sizeof(constants),
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(24u, 1u, 0u, 0u);
}

void BlockInteractionOverlayRenderer::renderBlockBreakOverlay(const IWorldView& worldView,
                                                               const glm::mat4& viewProj,
                                                               const BlockBreakRenderData& blockBreak,
                                                               RhiCommandList& commandList) {
    if (!m_breakOverlayVertexBuffer.isValid() || !m_breakOverlayCrossVertexBuffer.isValid() ||
        !blockBreak.active) {
        return;
    }

    const float breakProgress = blockBreak.progress01;
    if (breakProgress <= 0.0f) {
        return;
    }

    const glm::ivec3 target = blockBreak.blockPos;
    const BlockStateId targetState = worldView.getBlockState(target.x, target.y, target.z);
    const BlockID targetId = BlockStateRegistry::getBlockId(targetState);
    const BlockDef& targetDef = BlockRegistry::get(targetId);
    const bool useCrossOverlay = (targetDef.renderShape == BlockRenderShape::Cross);
    const BlockSelectionBox selectionBox = selectionBoxForTarget(targetState, blockBreak.hitNormal);
    const glm::vec3 boxCenter = (selectionBox.min + selectionBox.max) * 0.5f;
    const glm::vec3 boxSize = selectionBox.max - selectionBox.min;

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(target) + boxCenter);
    model = glm::scale(model, boxSize + glm::vec3(0.001f));
    model = glm::translate(model, glm::vec3(-0.5f));

    struct PushConstants {
        glm::mat4 viewProj;
        glm::mat4 model;
        glm::vec4 blockPosProgress;
        glm::ivec4 flags;
    };
    const PushConstants constants{
        viewProj, model, glm::vec4(glm::vec3(target), breakProgress),
        glm::ivec4(useCrossOverlay ? 1 : 0, 0, 0, 0)
    };
    commandList.setGraphicsPipeline(m_breakPipeline);
    commandList.setVertexBuffer(0u, useCrossOverlay ? m_breakOverlayCrossVertexBuffer
                                                     : m_breakOverlayVertexBuffer, 0u);
    commandList.pushConstants(&constants, sizeof(constants),
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(static_cast<uint32_t>(useCrossOverlay ? m_breakOverlayCrossVertexCount
                                                           : m_breakOverlayVertexCount),
                     1u, 0u, 0u);
}
