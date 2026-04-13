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
#include "../item/Item.h"

namespace {
struct BlockVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float normal;
    float windWeight;
    float layer;
};

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
constexpr float kCrossGrassMarker = -1.0f;
constexpr float kCrossFlowerMarker = -2.0f;

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
}

void DropRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_shader = resourceMgr.getShader("chunk");
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
        m_shader->setMat4(blockViewProjLoc, viewProj);
        m_shader->setInt("texArray", 0);
        m_shader->setInt("uForceBaseLod", 0);
        m_shader->setVec3("uGrassTintColor", glm::vec3(0.50f, 0.78f, 0.34f));
        m_shader->setFloat("uWindStrength", 0.0f);
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

        glm::mat4 model(1.0f);
        model = glm::translate(model, drop.position);
        model = glm::rotate(model, drop.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(drop.halfExtents * 2.0f));
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        if (itemTileIndex >= 0 && canRenderItems) {
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

        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        if (renderBlock == BlockType::AIR || !canRenderBlocks) {
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
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
    }

    glBindVertexArray(0);
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
    if (m_resourceMgr == nullptr || itemId == ItemType::AIR) {
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
    if (m_resourceMgr == nullptr || blockId == BlockType::AIR) {
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
        const float crossMarker = def.useGrassTint ? kCrossGrassMarker : kCrossFlowerMarker;

        const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = corners[idx];
                const glm::vec2& uvCoord = quadUV[idx];
                vertices.push_back({
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    crossMarker,
                    pos.y,
                    layer
                });
            }
        };

        emitQuad(kCrossQuadA);
        emitQuad(kCrossQuadB);
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
            vertices.push_back({
                pos.x,
                pos.y,
                pos.z,
                uvCoord.x,
                uvCoord.y,
                static_cast<float>(face),
                0.0f,
                layer
            });
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
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, normal)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, windWeight)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, layer)));

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
