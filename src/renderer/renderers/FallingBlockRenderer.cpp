#include "FallingBlockRenderer.h"

#include <cmath>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/camera/Camera.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/components/Components.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"

namespace {

struct FallingBlockPushConstants {
    glm::mat4 viewProj;
    glm::mat4 previousViewProj;
    glm::mat4 model;
    glm::mat4 previousModel;
    glm::vec4 lightAnimation;
};

/// Query world light (sunlight, blockLight) at a position. Falls back to
/// (1.0, 0.0) when the chunk is not loaded. Mirrors DropRenderer::queryWorldLight.
glm::vec2 queryWorldLight(const IWorldView& worldView, const glm::vec3& position) {
    const int bx = static_cast<int>(std::floor(position.x));
    const int by = static_cast<int>(std::floor(position.y));
    const int bz = static_cast<int>(std::floor(position.z));

    if (!worldView.isChunkLoadedForBlock(bx, by, bz)) {
        return {1.0f, 0.0f};
    }

    const glm::ivec2 cc = worldView.getChunkCoords(bx, bz);
    const auto& chunks = worldView.getActiveChunks();
    const auto it = chunks.find(IWorldView::chunkKey(cc.x, cc.y));
    if (it == chunks.end()) {
        return {1.0f, 0.0f};
    }

    const glm::ivec3 local = Chunk::worldToLocal(bx, by, bz);
    const uint8_t sun = it->second->getSunlight(local.x, local.y, local.z);
    const uint8_t block = it->second->getBlockLight(local.x, local.y, local.z);
    return {sun / 15.0f, block / 15.0f};
}

} // namespace

bool FallingBlockRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_rhiDevice = &resourceMgr.rhiDevice();
    m_shadowShader = resourceMgr.getShader("shadow_depth");
    return createGBufferRhiResources();
}

void FallingBlockRenderer::shutdown() {
    destroyGBufferRhiResources();
    for (auto& pair : m_meshes) {
        renderer::destroyBlockCubeMesh(pair.second);
    }
    m_meshes.clear();
    m_previousModelMatrices.clear();
    m_renderInstances.clear();
    m_resourceMgr = nullptr;
    m_shadowShader = nullptr;
}

void FallingBlockRenderer::prepareFrame(const IWorldView& worldView,
                                        const ecs::GameplayRegistry& registry) {
    m_renderInstances.clear();
    auto& reg = registry.registry();
    auto appendInstance = [&](const BlockStateId stateId,
                              const glm::vec3& position,
                              const std::size_t entityId) {
        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        model = glm::translate(model, glm::vec3(-0.5f));
        const auto previous = m_previousModelMatrices.find(entityId);
        m_renderInstances.push_back({
            stateId,
            model,
            previous != m_previousModelMatrices.end() ? previous->second : model,
            queryWorldLight(worldView, position)
        });
        m_previousModelMatrices[entityId] = model;
    };

    auto fallingView = reg.view<ecs::FallingBlockTag,
                                ecs::FallingBlockComponent,
                                ecs::TransformComponent,
                                ecs::DropEntityIdComponent>();
    for (const entt::entity entity : fallingView) {
        const auto& block = fallingView.get<ecs::FallingBlockComponent>(entity);
        const auto& transform = fallingView.get<ecs::TransformComponent>(entity);
        const auto& id = fallingView.get<ecs::DropEntityIdComponent>(entity);
        appendInstance(BlockStateRegistry::getDefaultState(block.blockId), transform.position, id.dropId);
    }

    auto movingView = reg.view<ecs::MovingBlockTag,
                               ecs::MovingBlockComponent,
                               ecs::TransformComponent,
                               ecs::DropEntityIdComponent>();
    for (const entt::entity entity : movingView) {
        const auto& block = movingView.get<ecs::MovingBlockComponent>(entity);
        const auto& transform = movingView.get<ecs::TransformComponent>(entity);
        const auto& id = movingView.get<ecs::DropEntityIdComponent>(entity);
        appendInstance(block.stateId, transform.position, id.dropId);
    }
}

const renderer::BlockCubeMesh* FallingBlockRenderer::getOrCreateMesh(BlockStateId stateId) {
    const auto it = m_meshes.find(stateId);
    if (it != m_meshes.end()) {
        return &it->second;
    }
    auto inserted = m_meshes.emplace(stateId, renderer::buildBlockStateCubeMesh(stateId, *m_resourceMgr));
    return &inserted.first->second;
}

void FallingBlockRenderer::renderToGBuffer(RhiCommandList& commandList,
                                           const glm::mat4& jitteredViewProj,
                                           const glm::mat4& previousViewProj,
                                           float animationTime) {
    if (!m_gbufferPipeline.isValid() || !m_gbufferBindGroup.isValid()) {
        return;
    }
    commandList.setGraphicsPipeline(m_gbufferPipeline);
    commandList.setBindGroup(0u, m_gbufferBindGroup);

    for (const RenderInstance& instance : m_renderInstances) {
        const renderer::BlockCubeMesh* mesh = getOrCreateMesh(instance.stateId);
        if (mesh == nullptr || !mesh->rhiValid()) {
            continue;
        }
        const FallingBlockPushConstants pushConstants{
            jitteredViewProj, previousViewProj, instance.model, instance.previousModel,
            glm::vec4(instance.light, animationTime, 0.0f)
        };
        commandList.setVertexBuffer(0u, mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(mesh->vertexCount, 1u, 0u, 0u);
    }
}

void FallingBlockRenderer::renderToShadowMap(const glm::mat4& shadowViewProj,
                                             const glm::mat4& shadowView,
                                             const glm::mat4& shadowProjection,
                                             float animationTime,
                                             float shaderTime) {
    if (m_resourceMgr == nullptr || m_shadowShader == nullptr) {
        return;
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const GLuint texArrayId = static_cast<GLuint>(renderer::rhi::gl::textureId(texArray.texture));
    if (texArrayId == 0) {
        return;
    }
    const GLuint grassColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getGrassColormap()));
    const GLuint foliageColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getFoliageColormap()));

    m_shadowShader->use();
    m_shadowShader->setInt("uUseModel", 1);
    m_shadowShader->setInt("uVertexFormat", 0);
    m_shadowShader->setInt("uForceBaseLod", 1);
    m_shadowShader->setInt("texArray", 0);
    m_shadowShader->setInt("uGrassColormap", 2);
    m_shadowShader->setInt("uFoliageColormap", 3);
    m_shadowShader->setMat4("viewProj", shadowViewProj);
    m_shadowShader->setMat4("uShadowModelView", shadowView);
    m_shadowShader->setMat4("uShadowProjection", shadowProjection);
    m_shadowShader->setMat4("uShadowProjectionInverse", glm::inverse(shadowProjection));
    m_shadowShader->setInt("uShadowPassMode", 0);
    m_shadowShader->setFloat("uAnimationTime", animationTime);
    m_shadowShader->setFloat("uTime", shaderTime);
    const int modelLoc = m_shadowShader->getUniformLocation("model");

    for (const RenderInstance& instance : m_renderInstances) {
        const renderer::BlockCubeMesh* mesh = getOrCreateMesh(instance.stateId);
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }
        m_shadowShader->setMat4(modelLoc, instance.model);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArrayId);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, grassColormapId);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, foliageColormapId);
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    // Restore uUseModel=0 so subsequent terrain shadow draws in the same
    // cascade (or next cascade) don't pick up the falling-block model matrix.
    m_shadowShader->use();
    m_shadowShader->setInt("uUseModel", 0);
    m_shadowShader->setInt("uVertexFormat", 1);
}

bool FallingBlockRenderer::createGBufferRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/falling_block_gbuffer_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/falling_block_gbuffer_rhi.frag");
    if (!vertexSource || !fragmentSource) return false;
    const RhiTextureHandle textures[] = {m_resourceMgr->getTextureArray().texture,
        m_resourceMgr->getGrassColormap(), m_resourceMgr->getFoliageColormap()};
    RhiTextureViewHandle* views[] = {&m_textureArrayView, &m_grassColormapView, &m_foliageColormapView};
    for (uint32_t i = 0; i < 3u; ++i) {
        RhiTextureViewDesc desc;
        desc.texture = textures[i];
        desc.viewType = i == 0u ? RhiTextureViewType::Texture2DArray : RhiTextureViewType::Texture2D;
        *views[i] = m_rhiDevice->createTextureView(desc);
    }
    RhiSamplerDesc samplerDesc;
    samplerDesc.addressU = RhiAddressMode::Repeat;
    samplerDesc.addressV = RhiAddressMode::Repeat;
    m_sampler = m_rhiDevice->createSampler(samplerDesc);
    auto createShader = [&](const char* name, RhiShaderStage stage, const std::string& source) {
        RhiShaderDesc desc;
        desc.debugName = name; desc.stage = stage; desc.source = source.c_str(); desc.sourceSize = source.size();
        return m_rhiDevice->createShader(desc);
    };
    m_gbufferVertexShader = createShader("FallingBlock.GBuffer.Vertex", RhiShaderStage::Vertex, *vertexSource);
    m_gbufferFragmentShader = createShader("FallingBlock.GBuffer.Fragment", RhiShaderStage::Fragment, *fragmentSource);
    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "FallingBlock.GBuffer.BindGroupLayout";
    for (uint32_t i = 0; i < 3u; ++i) layoutDesc.entries.push_back(
        {i, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_gbufferBindGroupLayout = m_rhiDevice->createBindGroupLayout(layoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FallingBlock.GBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_gbufferBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(FallingBlockPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_gbufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FallingBlock.GBuffer.Pipeline";
    pipelineDesc.vertexShader = m_gbufferVertexShader; pipelineDesc.fragmentShader = m_gbufferFragmentShader;
    pipelineDesc.layout = m_gbufferPipelineLayout;
    renderer::setBlockVertexInputLayout(pipelineDesc);
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true; pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::Rg8Unorm, RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rg16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(6u);
    m_gbufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_gbufferBindGroupLayout;
    const RhiTextureViewHandle textureViews[] = {m_textureArrayView, m_grassColormapView, m_foliageColormapView};
    for (uint32_t i = 0; i < 3u; ++i) {
        RhiBindGroupEntry entry; entry.binding = i;
        entry.resource.combinedTextureSampler = {textureViews[i], m_sampler};
        bindGroupDesc.entries.push_back(entry);
    }
    m_gbufferBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_textureArrayView.isValid() || !m_grassColormapView.isValid() || !m_foliageColormapView.isValid() ||
        !m_sampler.isValid() || !m_gbufferVertexShader.isValid() || !m_gbufferFragmentShader.isValid() ||
        !m_gbufferBindGroupLayout.isValid() || !m_gbufferPipelineLayout.isValid() ||
        !m_gbufferPipeline.isValid() || !m_gbufferBindGroup.isValid()) {
        destroyGBufferRhiResources(); return false;
    }
    return true;
}

void FallingBlockRenderer::destroyGBufferRhiResources() {
    if (m_rhiDevice) {
        if (m_gbufferBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_gbufferBindGroup);
        if (m_gbufferPipeline.isValid()) m_rhiDevice->destroyPipeline(m_gbufferPipeline);
        if (m_gbufferPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_gbufferPipelineLayout);
        if (m_gbufferBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_gbufferBindGroupLayout);
        if (m_gbufferFragmentShader.isValid()) m_rhiDevice->destroyShader(m_gbufferFragmentShader);
        if (m_gbufferVertexShader.isValid()) m_rhiDevice->destroyShader(m_gbufferVertexShader);
        if (m_sampler.isValid()) m_rhiDevice->destroySampler(m_sampler);
        if (m_foliageColormapView.isValid()) m_rhiDevice->destroyTextureView(m_foliageColormapView);
        if (m_grassColormapView.isValid()) m_rhiDevice->destroyTextureView(m_grassColormapView);
        if (m_textureArrayView.isValid()) m_rhiDevice->destroyTextureView(m_textureArrayView);
    }
    m_gbufferBindGroup = {}; m_gbufferPipeline = {}; m_gbufferPipelineLayout = {};
    m_gbufferBindGroupLayout = {}; m_gbufferFragmentShader = {}; m_gbufferVertexShader = {};
    m_sampler = {}; m_foliageColormapView = {}; m_grassColormapView = {}; m_textureArrayView = {};
    m_rhiDevice = nullptr;
}
