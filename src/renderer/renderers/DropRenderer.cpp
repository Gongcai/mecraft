#include "DropRenderer.h"
#include "../mesh/BlockMeshBuilder.h"
#include "../mesh/ItemModelMesh.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../rhi/RhiDevice.h"

#include <cstddef>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/camera/Camera.h"
#include "engine/platform/Window.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/DropSystem.h"
#include "../../world/IWorldView.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"
#include "../../item/Item.h"

namespace {
bool isTorchShape(const BlockDef& def) {
    return def.renderShapeName == "torch";
}

bool prefersBlockMeshForItem(const BlockID renderBlock) {
    if (renderBlock == 0) {
        return false;
    }
    const BlockDef& def = BlockRegistry::get(renderBlock);
    return isTorchShape(def) || def.renderShapeName == "model";
}
}

void DropRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_deferredShader = resourceMgr.getShader("drop_block");
    m_deferredItemShader = resourceMgr.getShader("item_model");
    m_shader = m_deferredShader;
    m_itemShader = m_deferredItemShader;
    m_gbufferShader = resourceMgr.getShader("drop_gbuffer");
    m_itemGBufferShader = resourceMgr.getShader("item_gbuffer");
    m_shadowShader = resourceMgr.getShader("shadow_depth");
    m_itemShadowShader = resourceMgr.getShader("item_shadow");
}

void DropRenderer::shutdown() {
    for (auto& pair : m_blockMeshes) {
        destroyMesh(pair.second);
    }
    m_blockMeshes.clear();
    for (auto& pair : m_itemMeshes) {
        destroyMesh(pair.second);
    }
    m_itemMeshes.clear();
    m_shader = nullptr;
    m_itemShader = nullptr;
    m_deferredShader = nullptr;
    m_deferredItemShader = nullptr;
    m_gbufferShader = nullptr;
    m_itemGBufferShader = nullptr;
    m_shadowShader = nullptr;
    m_itemShadowShader = nullptr;
    m_resourceMgr = nullptr;
}

void DropRenderer::setForwardMode(bool forward) {
    if (m_resourceMgr == nullptr) return;
    if (forward) {
        Shader* fwdBlock = m_resourceMgr->getShader("drop_block_forward");
        Shader* fwdItem = m_resourceMgr->getShader("item_model_forward");
        m_shader = fwdBlock ? fwdBlock : m_deferredShader;
        m_itemShader = fwdItem ? fwdItem : m_deferredItemShader;
    } else {
        m_shader = m_deferredShader;
        m_itemShader = m_deferredItemShader;
    }
}

void DropRenderer::render(const DropSystem& dropSystem, const Camera& camera, const Window& window) {
    if (m_resourceMgr == nullptr) {
        return;
    }

    const auto& drops = dropSystem.getDrops();
    if (drops.empty()) {
        return;
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const TextureAtlas& itemAtlas = m_resourceMgr->getItemTextureAtlas();
    const GLuint texArrayId = static_cast<GLuint>(renderer::rhi::gl::textureId(texArray.texture));
    const GLuint itemAtlasId = static_cast<GLuint>(renderer::rhi::gl::textureId(itemAtlas.texture));
    const bool canRenderBlocks = (m_shader != nullptr && texArrayId != 0);
    const bool canRenderItems = (m_itemShader != nullptr && itemAtlasId != 0);
    if (!canRenderBlocks && !canRenderItems) {
        return;
    }

    const glm::mat4 viewProj = camera.getProjectionMatrix(window.getAspectRatio()) * camera.getViewMatrix();
    int blockModelLoc = -1;
    if (canRenderBlocks) {
        const int blockViewProjLoc = m_shader->getUniformLocation("viewProj");
        blockModelLoc = m_shader->getUniformLocation("model");
        m_shader->use();
        m_shader->setMat4("view", camera.getViewMatrix());
        m_shader->setMat4(blockViewProjLoc, viewProj);
        m_shader->setInt("texArray", 0);
        m_shader->setInt("uForceBaseLod", 0);
        m_shader->setInt("uGrassColormap", 3);
        m_shader->setInt("uFoliageColormap", 4);
        m_shader->setFloat("uWindStrength", 0.0f);
        m_shader->setFloat("uWindSpeed", 0.0f);
        m_shader->setFloat("uWindSpatialFreq", 1.0f);
        m_shader->setFloat("uWindTime", 0.0f);
        m_shader->setInt("uFogEnabled", 0);
        m_shader->setFloat("uSkyIntensity", 1.0f);
        m_shader->setInt("uLightmapDay", 1);
        m_shader->setInt("uLightmapNight", 2);
    }

    int itemModelLoc = -1;
    if (canRenderItems) {
        const int itemViewProjLoc = m_itemShader->getUniformLocation("viewProj");
        itemModelLoc = m_itemShader->getUniformLocation("model");
        m_itemShader->use();
        m_itemShader->setMat4(itemViewProjLoc, viewProj);
        m_itemShader->setInt("uAtlas", 0);
    }

    const GLuint lightmapDayId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getLightmapDay()));
    const GLuint lightmapNightId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getLightmapNight()));
    const GLuint grassColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getGrassColormap()));
    const GLuint foliageColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getFoliageColormap()));

    for (const DropEntity& drop : drops) {
        const ItemDef& itemDef = ItemRegistry::get(drop.itemId);
        const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        const bool preferBlockMesh = prefersBlockMeshForItem(renderBlock);

        glm::mat4 model(1.0f);
        model = glm::translate(model, drop.position);
        model = glm::rotate(model, drop.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(drop.halfExtents * 2.0f));
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        if (!preferBlockMesh && itemTileIndex >= 0 && canRenderItems) {
            Mesh* mesh = getOrCreateItemMesh(drop.itemId);
            if (mesh != nullptr && mesh->vao != 0 && mesh->vertexCount > 0) {
                m_itemShader->use();
                m_itemShader->setMat4(itemModelLoc, model);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, itemAtlasId);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
            }
            continue;
        }

        if (renderBlock == 0 || !canRenderBlocks) {
            continue;
        }

        Mesh* mesh = getOrCreateBlockMesh(renderBlock);
        if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
            continue;
        }

        m_shader->use();
        m_shader->setMat4(blockModelLoc, model);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArrayId);
        // Bind lightmap textures for drop block rendering
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lightmapDayId);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, lightmapNightId);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, grassColormapId);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, foliageColormapId);
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

DropRenderer::Mesh* DropRenderer::getOrCreateItemMesh(const ItemID itemId) {
    const auto it = m_itemMeshes.find(itemId);
    if (it != m_itemMeshes.end()) {
        return &it->second;
    }

    Mesh mesh = buildItemMesh(itemId);
    auto inserted = m_itemMeshes.emplace(itemId, std::move(mesh));
    return &inserted.first->second;
}

DropRenderer::Mesh DropRenderer::buildItemMesh(const ItemID itemId) const {
    Mesh mesh;
    if (m_resourceMgr == nullptr || itemId == 0) {
        return mesh;
    }

    const ItemDef& itemDef = ItemRegistry::get(itemId);
    const int tileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    if (tileIndex < 0) {
        return mesh;
    }

    std::vector<ItemModelVertex> vertices;
    if (!buildExtrudedItemMesh(m_resourceMgr->getItemTextureAtlas(),
                               m_resourceMgr->getItemTexturePixels(),
                               tileIndex,
                               vertices)) {
        return mesh;
    }

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(ItemModelVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, shade)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, nx)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Drop.ItemMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(ItemModelVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    RhiDevice& rhiDevice = m_resourceMgr->rhiDevice();
    mesh.rhiVertexBuffer = rhiDevice.createBuffer(
        bufferDesc, vertices.data(), vertices.size() * sizeof(ItemModelVertex));
    mesh.rhiDevice = &rhiDevice;
    if (!mesh.rhiVertexBuffer.isValid()) {
        destroyMesh(mesh);
    }
    return mesh;
}

DropRenderer::Mesh* DropRenderer::getOrCreateBlockMesh(const BlockID blockId) {
    const auto it = m_blockMeshes.find(blockId);
    if (it != m_blockMeshes.end()) {
        return &it->second;
    }

    Mesh mesh = buildBlockMesh(blockId);
    auto inserted = m_blockMeshes.emplace(blockId, std::move(mesh));
    return &inserted.first->second;
}

DropRenderer::Mesh DropRenderer::buildBlockMesh(const BlockID blockId) const {
    Mesh mesh;
    if (m_resourceMgr == nullptr || blockId == 0) {
        return mesh;
    }

    renderer::BlockCubeMesh shared = renderer::buildBlockCubeMesh(blockId, *m_resourceMgr);
    mesh.vao = shared.vao;
    mesh.vbo = shared.vbo;
    mesh.rhiVertexBuffer = shared.rhiVertexBuffer;
    mesh.rhiDevice = shared.rhiDevice;
    mesh.vertexCount = shared.vertexCount;
    shared.vao = 0;
    shared.vbo = 0;
    shared.rhiVertexBuffer = {};
    shared.rhiDevice = nullptr;
    shared.vertexCount = 0;
    return mesh;
}

void DropRenderer::renderToGBuffer(const IWorldView& worldView, const DropSystem& dropSystem,
                                    const glm::mat4& jitteredViewProj,
                                    const glm::mat4& previousViewProj,
                                    float animationTime) {
    if (m_resourceMgr == nullptr) {
        return;
    }

    const auto& drops = dropSystem.getDrops();
    if (drops.empty()) {
        return;
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const TextureAtlas& itemAtlas = m_resourceMgr->getItemTextureAtlas();
    const GLuint texArrayId = static_cast<GLuint>(renderer::rhi::gl::textureId(texArray.texture));
    const GLuint itemAtlasId = static_cast<GLuint>(renderer::rhi::gl::textureId(itemAtlas.texture));
    const bool canRenderBlocks = (m_gbufferShader != nullptr && texArrayId != 0);
    const bool canRenderItems = (m_itemGBufferShader != nullptr && itemAtlasId != 0);
    if (!canRenderBlocks && !canRenderItems) {
        return;
    }

    // Block drop GBuffer setup
    int blockModelLoc = -1;
    int blockPrevModelLoc = -1;
    if (canRenderBlocks) {
        m_gbufferShader->use();
        m_gbufferShader->setMat4("viewProj", jitteredViewProj);
        m_gbufferShader->setMat4("prevViewProj", previousViewProj);
        blockModelLoc = m_gbufferShader->getUniformLocation("model");
        blockPrevModelLoc = m_gbufferShader->getUniformLocation("prevModel");
        m_gbufferShader->setInt("uVertexFormat", 0);
        m_gbufferShader->setInt("texArray", 0);
        m_gbufferShader->setInt("uForceBaseLod", 0);
        m_gbufferShader->setInt("uGrassColormap", 3);
        m_gbufferShader->setInt("uFoliageColormap", 4);
        m_gbufferShader->setFloat("uAnimationTime", animationTime);
    }

    // Item drop GBuffer setup
    int itemModelLoc = -1;
    int itemPrevModelLoc = -1;
    if (canRenderItems) {
        m_itemGBufferShader->use();
        m_itemGBufferShader->setMat4("viewProj", jitteredViewProj);
        m_itemGBufferShader->setMat4("prevViewProj", previousViewProj);
        itemModelLoc = m_itemGBufferShader->getUniformLocation("model");
        itemPrevModelLoc = m_itemGBufferShader->getUniformLocation("prevModel");
        m_itemGBufferShader->setInt("uAtlas", 0);
    }

    const GLuint grassColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getGrassColormap()));
    const GLuint foliageColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getFoliageColormap()));

    for (const DropEntity& drop : drops) {
        const ItemDef& itemDef = ItemRegistry::get(drop.itemId);
        const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        const bool preferBlockMesh = prefersBlockMeshForItem(renderBlock);

        glm::mat4 model(1.0f);
        model = glm::translate(model, drop.position);
        model = glm::rotate(model, drop.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(drop.halfExtents * 2.0f));
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        // Query world light at drop center position for deferred lighting.
        const glm::vec2 light = queryWorldLight(worldView, drop.position);

        if (!preferBlockMesh && itemTileIndex >= 0 && canRenderItems) {
            Mesh* mesh = getOrCreateItemMesh(drop.itemId);
            if (mesh != nullptr && mesh->vao != 0 && mesh->vertexCount > 0) {
                m_itemGBufferShader->use();
                auto it = m_previousModelMatrices.find(drop.id);
                m_itemGBufferShader->setMat4(itemPrevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
                m_itemGBufferShader->setMat4(itemModelLoc, model);
                m_itemGBufferShader->setFloat("uDropSunlight", light.x);
                m_itemGBufferShader->setFloat("uDropBlockLight", light.y);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, itemAtlasId);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                m_previousModelMatrices[drop.id] = model;
            }
            continue;
        }

        if (renderBlock == 0 || !canRenderBlocks) {
            continue;
        }

        Mesh* mesh = getOrCreateBlockMesh(renderBlock);
        if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
            continue;
        }

        m_gbufferShader->use();
        auto it = m_previousModelMatrices.find(drop.id);
        m_gbufferShader->setMat4(blockPrevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
        m_gbufferShader->setMat4(blockModelLoc, model);
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
        m_previousModelMatrices[drop.id] = model;
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DropRenderer::renderToShadowMap(const IWorldView& worldView, const DropSystem& dropSystem,
                                      const glm::mat4& shadowViewProj,
                                      const glm::mat4& shadowView,
                                      const glm::mat4& shadowProjection,
                                      float animationTime,
                                      float shaderTime) {
    (void)worldView; // Reserved for future shadow light query; shadow pass is depth-only currently.
    if (m_resourceMgr == nullptr) {
        return;
    }

    const auto& drops = dropSystem.getDrops();
    if (drops.empty()) {
        return;
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const TextureAtlas& itemAtlas = m_resourceMgr->getItemTextureAtlas();
    const GLuint texArrayId = static_cast<GLuint>(renderer::rhi::gl::textureId(texArray.texture));
    const GLuint itemAtlasId = static_cast<GLuint>(renderer::rhi::gl::textureId(itemAtlas.texture));
    const bool canRenderBlocks = (m_shadowShader != nullptr && texArrayId != 0);
    const bool canRenderItems = (m_itemShadowShader != nullptr && itemAtlasId != 0);
    if (!canRenderBlocks && !canRenderItems) {
        return;
    }

    // Block drop shadow setup — reuse shadow_depth shader with uUseModel=1
    int blockModelLoc = -1;
    if (canRenderBlocks) {
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
        blockModelLoc = m_shadowShader->getUniformLocation("model");
    }

    // Item drop shadow setup
    int itemModelLoc = -1;
    if (canRenderItems) {
        m_itemShadowShader->use();
        m_itemShadowShader->setMat4("viewProj", shadowViewProj);
        itemModelLoc = m_itemShadowShader->getUniformLocation("model");
        m_itemShadowShader->setInt("uAtlas", 0);
    }

    const GLuint grassColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getGrassColormap()));
    const GLuint foliageColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getFoliageColormap()));

    for (const DropEntity& drop : drops) {
        const ItemDef& itemDef = ItemRegistry::get(drop.itemId);
        const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        const bool preferBlockMesh = prefersBlockMeshForItem(renderBlock);

        glm::mat4 model(1.0f);
        model = glm::translate(model, drop.position);
        model = glm::rotate(model, drop.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(drop.halfExtents * 2.0f));
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        if (!preferBlockMesh && itemTileIndex >= 0 && canRenderItems) {
            Mesh* mesh = getOrCreateItemMesh(drop.itemId);
            if (mesh != nullptr && mesh->vao != 0 && mesh->vertexCount > 0) {
                m_itemShadowShader->use();
                m_itemShadowShader->setMat4(itemModelLoc, model);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, itemAtlasId);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
            }
            continue;
        }

        if (renderBlock == 0 || !canRenderBlocks) {
            continue;
        }

        Mesh* mesh = getOrCreateBlockMesh(renderBlock);
        if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
            continue;
        }

        m_shadowShader->use();
        m_shadowShader->setMat4(blockModelLoc, model);
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
    glBindTexture(GL_TEXTURE_2D, 0);

    // Restore uUseModel=0 so subsequent terrain shadow draws in the same
    // cascade (or next cascade) don't pick up the drop model matrix.
    if (canRenderBlocks) {
        m_shadowShader->use();
        m_shadowShader->setInt("uUseModel", 0);
        m_shadowShader->setInt("uVertexFormat", 1);
    }
}

glm::vec2 DropRenderer::queryWorldLight(const IWorldView& worldView, const glm::vec3& position) {
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

void DropRenderer::destroyMesh(Mesh& mesh) {
    if (mesh.rhiDevice != nullptr && mesh.rhiVertexBuffer.isValid()) {
        mesh.rhiDevice->destroyBuffer(mesh.rhiVertexBuffer);
        mesh.rhiVertexBuffer = {};
    }
    if (mesh.vbo != 0) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    mesh.vertexCount = 0;
    mesh.rhiDevice = nullptr;
}
