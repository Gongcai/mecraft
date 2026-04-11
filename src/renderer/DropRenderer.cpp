#include "DropRenderer.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "../core/Camera.h"
#include "../core/Window.h"
#include "../resource/ResourceMgr.h"
#include "../world/DropSystem.h"

namespace {
struct BlockVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float normal;
};

constexpr std::array<std::array<glm::vec3, 4>, 6> kFaceCorners = {{
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}}, // top
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, // bottom
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, // front (+z)
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, // back (-z)
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, // left (-x)
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}  // right (+x)
}};

constexpr std::array<int, 6> kFaceIndices = {{0, 1, 2, 0, 2, 3}};

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
}

void DropRenderer::shutdown() {
    for (auto& pair : m_blockMeshes) {
        destroyMesh(pair.second);
    }
    m_blockMeshes.clear();
    m_shader = nullptr;
    m_resourceMgr = nullptr;
}

void DropRenderer::render(const DropSystem& dropSystem, const Camera& camera, const Window& window) {
    if (m_shader == nullptr || m_resourceMgr == nullptr) {
        return;
    }

    const auto& drops = dropSystem.getDrops();
    if (drops.empty()) {
        return;
    }

    const TextureAtlas& atlas = m_resourceMgr->getAtlas();
    if (atlas.textureID == 0) {
        return;
    }

    const glm::mat4 viewProj = camera.getProjectionMatrix(window.getAspectRatio()) * camera.getViewMatrix();
    const int viewProjLoc = m_shader->getUniformLocation("viewProj");
    const int modelLoc = m_shader->getUniformLocation("model");

    m_shader->use();
    m_shader->setMat4(viewProjLoc, viewProj);
    m_shader->setInt("texAtlas", 0);
    m_shader->setInt("uForceBaseLod", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);

    for (const DropEntity& drop : drops) {
        if (drop.kind != DropKind::Block) {
            continue;
        }

        Mesh* mesh = getOrCreateBlockMesh(drop.blockId);
        if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
            continue;
        }

        glm::mat4 model(1.0f);
        model = glm::translate(model, drop.position);
        model = glm::rotate(model, drop.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(drop.halfExtents * 2.0f));
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        m_shader->setMat4(modelLoc, model);
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
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
    const TextureAtlas& atlas = m_resourceMgr->getAtlas();

    std::vector<BlockVertex> vertices;
    vertices.reserve(36);

    for (int face = 0; face < 6; ++face) {
        int tileIndex = getFaceTextureIndex(def, face);
        if (tileIndex < 0) {
            tileIndex = 0;
        }

        const auto uv = atlas.getUV(tileIndex);
        const float uMin = uv.first.x;
        const float vMin = uv.first.y;
        const float uMax = uv.second.x;
        const float vMax = uv.second.y;
        const std::array<glm::vec2, 4> faceUV = {{{uMin, vMin}, {uMax, vMin}, {uMax, vMax}, {uMin, vMax}}};

        for (const int idx : kFaceIndices) {
            const glm::vec3& pos = kFaceCorners[face][idx];
            const glm::vec2& uvCoord = faceUV[idx];
            vertices.push_back({
                pos.x,
                pos.y,
                pos.z,
                uvCoord.x,
                uvCoord.y,
                static_cast<float>(face)
            });
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

