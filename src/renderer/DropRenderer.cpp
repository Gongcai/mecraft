#include "DropRenderer.h"
#include "ItemModelMesh.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "../core/Camera.h"
#include "../core/Window.h"
#include "../resource/ResourceMgr.h"
#include "../world/DropSystem.h"
#include "../world/SubChunk.h"
#include "../item/Item.h"

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
constexpr float kCubeBiomeTintMarker = -3.0f;
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
    switch (face) {
        case 0:
            return def.texTop;
        case 1:
            return def.texBottom;
        case 2:
            return def.texFront;
        case 3:
            return def.texBack;
        case 4:
            return def.texLeft;
        case 5:
            return def.texRight;
        default:
            return 0;
    }
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
                        const TorchModelUvRect& uvRect) {
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
            layer
        ));
    }
}
}

void DropRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_shader = resourceMgr.getShader("drop_block");
    m_blockShaderUsesModelSwitch = false;
    if (m_shader == nullptr) {
        m_shader = resourceMgr.getShader("chunk_lit");
        m_blockShaderUsesModelSwitch = (m_shader != nullptr);
    }
    m_itemShader = resourceMgr.getShader("item_model");
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
    m_resourceMgr = nullptr;
    m_blockShaderUsesModelSwitch = false;
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
        if (m_blockShaderUsesModelSwitch) {
            m_shader->setInt("uUseModel", 1);
        }
        m_shader->setInt("texArray", 0);
        m_shader->setInt("uForceBaseLod", 0);
        m_shader->setVec3("uBiomeTintColor", glm::vec3(0.50f, 0.78f, 0.34f));
        m_shader->setVec3("uFoliageTintColor", glm::vec3(0.43f, 0.68f, 0.28f));
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
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
    }

    glBindVertexArray(0);
    if (canRenderBlocks && m_blockShaderUsesModelSwitch) {
        m_shader->use();
        m_shader->setInt("uUseModel", 0);
    }
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
        int tileIndex = def.texTop;
        if (tileIndex < 0) {
            tileIndex = def.texFront;
        }
        if (tileIndex < 0) {
            tileIndex = 0;
        }

        const float layer = static_cast<float>(tileIndex);
        const std::array<glm::vec2, 4> quadUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
        const float crossMarker = def.useBiomeTint ? kCrossBiomeTintMarker : kCrossFlowerMarker;

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
                    layer
                ));
            }
        };

        emitQuad(kCrossQuadA);
        emitQuad(kCrossQuadB);
    } else if (isTorchShape(def)) {
        int tileIndex = def.texTop;
        if (tileIndex < 0) {
            tileIndex = def.texFront;
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
        }}, kTorchTopUv);
        emitTorchModelFace(vertices, layer, 1.0f, {{
            {kTorchModelCoreMin, 0.0f, kTorchModelCoreMin},
            {kTorchModelCoreMax, 0.0f, kTorchModelCoreMin},
            {kTorchModelCoreMax, 0.0f, kTorchModelCoreMax},
            {kTorchModelCoreMin, 0.0f, kTorchModelCoreMax}
        }}, kTorchBottomUv);
        emitTorchModelFace(vertices, layer, 4.0f, {{
            {kTorchModelCoreMin, 0.0f, 0.0f},
            {kTorchModelCoreMin, 0.0f, 1.0f},
            {kTorchModelCoreMin, 1.0f, 1.0f},
            {kTorchModelCoreMin, 1.0f, 0.0f}
        }}, kTorchFullUv);
        emitTorchModelFace(vertices, layer, 5.0f, {{
            {kTorchModelCoreMax, 0.0f, 1.0f},
            {kTorchModelCoreMax, 0.0f, 0.0f},
            {kTorchModelCoreMax, 1.0f, 0.0f},
            {kTorchModelCoreMax, 1.0f, 1.0f}
        }}, kTorchFullUv);
        emitTorchModelFace(vertices, layer, 2.0f, {{
            {0.0f, 0.0f, kTorchModelCoreMax},
            {1.0f, 0.0f, kTorchModelCoreMax},
            {1.0f, 1.0f, kTorchModelCoreMax},
            {0.0f, 1.0f, kTorchModelCoreMax}
        }}, kTorchFullUv);
        emitTorchModelFace(vertices, layer, 3.0f, {{
            {1.0f, 0.0f, kTorchModelCoreMin},
            {0.0f, 0.0f, kTorchModelCoreMin},
            {0.0f, 1.0f, kTorchModelCoreMin},
            {1.0f, 1.0f, kTorchModelCoreMin}
        }}, kTorchFullUv);
    } else {
        for (int face = 0; face < 6; ++face) {
            int tileIndex = getFaceTextureIndex(def, face);
            if (tileIndex < 0) {
                tileIndex = 0;
            }

            const float layer = static_cast<float>(tileIndex);
            const std::array<glm::vec2, 4> faceUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};

            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = kFaceCorners[face][idx];
                const glm::vec2& uvCoord = faceUV[idx];
                vertices.push_back(makeBlockVertex(
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    def.useBiomeTint ? kCubeBiomeTintMarker : static_cast<float>(face),
                    1.0f,  // sunlight: full brightness for drop items
                    0.0f,   // blockLight
                    3.0f,   // ao: no occlusion
                    layer
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

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return mesh;
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
