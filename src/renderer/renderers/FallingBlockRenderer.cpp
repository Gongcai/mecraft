#include "FallingBlockRenderer.h"

#include <algorithm>
#include <optional>
#include <unordered_set>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/camera/Camera.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/GameResources.h"

namespace {

struct FallingBlockPushConstants {
    glm::mat4 modelViewProj;
    glm::mat4 previousModelViewProj;
    glm::mat4 model;
    glm::vec2 light;
    float animationTime;
    uint32_t objectId;
};

struct FallingBlockShadowPushConstants {
    glm::mat4 viewProj;
    glm::mat4 model;
    glm::vec4 animationTime;
};

} // namespace

bool FallingBlockRenderer::init(GameResources& resources, RhiDevice& rhiDevice) {
    m_resources = &resources;
    m_rhiDevice = &rhiDevice;
    return createGBufferRhiResources();
}

void FallingBlockRenderer::shutdown() {
    destroyGBufferRhiResources();
    for (auto& pair : m_meshes) {
        renderer::destroyBlockCubeMesh(pair.second);
    }
    m_meshes.clear();
    m_previousModelMatrices.clear();
    m_currentModelMatrices.clear();
    m_objectIds.clear();
    m_renderInstances.clear();
    m_resources = nullptr;
}

bool FallingBlockRenderer::prepareFrame(const renderer::contracts::GameplayRenderSnapshot& snapshot) {
    m_renderInstances.clear();
    m_currentModelMatrices.clear();
    std::unordered_set<renderer::contracts::GameplayRenderObjectKey> currentEntityIds;
    for (const renderer::contracts::FallingBlockRenderData& block : snapshot.fallingBlocks) {
        currentEntityIds.insert(block.objectKey);
        auto objectId = m_objectIds.find(block.objectKey);
        if (objectId == m_objectIds.end()) {
            const std::optional<renderer::contracts::StableObjectId> allocated =
                renderer::contracts::allocateStableSceneId<renderer::contracts::StableObjectIdTag>();
            if (!allocated.has_value()) {
                return false;
            }
            objectId = m_objectIds.emplace(block.objectKey, *allocated).first;
        }
        glm::mat4 model(1.0f);
        model = glm::translate(model, block.position);
        model = glm::translate(model, glm::vec3(-0.5f));
        const auto previous = m_previousModelMatrices.find(block.objectKey);
        m_renderInstances.push_back({block.stateId, model,
                                     previous != m_previousModelMatrices.end() ? previous->second : model, block.light,
                                     objectId->second});
        m_currentModelMatrices[block.objectKey] = model;
    }

    for (auto it = m_objectIds.begin(); it != m_objectIds.end();) {
        if (currentEntityIds.find(it->first) == currentEntityIds.end()) {
            it = m_objectIds.erase(it);
        } else {
            ++it;
        }
    }
    m_previousModelMatrices = m_currentModelMatrices;
    return true;
}

const renderer::BlockCubeMesh* FallingBlockRenderer::getOrCreateMesh(BlockStateId stateId) {
    const auto it = m_meshes.find(stateId);
    if (it != m_meshes.end()) {
        return &it->second;
    }
    auto inserted = m_meshes.emplace(stateId, renderer::buildBlockStateCubeMesh(stateId, *m_resources, *m_rhiDevice));
    return &inserted.first->second;
}

void FallingBlockRenderer::renderToGBuffer(RhiCommandList& commandList, const glm::mat4& jitteredViewProj,
                                           const glm::mat4& previousViewProj, float animationTime) {
    if (!m_gbufferPipeline.isValid() || !m_gbufferBindGroup.isValid()) {
        return;
    }
    commandList.setGraphicsPipeline(m_gbufferPipeline);
    commandList.setBindGroup(0u, m_gbufferBindGroup);

    for (const RenderInstance& instance : m_renderInstances) {
        const renderer::BlockCubeMesh* mesh = getOrCreateMesh(instance.stateId);
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }
        const FallingBlockPushConstants pushConstants{jitteredViewProj * instance.model,
                                                      previousViewProj * instance.previousModel,
                                                      instance.model,
                                                      instance.light,
                                                      animationTime,
                                                      instance.objectId.value};
        commandList.setVertexBuffer(0u, mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(mesh->vertexCount, 1u, 0u, 0u);
    }
}

void FallingBlockRenderer::renderToShadowMap(RhiCommandList& commandList, const glm::mat4& shadowViewProj,
                                             float animationTime) {
    if (!m_shadowPipeline.isValid() || !m_shadowBindGroup.isValid()) {
        return;
    }
    commandList.setGraphicsPipeline(m_shadowPipeline);
    commandList.setBindGroup(0u, m_shadowBindGroup);

    for (const RenderInstance& instance : m_renderInstances) {
        const renderer::BlockCubeMesh* mesh = getOrCreateMesh(instance.stateId);
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }
        const FallingBlockShadowPushConstants pushConstants{shadowViewProj, instance.model,
                                                            glm::vec4(animationTime, 0.0f, 0.0f, 0.0f)};
        commandList.setVertexBuffer(0u, mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(mesh->vertexCount, 1u, 0u, 0u);
    }
}

void FallingBlockRenderer::renderForward(RhiCommandList& commandList, const glm::mat4& viewProj,
                                         const float skyIntensity, const float animationTime) {
    if (!m_forwardPipeline.isValid() || !m_gbufferBindGroup.isValid()) {
        return;
    }
    struct ForwardPushConstants {
        glm::mat4 viewProj;
        glm::mat4 model;
        glm::vec4 lightingAnimation;
    };

    commandList.setGraphicsPipeline(m_forwardPipeline);
    commandList.setBindGroup(0u, m_gbufferBindGroup);
    for (const RenderInstance& instance : m_renderInstances) {
        const renderer::BlockCubeMesh* mesh = getOrCreateMesh(instance.stateId);
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }
        const ForwardPushConstants pushConstants{viewProj, instance.model,
                                                 glm::vec4(instance.light, skyIntensity, animationTime)};
        commandList.setVertexBuffer(0u, mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(mesh->vertexCount, 1u, 0u, 0u);
    }
}

bool FallingBlockRenderer::createGBufferRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/falling_block_gbuffer_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/falling_block_gbuffer_rhi.frag");
    const auto shadowVertexSource = renderer::rhi::loadShaderSource("assets/shaders/falling_block_shadow_rhi.vert");
    const auto shadowFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/falling_block_shadow_rhi.frag");
    const auto forwardVertexSource = renderer::rhi::loadShaderSource("assets/shaders/block_drop_forward_rhi.vert");
    const auto forwardFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/block_drop_forward_rhi.frag");
    if (!vertexSource || !fragmentSource || !shadowVertexSource || !shadowFragmentSource || !forwardVertexSource ||
        !forwardFragmentSource)
        return false;
    const RhiTextureHandle textures[] = {m_resources->blockTextures.textureArray().texture,
                                         m_resources->environmentTextures.getGrassColormap(),
                                         m_resources->environmentTextures.getFoliageColormap()};
    RhiTextureViewHandle* views[] = {&m_textureArrayView, &m_grassColormapView, &m_foliageColormapView};
    for (uint32_t i = 0; i < 3u; ++i) {
        RhiTextureViewDesc desc;
        desc.texture = textures[i];
        desc.viewType = i == 0u ? RhiTextureViewType::Texture2DArray : RhiTextureViewType::Texture2D;
        if (desc.viewType == RhiTextureViewType::Texture2DArray) {
            desc.mipCount = kRhiRemainingMipLevels;
            desc.layerCount = kRhiRemainingArrayLayers;
        }
        *views[i] = m_rhiDevice->createTextureView(desc);
    }
    RhiSamplerDesc samplerDesc;
    samplerDesc.addressU = RhiAddressMode::Repeat;
    samplerDesc.addressV = RhiAddressMode::Repeat;
    m_sampler = m_rhiDevice->createSampler(samplerDesc);
    auto createShader = [&](const char* name, RhiShaderStage stage, const std::string& source) {
        RhiShaderDesc desc;
        desc.debugName = name;
        desc.stage = stage;
        desc.source = source.c_str();
        desc.sourceSize = source.size();
        return m_rhiDevice->createShader(desc);
    };
    m_gbufferVertexShader = createShader("FallingBlock.GBuffer.Vertex", RhiShaderStage::Vertex, *vertexSource);
    m_gbufferFragmentShader = createShader("FallingBlock.GBuffer.Fragment", RhiShaderStage::Fragment, *fragmentSource);
    m_shadowVertexShader = createShader("FallingBlock.Shadow.Vertex", RhiShaderStage::Vertex, *shadowVertexSource);
    m_shadowFragmentShader =
        createShader("FallingBlock.Shadow.Fragment", RhiShaderStage::Fragment, *shadowFragmentSource);
    m_forwardVertexShader = createShader("FallingBlock.Forward.Vertex", RhiShaderStage::Vertex, *forwardVertexSource);
    m_forwardFragmentShader =
        createShader("FallingBlock.Forward.Fragment", RhiShaderStage::Fragment, *forwardFragmentSource);
    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "FallingBlock.GBuffer.BindGroupLayout";
    for (uint32_t i = 0; i < 3u; ++i)
        layoutDesc.entries.push_back(
            {i, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_gbufferBindGroupLayout = m_rhiDevice->createBindGroupLayout(layoutDesc);
    RhiBindGroupLayoutDesc shadowLayoutDesc;
    shadowLayoutDesc.debugName = "FallingBlock.Shadow.BindGroupLayout";
    shadowLayoutDesc.entries.push_back(
        {0u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_shadowBindGroupLayout = m_rhiDevice->createBindGroupLayout(shadowLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FallingBlock.GBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_gbufferBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(FallingBlockPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_gbufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "FallingBlock.Shadow.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_shadowBindGroupLayout;
    pipelineLayoutDesc.pushConstantBytes = sizeof(FallingBlockShadowPushConstants);
    m_shadowPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "FallingBlock.Forward.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_gbufferBindGroupLayout;
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    m_forwardPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FallingBlock.GBuffer.Pipeline";
    pipelineDesc.vertexShader = m_gbufferVertexShader;
    pipelineDesc.fragmentShader = m_gbufferFragmentShader;
    pipelineDesc.layout = m_gbufferPipelineLayout;
    renderer::setBlockVertexInputLayout(pipelineDesc);
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgb10A2Unorm,
                                 RhiTextureFormat::Rg8Unorm,   RhiTextureFormat::Rgba8Unorm,
                                 RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm,
                                 RhiTextureFormat::Rg32Uint,   RhiTextureFormat::Rg16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(8u);
    m_gbufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "FallingBlock.Shadow.Pipeline";
    pipelineDesc.vertexShader = m_shadowVertexShader;
    pipelineDesc.fragmentShader = m_shadowFragmentShader;
    pipelineDesc.layout = m_shadowPipelineLayout;
    pipelineDesc.colorFormats.clear();
    pipelineDesc.blend.attachments.clear();
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    m_shadowPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "FallingBlock.Forward.Pipeline";
    pipelineDesc.vertexShader = m_forwardVertexShader;
    pipelineDesc.fragmentShader = m_forwardFragmentShader;
    pipelineDesc.layout = m_forwardPipelineLayout;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.blend.attachments.resize(1u);
    pipelineDesc.vertexInput = {};
    renderer::setBlockVertexInputLayout(pipelineDesc);
    pipelineDesc.vertexInput.attributes.erase(
        std::remove_if(
            pipelineDesc.vertexInput.attributes.begin(), pipelineDesc.vertexInput.attributes.end(),
            [](const RhiVertexAttribute& attribute) { return attribute.location == 3u || attribute.location == 4u; }),
        pipelineDesc.vertexInput.attributes.end());
    m_forwardPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_gbufferBindGroupLayout;
    const RhiTextureViewHandle textureViews[] = {m_textureArrayView, m_grassColormapView, m_foliageColormapView};
    for (uint32_t i = 0; i < 3u; ++i) {
        RhiBindGroupEntry entry;
        entry.binding = i;
        entry.resource.combinedTextureSampler = {textureViews[i], m_sampler};
        bindGroupDesc.entries.push_back(entry);
    }
    m_gbufferBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    RhiBindGroupDesc shadowBindGroupDesc;
    shadowBindGroupDesc.layout = m_shadowBindGroupLayout;
    RhiBindGroupEntry shadowTextureEntry;
    shadowTextureEntry.binding = 0u;
    shadowTextureEntry.resource.combinedTextureSampler = {m_textureArrayView, m_sampler};
    shadowBindGroupDesc.entries.push_back(shadowTextureEntry);
    m_shadowBindGroup = m_rhiDevice->createBindGroup(shadowBindGroupDesc);
    if (!m_textureArrayView.isValid() || !m_grassColormapView.isValid() || !m_foliageColormapView.isValid() ||
        !m_sampler.isValid() || !m_gbufferVertexShader.isValid() || !m_gbufferFragmentShader.isValid() ||
        !m_gbufferBindGroupLayout.isValid() || !m_gbufferPipelineLayout.isValid() || !m_gbufferPipeline.isValid() ||
        !m_gbufferBindGroup.isValid() || !m_shadowVertexShader.isValid() || !m_shadowFragmentShader.isValid() ||
        !m_shadowBindGroupLayout.isValid() || !m_shadowPipelineLayout.isValid() || !m_shadowPipeline.isValid() ||
        !m_shadowBindGroup.isValid() || !m_forwardVertexShader.isValid() || !m_forwardFragmentShader.isValid() ||
        !m_forwardPipelineLayout.isValid() || !m_forwardPipeline.isValid()) {
        destroyGBufferRhiResources();
        return false;
    }
    return true;
}

void FallingBlockRenderer::destroyGBufferRhiResources() {
    if (m_rhiDevice) {
        if (m_forwardPipeline.isValid())
            m_rhiDevice->destroyPipeline(m_forwardPipeline);
        if (m_forwardPipelineLayout.isValid())
            m_rhiDevice->destroyPipelineLayout(m_forwardPipelineLayout);
        if (m_forwardFragmentShader.isValid())
            m_rhiDevice->destroyShader(m_forwardFragmentShader);
        if (m_forwardVertexShader.isValid())
            m_rhiDevice->destroyShader(m_forwardVertexShader);
        if (m_shadowBindGroup.isValid())
            m_rhiDevice->destroyBindGroup(m_shadowBindGroup);
        if (m_shadowPipeline.isValid())
            m_rhiDevice->destroyPipeline(m_shadowPipeline);
        if (m_shadowPipelineLayout.isValid())
            m_rhiDevice->destroyPipelineLayout(m_shadowPipelineLayout);
        if (m_shadowBindGroupLayout.isValid())
            m_rhiDevice->destroyBindGroupLayout(m_shadowBindGroupLayout);
        if (m_shadowFragmentShader.isValid())
            m_rhiDevice->destroyShader(m_shadowFragmentShader);
        if (m_shadowVertexShader.isValid())
            m_rhiDevice->destroyShader(m_shadowVertexShader);
        if (m_gbufferBindGroup.isValid())
            m_rhiDevice->destroyBindGroup(m_gbufferBindGroup);
        if (m_gbufferPipeline.isValid())
            m_rhiDevice->destroyPipeline(m_gbufferPipeline);
        if (m_gbufferPipelineLayout.isValid())
            m_rhiDevice->destroyPipelineLayout(m_gbufferPipelineLayout);
        if (m_gbufferBindGroupLayout.isValid())
            m_rhiDevice->destroyBindGroupLayout(m_gbufferBindGroupLayout);
        if (m_gbufferFragmentShader.isValid())
            m_rhiDevice->destroyShader(m_gbufferFragmentShader);
        if (m_gbufferVertexShader.isValid())
            m_rhiDevice->destroyShader(m_gbufferVertexShader);
        if (m_sampler.isValid())
            m_rhiDevice->destroySampler(m_sampler);
        if (m_foliageColormapView.isValid())
            m_rhiDevice->destroyTextureView(m_foliageColormapView);
        if (m_grassColormapView.isValid())
            m_rhiDevice->destroyTextureView(m_grassColormapView);
        if (m_textureArrayView.isValid())
            m_rhiDevice->destroyTextureView(m_textureArrayView);
    }
    m_forwardPipeline = {};
    m_forwardPipelineLayout = {};
    m_forwardFragmentShader = {};
    m_forwardVertexShader = {};
    m_shadowBindGroup = {};
    m_shadowPipeline = {};
    m_shadowPipelineLayout = {};
    m_shadowBindGroupLayout = {};
    m_shadowFragmentShader = {};
    m_shadowVertexShader = {};
    m_gbufferBindGroup = {};
    m_gbufferPipeline = {};
    m_gbufferPipelineLayout = {};
    m_gbufferBindGroupLayout = {};
    m_gbufferFragmentShader = {};
    m_gbufferVertexShader = {};
    m_sampler = {};
    m_foliageColormapView = {};
    m_grassColormapView = {};
    m_textureArrayView = {};
    m_rhiDevice = nullptr;
}
