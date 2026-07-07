#include "FallingBlockRenderer.h"

#include <cmath>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/camera/Camera.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/components/Components.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"

namespace {

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

void FallingBlockRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_gbufferShader = resourceMgr.getShader("drop_gbuffer");
    m_shadowShader = resourceMgr.getShader("shadow_depth");
}

void FallingBlockRenderer::shutdown() {
    for (auto& pair : m_meshes) {
        renderer::destroyBlockCubeMesh(pair.second);
    }
    m_meshes.clear();
    m_previousModelMatrices.clear();
    m_resourceMgr = nullptr;
    m_gbufferShader = nullptr;
    m_shadowShader = nullptr;
}

const renderer::BlockCubeMesh* FallingBlockRenderer::getOrCreateMesh(BlockStateId stateId) {
    const auto it = m_meshes.find(stateId);
    if (it != m_meshes.end()) {
        return &it->second;
    }
    auto inserted = m_meshes.emplace(stateId, renderer::buildBlockStateCubeMesh(stateId, *m_resourceMgr));
    return &inserted.first->second;
}

void FallingBlockRenderer::renderToGBuffer(const IWorldView& worldView,
                                           const ecs::GameplayRegistry& registry,
                                           const glm::mat4& jitteredViewProj,
                                           const glm::mat4& previousViewProj,
                                           float animationTime) {
    if (m_resourceMgr == nullptr || m_gbufferShader == nullptr) {
        return;
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const GLuint texArrayId = static_cast<GLuint>(renderer::rhi::gl::textureId(texArray.texture));
    if (texArrayId == 0) {
        return;
    }
    const GLuint grassColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getGrassColormap()));
    const GLuint foliageColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getFoliageColormap()));

    auto& reg = registry.registry();
    auto view = reg.view<ecs::FallingBlockTag,
                         ecs::FallingBlockComponent,
                         ecs::TransformComponent,
                         ecs::DropEntityIdComponent>();

    m_gbufferShader->use();
    m_gbufferShader->setMat4("viewProj", jitteredViewProj);
    m_gbufferShader->setMat4("prevViewProj", previousViewProj);
    const int modelLoc = m_gbufferShader->getUniformLocation("model");
    const int prevModelLoc = m_gbufferShader->getUniformLocation("prevModel");
    m_gbufferShader->setInt("uVertexFormat", 0);
    m_gbufferShader->setInt("texArray", 0);
    m_gbufferShader->setInt("uForceBaseLod", 0);
    m_gbufferShader->setInt("uGrassColormap", 3);
    m_gbufferShader->setInt("uFoliageColormap", 4);
    m_gbufferShader->setFloat("uAnimationTime", animationTime);

    for (const entt::entity entity : view) {
        const auto& block = view.get<ecs::FallingBlockComponent>(entity);
        const auto& transform = view.get<ecs::TransformComponent>(entity);
        const auto& idComp = view.get<ecs::DropEntityIdComponent>(entity);

        const renderer::BlockCubeMesh* mesh =
            getOrCreateMesh(BlockStateRegistry::getDefaultState(block.blockId));
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }

        // Full-cube model: translate to render position, no rotation, unit scale,
        // then offset so the cube spans [pos-0.5, pos+0.5] (centered on the cell).
        glm::mat4 model(1.0f);
        model = glm::translate(model, transform.position);
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        const glm::vec2 light = queryWorldLight(worldView, transform.position);

        const auto it = m_previousModelMatrices.find(idComp.dropId);
        m_gbufferShader->setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
        m_gbufferShader->setMat4(modelLoc, model);
        m_gbufferShader->setFloat("uDropSunlight", light.x);
        m_gbufferShader->setFloat("uDropBlockLight", light.y);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArrayId);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, grassColormapId);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, foliageColormapId);
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));

        m_previousModelMatrices[idComp.dropId] = model;
    }

    auto movingView = reg.view<ecs::MovingBlockTag,
                              ecs::MovingBlockComponent,
                              ecs::TransformComponent,
                              ecs::DropEntityIdComponent>();
    for (const entt::entity entity : movingView) {
        const auto& block = movingView.get<ecs::MovingBlockComponent>(entity);
        const auto& transform = movingView.get<ecs::TransformComponent>(entity);
        const auto& idComp = movingView.get<ecs::DropEntityIdComponent>(entity);

        const renderer::BlockCubeMesh* mesh = getOrCreateMesh(block.stateId);
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }

        glm::mat4 model(1.0f);
        model = glm::translate(model, transform.position);
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        const glm::vec2 light = queryWorldLight(worldView, transform.position);

        const auto it = m_previousModelMatrices.find(idComp.dropId);
        m_gbufferShader->setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
        m_gbufferShader->setMat4(modelLoc, model);
        m_gbufferShader->setFloat("uDropSunlight", light.x);
        m_gbufferShader->setFloat("uDropBlockLight", light.y);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArrayId);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, grassColormapId);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, foliageColormapId);
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));

        m_previousModelMatrices[idComp.dropId] = model;
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void FallingBlockRenderer::renderToShadowMap(const ecs::GameplayRegistry& registry,
                                             const glm::mat4& shadowViewProj,
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

    auto& reg = registry.registry();
    auto view = reg.view<ecs::FallingBlockTag,
                         ecs::FallingBlockComponent,
                         ecs::TransformComponent>();

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

    for (const entt::entity entity : view) {
        const auto& block = view.get<ecs::FallingBlockComponent>(entity);
        const auto& transform = view.get<ecs::TransformComponent>(entity);

        const renderer::BlockCubeMesh* mesh =
            getOrCreateMesh(BlockStateRegistry::getDefaultState(block.blockId));
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }

        glm::mat4 model(1.0f);
        model = glm::translate(model, transform.position);
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        m_shadowShader->setMat4(modelLoc, model);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArrayId);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, grassColormapId);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, foliageColormapId);
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
    }

    auto movingView = reg.view<ecs::MovingBlockTag,
                              ecs::MovingBlockComponent,
                              ecs::TransformComponent>();
    for (const entt::entity entity : movingView) {
        const auto& block = movingView.get<ecs::MovingBlockComponent>(entity);
        const auto& transform = movingView.get<ecs::TransformComponent>(entity);

        const renderer::BlockCubeMesh* mesh = getOrCreateMesh(block.stateId);
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }

        glm::mat4 model(1.0f);
        model = glm::translate(model, transform.position);
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        m_shadowShader->setMat4(modelLoc, model);
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
