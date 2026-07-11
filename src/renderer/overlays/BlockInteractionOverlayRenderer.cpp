#include "BlockInteractionOverlayRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockSelection.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../world/redstone/WireFaceGeometry.h"
#include "../core/Shader.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <array>
#include <cstdlib>

#include <glad/glad.h>

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
    m_outlineShader = resourceMgr.getShader("outline");
    m_breakOverlayShader = resourceMgr.getShader("break_overlay");
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
    if (!m_outlineVertexShader.isValid() || !m_outlineFragmentShader.isValid() ||
        !m_outlinePipelineLayout.isValid() || !m_outlinePipeline.isValid()) std::abort();
}

void BlockInteractionOverlayRenderer::shutdown() {
    if (m_rhiDevice != nullptr) {
        if (m_outlinePipeline.isValid()) m_rhiDevice->destroyPipeline(m_outlinePipeline);
        if (m_outlinePipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_outlinePipelineLayout);
        if (m_outlineFragmentShader.isValid()) m_rhiDevice->destroyShader(m_outlineFragmentShader);
        if (m_outlineVertexShader.isValid()) m_rhiDevice->destroyShader(m_outlineVertexShader);
    }
    m_outlinePipeline = {};
    m_outlinePipelineLayout = {};
    m_outlineFragmentShader = {};
    m_outlineVertexShader = {};
    if (m_rhiDevice != nullptr) {
        if (m_breakOverlayCrossVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_breakOverlayCrossVertexBuffer);
        if (m_breakOverlayVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_breakOverlayVertexBuffer);
        if (m_outlineVertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_outlineVertexBuffer);
    }
    m_breakOverlayCrossVertexBuffer = {};
    m_breakOverlayVertexBuffer = {};
    m_outlineVertexBuffer = {};
    m_rhiDevice = nullptr;
    if (m_outlineVbo != 0) {
        glDeleteBuffers(1, &m_outlineVbo);
        m_outlineVbo = 0;
    }
    if (m_outlineVao != 0) {
        glDeleteVertexArrays(1, &m_outlineVao);
        m_outlineVao = 0;
    }
    if (m_breakOverlayVbo != 0) {
        glDeleteBuffers(1, &m_breakOverlayVbo);
        m_breakOverlayVbo = 0;
    }
    if (m_breakOverlayVao != 0) {
        glDeleteVertexArrays(1, &m_breakOverlayVao);
        m_breakOverlayVao = 0;
    }
    if (m_breakOverlayCrossVbo != 0) {
        glDeleteBuffers(1, &m_breakOverlayCrossVbo);
        m_breakOverlayCrossVbo = 0;
    }
    if (m_breakOverlayCrossVao != 0) {
        glDeleteVertexArrays(1, &m_breakOverlayCrossVao);
        m_breakOverlayCrossVao = 0;
    }
}

void BlockInteractionOverlayRenderer::render(const IWorldView& worldView,
                                              const glm::mat4& viewProj,
                                              const BlockTargetRenderData& target,
                                              const BlockBreakRenderData& blockBreak,
                                              RhiCommandList& commandList) {
    renderBlockBreakOverlay(worldView, viewProj, blockBreak);
    renderBlockOutline(worldView, viewProj, target, commandList);
}

void BlockInteractionOverlayRenderer::initOutlineMesh() {
    if (m_outlineVao != 0) {
        return;
    }

    constexpr std::array<float, 72> kOutlineVertices = {
        0,0,0, 1,0,0,  1,0,0, 1,1,0,  1,1,0, 0,1,0,  0,1,0, 0,0,0,
        0,0,1, 1,0,1,  1,0,1, 1,1,1,  1,1,1, 0,1,1,  0,1,1, 0,0,1,
        0,0,0, 0,0,1,  1,0,0, 1,0,1,  1,1,0, 1,1,1,  0,1,0, 0,1,1
    };

    glGenVertexArrays(1, &m_outlineVao);
    glGenBuffers(1, &m_outlineVbo);

    glBindVertexArray(m_outlineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_outlineVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kOutlineVertices.size() * sizeof(float)),
                 kOutlineVertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "BlockOverlay.Outline.VertexBuffer";
    bufferDesc.size = kOutlineVertices.size() * sizeof(float);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    m_outlineVertexBuffer = m_rhiDevice->createBuffer(
        bufferDesc, kOutlineVertices.data(), bufferDesc.size);
    if (!m_outlineVertexBuffer.isValid()) std::abort();
}

void BlockInteractionOverlayRenderer::initBreakOverlayMesh() {
    if (m_breakOverlayVao != 0 && m_breakOverlayCrossVao != 0) {
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

    glGenVertexArrays(1, &m_breakOverlayVao);
    glGenBuffers(1, &m_breakOverlayVbo);

    glBindVertexArray(m_breakOverlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_breakOverlayVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kBreakOverlayVertices.size() * sizeof(float)),
                 kBreakOverlayVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    m_breakOverlayVertexCount = static_cast<GLsizei>(kBreakOverlayVertices.size() / 5);
    RhiBufferDesc overlayBufferDesc;
    overlayBufferDesc.debugName = "BlockOverlay.Break.VertexBuffer";
    overlayBufferDesc.size = kBreakOverlayVertices.size() * sizeof(float);
    overlayBufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    overlayBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    m_breakOverlayVertexBuffer = m_rhiDevice->createBuffer(
        overlayBufferDesc, kBreakOverlayVertices.data(), overlayBufferDesc.size);
    if (!m_breakOverlayVertexBuffer.isValid()) std::abort();

    glGenVertexArrays(1, &m_breakOverlayCrossVao);
    glGenBuffers(1, &m_breakOverlayCrossVbo);

    glBindVertexArray(m_breakOverlayCrossVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_breakOverlayCrossVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kBreakOverlayCrossVertices.size() * sizeof(float)),
                 kBreakOverlayCrossVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    m_breakOverlayCrossVertexCount = static_cast<GLsizei>(kBreakOverlayCrossVertices.size() / 5);
    RhiBufferDesc crossBufferDesc;
    crossBufferDesc.debugName = "BlockOverlay.BreakCross.VertexBuffer";
    crossBufferDesc.size = kBreakOverlayCrossVertices.size() * sizeof(float);
    crossBufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    crossBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    m_breakOverlayCrossVertexBuffer = m_rhiDevice->createBuffer(
        crossBufferDesc, kBreakOverlayCrossVertices.data(), crossBufferDesc.size);
    if (!m_breakOverlayCrossVertexBuffer.isValid()) std::abort();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
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
                                                               const BlockBreakRenderData& blockBreak) {
    if (m_breakOverlayShader == nullptr || m_breakOverlayVao == 0 || !blockBreak.active) {
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

    m_breakOverlayShader->use();
    m_breakOverlayShader->setMat4("viewProj", viewProj);
    m_breakOverlayShader->setMat4("model", model);
    m_breakOverlayShader->setFloat("breakProgress", breakProgress);
    m_breakOverlayShader->setVec3("blockWorldPos", glm::vec3(target));
    m_breakOverlayShader->setInt("uUseMeshUV", useCrossOverlay ? 1 : 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glBindVertexArray(useCrossOverlay ? m_breakOverlayCrossVao : m_breakOverlayVao);
    glDrawArrays(GL_TRIANGLES, 0, useCrossOverlay ? m_breakOverlayCrossVertexCount : m_breakOverlayVertexCount);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
