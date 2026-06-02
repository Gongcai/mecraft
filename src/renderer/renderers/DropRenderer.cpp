#include "DropRenderer.h"
#include "../mesh/ItemModelMesh.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

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
constexpr std::array<std::array<glm::vec3, 4>, 6> kFaceCorners = {{
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}}, // top
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, // bottom
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, // front (+z)
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, // back (-z)
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, // left (-x)
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}  // right (+x)
}};

constexpr std::array<glm::vec3, 4> kCrossQuadA = {{{0.1464f, 0.0f, 0.1464f}, {0.8536f, 0.0f, 0.8536f}, {0.8536f, 1.0f, 0.8536f}, {0.1464f, 1.0f, 0.1464f}}};
constexpr std::array<glm::vec3, 4> kCrossQuadB = {{{0.8536f, 0.0f, 0.1464f}, {0.1464f, 0.0f, 0.8536f}, {0.1464f, 1.0f, 0.8536f}, {0.8536f, 1.0f, 0.1464f}}};

constexpr std::array<int, 6> kFaceIndices = {{0, 1, 2, 0, 2, 3}};
constexpr float kCrossBiomeTintMarker = -1.0f;
constexpr float kCrossFlowerMarker = -2.0f;
// Torch opaque texels occupy inclusive pixel coordinates:
// left=7, right=8, top=6, bottom=15 on a 16x16 tile.
// We sample at texel centers to avoid pulling in the transparent neighbors.
constexpr float kTorchPixelLeft = 7.0f;
constexpr float kTorchPixelRight = 8.0f;
constexpr float kTorchPixelTop = 6.0f;
constexpr float kTorchPixelBottom = 15.0f;
constexpr float kTorchU0 = (kTorchPixelLeft + 0.5f) / 16.0f;
constexpr float kTorchU1 = (kTorchPixelRight + 0.5f) / 16.0f;
constexpr float kTorchSideV0 = (kTorchPixelTop + 0.5f) / 16.0f;
constexpr float kTorchSideV1 = (kTorchPixelBottom + 0.5f) / 16.0f;
constexpr float kTorchTopV0 = (kTorchPixelTop + 0.5f) / 16.0f;
constexpr float kTorchTopV1 = (kTorchPixelTop + 1.5f) / 16.0f;
constexpr float kTorchHalfWidth = 1.0f / 16.0f;
constexpr float kTorchHeight = 10.0f / 16.0f;
constexpr float kTorchModelPixel = 1.0f / 16.0f;
constexpr float kTorchModelCoreMin = 7.0f * kTorchModelPixel;
constexpr float kTorchModelCoreMax = 9.0f * kTorchModelPixel;
constexpr float kTorchModelCoreTop = 10.0f * kTorchModelPixel;

struct TorchModelUvRect {
    float u0;
    float v0;
    float u1;
    float v1;
};

int getFaceTextureIndex(const BlockDef& def, const int face) {
    return def.getFaceLayer(face);
}

bool isTorchShape(const BlockDef& def) {
    return def.renderShapeName == "torch";
}

TorchModelUvRect makeTorchModelSourceUvRect(const float left,
                                            const float top,
                                            const float right,
                                            const float bottom) {
    return {
        left * kTorchModelPixel,
        1.0f - bottom * kTorchModelPixel,
        right * kTorchModelPixel,
        1.0f - top * kTorchModelPixel
    };
}

void emitTorchModelFace(std::vector<BlockVertex>& vertices,
                        const float layer,
                        const float normal,
                        const std::array<glm::vec3, 4>& corners,
                        const TorchModelUvRect& uvRect,
                        const uint8_t derivativeMaterialId) {
    const std::array<glm::vec2, 4> uv = {{
        {uvRect.u0, uvRect.v0},
        {uvRect.u1, uvRect.v0},
        {uvRect.u1, uvRect.v1},
        {uvRect.u0, uvRect.v1}
    }};

    for (const int idx : kFaceIndices) {
        const glm::vec3& pos = corners[static_cast<size_t>(idx)];
        const glm::vec2& uvCoord = uv[static_cast<size_t>(idx)];
        vertices.push_back(makeBlockVertex(
            pos.x,
            pos.y,
            pos.z,
            uvCoord.x,
            uvCoord.y,
            normal,
            1.0f,
            0.0f,
            3.0f,
            layer,
            1.0f,
            0.0f,
            0.0f,
            BlockTintKinds::NONE,
            0,
            0,
            derivativeMaterialId
        ));
    }
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
    const bool canRenderBlocks = (m_shader != nullptr && texArray.textureID != 0);
    const bool canRenderItems = (m_itemShader != nullptr && itemAtlas.textureID != 0);
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

    for (const DropEntity& drop : drops) {
        const ItemDef& itemDef = ItemRegistry::get(drop.itemId);
        const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        const bool preferBlockMesh = (renderBlock != 0 && isTorchShape(BlockRegistry::get(renderBlock)));

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
                glBindTexture(GL_TEXTURE_2D, itemAtlas.textureID);
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
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);
        // Bind lightmap textures for drop block rendering
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapDay());
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapNight());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());
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

    const BlockDef& def = BlockRegistry::get(blockId);

    std::vector<BlockVertex> vertices;
    vertices.reserve(36);

    if (def.renderShape == BlockRenderShape::Cross) {
        int tileIndex = def.faceTop.firstLayer;
        if (tileIndex < 0) {
            tileIndex = def.faceFront.firstLayer;
        }
        if (tileIndex < 0) {
            tileIndex = 0;
        }

        const float layer = static_cast<float>(tileIndex);
        const std::array<glm::vec2, 4> quadUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
        uint8_t tintU = 0;
        uint8_t tintV = 0;
        computeDefaultBlockTintMapPosition(tintU, tintV);
        const uint8_t tintKind = blockTintKindFromBiomeTint(def.biomeTint);
        const float crossMarker = tintKind != BlockTintKinds::NONE ? kCrossBiomeTintMarker : kCrossFlowerMarker;

        const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = corners[idx];
                const glm::vec2& uvCoord = quadUV[idx];
                vertices.push_back(makeBlockVertex(
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    crossMarker,
                    1.0f,  // sunlight: full brightness for drop items
                    0.0f,   // blockLight
                    3.0f,   // ao: no occlusion
                    layer,
                    1.0f,
                    0.0f,
                    0.0f,
                    tintKind,
                    tintU,
                    tintV,
                    def.derivativeMaterialId
                ));
            }
        };

        emitQuad(kCrossQuadA);
        emitQuad(kCrossQuadB);
    } else if (isTorchShape(def)) {
        int tileIndex = def.faceTop.firstLayer;
        if (tileIndex < 0) {
            tileIndex = def.faceFront.firstLayer;
        }
        if (tileIndex < 0) {
            tileIndex = 0;
        }

        const float layer = static_cast<float>(tileIndex);
        const TorchModelUvRect kTorchTopUv = makeTorchModelSourceUvRect(7.0f, 6.0f, 9.0f, 8.0f);
        const TorchModelUvRect kTorchBottomUv = makeTorchModelSourceUvRect(7.0f, 13.0f, 9.0f, 15.0f);
        const TorchModelUvRect kTorchFullUv = makeTorchModelSourceUvRect(0.0f, 0.0f, 16.0f, 16.0f);

        emitTorchModelFace(vertices, layer, 0.0f, {{
            {kTorchModelCoreMin, kTorchModelCoreTop, kTorchModelCoreMax},
            {kTorchModelCoreMax, kTorchModelCoreTop, kTorchModelCoreMax},
            {kTorchModelCoreMax, kTorchModelCoreTop, kTorchModelCoreMin},
            {kTorchModelCoreMin, kTorchModelCoreTop, kTorchModelCoreMin}
        }}, kTorchTopUv, def.derivativeMaterialId);
        emitTorchModelFace(vertices, layer, 1.0f, {{
            {kTorchModelCoreMin, 0.0f, kTorchModelCoreMin},
            {kTorchModelCoreMax, 0.0f, kTorchModelCoreMin},
            {kTorchModelCoreMax, 0.0f, kTorchModelCoreMax},
            {kTorchModelCoreMin, 0.0f, kTorchModelCoreMax}
        }}, kTorchBottomUv, def.derivativeMaterialId);
        emitTorchModelFace(vertices, layer, 4.0f, {{
            {kTorchModelCoreMin, 0.0f, 0.0f},
            {kTorchModelCoreMin, 0.0f, 1.0f},
            {kTorchModelCoreMin, 1.0f, 1.0f},
            {kTorchModelCoreMin, 1.0f, 0.0f}
        }}, kTorchFullUv, def.derivativeMaterialId);
        emitTorchModelFace(vertices, layer, 5.0f, {{
            {kTorchModelCoreMax, 0.0f, 1.0f},
            {kTorchModelCoreMax, 0.0f, 0.0f},
            {kTorchModelCoreMax, 1.0f, 0.0f},
            {kTorchModelCoreMax, 1.0f, 1.0f}
        }}, kTorchFullUv, def.derivativeMaterialId);
        emitTorchModelFace(vertices, layer, 2.0f, {{
            {0.0f, 0.0f, kTorchModelCoreMax},
            {1.0f, 0.0f, kTorchModelCoreMax},
            {1.0f, 1.0f, kTorchModelCoreMax},
            {0.0f, 1.0f, kTorchModelCoreMax}
        }}, kTorchFullUv, def.derivativeMaterialId);
        emitTorchModelFace(vertices, layer, 3.0f, {{
            {1.0f, 0.0f, kTorchModelCoreMin},
            {0.0f, 0.0f, kTorchModelCoreMin},
            {0.0f, 1.0f, kTorchModelCoreMin},
            {1.0f, 1.0f, kTorchModelCoreMin}
        }}, kTorchFullUv, def.derivativeMaterialId);
    } else {
        for (int face = 0; face < 6; ++face) {
            int tileIndex = getFaceTextureIndex(def, face);
            if (tileIndex < 0) {
                tileIndex = 0;
            }

            const float layer = static_cast<float>(tileIndex);
            const std::array<glm::vec2, 4> faceUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
            uint8_t tintU = 0;
            uint8_t tintV = 0;
            computeDefaultBlockTintMapPosition(tintU, tintV);
            const uint8_t tintKind = blockTintKindFromBiomeTint(def.biomeTint);

            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = kFaceCorners[face][idx];
                const glm::vec2& uvCoord = faceUV[idx];
                vertices.push_back(makeBlockVertex(
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    static_cast<float>(face),
                    1.0f,  // sunlight: full brightness for drop items
                    0.0f,   // blockLight
                    3.0f,   // ao: no occlusion
                    layer,
                    1.0f,
                    0.0f,
                    0.0f,
                    tintKind,
                    tintU,
                    tintV,
                    def.derivativeMaterialId
                ));
            }
        }
    }

    if (vertices.empty()) {
        return mesh;
    }

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(BlockVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, normal)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, sunlight)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, blockLight)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, ao)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, layer)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animationFrameCount)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animationFps)));
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animated)));

    glEnableVertexAttribArray(10);
    glVertexAttribIPointer(10, 1, GL_UNSIGNED_SHORT, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, tintPacked)));
    for (GLuint attrib = 11; attrib <= 14; ++attrib) {
        glDisableVertexAttribArray(attrib);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return mesh;
}

void DropRenderer::renderToGBuffer(const IWorldView& worldView, const DropSystem& dropSystem,
                                    const glm::mat4& jitteredViewProj,
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
    const bool canRenderBlocks = (m_gbufferShader != nullptr && texArray.textureID != 0);
    const bool canRenderItems = (m_itemGBufferShader != nullptr && itemAtlas.textureID != 0);
    if (!canRenderBlocks && !canRenderItems) {
        return;
    }

    // Block drop GBuffer setup
    int blockModelLoc = -1;
    int blockPrevModelLoc = -1;
    if (canRenderBlocks) {
        m_gbufferShader->use();
        m_gbufferShader->setMat4("viewProj", jitteredViewProj);
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
        itemModelLoc = m_itemGBufferShader->getUniformLocation("model");
        itemPrevModelLoc = m_itemGBufferShader->getUniformLocation("prevModel");
        m_itemGBufferShader->setInt("uAtlas", 0);
    }

    for (const DropEntity& drop : drops) {
        const ItemDef& itemDef = ItemRegistry::get(drop.itemId);
        const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        const bool preferBlockMesh = (renderBlock != 0 && isTorchShape(BlockRegistry::get(renderBlock)));

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
                m_itemGBufferShader->setMat4(itemPrevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
                m_itemGBufferShader->setMat4(itemModelLoc, model);
                m_itemGBufferShader->setFloat("uDropSunlight", light.x);
                m_itemGBufferShader->setFloat("uDropBlockLight", light.y);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, itemAtlas.textureID);
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
        m_gbufferShader->setMat4(blockPrevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
        m_gbufferShader->setMat4(blockModelLoc, model);
        m_gbufferShader->setFloat("uDropSunlight", light.x);
        m_gbufferShader->setFloat("uDropBlockLight", light.y);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());
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
    const bool canRenderBlocks = (m_shadowShader != nullptr && texArray.textureID != 0);
    const bool canRenderItems = (m_itemShadowShader != nullptr && itemAtlas.textureID != 0);
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

    for (const DropEntity& drop : drops) {
        const ItemDef& itemDef = ItemRegistry::get(drop.itemId);
        const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        const bool preferBlockMesh = (renderBlock != 0 && isTorchShape(BlockRegistry::get(renderBlock)));

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
                glBindTexture(GL_TEXTURE_2D, itemAtlas.textureID);
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
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());
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
    if (mesh.vbo != 0) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    mesh.vertexCount = 0;
}
